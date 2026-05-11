#include "chunk_processor.hpp"
#include "clock_game_parser.hpp"
#include "../io/lz4_pgn_stream_reader.hpp"
#include "../io/lz4_constants.hpp"
#include <algorithm>
#include <filesystem>
#include <fmt/format.h>
#include <chrono>
#include <fstream>

namespace chessmimic::clock_converter {
ChunkProcessor::ChunkProcessor(size_t max_chunk_size, size_t num_threads, Logger* logger)
    : max_chunk_size_(max_chunk_size),
      num_threads_(num_threads == 0 ? std::thread::hardware_concurrency() : num_threads),
      logger_(logger) {
    thread_pool_ = std::make_unique<ThreadPool>(num_threads_);
    thread_chunk_files_.resize(num_threads_);
    thread_chunk_mutexes_.resize(num_threads_);
    for (size_t i = 0; i < num_threads_; ++i) {
        thread_chunk_mutexes_[i] = std::make_unique<std::mutex>();
    }
}

std::vector<std::string> ChunkProcessor::processFiles(const std::vector<std::string>& pgn_files,
                                                      const std::string& output_dir) {
    // Create output directory if needed
    std::filesystem::create_directories(output_dir);

    // Process files in parallel
    std::vector<std::future<void>> futures;

    for (size_t i = 0; i < pgn_files.size(); ++i) {
        size_t thread_id = i % num_threads_;

        futures.push_back(thread_pool_->enqueue([this, &pgn_files, i, &output_dir, thread_id] {
            processFile(pgn_files[i], output_dir, thread_id);
        }));
    }

    // Wait for all processing to complete
    for (auto& future : futures) {
        future.get();
    }

    // Collect all chunk files
    std::vector<std::string> all_chunks;
    for (const auto& thread_chunks : thread_chunk_files_) {
        all_chunks.insert(all_chunks.end(), thread_chunks.begin(), thread_chunks.end());
    }

    return all_chunks;
}

void ChunkProcessor::processFile(const std::string& pgn_file,
                                 const std::string& output_dir,
                                 size_t thread_id) {
    // Check if file exists
    if (!std::filesystem::exists(pgn_file)) {
        throw std::runtime_error(
            "PGN file not found: " + pgn_file + "\n"
            "Please verify:\n"
            "  - The file path is correct\n"
            "  - The file exists in the specified location\n"
            "  - You have read permissions for the file\n"
            "Use absolute paths for clarity, e.g.: /data/chess/games.pgn.lz4"
        );
    }

    // Get file info for logging
    auto file_size = std::filesystem::file_size(pgn_file);
    auto file_name = std::filesystem::path(pgn_file).filename().string();
    
    // Log file processing start
    size_t file_num = files_processed_.fetch_add(1) + 1;
    
    if (logger_) {
        CM_LOG_INFO("  [{}] Processing: {} ({:.2f} MB)", 
                      file_num,
                      file_name, 
                      static_cast<double>(file_size) / (1024.0 * 1024.0));
    }
    
    auto file_start_time = std::chrono::steady_clock::now();

    // Parse games and extract clock records
    ClockGameParser parser;

    std::vector<std::pair<std::string, ClockPositionRecord>> current_chunk;
    size_t current_chunk_size = 0;
    size_t chunk_id = 0;

    // Process file using LZ4PgnStreamReader (handles both .lz4 and plain text)
    LZ4PgnStreamReader reader(LZ4_DEFAULT_INPUT_BUFFER_SIZE, LZ4_DEFAULT_OUTPUT_BUFFER_SIZE);
    reader.processFile(pgn_file, [&](const std::string& pgn_game) {
        // Parse the complete game
        if (std::vector<ClockPositionRecord> records; parser.parseGame(pgn_game, records) && !records.empty()) {
            processGameRecords(records, current_chunk, current_chunk_size,
                             chunk_id, output_dir, thread_id);
        }
    });

    // Write final chunk if any
    if (!current_chunk.empty()) {
        std::ranges::sort(current_chunk,
                          [](const auto& a, const auto& b) {
                              return a.first < b.first;
                          });

        {
            std::string chunk_file = writeChunk(current_chunk, output_dir, thread_id, chunk_id);
            std::lock_guard lock(*thread_chunk_mutexes_[thread_id]);
            thread_chunk_files_[thread_id].push_back(chunk_file);
        }

        total_chunks_.fetch_add(1);

        // Update peak memory for final chunk
        size_t current_peak = peak_memory_.load();
        while (current_chunk_size > current_peak &&
            !peak_memory_.compare_exchange_weak(current_peak, current_chunk_size)) {
            // Loop until successful
        }
    }
    
    // Log file completion
    if (logger_) {
        auto file_end_time = std::chrono::steady_clock::now();
        auto file_duration = std::chrono::duration_cast<std::chrono::seconds>(file_end_time - file_start_time);
        
        CM_LOG_INFO("        Completed: {} in {}s", 
                      file_name, file_duration.count());
    }
}

void ChunkProcessor::processGameRecords(const std::vector<ClockPositionRecord>& records,
                                       std::vector<std::pair<std::string, ClockPositionRecord>>& current_chunk,
                                       size_t& current_chunk_size,
                                       size_t& chunk_id,
                                       const std::string& output_dir,
                                       size_t thread_id) {
    total_games_.fetch_add(1);

    for (const auto& record : records) {
        // Generate position key
        std::string key = record.getPositionKey();

        // Estimate size
        size_t record_size = estimateRecordSize(record);

        // Check if we need to write current chunk
        if (current_chunk_size + record_size > max_chunk_size_ && !current_chunk.empty()) {
            // Sort and write chunk
            std::ranges::sort(current_chunk,
                              [](const auto& a, const auto& b) {
                                  return a.first < b.first;
                              });

            {
                std::string chunk_file = writeChunk(current_chunk, output_dir, thread_id, chunk_id++);
                std::lock_guard lock(*thread_chunk_mutexes_[thread_id]);
                thread_chunk_files_[thread_id].push_back(chunk_file);
            }

            total_chunks_.fetch_add(1);

            // Update peak memory
            size_t current_peak = peak_memory_.load();
            while (current_chunk_size > current_peak &&
                !peak_memory_.compare_exchange_weak(current_peak, current_chunk_size)) {
                // Loop until successful
            }

            // Clear chunk
            current_chunk.clear();
            current_chunk_size = 0;
        }

        // Add record to chunk
        current_chunk.emplace_back(key, record);
        current_chunk_size += record_size;
        total_records_.fetch_add(1);

        // Update peak memory
        size_t current_peak = peak_memory_.load();
        while (current_chunk_size > current_peak &&
            !peak_memory_.compare_exchange_weak(current_peak, current_chunk_size)) {
            // Loop until successful
        }
    }
}

std::string ChunkProcessor::writeChunk(const std::vector<std::pair<std::string, ClockPositionRecord>>& records,
                                       const std::string& output_dir,
                                       size_t thread_id,
                                       size_t chunk_id) {
    // Generate chunk filename
    std::string filename = fmt::format("chunk_t{}_c{}.sorted", thread_id, chunk_id);
    std::filesystem::path chunk_path = std::filesystem::path(output_dir) / filename;

    // Write sorted records
    std::ofstream file(chunk_path);
    if (!file) {
        throw std::runtime_error(
            "Failed to create temporary chunk file: " + chunk_path.string() + "\n"
            "This is needed for intermediate processing results.\n"
            "Possible causes:\n"
            "  - Output directory does not exist: " + output_dir + "\n"
            "  - Insufficient permissions to write to temp directory\n"
            "  - Disk is full or quota exceeded\n"
            "  - Invalid filename generated\n"
            "Use --temp-dir to specify a writable location with sufficient space"
        );
    }

    for (const auto& [key, record] : records) {
        file << key << "\t" << record.toJson() << "\n";
    }

    file.close();
    return chunk_path.string();
}

size_t ChunkProcessor::estimateRecordSize(const ClockPositionRecord& record) {
    // Estimate based on JSON serialization size
    // This is approximate but good enough for chunking
    size_t size = 200; // Base overhead
    size += record.fen.size();
    size += record.recent_moves.size() * 6; // Approximate move size
    size += 50; // Numeric fields
    return size;
}

ChunkProcessor::Statistics ChunkProcessor::getStatistics() const {
    Statistics stats;
    stats.total_records_processed = total_records_.load();
    stats.total_chunks_created = total_chunks_.load();
    stats.peak_memory_usage = peak_memory_.load();
    stats.total_games_processed = total_games_.load();
    return stats;
}
} // namespace chessmimic::clock_converter
