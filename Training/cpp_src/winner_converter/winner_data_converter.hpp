#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <sstream>
#include "../core/data_converter_base.hpp"
#include "../pgn_converter/shuffle_manager.hpp"
#include "../io/lz4_pgn_stream_reader.hpp"
#include "../io/lz4_constants.hpp"
#include "winner_game_parser.hpp"
#include "winner_position_record.hpp"

// Forward declaration
namespace chessmimic {
    class BagzRecordWriter;
}

namespace chessmimic::winner_converter {

/**
 * Main converter class for PGN to winner-labeled BAGZ conversion.
 * Coordinates the extraction of chess positions labeled with game outcomes.
 */
class WinnerDataConverter : public DataConverterBase {
public:
    /**
     * Configuration for the converter
     */
    struct Config {
        // Input/Output
        std::vector<std::string> pgn_paths;
        std::string output_bagz_path;
        std::string temp_dir = "temp_winner_converter";
        
        // Filtering
        bool filter_draws = false;
        int min_rating = 0;
        int max_rating = 9999;
        
        // Processing
        unsigned int num_threads = std::thread::hardware_concurrency();
        double memory_limit_gb = 0.0;  // 0 = auto-detect 50% of system RAM
        
        // Shuffle
        bool shuffle_enabled = false;
        unsigned int shuffle_seed = 0;
        size_t shuffle_buckets = 256;           // Number of shuffle buckets
        size_t shuffle_memory_threshold_mb = 1024; // Threshold for in-memory vs bucket shuffle
        size_t bucket_ram_limit_mb = 4096;      // Total RAM for bucket operations
        
        // Other
        bool keep_temp_files = false;
        int log_level = 1;  // 0=ERROR, 1=INFO, 2=DEBUG
    };
    
    /**
     * Constructor with configuration
     */
    explicit WinnerDataConverter(Config config);
    
    /**
     * Destructor - cleans up temporary files if needed
     */
    ~WinnerDataConverter() override;
    
    /**
     * Main conversion method
     * @return true if conversion succeeded
     */
    bool convert();
    
    /**
     * Parse command-line arguments into Config
     * @param argc Argument count
     * @param argv Argument values
     * @param config Output configuration
     * @return true if parsing succeeded
     */
    static bool parseArguments(int argc, char* argv[], Config& config);
    
private:
    Config config_;
    std::unique_ptr<ShuffleManager> shuffle_manager_;
    WinnerGameParser game_parser_;
    
    // Statistics
    std::atomic<int> games_processed_{0};
    std::atomic<int> games_filtered_{0};
    std::atomic<int> positions_extracted_{0};
    
    /**
     * Process multiple PGN files in parallel
     * @param output_path Path to write records (temporary or final)
     * @return true if processing succeeded
     */
    bool processFiles(const std::string& output_path);
    
    /**
     * Process a single PGN file
     * @param pgn_path Path to PGN file
     * @param writer Reference to BAGZ writer
     */
    void processSingleFile(const std::string& pgn_path, BagzRecordWriter& writer);
    
    /**
     * Apply filtering to a game based on configuration
     * @param records Position records from the game
     * @param winner Game outcome (1, 0, -1)
     * @return true if game should be included
     */
    [[nodiscard]] bool shouldIncludeGame(const std::vector<WinnerPositionRecord>& records, int winner) const;
    
    /**
     * Write output with optional shuffle
     * @param temp_path Path to temporary file (if shuffle enabled)
     * @return true if writing succeeded
     */
    bool writeOutput(const std::string& temp_path);
    
    /**
     * Clean up temporary files
     */
    void cleanupTempFiles() const;
    
    /**
     * Write a position record in intermediate format for shuffling
     * @param out Output stream
     * @param record The position record to write
     */
    void writeIntermediateRecord(std::ofstream& out, const WinnerPositionRecord& record) const;

    /**
     * Process a file with LZ4 decompression support
     * @param pgn_path Path to PGN file (may be .lz4 compressed)
     * @param callback Function to process parsed game records
     */
    template<typename Callback>
    void processFileWithLZ4Support(const std::string& pgn_path, Callback callback);

    /**
     * Print usage help
     */
    static void printHelp();
};

// Template implementation must be in header
template<typename Callback>
void WinnerDataConverter::processFileWithLZ4Support(const std::string& pgn_path, Callback callback) {
    LZ4PgnStreamReader reader(LZ4_DEFAULT_INPUT_BUFFER_SIZE, LZ4_DEFAULT_OUTPUT_BUFFER_SIZE);
    reader.processFile(pgn_path, [&](const std::string& pgn_game) {
        // Parse the complete game using a string stream
        std::istringstream game_stream(pgn_game);
        game_parser_.parseStream(game_stream, callback);
    });
}

} // namespace chessmimic::winner_converter