#include "../pgn_converter/pgn_to_bagz_converter.hpp"
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <utility>
#include <algorithm>
#include <ranges>
#include <cstring>
#include <cerrno>

namespace chessmimic {
// Use the FileUtils namespace
using namespace FileUtils;

// Constructor
BucketManager::BucketManager(
    std::string temp_dir,
    size_t num_buckets,
    size_t total_memory_mb,
    size_t max_bucket_size_bytes,
    Logger* logger
)
    : temp_dir_(std::move(temp_dir)),
      num_buckets_(num_buckets),
      total_memory_bytes_(total_memory_mb * 1024 * 1024),
      max_bucket_size_bytes_(max_bucket_size_bytes),
      logger_(logger) {
    // Initialize bucket stats and containers for primary buckets
    bucket_stats_.resize(num_buckets_);
    bucket_files_.resize(num_buckets_);
    buffers_.resize(num_buckets_);

    // Create mutexes - mutexes cannot be resized because they are not copyable
    for (size_t i = 0; i < num_buckets_; ++i) {
        bucket_mutexes_.emplace_back(std::make_unique<std::mutex>());
    }

    // Initialize buffer for each bucket to a reasonable starting size
    constexpr size_t initial_buffer_size = 64 * 1024; // 64KB initial size
    for (size_t i = 0; i < num_buckets_; ++i) {
        buffers_[i].reserve(initial_buffer_size);
        bucket_stats_[i].file_path = getBucketFilePath(i);

        // Add primary buckets to overflow map
        OverflowInfo info{i, 0, SIZE_MAX}; // Primary bucket, index 0, no next bucket
        std::lock_guard lock(overflow_map_mutex_);
        overflow_map_[i] = info;
    }
    
    // Calculate initial per-bucket memory limit
    recalculatePerBucketLimit();

    CM_LOG_INFO("Initialized BucketManager with {} primary buckets, {}MB total memory ({}MB per bucket), {} bytes max bucket size",
                  num_buckets_, total_memory_mb, per_bucket_limit_bytes_.load() / (1024 * 1024), max_bucket_size_bytes_);
}

// Destructor
BucketManager::~BucketManager() {
    flushAllBuffers();
    closeAllBuckets();
}

// Initialize bucket directory structure
void BucketManager::initialize() const {
    // Create temp directory if it doesn't exist
    std::filesystem::create_directories(temp_dir_);

    // Create bucket subdirectory
    std::string bucket_dir = temp_dir_ + "/buckets";
    std::filesystem::create_directories(bucket_dir);

    CM_LOG_INFO("Initialized bucket directory: {}", bucket_dir);
}

// Write a record to the appropriate bucket
void BucketManager::writeRecord(const BucketRecord& record, size_t record_counter) {
    size_t primary_idx = getPrimaryBucketIndex(record, record_counter);

    // Fast path: hold the shared stats lock for the duration of vector access
    // and the per-bucket lock for the duration of the buffer write.
    {
        std::shared_lock stats_lock(bucket_stats_mutex_);
        std::unique_lock primary_lock(*bucket_mutexes_[primary_idx]);

        if (!isBucketOverflowNeededLocked(primary_idx, record.getTotalSize())) {
            appendRecordLocked(record, primary_idx);
            return;
        }
    }

    // Slow path: stats and bucket locks are released. getBucketForRecordWithLock
    // may take the unique stats lock via createOverflowBucket; holding the
    // shared stats lock here would deadlock.
    size_t bucket_idx = getBucketForRecordWithLock(primary_idx, record);

    std::shared_lock stats_lock(bucket_stats_mutex_);
    std::unique_lock bucket_lock(*bucket_mutexes_[bucket_idx]);
    appendRecordLocked(record, bucket_idx);
}

// Caller holds shared_lock(bucket_stats_mutex_) and *bucket_mutexes_[bucket_idx].
void BucketManager::appendRecordLocked(const BucketRecord& record, size_t bucket_idx) {
    ensureBucketOpen(bucket_idx);

    size_t record_size = record.getTotalSize();
    size_t current_buffer_size = buffers_[bucket_idx].size();
    buffers_[bucket_idx].resize(current_buffer_size + record_size);

    char* buffer_ptr = buffers_[bucket_idx].data() + current_buffer_size;

    std::memcpy(buffer_ptr, &record.record_size, sizeof(record.record_size));
    buffer_ptr += sizeof(record.record_size);

    std::memcpy(buffer_ptr, &record.key_len, sizeof(record.key_len));
    buffer_ptr += sizeof(record.key_len);

    std::memcpy(buffer_ptr, record.key.data(), record.key.size());
    buffer_ptr += record.key.size();

    std::memcpy(buffer_ptr, &record.record_len, sizeof(record.record_len));
    buffer_ptr += sizeof(record.record_len);

    std::memcpy(buffer_ptr, record.record.data(), record.record.size());

    bucket_stats_[bucket_idx].record_count++;
    bucket_stats_[bucket_idx].total_bytes += record_size;
    bucket_stats_[bucket_idx].buffer_size = buffers_[bucket_idx].size();
    ++total_records_;
    total_bytes_ += record_size;

    if (buffers_[bucket_idx].size() >= per_bucket_limit_bytes_.load()) {
        flushBuffer(bucket_idx);
    }
}

// Gets the appropriate bucket index for a record, handling overflow if needed
size_t BucketManager::getBucketForRecord(const BucketRecord& record, size_t record_counter) {
    // Get primary bucket index using record counter for better distribution
    uint32_t primary_idx = record.getBucketIndex(static_cast<uint32_t>(num_buckets_), record_counter);

    // Check if we need to overflow
    if (!isBucketOverflowNeeded(primary_idx, record.getTotalSize())) {
        return primary_idx; // Return primary bucket if no overflow needed
    }

    // Need to find or create an overflow bucket
    // Lock to prevent multiple threads from creating overflow buckets simultaneously
    std::lock_guard lock(overflow_map_mutex_);

    // Get the overflow chain for this primary bucket
    size_t current_bucket = primary_idx;

    while (true) {
        // Check if there's a next bucket in the chain
        size_t next_bucket = getNextBucketInChain(current_bucket);

        // If no next bucket, create a new overflow bucket
        if (next_bucket == SIZE_MAX) {
            size_t new_overflow_idx = createOverflowBucket(primary_idx);

            // Update the overflow map to link the current bucket to the new overflow bucket
            overflow_map_[current_bucket].next_overflow_bucket = new_overflow_idx;

            CM_LOG_DEBUG("Created new overflow bucket {} for primary bucket {}",
                           new_overflow_idx, primary_idx);

            return new_overflow_idx;
        }

        // If there is a next bucket, check if it has room
        current_bucket = next_bucket;

        if (!isBucketOverflowNeeded(current_bucket, record.getTotalSize())) {
            return current_bucket;
        }
    }
}

// Check if a bucket needs to overflow based on current size + new record size.
// Caller must hold a shared lock on bucket_stats_mutex_.
bool BucketManager::isBucketOverflowNeededLocked(size_t bucket_index, size_t additional_bytes) const {
    if (max_bucket_size_bytes_ == 0) {
        return false;
    }
    size_t total_bucket_size = bucket_stats_[bucket_index].total_bytes + additional_bytes;
    return total_bucket_size > max_bucket_size_bytes_;
}

bool BucketManager::isBucketOverflowNeeded(size_t bucket_index, size_t additional_bytes) const {
    if (max_bucket_size_bytes_ == 0) {
        return false;
    }
    std::shared_lock<std::shared_mutex> lock(bucket_stats_mutex_);
    return isBucketOverflowNeededLocked(bucket_index, additional_bytes);
}

// Creates a new overflow bucket and returns its index.
// Caller holds overflow_map_mutex_; this function takes a unique lock on
// bucket_stats_mutex_ while growing the per-bucket containers.
size_t BucketManager::createOverflowBucket(size_t primary_bucket_idx) {
    size_t overflow_idx = num_buckets_ + next_overflow_idx_++;

    // Compute the sequence number while we still hold overflow_map_mutex_
    // (caller-held). Then determine the file path.
    size_t overflow_sequence_idx = 1;
    for (const auto& info : overflow_map_ | std::views::values) {
        if (info.primary_bucket_idx == primary_bucket_idx && info.overflow_idx >= overflow_sequence_idx) {
            overflow_sequence_idx = info.overflow_idx + 1;
        }
    }
    std::string file_path = getBucketFilePath(primary_bucket_idx, true, overflow_sequence_idx);

    // All vector growth and new-slot initialization happens under the unique
    // stats lock so concurrent readers can't observe a partially-grown state.
    {
        std::unique_lock lock(bucket_stats_mutex_);

        bucket_stats_.resize(overflow_idx + 1);
        bucket_files_.resize(overflow_idx + 1);
        buffers_.resize(overflow_idx + 1);
        bucket_mutexes_.emplace_back(std::make_unique<std::mutex>());

        bucket_stats_[overflow_idx].file_path = file_path;
        bucket_stats_[overflow_idx].is_overflow = true;
        bucket_stats_[overflow_idx].overflow_index = overflow_sequence_idx;
        bucket_stats_[overflow_idx].primary_bucket = primary_bucket_idx;
        buffers_[overflow_idx].reserve(64 * 1024);
    }

    overflow_map_[overflow_idx] = OverflowInfo{primary_bucket_idx, overflow_sequence_idx, SIZE_MAX};

    CM_LOG_INFO("Created overflow bucket {} (sequence {}) for primary bucket {}",
                  overflow_idx, overflow_sequence_idx, primary_bucket_idx);

    recalculatePerBucketLimit();

    return overflow_idx;
}

// Find the next bucket in overflow chain
size_t BucketManager::getNextBucketInChain(size_t bucket_index) const {
    if (auto it = overflow_map_.find(bucket_index); it != overflow_map_.end()) {
        return it->second.next_overflow_bucket;
    }
    return SIZE_MAX; // No next bucket
}

// Flush all in-memory buffers to disk
void BucketManager::flushAllBuffers() {
    std::shared_lock stats_lock(bucket_stats_mutex_);
    for (size_t i = 0; i < buffers_.size(); ++i) {
        std::lock_guard<std::mutex> lock(*bucket_mutexes_[i]);
        if (!buffers_[i].empty()) {
            flushBuffer(i);
        }
    }
    CM_LOG_DEBUG("Flushed all bucket buffers to disk");
}

// Close all open bucket files
void BucketManager::closeAllBuckets() {
    std::shared_lock stats_lock(bucket_stats_mutex_);
    for (size_t i = 0; i < bucket_files_.size(); ++i) {
        if (bucket_files_[i] && bucket_files_[i]->is_open()) {
            bucket_files_[i]->close();
            bucket_stats_[i].is_open = false;
        }
    }
    CM_LOG_DEBUG("Closed all bucket files");
}

// Get stats for all buckets
const std::vector<BucketManager::BucketStats>& BucketManager::getBucketStats() const {
    // Note: Returning a const reference is safe here without locking since the caller
    // should not modify the vector. However, if the caller stores this reference
    // and another thread resizes the vector, it could become invalid.
    // For complete safety, consider returning a copy instead of a reference.
    return bucket_stats_;
}

// Get total record count across all buckets
size_t BucketManager::getTotalRecordCount() const {
    return total_records_;
}

// Get total size across all buckets
size_t BucketManager::getTotalSizeBytes() const {
    return total_bytes_;
}

// Get all bucket paths (including overflow buckets)
std::vector<std::string> BucketManager::getAllBucketPaths() const {
    std::vector<std::string> paths;
    
    // Protect read access to bucket_stats_ vector
    std::shared_lock lock(bucket_stats_mutex_);
    paths.reserve(bucket_stats_.size());

    for (const auto& stats : bucket_stats_) {
        if (stats.record_count > 0) {
            paths.push_back(stats.file_path);
        }
    }

    return paths;
}

// Get overflow chain for a bucket
std::vector<size_t> BucketManager::getOverflowChain(size_t bucket_index) const {
    std::vector<size_t> chain;

    // If bucket_index is not a primary bucket, find its primary bucket first
    size_t primary_idx = bucket_index;
    for (const auto& [idx, info] : overflow_map_) {
        if (idx == bucket_index && info.overflow_idx > 0) {
            primary_idx = info.primary_bucket_idx;
            break;
        }
    }

    // Start with the primary bucket
    chain.push_back(primary_idx);

    // Follow the overflow chain
    size_t current = primary_idx;
    while (true) {
        size_t next = getNextBucketInChain(current);
        if (next == SIZE_MAX) {
            break;
        }
        chain.push_back(next);
        current = next;
    }

    return chain;
}

// Clean up temporary bucket files
void BucketManager::cleanupBucketFiles() {
    closeAllBuckets();

    // Protect read access to bucket_stats_ vector
    std::shared_lock lock(bucket_stats_mutex_);
    
    for (auto& bucket_stat : bucket_stats_) {
        try {
            if (std::filesystem::exists(bucket_stat.file_path)) {
                std::filesystem::remove(bucket_stat.file_path);
            }
        }
        catch (const std::exception& e) {
            CM_LOG_WARNING("Failed to remove bucket file {}: {}",
                             bucket_stat.file_path, e.what());
            throw;
        }
    }

    // Try to remove the bucket directory
    try {
        std::filesystem::remove(temp_dir_ + "/buckets");
    }
    catch (const std::exception& e) {
        CM_LOG_WARNING("Failed to remove bucket directory: {}", e.what());
        throw;
    }

    CM_LOG_INFO("Cleaned up all bucket files");
}

// Gets a bucket file path
std::string BucketManager::getBucketFilePath(size_t bucket_index, bool is_overflow, size_t overflow_idx) const {
    std::ostringstream path;

    if (is_overflow) {
        // Overflow bucket path: temp_dir/buckets/bucket_XXXXX_overflow_YY.dat
        path << temp_dir_ << "/buckets/bucket_"
            << std::setw(5) << std::setfill('0') << bucket_index
            << "_overflow_" << std::setw(2) << std::setfill('0') << overflow_idx << ".dat";
    }
    else {
        // Primary bucket path: temp_dir/buckets/bucket_XXXXX.dat
        path << temp_dir_ << "/buckets/bucket_"
            << std::setw(5) << std::setfill('0') << bucket_index << ".dat";
    }

    return path.str();
}

// Ensures a bucket file is open and ready for writing.
// Caller holds shared_lock(bucket_stats_mutex_) and *bucket_mutexes_[bucket_index].
void BucketManager::ensureBucketOpen(size_t bucket_index) {
    if (bucket_files_[bucket_index] && bucket_files_[bucket_index]->is_open()) {
        return;
    }

    const std::string& file_path = bucket_stats_[bucket_index].file_path;

    bucket_files_[bucket_index] = std::make_unique<std::ofstream>(
        file_path,
        std::ios::binary | std::ios::out | std::ios::app
    );

    throw_if_failed(*bucket_files_[bucket_index], file_path);

    bucket_stats_[bucket_index].is_open = true;

    CM_LOG_DEBUG("Opened bucket file: {}", file_path);
}

// Flushes the buffer for a specific bucket to disk.
// Caller holds shared_lock(bucket_stats_mutex_) and *bucket_mutexes_[bucket_index].
void BucketManager::flushBuffer(size_t bucket_index) {
    if (buffers_[bucket_index].empty()) {
        return;
    }

    ensureBucketOpen(bucket_index);

    bucket_files_[bucket_index]->write(
        buffers_[bucket_index].data(),
        static_cast<std::streamsize>(buffers_[bucket_index].size())
    );

    if (!bucket_files_[bucket_index]->good()) {
        const std::string& file_path = bucket_stats_[bucket_index].file_path;
        throw std::runtime_error("Failed to write to bucket file: " + file_path +
                               ", errno=" + std::to_string(errno) +
                               " (" + std::strerror(errno) + ")");
    }

    bucket_files_[bucket_index]->flush();

    if (!bucket_files_[bucket_index]->good()) {
        const std::string& file_path = bucket_stats_[bucket_index].file_path;
        throw std::runtime_error("Failed to flush bucket file: " + file_path +
                               ", errno=" + std::to_string(errno) +
                               " (" + std::strerror(errno) + ")");
    }

    size_t buffer_capacity = buffers_[bucket_index].capacity();
    buffers_[bucket_index].clear();
    buffers_[bucket_index].reserve(buffer_capacity);

    bucket_stats_[bucket_index].buffer_size = 0;

    CM_LOG_DEBUG("Flushed buffer for bucket {}: {} bytes",
                   bucket_index, buffers_[bucket_index].capacity());
}

// Recalculate per-bucket memory limit based on current bucket count
void BucketManager::recalculatePerBucketLimit() {
    // Count active buckets (those with file handles or records)
    size_t active_buckets = 0;
    
    // Protect read access to bucket_stats_ vector
    {
        std::shared_lock lock(bucket_stats_mutex_);
        for (size_t i = 0; i < bucket_stats_.size(); ++i) {
            if (i < num_buckets_ || bucket_stats_[i].record_count > 0) {
                active_buckets++;
            }
        }
    }
    
    // Ensure we always have at least 1 bucket
    if (active_buckets == 0) {
        active_buckets = num_buckets_;
    }
    
    // Calculate new per-bucket limit
    size_t new_limit = total_memory_bytes_ / active_buckets;
    per_bucket_limit_bytes_.store(new_limit);
    
    CM_LOG_DEBUG("Recalculated per-bucket memory limit: {} MB per bucket ({} total buckets)",
                   new_limit / (1024 * 1024), active_buckets);
}

// Get the primary bucket index for a record (without overflow checking)
size_t BucketManager::getPrimaryBucketIndex(const BucketRecord& record, size_t record_counter) const {
    return record.getBucketIndex(static_cast<uint32_t>(num_buckets_), record_counter);
}

// Gets the appropriate bucket index starting from a specific bucket in the chain
// Must be called while holding the primary bucket's lock
size_t BucketManager::getBucketForRecordWithLock(size_t primary_idx, const BucketRecord& record) {
    // We already know primary bucket needs overflow, start checking the chain
    size_t current_bucket = primary_idx;
    
    // Lock to prevent multiple threads from creating overflow buckets simultaneously
    std::lock_guard lock(overflow_map_mutex_);
    
    while (true) {
        // Check if there's a next bucket in the chain
        size_t next_bucket = getNextBucketInChain(current_bucket);
        
        // If no next bucket, create a new overflow bucket
        if (next_bucket == SIZE_MAX) {
            size_t new_overflow_idx = createOverflowBucket(primary_idx);
            
            // Update the overflow map to link the current bucket to the new overflow bucket
            overflow_map_[current_bucket].next_overflow_bucket = new_overflow_idx;
            
            CM_LOG_DEBUG("Created new overflow bucket {} for primary bucket {}",
                           new_overflow_idx, primary_idx);
            
            return new_overflow_idx;
        }
        
        // If there is a next bucket, check if it has room
        current_bucket = next_bucket;
        
        if (!isBucketOverflowNeeded(current_bucket, record.getTotalSize())) {
            return current_bucket;
        }
    }
}
} // namespace chessmimic
