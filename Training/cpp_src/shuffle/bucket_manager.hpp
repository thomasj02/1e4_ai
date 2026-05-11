#pragma once

#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <unordered_map>
#include "../utils/logger.hpp"
#include "bucket_record.hpp"

namespace chessmimic {

// Forward declaration
class Logger;

// BucketManager class for two-pass bucket shuffle
class BucketManager {
public:
    // Struct to track overflow relationships
    struct OverflowInfo {
        size_t primary_bucket_idx;         // Original bucket index
        size_t overflow_idx;               // Index within overflow sequence (0 = primary bucket)
        size_t next_overflow_bucket;       // Next overflow bucket in chain, SIZE_MAX if none
    };

    struct BucketStats {
        std::string file_path;             // Path to the bucket file
        size_t record_count{0};            // Number of records in the bucket
        size_t total_bytes{0};             // Total bytes in the bucket
        bool is_open{false};               // Whether the bucket file is currently open
        size_t buffer_size{0};             // Current size of in-memory buffer
        bool is_overflow{false};           // Whether this is an overflow bucket
        size_t overflow_index{0};          // If overflow bucket, which one in the chain (1-based)
        size_t primary_bucket{SIZE_MAX};   // Primary bucket this overflow belongs to
    };

    // Constructor
    BucketManager(
        std::string  temp_dir,
        size_t num_buckets,
        size_t total_memory_mb,        // Total memory to distribute across all buckets
        size_t max_bucket_size_bytes,  // Changed from MB to bytes
        Logger* logger
    );

    // Destructor to clean up resources
    ~BucketManager();

    // Initialize bucket files (creates directories)
    void initialize() const;

    // Write a record to the appropriate bucket
    void writeRecord(const BucketRecord& record, size_t record_counter = 0);

    // Flush all in-memory buffers to disk
    void flushAllBuffers();

    // Close all open bucket files
    void closeAllBuckets();

    // Get stats for all buckets
    [[nodiscard]] const std::vector<BucketStats>& getBucketStats() const;

    // Get total record count across all buckets
    [[nodiscard]] size_t getTotalRecordCount() const;

    // Get total size across all buckets
    [[nodiscard]] size_t getTotalSizeBytes() const;

    // Get all bucket paths (including overflow buckets)
    [[nodiscard]] std::vector<std::string> getAllBucketPaths() const;

    // Get overflow chain for a bucket
    [[nodiscard]] std::vector<size_t> getOverflowChain(size_t bucket_index) const;

    // Clean up temporary bucket files
    void cleanupBucketFiles();

private:
    std::string temp_dir_;                                       // Directory for bucket files
    size_t num_buckets_;                                         // Number of primary buckets
    size_t total_memory_bytes_;                                  // Total memory limit across all buckets
    size_t max_bucket_size_bytes_;                               // Maximum size of a bucket before overflow
    Logger* logger_;                                             // Logger instance (not owned by this class)
    std::atomic<size_t> per_bucket_limit_bytes_{0};              // Current per-bucket memory limit

    std::vector<BucketStats> bucket_stats_;                      // Stats for all buckets (primary + overflow)
    std::vector<std::unique_ptr<std::ofstream>> bucket_files_;   // File handles for all buckets
    std::vector<std::vector<char>> buffers_;                     // In-memory buffers for all buckets
    std::vector<std::unique_ptr<std::mutex>> bucket_mutexes_;    // Mutex for each bucket
    std::atomic<size_t> total_records_{0};                       // Total records across all buckets
    std::atomic<size_t> total_bytes_{0};                         // Total bytes across all buckets
    std::atomic<size_t> next_overflow_idx_{0};                   // Next available overflow bucket index
    
    // Map to track overflow bucket relationships - key is bucket index, value is overflow info
    std::unordered_map<size_t, OverflowInfo> overflow_map_;
    std::mutex overflow_map_mutex_;                              // Mutex for thread-safe access to overflow_map_
    mutable std::shared_mutex bucket_stats_mutex_;               // Mutex for thread-safe access to bucket_stats_ vector

    // Gets a bucket file path
    [[nodiscard]] std::string getBucketFilePath(size_t bucket_index, bool is_overflow = false, size_t overflow_idx = 0) const;

    // Appends a record into the given bucket's buffer, flushing if the
    // per-bucket limit is reached. Caller must hold a shared lock on
    // bucket_stats_mutex_ and the per-bucket mutex bucket_mutexes_[bucket_idx].
    void appendRecordLocked(const BucketRecord& record, size_t bucket_idx);

    // Ensures a bucket file is open and ready for writing.
    // Caller must hold a shared lock on bucket_stats_mutex_ and the per-bucket
    // mutex bucket_mutexes_[bucket_index].
    void ensureBucketOpen(size_t bucket_index);

    // Flushes the buffer for a specific bucket to disk.
    // Caller must hold a shared lock on bucket_stats_mutex_ and the per-bucket
    // mutex bucket_mutexes_[bucket_index].
    void flushBuffer(size_t bucket_index);

    // Creates a new overflow bucket and returns its index
    size_t createOverflowBucket(size_t primary_bucket_idx);

    // Gets the appropriate bucket index for a record (handling overflow if needed)
    // This method should be called while NOT holding any bucket locks to avoid deadlocks
    size_t getBucketForRecord(const BucketRecord& record, size_t record_counter);

    // Gets the appropriate bucket index starting from a specific bucket in the chain
    // Must be called while holding the primary bucket's lock
    size_t getBucketForRecordWithLock(size_t primary_idx, const BucketRecord& record);

    // Checks if a bucket needs to overflow. Acquires its own shared lock.
    bool isBucketOverflowNeeded(size_t bucket_index, size_t additional_bytes) const;

    // Same as isBucketOverflowNeeded but without acquiring the lock.
    // Caller must hold a shared lock on bucket_stats_mutex_.
    bool isBucketOverflowNeededLocked(size_t bucket_index, size_t additional_bytes) const;
    
    // Find the next bucket in overflow chain
    size_t getNextBucketInChain(size_t bucket_index) const;
    
    // Recalculate per-bucket memory limit based on current bucket count
    void recalculatePerBucketLimit();
    
    // Get the primary bucket index for a record (without overflow checking)
    size_t getPrimaryBucketIndex(const BucketRecord& record, size_t record_counter) const;
};

} // namespace chessmimic