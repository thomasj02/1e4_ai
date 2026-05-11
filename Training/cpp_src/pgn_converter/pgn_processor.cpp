#include "pgn_processor.hpp"
#include "chess_pgn_visitor.hpp"
#include "record_processor.hpp"
#include "../utils/progress_tracker.hpp"
#include "../utils/stopwatch.hpp"
#include "../utils/file_utils.hpp"

#include <lz4.h>
#include <lz4frame.h>
#include <iostream>
#include <filesystem>
#include <iomanip>
#include <utility>
#include <memory>
#include <fstream>
#include <atomic>
#include <mutex>
#include <exception>


// Use the file utilities
using namespace chessmimic::FileUtils;

namespace chessmimic {

PgnProcessor::PgnProcessor(
    std::string temp_dir,
    int min_rating,
    int max_rating,
    int min_ply,
    int recent_moves,
    int min_clock_seconds,
    int max_games,
    ThreadPool* thread_pool,
    Logger* logger
) : temp_dir_(std::move(temp_dir)),
    min_rating_(min_rating),
    max_rating_(max_rating),
    min_ply_(min_ply),
    recent_moves_(recent_moves),
    min_clock_seconds_(min_clock_seconds),
    max_games_(max_games),
    thread_pool_(thread_pool),
    logger_(logger) {
    
    // Ensure the temp directory exists
    ensureDirectoryExists(temp_dir_);
}

std::vector<std::string> PgnProcessor::extractRecordsFromPgn(
    const std::vector<std::string>& pgn_paths, 
    const std::string& output_dir
) {
    SCOPED_TIMER("PgnProcessor::extractRecordsFromPgn");
    
    // First, gather all files to process
    std::vector<std::string> all_pgn_files;
    std::vector<std::string> output_files;

    // Process each input path
    {
        SCOPED_TIMER("PgnProcessor::FindPgnFiles");
        for (const auto& pgn_path : pgn_paths) {
            if (std::filesystem::is_directory(pgn_path)) {
                // Process all .pgn files in directory
                auto pgn_files = listPgnFiles(pgn_path);
                all_pgn_files.insert(all_pgn_files.end(), pgn_files.begin(), pgn_files.end());
            }
            else {
                // Add single file to the list
                all_pgn_files.push_back(pgn_path);
            }
        }
    }

    size_t total_files = all_pgn_files.size();
    CM_LOG_INFO("Processing {} PGN files using parallel processing", total_files);

    // Create progress tracker
    ProgressTracker progress(total_files, "PGN Processing");

    // Process files in parallel using thread pool
    std::atomic files_processed{0};
    std::vector<std::future<void>> futures;
    output_files.reserve(all_pgn_files.size());

    // Ensure output directory exists
    ensureDirectoryExists(output_dir);
    // ReSharper disable once CppTooWideScope
    std::mutex cout_mutex;  // This needs to be wider scoped to be used in the lambda

    // Process all collected files
    {
        SCOPED_TIMER("PgnProcessor::ScheduleFileTasks");
        for (size_t i = 0; i < all_pgn_files.size(); ++i) {
            const auto& pgn_file = all_pgn_files[i];
            std::string full_pgn_path = std::filesystem::path(pgn_file).string();
            std::ranges::replace(full_pgn_path, '/', '_');
            std::string output_file = output_dir + "/" + full_pgn_path + ".records";
            output_files.push_back(output_file);

            // Enqueue task to thread pool
            futures.push_back(thread_pool_->enqueue([this, pgn_file, output_file, i, &all_pgn_files,
                &files_processed, &cout_mutex, &progress] {
                try {
                    // Thread-safe output
                    {
                        std::lock_guard lock(cout_mutex);
                        CM_LOG_INFO("Processing file {}/{}: {}", i + 1, all_pgn_files.size(), pgn_file);
                    }

                    // Add timer for individual file processing
                    SCOPED_TIMER("PgnProcessor::ProcessSingleFile");

                    // Process the file
                    processPgnFile(pgn_file, output_file);

                    // Update counter and report progress
                    int completed = ++files_processed;
                    progress.update();

                    if (completed % 10 == 0 || completed == static_cast<int>(all_pgn_files.size())) {
                        std::lock_guard lock(cout_mutex);
                        CM_LOG_INFO("Completed {}/{} files", completed, all_pgn_files.size());
                    }
                }
                catch (const std::exception& e) {
                    CM_LOG_ERROR("Worker exception while processing {}: {}", pgn_file, e.what());
                    throw;
                }
                catch (...) {
                    CM_LOG_ERROR("Worker threw non-standard exception while processing {}", pgn_file);
                    throw;
                }
            }));
        }
    }

    // Wait for all tasks to complete
    {
        SCOPED_TIMER("PgnProcessor::WaitForCompletion");
        int task_index = 0;
        for (auto& future : futures) {
            try {
                future.get(); // Use get() instead of wait()
            }
            catch (const std::exception& e) {
                CM_LOG_ERROR("Fatal: task for file {} threw {}", all_pgn_files[task_index], e.what());
                std::terminate();
            }
            catch (...) {
                CM_LOG_ERROR("Fatal: task for file {} threw an unknown exception", all_pgn_files[task_index]);
                std::terminate();
            }
            task_index++;
        }
        // Optional: thread_pool_->wait_all(); // May not be strictly necessary after getting all futures
    }
    CM_LOG_INFO("Processed {} files in total.", total_files);
    return output_files;
}

void PgnProcessor::processPgnFile(const std::string& pgn_file, const std::string& output_file) {
    SCOPED_TIMER("PgnProcessor::processPgnFile");
    
    if (endsWith(pgn_file, ".pgn.lz4")) {
        // Process LZ4 compressed PGN file
        SCOPED_TIMER("PgnProcessor::processCompressedFile");
        processCompressedPgnFile(pgn_file, output_file);
    }
    else {
        // Process regular PGN file
        SCOPED_TIMER("PgnProcessor::processUncompressedFile");
        std::ifstream file(pgn_file);
        throw_if_failed(file, pgn_file);
        processUncompressedPgnFile(file, output_file);
    }
}

void PgnProcessor::processUncompressedPgnFile(std::istream& stream, const std::string& output_file) {
    std::ofstream out(output_file, std::ios::binary);
    if (!out.is_open()) {
        const int err = errno;
        const std::error_code ec(err, std::generic_category());
        throw std::system_error(ec, "Failed to create output file \"" + output_file + "\": " + std::strerror(err));
    }

    // Create visitor and parser
    ChessPgnVisitor visitor(
        min_rating_,
        max_rating_,
        min_ply_,
        recent_moves_ * 2,
        min_clock_seconds_
    );

    // Reset stream state to make sure we start at the beginning
    stream.clear();
    stream.seekg(0, std::ios::beg);

    // Enable exceptions on the stream to check for failures
    stream.exceptions(std::ifstream::badbit);

    chess::pgn::StreamParser parser(stream);

    int game_count = 0;
    int valid_game_count = 0;
    int position_records = 0;

    // Set up an override for the endPgn method to capture records after each game
    visitor.setEndPgnCallback([&](auto& v) {
        game_count++;

        // Check if we've reached the max games limit
        if (max_games_ > 0 && game_count >= max_games_) {
            std::cout << "Reached maximum game limit: " << max_games_ << std::endl;
            v.skipPgn(true); // Skip remaining games
            return;
        }

        if (v.isValidGame()) {
            valid_game_count++;
            auto records = v.getPositionRecords();
            position_records += records.size();

            if (valid_game_count % 1000 == 0 || valid_game_count < 5) {
                std::cout << "Game " << game_count << " valid, has " << records.size()
                    << " position records. Total valid: " << valid_game_count << std::endl;
            }

            // Write each position record to the output file
            for (const auto& [key, record] : records) {
                // Use RecordProcessor to write the record
                RecordProcessor::writeRawRecord(out, key, record.toJson());
            }
        }
        else if (game_count % 5000 == 0) {
            std::cout << "Game " << game_count << " invalid, skipping. Total valid: " << valid_game_count << std::endl;
        }

        if (game_count % 10000 == 0) {
            std::cout << "Processed " << game_count << " games, " << valid_game_count
                << " valid with " << position_records << " position records" << std::endl;
        }
    });

    // Now call readGames once to process all games
    CM_LOG_INFO("Calling readGames to process all games...");

    if (auto error = parser.readGames(visitor); error && error != chess::pgn::StreamParserError::NotEnoughData) {
        CM_LOG_INFO("Warning: Error parsing PGN: {}", error.message());
    }
    else {
        CM_LOG_INFO("Successfully processed all games in file");
    }

    CM_LOG_INFO("Processed {} games, {} valid", game_count, valid_game_count);

    CM_LOG_INFO("Completed file with {} games, {} valid",
                game_count, valid_game_count);
}

void PgnProcessor::processCompressedPgnFile(const std::string& pgn_file, const std::string& output_file) {
    CM_LOG_INFO("Processing LZ4 compressed file: {}", pgn_file);

    // Open the compressed file
    std::ifstream file(pgn_file, std::ios::binary);
    throw_if_failed(file, pgn_file);

    // Create a temporary file to hold the decompressed content
    std::string full_pgn_path = std::filesystem::path(pgn_file).string();
    // Replace all '/' with '_' to avoid issues with temp file names
    std::ranges::replace(full_pgn_path, '/', '_');
    std::string temp_decompressed = temp_dir_ + "/" + full_pgn_path + ".decompressed";
    std::ofstream decompressed_out(temp_decompressed, std::ios::binary);
    if (!decompressed_out.is_open()) {
        const int err = errno;
        const std::error_code ec(err, std::generic_category());
        throw std::system_error(
            ec, "Failed to create decompressed file \"" + temp_decompressed + "\": " + std::strerror(err));
    }

    // Setup LZ4 decompression context
    LZ4F_decompressionContext_t ctx;
    if (LZ4F_errorCode_t error = LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION); LZ4F_isError(error)) {
        throw std::runtime_error("Failed to create LZ4 decompression context: " +
            std::string(LZ4F_getErrorName(error)));
    }

