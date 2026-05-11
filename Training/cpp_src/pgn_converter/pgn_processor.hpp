#pragma once

#include <string>
#include <vector>

#include "../utils/thread_pool.hpp"
#include "../utils/logger.hpp"

namespace chessmimic {

// Forward declarations
class Logger;

// PgnProcessor class for handling PGN file extraction
class PgnProcessor {
public:
    // Constructor
    PgnProcessor(
        std::string temp_dir,
        int min_rating,
        int max_rating,
        int min_ply,
        int recent_moves,
        int min_clock_seconds,
        int max_games,
        ThreadPool* thread_pool,
        Logger* logger
    );

    // Public methods
    std::vector<std::string> extractRecordsFromPgn(
        const std::vector<std::string>& pgn_paths, 
        const std::string& output_dir
    );

private:
    std::string temp_dir_;
    int min_rating_;
    int max_rating_;
    int min_ply_;
    int recent_moves_;
    int min_clock_seconds_;
    int max_games_;
    ThreadPool* thread_pool_;
    Logger* logger_;

    // Private methods
    void processPgnFile(const std::string& pgn_file, const std::string& output_file);
    void processUncompressedPgnFile(std::istream& stream, const std::string& output_file);
    void processCompressedPgnFile(const std::string& pgn_file, const std::string& output_file);
    static std::vector<std::string> listPgnFiles(const std::string& dir);
};

} // namespace chessmimic