#include "shuffle_manager.hpp"
#include "record_processor.hpp"
#include "pgn_to_bagz_converter.hpp" // For BucketManager
#include "../utils/stopwatch.hpp"

#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <atomic>
#include <random>
#include <algorithm>

namespace chessmimic {
// Use the FileUtils namespace
using namespace FileUtils;

// Constructor
ShuffleManager::ShuffleManager(
    std::string temp_dir,
    unsigned int shuffle_seed,
    bool keep_temp_files,
    ThreadPool* thread_pool,
    Logger* logger
)
    : temp_dir_(std::move(temp_dir)),
      shuffle_seed_(shuffle_seed),
      keep_temp_files_(keep_temp_files),
      thread_pool_(thread_pool),
      logger_(logger) {
    CM_LOG_DEBUG("Initialized ShuffleManager with seed={}", shuffle_seed_);
}

// In-memory shuffle implementation
std::string ShuffleManager::shuffleRecords(const std::string& input_file, const std::string& output_path) const {
    // Determine output file path
    std::string output_file = output_path.empty() ? 
        temp_dir_ + "/shuffled.bagz" : output_path;

    // Read all records into memory for shuffling
    std::vector<std::string> records;
    records.reserve(1000000); // Reserve space for efficiency
    
    // Open the input file
    std::ifstream in(input_file, std::ios::binary);
    throw_if_failed(in, input_file);

    // Read all records (just the JSON data, not the keys)
    std::string key;
    std::string record;
    while (RecordProcessor::readRecord(in, key, record)) {
        records.push_back(record);
    }
    in.close();

    CM_LOG_INFO("Shuffling {} records in-memory...", records.size());

    // Shuffle the records
    std::random_device rd;
    std::mt19937 g(shuffle_seed_ != 0 ? std::mt19937(shuffle_seed_) : std::mt19937(rd()));
    std::ranges::shuffle(records, g);

    // Create BAGZ writer for output
    BagWriter writer(output_file, false); // No separate limits for output files
    CM_LOG_DEBUG("Writing shuffled output to BAGZ: {}", output_file);

    // Write records in shuffled order
    for (const auto& shuffled_record : records) {
        // Convert to vector<uint8_t>
        std::vector<uint8_t> data(shuffled_record.begin(), shuffled_record.end());
        writer.write(data);
    }

    writer.close();

    // Remove input file after shuffling if not keeping temp files
    if (!keep_temp_files_) {
        std::filesystem::remove(input_file);
    }

    return output_file;
}


// Two-pass bucket shuffle implementation
std::string ShuffleManager::bucketShuffleRecords(
    const std::string& input_file, 
    BucketManager& bucket_manager,
    const std::string& output_path
) const {
    SCOPED_TIMER("ShuffleManager::bucketShuffleRecords");
    CM_LOG_INFO("Starting two-pass bucket shuffle...");

    // Phase 1: Distribute records to buckets
    CM_LOG_INFO("Pass 1: Distributing records to buckets...");
    {
        SCOPED_TIMER("ShuffleManager::distributeToBuckets");
        distributeToBuckets(input_file, bucket_manager);
    }

    // Cleanup bucket manager to ensure all files are properly closed
    {
        SCOPED_TIMER("ShuffleManager::bucketCleanup");
        bucket_manager.flushAllBuffers();
        bucket_manager.closeAllBuckets();
    }

    // Get list of bucket files for parallel processing
    std::vector<std::string> bucket_paths;

    {
        SCOPED_TIMER("ShuffleManager::gatherBucketPaths");
        for (const auto& stats = bucket_manager.getBucketStats(); const auto& stat : stats) {
            // Only include buckets that have records
            if (stat.record_count > 0) {
                bucket_paths.push_back(stat.file_path);
            }
        }
    }

    // Process buckets in parallel (Pass 2)
    CM_LOG_INFO("Pass 2: Processing {} buckets with records...", bucket_paths.size());
    
    // Use the provided output path if specified, otherwise use the default temp path
    std::string final_output_path = output_path.empty() ? 
        temp_dir_ + "/bucket_shuffled.records" : output_path;
        
    std::string output_file;
    {
        SCOPED_TIMER("ShuffleManager::processBucketsParallel");
        output_file = processBucketsParallel(bucket_paths, final_output_path);
    }

    CM_LOG_INFO("Two-pass bucket shuffle complete");
    return output_file;
}

// Distribute records to buckets (Pass 1 of two-pass shuffle)
void ShuffleManager::distributeToBuckets(const std::string& input_file, BucketManager& bucket_manager) const {
    SCOPED_TIMER("ShuffleManager::distributeToBuckets_impl");
    
    // Open the input file
    std::ifstream in(input_file, std::ios::binary);
    throw_if_failed(in, input_file);

    std::string key;
    std::string record;
    size_t record_count = 0;


    // Process each record
    while (RecordProcessor::readRecord(in, key, record)) {
        // Create a bucket record
        BucketRecord bucket_record;
        bucket_record.key_len = key.size();
        bucket_record.key = key;
        bucket_record.record_len = record.size();
        bucket_record.record = record;

        // Calculate total record size
        bucket_record.record_size = sizeof(bucket_record.key_len) + bucket_record.key_len +
            sizeof(bucket_record.record_len) + bucket_record.record_len;

        // Write the record to the appropriate bucket, passing record count for distribution
        bucket_manager.writeRecord(bucket_record, record_count);

        // Increment record count
        record_count++;

        // Log progress periodically
        if (record_count % 1'000'000 == 0) {
            CM_LOG_INFO("Distributed {} records to buckets", record_count);
        }
        // Flush the buffer if we reach the threshold
        if (constexpr size_t FLUSH_EVERY_N_RECORDS = 1'000'000; record_count % FLUSH_EVERY_N_RECORDS == 0) {
            bucket_manager.flushAllBuffers();
            CM_LOG_INFO("Flushed buffers after {} records", record_count);
        }
    }

    // Flush all buffers to ensure all data is written to disk
    bucket_manager.flushAllBuffers();

    // Log bucket statistics
    const auto& stats = bucket_manager.getBucketStats();
    size_t min_records = std::numeric_limits<size_t>::max();
    size_t max_records = 0;
    size_t empty_buckets = 0;

    for (const auto& bucket_stat : stats) {
        min_records = std::min(min_records, bucket_stat.record_count);
        max_records = std::max(max_records, bucket_stat.record_count);
        if (bucket_stat.record_count == 0) {
            empty_buckets++;
        }
    }

    CM_LOG_INFO("Bucket distribution complete: {} records distributed", record_count);
    CM_LOG_INFO("Bucket statistics: min={}, max={}, empty={}",
                 min_records == std::numeric_limits<size_t>::max() ? 0 : min_records,
                 max_records, empty_buckets);
    CM_LOG_INFO("Total bytes written to buckets: {} MB",
                 bucket_manager.getTotalSizeBytes() / (1024 * 1024));

    // Remove input file if not keeping temp files
    if (!keep_temp_files_) {
        std::filesystem::remove(input_file);
    }
}

// Process buckets in parallel (Pass 2 of two-pass shuffle)
std::string ShuffleManager::processBucketsParallel(
    const std::vector<std::string>& bucket_paths,
    const std::string& output_path
) const {
    SCOPED_TIMER("ShuffleManager::processBucketsParallel_impl");
    // Use the provided output path if specified, otherwise use the default temp path
    std::string output_file = output_path.empty() ? 
        temp_dir_ + "/bucket_shuffled.records" : output_path;
        
    // Convert to absolute path if it's not already
    if (!std::filesystem::path(output_file).is_absolute()) {
        output_file = std::filesystem::absolute(output_file).string();
        CM_LOG_INFO("Converting relative output path to absolute: {} ", output_file);
    }

    // Ensure parent directory exists
    ensureDirectoryExists(std::filesystem::path(output_file).parent_path());

    // Log where we're writing to
    CM_LOG_INFO("Writing output to: {}", output_file);

    // Create BAGZ writer
    BagWriter writer(output_file, false); // No separate limits for intermediate files
    
    // Log debug message about BagWriter
    CM_LOG_DEBUG("Created BagWriter with output_file: {}", output_file);

    // Create a mutex for thread-safe access to the writer
    std::mutex writer_mutex;

    // Process buckets in parallel
    CM_LOG_INFO("Pass 2: Processing {} buckets in parallel...", bucket_paths.size());

    // Track primary and overflow buckets
    std::unordered_map<std::string, bool> processed_buckets;
    
    // Sort bucket paths to ensure primary buckets are processed before overflow buckets
    // This improves determinism in the output
    std::vector<std::string> sorted_paths = bucket_paths;
    std::ranges::sort(sorted_paths, [](const std::string& a, const std::string& b) {
        // Primary buckets come before overflow buckets
        bool a_is_overflow = a.find("overflow") != std::string::npos;
        bool b_is_overflow = b.find("overflow") != std::string::npos;
        
        if (a_is_overflow != b_is_overflow) {
            return !a_is_overflow; // Primary buckets first
        }
        
        // Alphabetical order within each group
        return a < b;
    });
    
    // Create a progress counter
    std::atomic<size_t> completed_buckets{0};

    // Create task list for parallel processing
    std::vector<std::future<void>> tasks;

    // Process each bucket individually
    for (const auto& bucket_path : sorted_paths) {
        tasks.push_back(thread_pool_->enqueue(
            [this, &writer, &writer_mutex, &completed_buckets, bucket_path, 
             total_buckets = sorted_paths.size(), &processed_buckets] {
                // Process this bucket
                processShuffledBucket(bucket_path, writer, writer_mutex);
                
                // Mark bucket as processed - we don't need thread safety here
                // as each bucket is processed by its own thread
                processed_buckets[bucket_path] = true;

                // Update progress
                if (size_t completed = ++completed_buckets; 
                    completed % 16 == 0 || completed == total_buckets) {
                    CM_LOG_INFO("Processed {}/{} buckets", completed, total_buckets);
                }

                // Remove bucket file if not keeping temp files
                if (!keep_temp_files_) {
                    std::filesystem::remove(bucket_path);
                }
            }));
    }

    // Wait for all tasks to complete
    for (auto& task : tasks) {
        task.wait();
    }

    // Close the writer
    writer.close();

    CM_LOG_INFO("Pass 2 complete: All {} buckets processed and written to {}", 
                sorted_paths.size(), output_file);
    return output_file;
}

// Process a single bucket (used in Pass 2 of two-pass shuffle)
void ShuffleManager::processShuffledBucket(const std::string& bucket_path,
                                           const BagWriter& writer,
                                           std::mutex& writer_mutex) const {
    try {
        // Always use in-memory shuffle
        SCOPED_TIMER("ShuffleManager::loadAndShuffleRecords");
        
        // Read all records from the bucket file sequentially
        std::ifstream in(bucket_path, std::ios::binary);
        throw_if_failed(in, bucket_path);

        // Vector to hold all records from this bucket
        std::vector<std::string> records;
        
        // Process all records in the file
        std::string key;
        std::string record;
        size_t record_count = 0;
        
        // Read records sequentially into memory
        while (RecordProcessor::readRecord(in, key, record)) {
            records.push_back(record);
            record_count++;
        }
        
        // Create a thread-local random number generator with seed
        std::random_device rd;
        std::mt19937 rng(shuffle_seed_ != 0 ? shuffle_seed_ : rd());
        
        // Shuffle all records in memory
        std::ranges::shuffle(records, rng);
        
        // Write records to BAGZ (thread-safe)
        {
            std::lock_guard lock(writer_mutex);
            for (const auto& record_str : records) {
                writer.write(std::vector<uint8_t>(record_str.begin(), record_str.end()));
            }
        }
        
        CM_LOG_DEBUG("Processed {} records from bucket: {}", record_count, bucket_path);
    }
    catch (const std::exception& e) {
        CM_LOG_ERROR("Error processing bucket {}: {}", bucket_path, e.what());
        throw;
    }
}



// Scan record offsets in a memory-mapped file
std::vector<size_t> ShuffleManager::scanRecordOffsets(const MemoryMappedFile& mmap_file) {
    std::vector<size_t> offsets;

    // Check if file is successfully mapped
    if (!mmap_file.is_open() || mmap_file.size() == 0) {
        CM_LOG_WARNING("Failed to scan offsets, file is not properly mapped: {}", mmap_file.path());
        return offsets;
    }

    // Reserve initial capacity to reduce reallocations
    offsets.reserve(1000);

    const char* data = mmap_file.data();
    size_t size = mmap_file.size();
    size_t pos = 0;

    // Scan through file to find record positions
    while (pos + sizeof(uint32_t) <= size) {
        // Store current position as a record offset
        offsets.push_back(pos);

        // Read record size
        uint32_t record_size;
        std::memcpy(&record_size, data + pos, sizeof(record_size));

        // Move to next record
        pos += sizeof(record_size) + record_size;

        // Safety check to avoid infinite loop or buffer overrun
        if (record_size == 0 || pos > size) {
            CM_LOG_WARNING("Invalid record found at offset {} in file: {}",
                             pos - sizeof(record_size), mmap_file.path());
            break;
        }
    }

    CM_LOG_DEBUG("Scanned {} record offsets in file: {}", offsets.size(), mmap_file.path());
    return offsets;
}

// Get a record at a specific offset in a memory-mapped file
MmapRecordInfo ShuffleManager::getRecordAtOffset(const MemoryMappedFile& mmap_file, size_t offset) {
    MmapRecordInfo info;
    info.offset = offset;

    // Check if offset is within bounds
    if (offset + sizeof(uint32_t) > mmap_file.size()) {
        CM_LOG_ERROR("Offset {} is out of bounds for file size {}: {}",
                     offset, mmap_file.size(), mmap_file.path());
        throw std::out_of_range(fmt::format("Offset {} is out of bounds for file size {}: {}",
                                           offset, mmap_file.size(), mmap_file.path()));
    }

    const char* data = mmap_file.data();

    // Read record size
    std::memcpy(&info.size, data + offset, sizeof(info.size));

    // Check if the entire record is within bounds
    if (offset + sizeof(info.size) + info.size > mmap_file.size()) {
        CM_LOG_ERROR("Record at offset {} with size {} exceeds file bounds {}: {}",
                     offset, info.size, mmap_file.size(), mmap_file.path());
        throw std::out_of_range(fmt::format("Record at offset {} with size {} exceeds file bounds {}: {}",
                                           offset, info.size, mmap_file.size(), mmap_file.path()));
    }

    // Move position to start of key length
    size_t pos = offset + sizeof(info.size);

    // Read key length
    uint32_t key_len;
    std::memcpy(&key_len, data + pos, sizeof(key_len));
    pos += sizeof(key_len);

    // Read key
    info.key.resize(key_len);
    std::memcpy(&info.key[0], data + pos, key_len);
    pos += key_len;

    // Read record length
    uint32_t record_len;
    std::memcpy(&record_len, data + pos, sizeof(record_len));
    pos += sizeof(record_len);

    // Read record
    info.record.resize(record_len);
    std::memcpy(&info.record[0], data + pos, record_len);

    return info;
}

} // namespace chessmimic