    // Read and decompress the file in chunks
    constexpr size_t src_buffer_size = 1024 * 1024; // 1MB input buffer
    constexpr size_t dst_buffer_size = 4 * 1024 * 1024; // 4MB output buffer
    std::vector<char> src_buffer(src_buffer_size);
    std::vector<char> dst_buffer(dst_buffer_size);

    size_t total_read = 0;
    size_t total_written = 0;

    try {
        // First read a chunk to get the frame header
        file.read(src_buffer.data(), src_buffer_size);
        size_t bytes_read = file.gcount();
        total_read += bytes_read;

        if (bytes_read == 0) {
            throw std::runtime_error("Empty LZ4 file: " + pgn_file);
        }

        // Process each chunk
        while (bytes_read > 0) {
            size_t src_pos = 0;

            while (src_pos < bytes_read) {
                size_t src_size = bytes_read - src_pos;
                size_t dst_size = dst_buffer_size;

                // Decompress a chunk
                size_t ret = LZ4F_decompress(ctx,
                                            dst_buffer.data(), &dst_size,
                                            src_buffer.data() + src_pos, &src_size,
                                            nullptr);

                if (LZ4F_isError(ret)) {
                    throw std::runtime_error("LZ4 decompression error: " +
                        std::string(LZ4F_getErrorName(ret)));
                }

                // Write decompressed data
                decompressed_out.write(dst_buffer.data(), static_cast<std::streamsize>(dst_size));
                total_written += dst_size;

                src_pos += src_size;

                // If we've consumed all input and there's more expected, break to read more
                if (src_pos >= bytes_read && ret > 0) {
                    break;
                }
            }

            // Read the next chunk
            file.read(src_buffer.data(), src_buffer_size);
            bytes_read = file.gcount();
            total_read += bytes_read;
        }
    }
    catch (const std::exception&) {
        LZ4F_freeDecompressionContext(ctx);
        throw;
    }

