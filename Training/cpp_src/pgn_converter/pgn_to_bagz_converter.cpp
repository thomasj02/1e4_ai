#include "pgn_to_bagz_converter.hpp"
#include "chess_pgn_visitor.hpp"
#include "record_processor.hpp"
#include "../utils/progress_tracker.hpp"
#include "../utils/stopwatch.hpp"

#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <unistd.h> // For getpid


namespace chessmimic {
PgnToBagzConverter::PgnToBagzConverter(Config config)
    : config_(std::move(config)) {
    // Auto-detect total buffer memory if not specified (use 50% of system RAM)
    config_.total_buffer_memory_mb = autoDetectMemoryMB(config_.total_buffer_memory_mb, 0.5);

    // Ensure temp directory is set
    if (config_.temp_dir.empty()) {
        // Generate a unique temp directory by adding a timestamp and process ID
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count() % 1000;
        std::stringstream ss;
        ss << "pgn_to_bagz_" << std::put_time(std::localtime(&now_time_t), "%Y%m%d_%H%M%S")
            << "_" << now_ms << "_" << getpid();
        config_.temp_dir = std::filesystem::temp_directory_path() / ss.str();
    }

    // Ensure directories exist
    ensureDirectories();

    // Initialize thread pool and logger using base class methods
    thread_pool_ = createThreadPool(config_.num_threads);
    logger_ = createLogger(1);  // Default to INFO level

    // Initialize external sorter
    external_sorter_ = std::make_unique<ExternalSorter>(
        config_.temp_dir,
        config_.sort_chunk_size,
        thread_pool_.get(),
        logger_.get(),
        config_.keep_temp_files
    );

    // Initialize pgn processor
    pgn_processor_ = std::make_unique<PgnProcessor>(
        config_.temp_dir,
        config_.min_rating,
        config_.max_rating,
        config_.min_ply,
        config_.recent_moves,
        config_.min_clock_seconds,
        config_.max_games,
        thread_pool_.get(),
        logger_.get()
    );

    // Initialize shuffle manager
    shuffle_manager_ = std::make_unique<ShuffleManager>(
        config_.temp_dir,
        config_.shuffle_seed,
        config_.keep_temp_files,
        thread_pool_.get(),
        logger_.get()
    );

    CM_LOG_INFO("Initialized with {} threads", config_.num_threads);
    size_t system_memory_mb = FileUtils::getSystemMemory() / (1024 * 1024);
    CM_LOG_INFO("System memory: {} MB, using {} MB ({:.0f}%) for total buffer memory", 
                 system_memory_mb, config_.total_buffer_memory_mb, 
                 config_.total_buffer_memory_mb * 100.0 / system_memory_mb);
    CM_LOG_INFO("Using temporary directory: {}", config_.temp_dir);
}

PgnToBagzConverter::~PgnToBagzConverter() {
    if (!config_.keep_temp_files) {
        cleanupTempFiles();
    }
}

void PgnToBagzConverter::convert() {
    CM_LOG_INFO("Converting {} source(s) → {}", config_.pgn_paths.size(), config_.bagz_path);

    // Start the overall timer
    startTimer("Total");

    try {
        // Clear any previous common moves data
        RecordProcessor::clearCommonMoves();

        // If we're skipping common moves, read the common moves file
        if (config_.skip_common_moves && !config_.common_moves_path.empty()) {
            int numCommonMoves = RecordProcessor::readCommonMovesFile(config_.common_moves_path);
            if (numCommonMoves < 0) {
                CM_LOG_INFO("Error: Failed to read common moves file: {}", config_.common_moves_path);
                throw std::runtime_error("Failed to read common moves file");
            }
            CM_LOG_INFO("Read {} common positions from {}", numCommonMoves, config_.common_moves_path);
        }

        // Phase 1: Extract records from PGN files
        CM_LOG_INFO("Phase 1: Extracting records from PGN files...");
        startTimer("Phase 1: Extract Records");
        std::vector<std::string> record_files = runPhase1_ExtractRecords();
        stopTimer("Phase 1: Extract Records");
        CM_LOG_INFO("Phase 1 completed in {}",
                     StopwatchManager::getInstance().getElapsedSeconds("Phase 1: Extract Records"));

        if (config_.run_phases == 1) {
            CM_LOG_INFO("Completed Phase 1 only.");
            // Keep temp files for inspection if only running phase 1
            config_.keep_temp_files = true;

            // Print timing report and stop the overall timer
            stopTimer("Total");
            printTimingReport();
            return;
        }

        // Phase 2: Sort, aggregate and shuffle records
        CM_LOG_INFO("Phase 2: Sorting, aggregating, and shuffling records...");
        startTimer("Phase 2: Sort and Shuffle");
        std::string shuffled_file = runPhase2_SortAndShuffle(record_files);
        stopTimer("Phase 2: Sort and Shuffle");
        CM_LOG_INFO("Phase 2 completed in {}",
                     StopwatchManager::getInstance().getElapsedSeconds("Phase 2: Sort and Shuffle"));

        if (config_.run_phases == 2) {
            CM_LOG_INFO("Completed Phases 1 and 2.");
            // Keep temp files for inspection if not running phase 3
            config_.keep_temp_files = true;

            // Print timing report and stop the overall timer
            stopTimer("Total");
            printTimingReport();
            return;
        }

        // Write common moves file if enabled
        if (config_.write_common_moves && config_.max_moves_per_position > 0) {
            startTimer("Write Common Moves");
            int num_common_positions = RecordProcessor::writeCommonMovesFile(config_.common_moves_path);
            stopTimer("Write Common Moves");

            if (num_common_positions > 0) {
                CM_LOG_INFO("Wrote {} common positions to {}", num_common_positions, config_.common_moves_path);
            }
            else if (num_common_positions == 0) {
                CM_LOG_INFO("No common positions found (positions with > {} moves)", config_.max_moves_per_position);
            }
            else {
                CM_LOG_INFO("Failed to write common positions file {}", config_.common_moves_path);
            }
        }

        // Stop the overall timer and print timing report
        stopTimer("Total");
        CM_LOG_INFO("All phases completed successfully in {}",
                     StopwatchManager::getInstance().getElapsedSeconds("Total"));
        printTimingReport();
    }
    catch (const std::exception& e) {
        CM_LOG_INFO("Error: {}", e.what());
        // Stop all timers in case of error
        try {
            stopTimer("Phase 1: Extract Records");
        }
        catch (...) {
        }
        try {
            stopTimer("Phase 2: Sort and Shuffle");
        }
        catch (...) {
        }
        try {
            stopTimer("Write Common Moves");
        }
        catch (...) {
        }
        try {
            stopTimer("Total");
        }
        catch (...) {
        }

        CM_LOG_INFO("Error: {}", e.what());

        // Print timing report even in case of error
        try {
            printTimingReport();
        }
        catch (...) {
        }
        throw;
    }
}

void PgnToBagzConverter::initializeBucketManager() {
    if (!bucket_manager_) {
        bucket_manager_ = std::make_unique<BucketManager>(
            config_.temp_dir,
            config_.shuffle_buckets,
            config_.total_buffer_memory_mb,
            config_.max_bucket_size_mb * 1024 * 1024, // Convert MB to bytes
            logger_.get()
        );
        bucket_manager_->initialize();
        CM_LOG_INFO("Initialized BucketManager with max bucket size: {} MB",
                     config_.max_bucket_size_mb);
    }
}

// Phase 1: Extract records from PGN files
std::vector<std::string> PgnToBagzConverter::runPhase1_ExtractRecords() const {
    // Create output directory for records
    SCOPED_TIMER("Phase 1.1: Directory Setup");
    std::string records_dir = config_.temp_dir + "/records";
    FileUtils::ensureDirectoryExists(records_dir);

    // Use the PgnProcessor to process all PGN files
    {
        SCOPED_TIMER("Phase 1.2: PGN Processing");
        return pgn_processor_->extractRecordsFromPgn(config_.pgn_paths, records_dir);
    }
}

// Phase 2: Sort, aggregate and shuffle records
std::string PgnToBagzConverter::runPhase2_SortAndShuffle(const std::vector<std::string>& record_files) {
    // Use the combined sort and aggregate method that avoids the intermediate file
    CM_LOG_DEBUG("Sorting & aggregating {} record files from Phase 1", record_files.size());

    std::string aggregated_file;
    {
        SCOPED_TIMER("Phase 2.1: Sort and Aggregate");
        aggregated_file = external_sorter_->sortAndAggregateRecords(
            record_files,
            config_.max_moves_per_position
        );
    }

    // Step 3: Shuffle the records
    if (config_.shuffle_buckets > 0) {
        CM_LOG_INFO("Using two-pass bucket shuffle with {} buckets", config_.shuffle_buckets);

        // Initialize bucket manager for two-pass shuffle
        {
            SCOPED_TIMER("Phase 2.2: Initialize Bucket Manager");
            initializeBucketManager();
        }

        // Convert bagz_path to absolute path if it's relative
        std::string absolute_bagz_path = config_.bagz_path;
        if (!std::filesystem::path(absolute_bagz_path).is_absolute()) {
            // Make it absolute based on current working directory
            absolute_bagz_path = std::filesystem::absolute(absolute_bagz_path).string();
            CM_LOG_INFO("Converting relative path to absolute: {} → {}",
                         config_.bagz_path, absolute_bagz_path);
        }

        // Pass the final output path to the bucket shuffle process
        // This will make it write directly to the final BAGZ file
        std::string final_output;
        {
            SCOPED_TIMER("Phase 2.3: Bucket Shuffle");
            final_output = shuffle_manager_->bucketShuffleRecords(
                aggregated_file,
                *bucket_manager_,
                absolute_bagz_path // Pass the absolute final output path
            );
        }

        // Return the path to the final output file
        return final_output;
    }

    CM_LOG_INFO("Using traditional in-memory shuffle");

    // Use ShuffleManager to perform in-memory shuffle
    // Note: This still uses the temp directory for the output
    {
        SCOPED_TIMER("Phase 2.3: In-Memory Shuffle");
        return shuffle_manager_->shuffleRecords(aggregated_file);
    }
}


void PgnToBagzConverter::ensureDirectories() const {
    // Ensure directories exist
    FileUtils::ensureDirectoryExists(config_.temp_dir);
    FileUtils::ensureDirectoryExists(std::filesystem::path(config_.bagz_path).parent_path());
}

void PgnToBagzConverter::cleanupTempFiles() const {
    // Use base class method for cleanup
    DataConverterBase::cleanupTempFiles(config_.temp_dir);
}
} // namespace chessmimic
