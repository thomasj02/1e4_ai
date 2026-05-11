#pragma once

#include <string>
#include <vector>
#include <memory>
#include "../core/data_converter_base.hpp"
#include "external_sorter.hpp"
#include "pgn_processor.hpp"
#include "shuffle_manager.hpp"
#include "../shuffle/bucket_manager.hpp"

namespace chessmimic {

// Main converter class - acts as a coordinator for the PGN to BAGZ conversion process
class PgnToBagzConverter : public DataConverterBase {
public:
    // Configuration
    struct Config {
        std::vector<std::string> pgn_paths; // Input PGN files or directories
        std::string bagz_path; // Output BAGZ file
        std::string temp_dir; // Temporary directory for intermediate files
        std::string common_moves_path = "common_moves.txt"; // Output file for positions with more than max_moves
        int min_rating = 0; // Minimum player rating
        int max_rating = 10000; // Maximum player rating
        int min_clock_seconds = 0; // Minimum remaining clock time
        int min_ply = 0; // Minimum game ply to include
        int recent_moves = 6; // Number of recent moves to track
        int max_moves_per_position = 25; // Max moves for a position
        int sort_chunk_size = 1'000'000; // Records per sorting chunk
        bool keep_temp_files = false; // Keep temporary files
        bool write_common_moves = true; // Write positions with more than max_moves to file
        bool skip_common_moves = false; // Skip positions in common_moves file
        int run_phases = 3; // Which phases to run (1=phase1 only, 2=phase1+2, 3=all)
        int max_games = 0; // Maximum games to process (0 = no limit)
        unsigned int num_threads = std::thread::hardware_concurrency(); // Default to available cores
        size_t shuffle_buckets = 256; // Number of buckets for two-pass shuffle
        size_t max_bucket_size_mb = 1024; // Maximum bucket file size before creating overflow bucket (MB)
        size_t total_buffer_memory_mb = 0; // Total memory for buffering across ALL buckets (MB) - 0 = auto-detect 50% of system RAM
        unsigned int shuffle_seed = 0; // Seed for shuffling (0 = random)
    };

    // Constructor
    explicit PgnToBagzConverter(Config config);
    
    // Destructor
    ~PgnToBagzConverter() override;

    // Main conversion method - coordinates the entire process
    void convert();

private:
    Config config_;
    std::unique_ptr<BucketManager> bucket_manager_; // Bucket manager for two-pass shuffle
    std::unique_ptr<ExternalSorter> external_sorter_; // External sorter for sorting records
    std::unique_ptr<PgnProcessor> pgn_processor_; // PGN processor for extracting records from PGN files
    std::unique_ptr<ShuffleManager> shuffle_manager_; // Shuffle manager for shuffling records

    // Initialize components as needed
    void initializeBucketManager();

    // Phase 1: Extract records from PGN files
    [[nodiscard]] std::vector<std::string> runPhase1_ExtractRecords() const;

    // Phase 2: Sort, aggregate and shuffle records
    std::string runPhase2_SortAndShuffle(const std::vector<std::string>& record_files);


    // Utility methods
    void cleanupTempFiles() const;
    void ensureDirectories() const;
};

} // namespace chessmimic