    // Clean up
    LZ4F_freeDecompressionContext(ctx);
    decompressed_out.close();

    CM_LOG_INFO("Decompressed {} bytes to {} bytes ({})",
                total_read, total_written, pgn_file);

    // Let's check for multiple games in the decompressed file
    std::ifstream count_games(temp_decompressed);
    throw_if_failed(count_games, temp_decompressed);

    // Count how many games we have by looking for [Event tags
    std::string line;
    int game_markers = 0;
    while (std::getline(count_games, line)) {
        if (line.find("[Event ") != std::string::npos) {
            game_markers++;
        }
    }
    CM_LOG_INFO("Found {} games in decompressed file", game_markers);

    // Now process the decompressed file
    std::ifstream decompressed_in(temp_decompressed);
    throw_if_failed(decompressed_in, temp_decompressed);

    processUncompressedPgnFile(decompressed_in, output_file);

    // Always keep the decompressed file for debugging
    CM_LOG_INFO("Kept decompressed file at: {}", temp_decompressed);
}

std::vector<std::string> PgnProcessor::listPgnFiles(const std::string& dir) {
    std::vector<std::string> pgn_files;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (std::string path = entry.path().string(); endsWith(path, ".pgn") || endsWith(path, ".pgn.lz4")) {
            pgn_files.push_back(path);
        }
    }

    return pgn_files;
}

} // namespace chessmimic
