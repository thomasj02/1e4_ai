#pragma once

#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include "../core/data_converter_base.hpp"
#include "../pgn_converter/shuffle_manager.hpp"

namespace chessmimic {

/**
 * Main coordinator class for converting PGN files to clock training BAGZ format
 */
class ClockDataConverter : public DataConverterBase {
public:
    // Configuration structure
    struct Config {
        // Input/Output paths
        std::vector<std::string> pgn_paths;
        std::string output_bagz_path;
        std::string temp_dir = "temp_clock_converter";
        
        // Common positions files
        std::string common_positions_with_history_path;
        std::string common_positions_fen_only_path;
        bool write_common_positions = false;
        bool skip_common_positions = false;
        
        // Processing parameters
        unsigned int num_threads = std::thread::hardware_concurrency();
        int max_positions_per_key = 25;
        double memory_limit_gb = 0.0;  // 0 = auto-detect 50% of system RAM
        
        // Shuffle configuration
        bool shuffle_enabled = false;
        unsigned int shuffle_seed = 0;  // 0 = random seed
        size_t shuffle_buckets = 1024;  // Number of buckets for two-pass shuffle
        size_t max_bucket_size_mb = 512;  // Maximum bucket size before creating overflow bucket
        size_t shuffle_memory_threshold_mb = 4096;  // File size threshold for in-memory vs bucket shuffle
        size_t total_buffer_memory_mb = 0;  // Total memory for buffering across ALL buckets (0 = auto-detect 50% of system RAM)
        
        // Other options
        bool keep_temp_files = false;
        int log_level = 1;  // 0=ERROR, 1=INFO, 2=DEBUG
    };

    /**
     * Constructor
     */
    explicit ClockDataConverter(Config  config);
    
    /**
     * Destructor
     */
    ~ClockDataConverter() override;

    /**
     * Main conversion method
     */
    void convert();

    /**
     * Parse command line arguments
     */
    static Config parseArguments(int argc, char* argv[]);

    /**
     * Validate configuration
     */
    static void validateConfig(const Config& config);

    /**
     * Get calculated chunk size for memory management
     */
    [[nodiscard]] size_t getMaxChunkSize() const { return max_chunk_size_; }

    /**
     * Distribute files across threads
     */
    static std::vector<std::vector<std::string>> distributeFiles(
        const std::vector<std::string>& files, 
        unsigned int num_threads
    );
    

private:
    Config config_;
    size_t max_chunk_size_;
    std::filesystem::path temp_path_;

    /**
     * Initialize components
     */
    void initialize();

    /**
     * Calculate memory limits based on system resources
     */
    void calculateMemoryLimits();

    /**
     * Create necessary directories
     */
    void createDirectories();

    /**
     * Clean up temporary files
     */
    void cleanupTempFiles() const;

    /**
     * Phase 1: Parallel extraction and sorting
     */
    [[nodiscard]] std::vector<std::string> runPhase1_ExtractAndSort();

    /**
     * Phase 2: K-way merge with group counting
     */
    void runPhase2_MergeAndWrite(const std::vector<std::string>& sorted_chunks);

    /**
     * Print usage information
     */
    static void printUsage();
    
    /**
     * Helper function to collect multiple arguments after a flag until the next flag
     */
    static std::vector<std::string> getMultipleArgumentsUntilNextFlag(int argc, char* argv[], const std::string& flag);
    
    /**
     * Helper to format duration in human-readable format
     */
    static std::string formatDuration(std::chrono::seconds duration);

    /**
     * Expand paths to collect all PGN files (handles directories)
     */
    [[nodiscard]] std::vector<std::string> collectPgnFiles() const;
};

} // namespace chessmimic