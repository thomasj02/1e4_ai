#pragma once

#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <unordered_map>
#include <thread>
#include "position_data.hpp"
#include "interfaces.hpp"
#include "../utils/thread_pool.hpp"
#include "../utils/logger.hpp"

namespace chessmimic {

class CommonPositionExtractor {
public:
    struct Config {
        std::string bagz_path;
        std::string output_path;
        std::string temp_dir;
        int threshold = 25;
        size_t chunk_size = 1'000'000;
        size_t num_threads = std::thread::hardware_concurrency();
        bool keep_temp_files = false;
        size_t max_records = 0;  // 0 = process all records
    };

    // Constructor for production use
    explicit CommonPositionExtractor(Config config);
    
    // Constructor for testing with dependency injection (updated to use factory)
    CommonPositionExtractor(Config config,
                           std::unique_ptr<IBagzReaderFactory> reader_factory,
                           std::unique_ptr<IFileOperations> file_ops,
                           std::shared_ptr<ThreadPool> thread_pool,
                           std::shared_ptr<Logger> logger);
    
    ~CommonPositionExtractor();

    // Main extraction method
    void extract();
    
    // Public methods for testing individual phases
    std::vector<std::string> processChunks();
    std::vector<std::string> sortChunks(const std::vector<std::string>& chunk_files);
    void mergeAndFilter(const std::vector<std::string>& sorted_files);
    
    // Static utility methods that can be tested independently
    static PositionData extractPositionFromRecord(const std::string& record_str);
    static std::string extractFenFromRecord(const std::string& record_json);
    static void aggregatePosition(PositionData& target, const PositionData& source);

    // Non-static methods that use file_ops_
    std::vector<PositionData> loadPositionsFromFile(const std::string& path) const;
    void savePositionsToFile(const std::string& path, const std::vector<PositionData>& positions) const;

private:
    Config config_;
    std::unique_ptr<IBagzReaderFactory> reader_factory_;
    std::unique_ptr<IFileOperations> file_ops_;
    std::shared_ptr<ThreadPool> thread_pool_;
    std::shared_ptr<Logger> logger_;
    
    // Thread-local storage for readers
    mutable std::unordered_map<std::thread::id, std::unique_ptr<IBagzReader>> reader_pool_;
    mutable std::mutex pool_mutex_; // Protects reader_pool_ access

    // Internal methods
    std::string processChunk(size_t start_idx, size_t end_idx) const;
    std::string sortChunk(const std::string& chunk_file, size_t chunk_idx);
    void mergeAndFilterInternal(const std::vector<std::vector<std::string>>& file_lines);
    
    void ensureTempDirectory();
    void cleanupTempFiles() const;
    
    // Get or create a reader for the current thread
    IBagzReader* getThreadLocalReader() const;
};

} // namespace chessmimic