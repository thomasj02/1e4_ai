#include "winner_data_converter.hpp"
#include "../core/bagz_record_writer.hpp"
#include "../core/bagz.hpp"
#include "../utils/file_utils.hpp"
#include "../utils/progress_tracker.hpp"
#include "../utils/stopwatch.hpp"
#include "../shuffle/shuffle_constants.hpp"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <getopt.h>
#include <cstring>
#include <mutex>
#include <simdjson.h>

#include "shuffle/bucket_manager.hpp"

namespace fs = std::filesystem;
using chessmimic::BagzRecordWriter;
using chessmimic::BagFileReader;

namespace chessmimic::winner_converter {

WinnerDataConverter::WinnerDataConverter(Config config)
    : config_(std::move(config)) {

    // Auto-detect memory limit if not specified (use 50% of system RAM)
    config_.memory_limit_gb = autoDetectMemory(config_.memory_limit_gb, 0.5);

    // Ensure temp directory is set
    if (config_.temp_dir.empty()) {
        config_.temp_dir = "temp_winner_converter";
    }

    // Create thread pool and logger using base class methods
    thread_pool_ = createThreadPool(config_.num_threads);
    logger_ = createLogger(config_.log_level);
    
    // Create shuffle manager if needed
    if (config_.shuffle_enabled) {
        shuffle_manager_ = std::make_unique<ShuffleManager>(
            config_.temp_dir,
            config_.shuffle_seed,
            config_.keep_temp_files,
            thread_pool_.get(),
            logger_.get()
        );
    }
    
    CM_LOG_INFO("WinnerDataConverter initialized with {} threads", config_.num_threads);
    CM_LOG_INFO("Memory limit: {:.1f} GB", config_.memory_limit_gb);
    if (config_.filter_draws) {
        CM_LOG_INFO("Filtering draws - only decisive games will be included");
    }
    CM_LOG_INFO("Rating range: {} - {}", config_.min_rating, config_.max_rating);
}

WinnerDataConverter::~WinnerDataConverter() {
    if (!config_.keep_temp_files) {
        cleanupTempFiles();
    }
}

bool WinnerDataConverter::convert() {
    CM_LOG_INFO("Converting {} PGN source(s) to {}", 
                config_.pgn_paths.size(), config_.output_bagz_path);
    
    // Start overall timer
    startTimer("Total Conversion");
    
    try {
        // Ensure directories exist
        FileUtils::ensureDirectoryExists(config_.temp_dir);
        FileUtils::ensureDirectoryExists(fs::path(config_.output_bagz_path).parent_path());
        
        // Determine output path based on shuffle setting
        std::string initial_output = config_.output_bagz_path;
        if (config_.shuffle_enabled) {
            initial_output = config_.temp_dir + "/unsorted_records.data";  // Use .data for intermediate format
        }
        
        // Process all PGN files
        CM_LOG_INFO("Processing PGN files...");
        startTimer("PGN Processing");
        bool success = processFiles(initial_output);
        stopTimer("PGN Processing");
        
        if (!success) {
            CM_LOG_ERROR("Failed to process PGN files");
            return false;
        }
        
        // Log statistics
        CM_LOG_INFO("Processed {} games ({} filtered), extracted {} positions",
                    games_processed_.load(), games_filtered_.load(), positions_extracted_.load());
        
        // Apply shuffle if enabled
        if (config_.shuffle_enabled) {
            CM_LOG_INFO("Shuffling records...");
            startTimer("Shuffle");
            success = writeOutput(initial_output);
            stopTimer("Shuffle");
            
            if (!success) {
                CM_LOG_ERROR("Failed to shuffle records");
                return false;
            }
        }
        
        stopTimer("Total Conversion");
        
        // Print timing report
        CM_LOG_INFO("\nTiming Report:");
        printTimingReport();
        
        return true;
        
    } catch (const std::exception& e) {
        CM_LOG_ERROR("Exception during conversion: {}", e.what());
        return false;
    }
}

bool WinnerDataConverter::processFiles(const std::string& output_path) {
    // Collect all PGN files to process
    std::vector<std::string> all_pgn_files;
    
    for (const auto& pgn_path : config_.pgn_paths) {
        if (fs::is_directory(pgn_path)) {
            // Process all .pgn and .pgn.lz4 files in directory
            for (const auto& entry : fs::recursive_directory_iterator(pgn_path)) {
                if (std::string path = entry.path().string(); FileUtils::endsWith(path, ".pgn") || FileUtils::endsWith(path, ".pgn.lz4")) {
                    all_pgn_files.push_back(path);
                }
            }
        } else {
            all_pgn_files.push_back(pgn_path);
        }
    }
    
    if (all_pgn_files.empty()) {
        CM_LOG_ERROR("No PGN files found to process");
        return false;
    }
    
    CM_LOG_INFO("Found {} PGN files to process", all_pgn_files.size());
    
    // Create progress tracker
    ProgressTracker progress(all_pgn_files.size(), "PGN Files");
    
    if (config_.shuffle_enabled) {
        // Create temp directory for intermediate files
        fs::create_directories(config_.temp_dir);
        
        // Vector to store intermediate file paths
        std::vector<std::string> intermediate_files;
        std::mutex files_mutex;  // Only for updating the vector
        
        // Process files in parallel, each writing to its own intermediate file
        std::vector<std::future<void>> futures;
        futures.reserve(all_pgn_files.size());
        
        for (size_t i = 0; i < all_pgn_files.size(); ++i) {
            const auto& pgn_file = all_pgn_files[i];
            futures.push_back(thread_pool_->enqueue([this, pgn_file, i, &intermediate_files, &files_mutex, &progress] {
                try {
                    // Create unique intermediate file for this thread
                    std::string intermediate_path = (fs::path(config_.temp_dir) / 
                        ("intermediate_" + std::to_string(i) + ".data")).string();
                    
                    std::ofstream out_file(intermediate_path, std::ios::binary);
                    if (!out_file) {
                        CM_LOG_ERROR("Failed to create intermediate file: {}", intermediate_path);
                        progress.update();
                        return;
                    }
                    
                    // Local counters
                    int local_positions = 0;
                    
                    // Process file with LZ4 support - no mutex needed!
                    processFileWithLZ4Support(pgn_file, [this, &out_file, &local_positions](std::vector<WinnerPositionRecord>&& records) {
                        ++games_processed_;
                        
                        if (records.empty()) {
                            ++games_filtered_;
                            return;
                        }
                        
                        // Check if we should include this game
                        if (int winner = records[0].winner; !shouldIncludeGame(records, winner)) {
                            ++games_filtered_;
                            return;
                        }
                        
                        // Write records directly to this thread's file
                        for (const auto& record : records) {
                            writeIntermediateRecord(out_file, record);
                            local_positions++;
                        }
                    });
                    
                    out_file.close();
                    
                    // Update global counter
                    positions_extracted_ += local_positions;
                    
                    // Add to list of intermediate files (only place we need mutex)
                    if (local_positions > 0) {
                        std::lock_guard lock(files_mutex);
                        intermediate_files.push_back(intermediate_path);
                    } else {
                        // Remove empty file
                        fs::remove(intermediate_path);
                    }
                    
                } catch (const std::exception& e) {
                    CM_LOG_ERROR("Error processing file {}: {}", pgn_file, e.what());
                }
                
                progress.update();
            }));
        }
        
        // Wait for all files to be processed
        for (auto& future : futures) {
            try {
                future.get();
            } catch (const std::exception& e) {
                CM_LOG_ERROR("Error processing file: {}", e.what());
                return false;
            }
        }
        
        // Now merge all intermediate files into the output path
        std::ofstream final_intermediate(output_path, std::ios::binary);
        if (!final_intermediate) {
            CM_LOG_ERROR("Failed to create final intermediate file: {}", output_path);
            return false;
        }
        
        // Create progress tracker for merging
        ProgressTracker merge_progress(intermediate_files.size(), "Merging intermediate files");
        
        // Read and write all intermediate files
        for (const auto& int_file : intermediate_files) {
            std::ifstream in(int_file, std::ios::binary);
            if (!in) {
                CM_LOG_ERROR("Failed to open intermediate file: {}", int_file);
                merge_progress.update();
                continue;
            }
            
            // Copy contents
            final_intermediate << in.rdbuf();
            in.close();
            
            // Remove intermediate file if not keeping
            if (!config_.keep_temp_files) {
                fs::remove(int_file);
            }
            
            merge_progress.update();
        }
        
        final_intermediate.close();
        
    } else {
        // Direct BAGZ writing (no shuffle) - each thread writes to its own file
        // Create temp directory for intermediate BAGZ files
        fs::create_directories(config_.temp_dir);
        
        // Vector to store intermediate BAGZ file paths
        std::vector<std::string> intermediate_bagz_files;
        std::mutex files_mutex;  // Only for updating the vector
        
        // Process files in parallel, each writing to its own BAGZ file
        std::vector<std::future<void>> futures;
        futures.reserve(all_pgn_files.size());
        
        for (size_t i = 0; i < all_pgn_files.size(); ++i) {
            const auto& pgn_file = all_pgn_files[i];
            futures.push_back(thread_pool_->enqueue([this, pgn_file, i, &intermediate_bagz_files, &files_mutex, &progress] {
                try {
                    // Create unique BAGZ file for this thread
                    std::string intermediate_path = (fs::path(config_.temp_dir) / 
                        ("intermediate_" + std::to_string(i) + ".bagz")).string();
                    
                    BagzRecordWriter writer(intermediate_path);
                    
                    // Local counters
                    int local_positions = 0;
                    
                    // Process file with LZ4 support - no mutex needed!
                    processFileWithLZ4Support(pgn_file, [this, &writer, &local_positions](std::vector<WinnerPositionRecord>&& records) {
                        ++games_processed_;
                        
                        if (records.empty()) {
                            ++games_filtered_;
                            CM_LOG_DEBUG("Game filtered: no records");
                            return;
                        }
                        
                        // Check if we should include this game
                        if (int winner = records[0].winner; !shouldIncludeGame(records, winner)) {
                            ++games_filtered_;
                            CM_LOG_DEBUG("Game filtered: shouldIncludeGame returned false");
                            return;
                        }
                        
                        CM_LOG_DEBUG("Writing {} position records", records.size());
                        
                        // Write records directly to this thread's BAGZ file
                        for (const auto& record : records) {
                            writer.writeRecord(record);
                            local_positions++;
                        }
                    });
                    
                    writer.close();
                    
                    // Update global counter
                    positions_extracted_ += local_positions;
                    
                    // Add to list of intermediate files (only place we need mutex)
                    if (local_positions > 0) {
                        std::lock_guard lock(files_mutex);
                        intermediate_bagz_files.push_back(intermediate_path);
                    } else {
                        // Remove empty file
                        fs::remove(intermediate_path);
                    }
                    
                } catch (const std::exception& e) {
                    CM_LOG_ERROR("Error processing file {}: {}", pgn_file, e.what());
                }
                
                progress.update();
            }));
        }
        
        // Wait for all files to be processed
        for (auto& future : futures) {
            try {
                future.get();
            } catch (const std::exception& e) {
                CM_LOG_ERROR("Error processing file: {}", e.what());
                return false;
            }
        }
        
        // Now merge all intermediate BAGZ files into the final output
        BagzRecordWriter final_writer(output_path);
        simdjson::dom::parser parser;
        
        // Create progress tracker for merging
        ProgressTracker merge_progress(intermediate_bagz_files.size(), "Merging BAGZ files");
        
        for (const auto& bagz_file : intermediate_bagz_files) {
            BagFileReader reader(bagz_file);
            
            // Copy all records
            for (size_t i = 0; i < reader.size(); ++i) {
                auto data = reader.get_record(i);
                std::string json(data.begin(), data.end());
                WinnerPositionRecord record = WinnerPositionRecord::fromJson(json, parser);
                final_writer.writeRecord(record);
            }
            
            // Remove intermediate file if not keeping
            if (!config_.keep_temp_files) {
                fs::remove(bagz_file);
            }
            
            merge_progress.update();
        }
        
        final_writer.close();
    }
    
    return true;
}

void WinnerDataConverter::processSingleFile(const std::string& pgn_path, BagzRecordWriter& writer) {
    CM_LOG_DEBUG("Processing file: {}", pgn_path);
    
    try {
        // Process file with LZ4 support
        processFileWithLZ4Support(pgn_path, [this, &writer](std::vector<WinnerPositionRecord>&& records) {
            ++games_processed_;
            
            if (records.empty()) {
                ++games_filtered_;
                return;
            }
            
            // Check if we should include this game
            if (int winner = records[0].winner; !shouldIncludeGame(records, winner)) {
                ++games_filtered_;
                return;
            }
            
            // Write all position records
            for (const auto& record : records) {
                writer.writeRecord(record);
                ++positions_extracted_;
            }
        });
    } catch (const std::exception& e) {
        CM_LOG_ERROR("Error processing file {}: {}", pgn_path, e.what());
    }
}

bool WinnerDataConverter::shouldIncludeGame(const std::vector<WinnerPositionRecord>& records, 
                                           int winner) const {
    // Filter draws if requested
    if (config_.filter_draws && winner == 0) {
        return false;
    }
    
    // Check rating range
    if (!records.empty()) {
        const auto& first_record = records[0];

        if (int avg_rating = (first_record.white_rating + first_record.black_rating) / 2; avg_rating < config_.min_rating || avg_rating > config_.max_rating) {
            return false;
        }
    }
    
    return true;
}

bool WinnerDataConverter::writeOutput(const std::string& temp_path) {
    if (!config_.shuffle_enabled || !shuffle_manager_) {
        // No shuffle needed, output is already at final location
        return true;
    }
    
    // Determine shuffle method based on file size
    size_t file_size = fs::file_size(temp_path);
    size_t threshold = config_.shuffle_memory_threshold_mb * 1024 * 1024;
    
    try {
        if (file_size < threshold) {
            // In-memory shuffle
            CM_LOG_INFO("Using in-memory shuffle (file size: {:.2f} MB < threshold: {} MB)", 
                        static_cast<double>(file_size) / (1024.0 * 1024.0), 
                        config_.shuffle_memory_threshold_mb);

            // If shuffled file is not at final location, move it
            if (std::string shuffled = shuffle_manager_->shuffleRecords(temp_path, config_.output_bagz_path); shuffled != config_.output_bagz_path) {
                fs::rename(shuffled, config_.output_bagz_path);
            }
        } else {
            // Bucket-based shuffle
            CM_LOG_INFO("Using bucket shuffle (file size: {:.2f} MB >= threshold: {} MB)", 
                        static_cast<double>(file_size) / (1024.0 * 1024.0), 
                        config_.shuffle_memory_threshold_mb);
            CM_LOG_INFO("Creating {} shuffle buckets", config_.shuffle_buckets);
            
            // Create buckets subdirectory
            fs::create_directories(fs::path(config_.temp_dir) / "buckets");
            
            // Create bucket manager
            BucketManager bucket_manager(
                config_.temp_dir,
                config_.shuffle_buckets,
                config_.bucket_ram_limit_mb,  // Total memory in MB
                DEFAULT_MAX_BUCKET_SIZE_BYTES,
                logger_.get()
            );
            
            // Perform bucket shuffle
            std::string shuffled = shuffle_manager_->bucketShuffleRecords(
                temp_path, 
                bucket_manager, 
                config_.output_bagz_path
            );
            
            // If shuffled file is not at final location, move it
            if (shuffled != config_.output_bagz_path) {
                fs::rename(shuffled, config_.output_bagz_path);
            }
        }
        
        // Remove intermediate file and buckets directory if not keeping temp files
        if (!config_.keep_temp_files) {
            fs::remove(temp_path);
            
            // Remove buckets directory if it exists
            if (fs::path buckets_dir = fs::path(config_.temp_dir) / "buckets"; fs::exists(buckets_dir)) {
                fs::remove_all(buckets_dir);
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        CM_LOG_ERROR("Shuffle failed: {}", e.what());
        return false;
    }
}

void WinnerDataConverter::cleanupTempFiles() const {
    try {
        // Use base class method for cleanup
        DataConverterBase::cleanupTempFiles(config_.temp_dir);
        CM_LOG_DEBUG("Cleaned up temporary directory: {}", config_.temp_dir);
    } catch (const std::exception& e) {
        CM_LOG_WARNING("Failed to clean up temp directory: {}", e.what());
    }
}

bool WinnerDataConverter::parseArguments(int argc, char* argv[], Config& config) {
    static option long_options[] = {
        {"output", required_argument, nullptr, 'o'},
        {"threads", required_argument, nullptr, 't'},
        {"memory", required_argument, nullptr, 'm'},
        {"temp-dir", required_argument, nullptr, 'd'},
        {"filter-draws", no_argument, nullptr, 'f'},
        {"min-rating", required_argument, nullptr, 'r'},
        {"max-rating", required_argument, nullptr, 'R'},
        {"shuffle", no_argument, nullptr, 's'},
        {"shuffle-seed", required_argument, nullptr, 'S'},
        {"shuffle-buckets", required_argument, nullptr, 'B'},
        {"shuffle-memory", required_argument, nullptr, 'M'},
        {"bucket-memory", required_argument, nullptr, 'b'},
        {"keep-temp-files", no_argument, nullptr, 'k'},
        {"log-level", required_argument, nullptr, 'l'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };
    
    int opt;
    int option_index = 0;
    
    while ((opt = getopt_long(argc, argv, "o:t:m:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'o':
                config.output_bagz_path = optarg;
                break;
                
            case 't':
                config.num_threads = std::stoul(optarg);
                break;
                
            case 'm':
                config.memory_limit_gb = std::stod(optarg);
                break;
                
            case 'd':
                config.temp_dir = optarg;
                break;
                
            case 'f':
                config.filter_draws = true;
                break;
                
            case 'r':
                config.min_rating = std::stoi(optarg);
                break;
                
            case 'R':
                config.max_rating = std::stoi(optarg);
                break;
                
            case 's':
                config.shuffle_enabled = true;
                break;
                
            case 'S':
                config.shuffle_seed = std::stoul(optarg);
                break;
                
            case 'B':
                config.shuffle_buckets = std::stoull(optarg);
                break;
                
            case 'M':
                config.shuffle_memory_threshold_mb = std::stoull(optarg);
                break;
                
            case 'b':
                config.bucket_ram_limit_mb = std::stoull(optarg);
                break;
                
            case 'k':
                config.keep_temp_files = true;
                break;
                
            case 'l':
                config.log_level = std::stoi(optarg);
                break;
                
            case 'h':
                printHelp();
                return false;
                
            default:
                std::cerr << "Unknown option. Use -h for help." << std::endl;
                return false;
        }
    }
    
    // Collect remaining arguments as input PGN files
    for (int i = optind; i < argc; i++) {
        config.pgn_paths.emplace_back(argv[i]);
    }
    
    // Validate required arguments
    if (config.output_bagz_path.empty()) {
        std::cerr << "Error: Output file is required (-o option)" << std::endl;
        return false;
    }
    
    if (config.pgn_paths.empty()) {
        std::cerr << "Error: At least one input PGN file is required" << std::endl;
        return false;
    }
    
    return true;
}

void WinnerDataConverter::writeIntermediateRecord(std::ofstream& out, const WinnerPositionRecord& record) const {
    // Create key (using FEN as the key for simplicity)
    std::string key = record.fen;
    
    // Serialize record to JSON
    std::string json = record.toJson();
    
    // Calculate sizes
    auto key_len = static_cast<uint32_t>(key.size());
    auto record_len = static_cast<uint32_t>(json.size());
    uint32_t total_size = sizeof(key_len) + key_len + sizeof(record_len) + record_len;
    
    // Write in BucketRecord format
    out.write(reinterpret_cast<const char*>(&total_size), sizeof(total_size));
    out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
    out.write(key.data(), key_len);
    out.write(reinterpret_cast<const char*>(&record_len), sizeof(record_len));
    out.write(json.data(), record_len);
}

void WinnerDataConverter::printHelp() {
    std::cout << R"(
pgn_to_winner_bagz - Convert PGN chess games to winner-labeled BAGZ format

Usage: pgn_to_winner_bagz [options] <pgn_files...> -o <output.bagz>

Options:
  -o, --output <path>          Output BAGZ file (required)
  -t, --threads <n>            Number of threads (default: hardware concurrency)
  -m, --memory <gb>            Memory limit in GB (default: auto-detect)
  --temp-dir <path>            Temporary directory (default: temp_winner_converter)
  
  --filter-draws               Skip games that ended in draws (only include decisive games)
  --min-rating <n>             Minimum average rating (default: 0)
  --max-rating <n>             Maximum average rating (default: 9999)
  
  --shuffle                    Enable output shuffling (recommended for training)
  --shuffle-seed <n>           Shuffle random seed (default: random)
  --shuffle-buckets <n>        Number of buckets for shuffle (default: 256)
  --shuffle-memory <MB>        Memory threshold for in-memory shuffle (default: 1024)
  --bucket-memory <MB>         Total memory for bucket operations (default: 4096)
  
  --keep-temp-files            Don't delete temporary files
  --log-level <n>              0=ERROR, 1=INFO, 2=DEBUG (default: 1)
  -h, --help                   Show this help message

Examples:
  # Convert single PGN file
  pgn_to_winner_bagz games.pgn -o training_data.bagz
  
  # Convert directory with shuffle
  pgn_to_winner_bagz /path/to/pgn_dir/ -o shuffled_data.bagz --shuffle
  
  # Filter for decisive games only with rating range
  pgn_to_winner_bagz games.pgn -o decisive_only.bagz --filter-draws --min-rating 1600 --max-rating 2400
  
  # Use specific resources
  pgn_to_winner_bagz large_dataset.pgn -o output.bagz -t 8 -m 16 --temp-dir /fast_ssd/temp

Each position is labeled with the game outcome:
  1.0 = White wins
  0.5 = Draw
  0.0 = Black wins
)";
}

} // namespace chessmimic::winner_converter