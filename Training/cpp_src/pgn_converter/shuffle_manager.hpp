#pragma once

#include <string>
#include <vector>
#include <random>
#include <mutex>

#include "../utils/thread_pool.hpp"
#include "../utils/logger.hpp"
#include "../core/bagz.hpp"
#include "../utils/file_utils.hpp"
#include "../shuffle/bucket_record.hpp"

namespace chessmimic {

// Forward declarations
class Logger;
class BucketManager;

// Structure to hold record information from memory-mapped files
struct MmapRecordInfo {
    size_t offset;     // Offset of record in file
    uint32_t size;     // Size of record (including all fields)
    std::string key;   // Position key
    std::string record; // JSON record
};

// ShuffleManager class for managing record shuffling operations
class ShuffleManager {
public:
    // Constructor
    ShuffleManager(
        std::string temp_dir,
        unsigned int shuffle_seed,
        bool keep_temp_files,
        ThreadPool* thread_pool,
        Logger* logger
    );

    // Public methods
    [[nodiscard]] std::string shuffleRecords(const std::string& input_file, const std::string& output_path = "") const; // In-memory shuffle
    std::string bucketShuffleRecords(
        const std::string& input_file, 
        BucketManager& bucket_manager, 
        const std::string& output_path = "" // Optional output path, if not specified uses temp dir
    ) const; // Two-pass bucket shuffle

private:
    std::string temp_dir_;
    unsigned int shuffle_seed_;
    bool keep_temp_files_;
    ThreadPool* thread_pool_;
    Logger* logger_;

    // Private methods
    void distributeToBuckets(const std::string& input_file, BucketManager& bucket_manager) const;
    [[nodiscard]] static std::vector<size_t> scanRecordOffsets(const FileUtils::MemoryMappedFile& mmap_file);
    [[nodiscard]] static MmapRecordInfo getRecordAtOffset(const FileUtils::MemoryMappedFile& mmap_file, size_t offset);
    void processShuffledBucket(const std::string& bucket_path, const BagWriter& writer, std::mutex& writer_mutex) const;
    [[nodiscard]] std::string processBucketsParallel(
        const std::vector<std::string>& bucket_paths,
        const std::string& output_path = ""
    ) const;
};

} // namespace chessmimic