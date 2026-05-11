#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include "clock_position_record.hpp"
#include "../utils/thread_pool.hpp"
#include "../utils/logger.hpp"

namespace chessmimic::clock_converter {

/**
 * Processes PGN files to extract clock position records and write them
 * to sorted chunk files for later merging.
 */
class ChunkProcessor {
public:
    struct Statistics {
        size_t total_records_processed = 0;
        size_t total_chunks_created = 0;
        size_t peak_memory_usage = 0;
        size_t total_games_processed = 0;
    };
    
    /**
     * Create a chunk processor.
     * 
     * @param max_chunk_size Maximum size of each chunk in bytes
     * @param num_threads Number of threads to use (default: hardware concurrency)
     * @param logger Logger instance (optional)
     */
    explicit ChunkProcessor(size_t max_chunk_size, size_t num_threads = 0, Logger* logger = nullptr);
    
    /**
     * Process PGN files and create sorted chunks.
     * 
     * @param pgn_files List of PGN files to process
     * @param output_dir Directory to write chunk files
     * @return List of created chunk file paths
     */
    std::vector<std::string> processFiles(const std::vector<std::string>& pgn_files,
                                         const std::string& output_dir);
    
    /**
     * Get processing statistics.
     * 
     * @return Current statistics
     */
    [[nodiscard]] Statistics getStatistics() const;
    
private:
    // Process a single PGN file
    void processFile(const std::string& pgn_file,
                    const std::string& output_dir,
                    size_t thread_id);
    
    // Process records from a parsed game
    void processGameRecords(const std::vector<ClockPositionRecord>& records,
                           std::vector<std::pair<std::string, ClockPositionRecord>>& current_chunk,
                           size_t& current_chunk_size,
                           size_t& chunk_id,
                           const std::string& output_dir,
                           size_t thread_id);
    
    // Write a sorted chunk to disk
    static std::string writeChunk(const std::vector<std::pair<std::string, ClockPositionRecord>>& records,
                                 const std::string& output_dir,
                                 size_t thread_id,
                                 size_t chunk_id);
    
    // Estimate memory usage of a record
    static size_t estimateRecordSize(const ClockPositionRecord& record);
    
    size_t max_chunk_size_;
    size_t num_threads_;
    std::unique_ptr<ThreadPool> thread_pool_;
    Logger* logger_;
    
    // Statistics (thread-safe)
    std::atomic<size_t> total_records_{0};
    std::atomic<size_t> total_chunks_{0};
    std::atomic<size_t> peak_memory_{0};
    std::atomic<size_t> total_games_{0};
    std::atomic<size_t> files_processed_{0};
    
    // Chunk files created by each thread
    std::vector<std::vector<std::string>> thread_chunk_files_;
    std::vector<std::unique_ptr<std::mutex>> thread_chunk_mutexes_;
};

} // namespace chessmimic::clock_converter