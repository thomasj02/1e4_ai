#include "clock_data_converter.hpp"
#include "chunk_processor.hpp"
#include "sorted_merge_writer.hpp"
#include "../core/bagz.hpp"
#include "../utils/file_utils.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <utility>
#include <sys/sysinfo.h>
#include <chrono>

#include "../shuffle/bucket_manager.hpp"

namespace chessmimic {
ClockDataConverter::ClockDataConverter(Config config)
    : config_(std::move(config)), max_chunk_size_(0) {
    // Auto-detect total buffer memory if not specified
    config_.total_buffer_memory_mb = autoDetectMemoryMB(config_.total_buffer_memory_mb, 0.5);
    initialize();
}

ClockDataConverter::~ClockDataConverter() {
    if (!config_.keep_temp_files) {
        cleanupTempFiles();
    }
}

void ClockDataConverter::initialize() {
    // Create logger and thread pool using base class methods
    logger_ = createLogger(config_.log_level);
    thread_pool_ = createThreadPool(config_.num_threads);

    // Calculate memory limits
    calculateMemoryLimits();

    // Create directories
    createDirectories();

    // Log configuration details
    CM_LOG_INFO("=== Clock Data Converter Configuration ===");
    CM_LOG_INFO("Threads: {}", config_.num_threads);
    CM_LOG_INFO("Memory limit: {:.2f} GB", config_.memory_limit_gb);
    CM_LOG_INFO("Memory per thread: {:.2f} GB", 
                static_cast<double>(max_chunk_size_) / (1024.0 * 1024.0 * 1024.0));
    CM_LOG_INFO("Output file: {}", config_.output_bagz_path);
    CM_LOG_INFO("Temp directory: {}", config_.temp_dir);
    
    if (config_.shuffle_enabled) {
        CM_LOG_INFO("Shuffling: ENABLED (seed: {}, buckets: {})", 
                    config_.shuffle_seed, config_.shuffle_buckets);
        CM_LOG_INFO("Shuffle memory threshold: {} MB", config_.shuffle_memory_threshold_mb);
        CM_LOG_INFO("Total buffer memory: {} MB", config_.total_buffer_memory_mb);
    } else {
        CM_LOG_INFO("Shuffling: DISABLED");
    }
    
    if (config_.write_common_positions) {
        CM_LOG_INFO("Writing common positions (threshold: {} occurrences)", 
                    config_.max_positions_per_key);
    }
    
    if (config_.skip_common_positions) {
        CM_LOG_INFO("Skipping common positions from: {}", 
                    config_.common_positions_fen_only_path);
    }
    
    CM_LOG_INFO("========================================");
}

void ClockDataConverter::calculateMemoryLimits() {
    // Auto-detect memory limit using 50% of system memory
    config_.memory_limit_gb = autoDetectMemory(config_.memory_limit_gb, 0.5);

    // Calculate chunk size: total_memory / num_threads
    double available_memory_gb = config_.memory_limit_gb;
    double chunk_size_gb = available_memory_gb / config_.num_threads;
    max_chunk_size_ = static_cast<size_t>(chunk_size_gb * 1024 * 1024 * 1024);

    // Format to 2 decimal places
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << chunk_size_gb;
    CM_LOG_INFO("Memory per thread: {} GB", ss.str());
}

void ClockDataConverter::createDirectories() {
    temp_path_ = std::filesystem::path(config_.temp_dir);

    // Create temp directory using base class method
    ensureTempDirectory(config_.temp_dir);

    // Create output directory if it has a parent
    if (std::filesystem::path output_path(config_.output_bagz_path); output_path.has_parent_path() && !output_path.
        parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    // Create common positions directories if needed
    if (config_.write_common_positions) {
        if (!config_.common_positions_with_history_path.empty()) {
            if (std::filesystem::path history_path(config_.common_positions_with_history_path); history_path.
                has_parent_path() && !history_path.parent_path().empty()) {
                std::filesystem::create_directories(history_path.parent_path());
            }
        }
        if (!config_.common_positions_fen_only_path.empty()) {
            if (std::filesystem::path fen_path(config_.common_positions_fen_only_path); fen_path.has_parent_path() && !
                fen_path.parent_path().empty()) {
                std::filesystem::create_directories(fen_path.parent_path());
            }
        }
    }
}

void ClockDataConverter::cleanupTempFiles() const {
    // Use base class method for cleanup
    DataConverterBase::cleanupTempFiles(temp_path_);
}

void ClockDataConverter::convert() {
    auto start_time = std::chrono::steady_clock::now();

    CM_LOG_INFO("Starting clock data conversion");

    // Expand directories to get all PGN files
    auto expanded_files = collectPgnFiles();
    if (expanded_files.empty()) {
        throw std::runtime_error("No PGN files found in the specified paths");
    }

    // Log directory expansion if any directories were provided
    if (expanded_files.size() != config_.pgn_paths.size()) {
        CM_LOG_INFO("Expanded {} input paths to {} PGN files",
                    config_.pgn_paths.size(), expanded_files.size());
    }

    // Replace paths with expanded file list
    config_.pgn_paths = std::move(expanded_files);
    CM_LOG_INFO("Processing {} PGN files", config_.pgn_paths.size());

    // Calculate total input size
    size_t total_input_size = 0;
    for (const auto& path : config_.pgn_paths) {
        total_input_size += std::filesystem::file_size(path);
    }
    CM_LOG_INFO("Total input size: {:.2f} MB",
                static_cast<double>(total_input_size) / (1024.0 * 1024.0));

    // Phase 1: Extract and sort
    auto phase1_start = std::chrono::steady_clock::now();
    auto sorted_chunks = runPhase1_ExtractAndSort();
    auto phase1_end = std::chrono::steady_clock::now();
    auto phase1_duration = std::chrono::duration_cast<std::chrono::seconds>(phase1_end - phase1_start);
    CM_LOG_INFO("Phase 1 completed in {}", formatDuration(phase1_duration));

    // Phase 2: Merge and write
    auto phase2_start = std::chrono::steady_clock::now();
    runPhase2_MergeAndWrite(sorted_chunks);
    auto phase2_end = std::chrono::steady_clock::now();
    auto phase2_duration = std::chrono::duration_cast<std::chrono::seconds>(phase2_end - phase2_start);
    CM_LOG_INFO("Phase 2 completed in {}", formatDuration(phase2_duration));

    // Total time
    auto end_time = std::chrono::steady_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    
    CM_LOG_INFO("========================================");
    CM_LOG_INFO("Conversion complete!");
    CM_LOG_INFO("Total time: {}", formatDuration(total_duration));
    CM_LOG_INFO("Output file: {}", config_.output_bagz_path);
    
    // Check output file size
    if (std::filesystem::exists(config_.output_bagz_path)) {
        size_t output_size = std::filesystem::file_size(config_.output_bagz_path);
        CM_LOG_INFO("Output size: {:.2f} MB", 
                    static_cast<double>(output_size) / (1024.0 * 1024.0));
    }
}

std::vector<std::string> ClockDataConverter::runPhase1_ExtractAndSort() {
    CM_LOG_INFO("Phase 1: Extracting and sorting positions");
    CM_LOG_INFO("  - Reading PGN files and parsing games");
    CM_LOG_INFO("  - Extracting clock data from each position");
    CM_LOG_INFO("  - Creating sorted chunks for efficient merging");

    // Create chunk processor
    clock_converter::ChunkProcessor processor(max_chunk_size_, config_.num_threads, logger_.get());

    // Process files and generate sorted chunks
    CM_LOG_INFO("Processing {} files using {} threads...", 
                  config_.pgn_paths.size(), config_.num_threads);
    auto chunk_files = processor.processFiles(config_.pgn_paths, temp_path_.string());

    // Get statistics
    auto [total_records_processed, total_chunks_created, peak_memory_usage, total_games_processed] = processor.
        getStatistics();
    
    CM_LOG_INFO("Phase 1 statistics:");
    CM_LOG_INFO("  - Games processed: {}", total_games_processed);
    CM_LOG_INFO("  - Positions extracted: {}", total_records_processed);
    CM_LOG_INFO("  - Chunks created: {}", total_chunks_created);
    CM_LOG_INFO("  - Peak memory usage: {:.2f} MB", 
                  static_cast<double>(peak_memory_usage) / (1024.0 * 1024.0));
    
    if (total_games_processed > 0) {
        double avg_positions_per_game = static_cast<double>(total_records_processed) / total_games_processed;
        CM_LOG_INFO("  - Average positions per game: {:.1f}", avg_positions_per_game);
    }

    return chunk_files;
}

void ClockDataConverter::runPhase2_MergeAndWrite(const std::vector<std::string>& sorted_chunks) {
    CM_LOG_INFO("Phase 2: Merging and writing output");
    CM_LOG_INFO("  - Performing K-way merge of {} sorted chunks", sorted_chunks.size());
    CM_LOG_INFO("  - Grouping positions by key");
    CM_LOG_INFO("  - Filtering based on occurrence threshold");

    if (sorted_chunks.empty()) {
        CM_LOG_WARNING("No chunks to merge");
        return;
    }

    // Configure sorted merge writer
    clock_converter::SortedMergeWriter::Config merge_config;
    
    // If shuffling is enabled, write to a temporary file first
    std::string temp_output_path;
    if (config_.shuffle_enabled) {
        temp_output_path = (temp_path_ / "pre_shuffle.records").string();
        merge_config.output_bagz_path = "";  // Disable BAGZ output
        merge_config.output_records_path = temp_output_path;  // Write to intermediate format
    } else {
        merge_config.output_bagz_path = config_.output_bagz_path;
    }
    
    merge_config.max_positions_per_key = config_.max_positions_per_key;
    merge_config.write_common_positions = config_.write_common_positions;
    merge_config.skip_common_positions = config_.skip_common_positions;
    merge_config.common_positions_with_history_path = config_.common_positions_with_history_path;
    merge_config.common_positions_fen_only_path = config_.common_positions_fen_only_path;

    // Create merger and process chunks
    clock_converter::SortedMergeWriter merger(merge_config);
    merger.mergeChunks(sorted_chunks);

    // Get statistics
    auto [total_records_processed, unique_positions, common_positions_found, records_written_to_bagz,
        records_written_to_common, records_skipped] = merger.getStatistics();
    
    // If shuffling is enabled, perform shuffle and convert to BAGZ
    if (config_.shuffle_enabled) {
        CM_LOG_INFO("Phase 2.5: Shuffling records");
        CM_LOG_INFO("  - Randomizing record order for training");
        
        // Create shuffle manager
        ShuffleManager shuffle_manager(
            temp_path_.string(),
            config_.shuffle_seed,
            config_.keep_temp_files,
            thread_pool_.get(),
            logger_.get()
        );
        
        // Determine if we should use in-memory or bucket shuffle based on file size
        std::filesystem::path temp_file_path(temp_output_path);
        size_t file_size = std::filesystem::file_size(temp_file_path);

        if (size_t memory_threshold = config_.shuffle_memory_threshold_mb * 1024 * 1024; file_size < memory_threshold) {
            // Use in-memory shuffle for smaller files
            CM_LOG_INFO("  - Using in-memory shuffle (file size: {:.2f} MB < threshold: {} MB)", 
                         static_cast<double>(file_size) / (1024.0 * 1024.0), 
                         config_.shuffle_memory_threshold_mb);
            // In-memory shuffle now also writes directly to BAGZ format
            [[maybe_unused]] auto output_file = shuffle_manager.shuffleRecords(temp_output_path, config_.output_bagz_path);
        } else {
            // Use bucket shuffle for larger files
            CM_LOG_INFO("  - Using bucket shuffle (file size: {:.2f} MB >= threshold: {} MB)", 
                         static_cast<double>(file_size) / (1024.0 * 1024.0), 
                         config_.shuffle_memory_threshold_mb);
            CM_LOG_INFO("  - Creating {} shuffle buckets", config_.shuffle_buckets);
            
            // Create bucket manager
            BucketManager bucket_manager(
                temp_path_.string(),
                config_.shuffle_buckets,
                config_.total_buffer_memory_mb,
                config_.max_bucket_size_mb * 1024 * 1024,  // Convert MB to bytes
                logger_.get()
            );
            
            // Initialize bucket directories
            bucket_manager.initialize();
            
            // Write directly to the final output path
            [[maybe_unused]] auto output_file = shuffle_manager.bucketShuffleRecords(temp_output_path, bucket_manager, config_.output_bagz_path);
        }
        
        CM_LOG_INFO("  - Shuffle complete");
    }
    
    CM_LOG_INFO("Phase 2 statistics:");
    CM_LOG_INFO("  - Total records processed: {}", total_records_processed);
    CM_LOG_INFO("  - Unique positions: {}", unique_positions);
    
    if (config_.write_common_positions || config_.skip_common_positions) {
        CM_LOG_INFO("  - Common positions found: {}", common_positions_found);
    }
    
    if (!config_.shuffle_enabled) {
        CM_LOG_INFO("  - Records written to BAGZ: {}", records_written_to_bagz);
    }
    
    if (config_.write_common_positions) {
        CM_LOG_INFO("  - Records written to common files: {}", records_written_to_common);
    }
    
    if (config_.skip_common_positions) {
        CM_LOG_INFO("  - Records skipped (common positions): {}", records_skipped);
    }
}

void ClockDataConverter::validateConfig(const Config& config) {
    if (config.pgn_paths.empty()) {
        throw std::invalid_argument(
            "No PGN files specified. Please provide one or more .pgn.lz4 files as arguments.\n"
            "Usage: pgn_to_clock_bagz [OPTIONS] <input_files...>"
        );
    }

    if (config.output_bagz_path.empty()) {
        throw std::invalid_argument(
            "No output BAGZ file specified. Use --output <path> to specify the output file.\n"
            "Example: --output data/train/clock_1700_1800.bagz"
        );
    }

    if (config.num_threads == 0) {
        throw std::invalid_argument(
            "Number of threads must be > 0. Use --threads <n> to specify thread count.\n"
            "Recommended: Use number of CPU cores for optimal performance."
        );
    }

    if (config.max_positions_per_key <= 0) {
        throw std::invalid_argument(
            "max_positions_per_key must be > 0. Use --max-positions-per-key <n> to set threshold.\n"
            "This controls which positions are considered 'common' and filtered out.\n"
            "Typical values: 25-100 depending on dataset size."
        );
    }

    if (config.write_common_positions) {
        if (config.common_positions_with_history_path.empty() &&
            config.common_positions_fen_only_path.empty()) {
            throw std::invalid_argument(
                "When using --write-common-positions, you must specify at least one output file:\n"
                "  --common-positions-with-history <path> : For positions with move sequences\n"
                "  --common-positions-fen-only <path>     : For positions grouped by FEN only\n"
                "Example: --common-positions-with-history common_with_history.jsonl"
            );
        }
    }

    if (config.skip_common_positions && config.common_positions_fen_only_path.empty()) {
        throw std::invalid_argument(
            "When using --skip-common-positions, you must specify the FEN-only common positions file:\n"
            "  --common-positions-fen-only <path>\n"
            "This should be the FEN-only file generated during training data creation.\n"
            "Positions are skipped based on their FEN only, regardless of move history.\n"
            "Example: --common-positions-fen-only data/train/common_positions_fen_only.jsonl"
        );
    }

    // Check file existence for skip mode
    if (config.skip_common_positions) {
        if (!std::filesystem::exists(config.common_positions_fen_only_path)) {
            throw std::invalid_argument(
                "FEN-only common positions file not found: " + config.common_positions_fen_only_path + "\n"
                "Please ensure the file exists and the path is correct."
            );
        }
    }

    // Validate PGN file/directory paths
    for (const auto& path : config.pgn_paths) {
        if (!std::filesystem::exists(path)) {
            throw std::invalid_argument(
                "PGN path not found: " + path + "\n"
                "Please check the path and ensure it exists."
            );
        }
        // Allow directories (they will be expanded in convert())
        if (std::filesystem::is_directory(path)) {
            continue;
        }
        // For files, check valid extensions
        if (!FileUtils::endsWith(path, ".pgn") && !FileUtils::endsWith(path, ".pgn.lz4")) {
            throw std::invalid_argument(
                "Invalid file extension for: " + path + "\n"
                "Expected .pgn or .pgn.lz4 files, or a directory containing them."
            );
        }
    }
}

std::vector<std::string> ClockDataConverter::getMultipleArgumentsUntilNextFlag(int argc, char* argv[], const std::string& flag) {
    std::vector<std::string> values;
    for (int i = 1; i < argc; ++i) {
        if (std::string arg = argv[i]; arg == flag) {
            // Found the flag, collect all arguments until we hit another flag
            for (int j = i + 1; j < argc; ++j) {
                std::string next_arg = argv[j];
                // Check if this is another flag (starts with --)
                if (next_arg.substr(0, 2) == "--") {
                    // We hit another flag, stop collecting
                    break;
                }
                values.emplace_back(next_arg);
            }
            // We only process the first occurrence of this flag
            break;
        }
    }
    return values;
}

ClockDataConverter::Config ClockDataConverter::parseArguments(int argc, char* argv[]) {
    Config config;

    // First check for --pgn flag to get PGN files
    std::vector<std::string> pgn_from_flag = getMultipleArgumentsUntilNextFlag(argc, argv, "--pgn");
    if (!pgn_from_flag.empty()) {
        config.pgn_paths = pgn_from_flag;
    }

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        if (std::string arg = argv[i]; arg == "--pgn") {
            // Skip --pgn and its arguments (already processed above)
            for (int j = i + 1; j < argc; ++j) {
                if (std::string next_arg = argv[j]; next_arg.substr(0, 2) == "--") {
                    break;
                }
                i = j;  // Skip these arguments in the main loop
            }
        }
        else if (arg == "--output" && i + 1 < argc) {
            config.output_bagz_path = argv[++i];
        }
        else if (arg == "--threads" && i + 1 < argc) {
            config.num_threads = std::stoi(argv[++i]);
        }
        else if (arg == "--max-positions-per-key" && i + 1 < argc) {
            config.max_positions_per_key = std::stoi(argv[++i]);
        }
        else if (arg == "--write-common-positions") {
            config.write_common_positions = true;
        }
        else if (arg == "--skip-common-positions") {
            config.skip_common_positions = true;
        }
        else if (arg == "--common-positions-with-history" && i + 1 < argc) {
            config.common_positions_with_history_path = argv[++i];
        }
        else if (arg == "--common-positions-fen-only" && i + 1 < argc) {
            config.common_positions_fen_only_path = argv[++i];
        }
        else if (arg == "--temp-dir" && i + 1 < argc) {
            config.temp_dir = argv[++i];
        }
        else if (arg == "--memory-limit" && i + 1 < argc) {
            config.memory_limit_gb = std::stod(argv[++i]);
        }
        else if (arg == "--keep-temp-files") {
            config.keep_temp_files = true;
        }
        else if (arg == "--log-level" && i + 1 < argc) {
            config.log_level = std::stoi(argv[++i]);
        }
        else if (arg == "--shuffle") {
            config.shuffle_enabled = true;
        }
        else if (arg == "--shuffle-seed" && i + 1 < argc) {
            config.shuffle_seed = std::stoi(argv[++i]);
        }
        else if (arg == "--shuffle-buckets" && i + 1 < argc) {
            config.shuffle_buckets = std::stoi(argv[++i]);
        }
        else if (arg == "--bucket-size-mb" && i + 1 < argc) {
            config.max_bucket_size_mb = std::stoi(argv[++i]);
        }
        else if (arg == "--bucket-ram-limit-mb" && i + 1 < argc) {
            // DEPRECATED: Map to shuffle_memory_threshold_mb for backward compatibility
            config.shuffle_memory_threshold_mb = std::stoi(argv[++i]);
            std::cerr << "Warning: --bucket-ram-limit-mb is deprecated. Use --shuffle-memory-threshold-mb instead." << std::endl;
        }
        else if (arg == "--shuffle-memory-threshold-mb" && i + 1 < argc) {
            config.shuffle_memory_threshold_mb = std::stoi(argv[++i]);
        }
        else if (arg == "--total-buffer-memory-mb" && i + 1 < argc) {
            config.total_buffer_memory_mb = std::stoi(argv[++i]);
        }
        else if (arg == "--help" || arg == "-h") {
            printUsage();
            exit(0);
        }
        else if (arg.starts_with("--")) {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage();
            exit(1);
        }
        else {
            // For backward compatibility: treat positional arguments as PGN files
            // Only add if we didn't get PGN files from --pgn flag
            if (pgn_from_flag.empty()) {
                config.pgn_paths.push_back(arg);
            }
        }
    }

    return config;
}

std::vector<std::vector<std::string>> ClockDataConverter::distributeFiles(
    const std::vector<std::string>& files,
    unsigned int num_threads
) {
    std::vector<std::vector<std::string>> distribution(num_threads);

    // Round-robin distribution
    for (size_t i = 0; i < files.size(); ++i) {
        distribution[i % num_threads].push_back(files[i]);
    }

    return distribution;
}

void ClockDataConverter::printUsage() {
    std::cout << "Usage: pgn_to_clock_bagz --pgn <path> [<path> ...] --output <bagz_file> [options]\n"
        << "       pgn_to_clock_bagz [PGN files...] --output <bagz_file> [options]  (backward compatible)\n"
        << "\n"
        << "Paths can be individual .pgn/.pgn.lz4 files or directories containing them.\n"
        << "\n"
        << "Options:\n"
        << "  --pgn <path> [<path> ...]            PGN file(s) or directory(s) to process\n"
        << "  --output <path>                      Output BAGZ file (required)\n"
        << "  --threads <n>                        Number of threads (default: auto)\n"
        << "  --max-positions-per-key <n>          Threshold for common positions (default: 25)\n"
        << "  --write-common-positions             Write common positions to JSONL files\n"
        << "  --skip-common-positions              Skip positions from FEN-only common positions file\n"
        << "  --common-positions-with-history <f>  Common positions with move history JSONL (output only)\n"
        << "  --common-positions-fen-only <f>      Common positions FEN-only JSONL (output + skip)\n"
        << "  --temp-dir <path>                    Temporary directory (default: temp_clock_converter)\n"
        << "  --memory-limit <gb>                  Memory limit in GB (default: auto)\n"
        << "  --keep-temp-files                    Don't delete temporary files\n"
        << "  --log-level <n>                      0=ERROR, 1=INFO, 2=DEBUG (default: 1)\n"
        << "  --shuffle                            Enable shuffling of output records\n"
        << "  --shuffle-seed <n>                   Seed for shuffling (0=random, default: 0)\n"
        << "  --shuffle-buckets <n>                Number of shuffle buckets (default: 1024)\n"
        << "  --bucket-size-mb <n>                 Max bucket file size in MB (default: 512)\n"
        << "  --shuffle-memory-threshold-mb <n>    File size threshold for bucket shuffle (default: 4096)\n"
        << "  --total-buffer-memory-mb <n>         Total buffer memory across all buckets (default: 2048)\n"
        << "  --help                               Show this help message\n"
        << std::endl;
}

std::string ClockDataConverter::formatDuration(std::chrono::seconds duration) {
    auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
    duration -= hours;
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration);
    duration -= minutes;
    auto seconds = duration;
    
    std::stringstream ss;
    if (hours.count() > 0) {
        ss << hours.count() << "h " << minutes.count() << "m " << seconds.count() << "s";
    } else if (minutes.count() > 0) {
        ss << minutes.count() << "m " << seconds.count() << "s";
    } else {
        ss << seconds.count() << "s";
    }
    return ss.str();
}

std::vector<std::string> ClockDataConverter::collectPgnFiles() const {
    namespace fs = std::filesystem;
    std::vector<std::string> all_pgn_files;

    for (const auto& pgn_path : config_.pgn_paths) {
        if (fs::is_directory(pgn_path)) {
            // Recursively process all .pgn and .pgn.lz4 files in directory
            for (const auto& entry : fs::recursive_directory_iterator(pgn_path)) {
                if (entry.is_regular_file()) {
                    if (std::string path = entry.path().string();
                        FileUtils::endsWith(path, ".pgn") || FileUtils::endsWith(path, ".pgn.lz4")) {
                        all_pgn_files.push_back(path);
                    }
                }
            }
        } else {
            all_pgn_files.push_back(pgn_path);
        }
    }

    // Sort for deterministic ordering
    std::ranges::sort(all_pgn_files);

    return all_pgn_files;
}

} // namespace chessmimic
