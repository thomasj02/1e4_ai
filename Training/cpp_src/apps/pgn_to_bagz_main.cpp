#include <iostream>
#include <string>
#include <vector>

// Try to use jemalloc if available
#if defined(HAVE_JEMALLOC)
#include <jemalloc/jemalloc.h>
#endif

#include "pgn_converter/chess_pgn_visitor.hpp"
#include "pgn_converter/pgn_to_bagz_converter.hpp"

// Parse command line arguments
std::string getArgument(int argc, char* argv[], const std::string& flag, const std::string& default_value = "") {
    for (int i = 1; i < argc; ++i) {
        if (std::string arg = argv[i]; arg == flag && i + 1 < argc) {
            return argv[i + 1];
        }
    }
    return default_value;
}

std::vector<std::string> getMultipleArguments(int argc, char* argv[], const std::string& flag) {
    std::vector<std::string> values;
    for (int i = 1; i < argc; ++i) {
        if (std::string arg = argv[i]; arg == flag && i + 1 < argc) {
            // Add the next argument after the flag
            values.emplace_back(argv[i + 1]);
            i++; // Skip the value we just added
        }
    }
    return values;
}

std::vector<std::string> getMultipleArgumentsUntilNextFlag(int argc, char* argv[], const std::string& flag) {
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

int getIntArgument(int argc, char* argv[], const std::string& flag, int default_value) {
    std::string value = getArgument(argc, argv, flag);
    if (value.empty()) {
        return default_value;
    }
    try {
        return std::stoi(value);
    }
    catch (const std::exception&) {
        std::cerr << "Warning: Invalid value for " << flag << ", using default: " << default_value << std::endl;
        return default_value;
    }
}

bool getBoolArgument(int argc, char* argv[], const std::string& flag, bool default_value) {
    for (int i = 1; i < argc; ++i) {
        if (std::string arg = argv[i]; arg == flag) {
            return true;
        }
    }
    return default_value;
}

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " --pgn <pgn_path> [<pgn_path> ...] --bagz <bagz_path> [options]" <<
        std::endl;
    std::cout << "Options:" << std::endl;
    std::cout <<
        "  --pgn <path> [<path> ...] PGN file(s) or directory (recursively searched for .pgn files)" <<
        std::endl;
    std::cout << "  --bagz <path>             Output BAGZ file path (required)" << std::endl;
    std::cout << "  --temp_dir <path>         Temporary directory for intermediate files" << std::endl;
    std::cout << "  --min_rating <int>        Minimum player rating (default: 0)" << std::endl;
    std::cout << "  --max_rating <int>        Maximum player rating (default: 10000)" << std::endl;
    std::cout << "  --min_clock <int>         Minimum remaining clock time in seconds (default: 0)" << std::endl;
    std::cout << "  --min_ply <int>           Minimum game ply to include (default: 0)" << std::endl;
    std::cout << "  --recent_moves <int>      Number of recent moves to track (default: 6)" << std::endl;
    std::cout << "  --max_moves <int>         Maximum moves per position (default: 25)" << std::endl;
    std::cout << "  --chunk_size <int>        Records per sorting chunk (default: 10000)" << std::endl;
    std::cout << "  --keep_temp_files         Keep temporary files after conversion" << std::endl;
    std::cout <<
        "  --common_moves <path>     Path to output/input positions with more than max_moves (default: common_moves.jsonl)"
        << std::endl;
    std::cout << "  --write_common_moves      Write positions with more than max_moves to file (default: off)" <<
        std::endl;
    std::cout << "  --skip_common_moves       Skip positions in common_moves file (default: off)" << std::endl;
    std::cout << "  --run_phases <int>        Which phases to run: 1=phase1 only, 2=phase1+2, 3=all (default: 3)" <<
        std::endl;
    std::cout << "  --max_games <int>         Maximum number of games to process, 0=no limit (default: 0)" << std::endl;
    std::cout << "  --threads <int>           Number of threads to use (default: auto)" << std::endl;
    std::cout << "  --max_memory <int>        Maximum memory usage in MB (default: 4096)" << std::endl;
    std::cout << "  --help                    Display this help message" << std::endl;
#if defined(HAVE_JEMALLOC)
    std::cout << "  --jemalloc-stats          Print jemalloc statistics and exit" << std::endl;
#endif
}

int main(int argc, char* argv[]) {
    // Report memory allocator in use
#if defined(HAVE_JEMALLOC)
    std::cout << "Using jemalloc memory allocator" << std::endl;
    if (argc > 1 && std::string(argv[1]) == "--jemalloc-stats") {
        // Print jemalloc statistics and exit if requested
        // Use the right namespace for jemalloc functions
#if defined(JEMALLOC_EXPORT)
        malloc_stats_print(nullptr, nullptr, nullptr);
#else
            je_malloc_stats_print(nullptr, nullptr, nullptr);
#endif
        return 0;
    }
#else
    std::cout << "Using system memory allocator" << std::endl;
#endif

    // Check for help
    if (argc < 2 || getBoolArgument(argc, argv, "--help", false)) {
        printUsage(argv[0]);
        return 0;
    }

    // Parse required arguments
    std::vector<std::string> pgn_paths = getMultipleArgumentsUntilNextFlag(argc, argv, "--pgn");
    std::string bagz_path = getArgument(argc, argv, "--bagz");

    if (pgn_paths.empty() || bagz_path.empty()) {
        std::cerr << "Error: Missing required arguments" << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    // Parse optional arguments
    chessmimic::PgnToBagzConverter::Config config;
    config.pgn_paths = pgn_paths;
    config.bagz_path = bagz_path;
    config.temp_dir = getArgument(argc, argv, "--temp_dir");
    config.min_rating = getIntArgument(argc, argv, "--min_rating", 0);
    config.max_rating = getIntArgument(argc, argv, "--max_rating", 10000);
    config.min_clock_seconds = getIntArgument(argc, argv, "--min_clock", 0);
    config.min_ply = getIntArgument(argc, argv, "--min_ply", 0);
    config.recent_moves = getIntArgument(argc, argv, "--recent_moves", 6);
    config.max_moves_per_position = getIntArgument(argc, argv, "--max_moves", 25);
    config.sort_chunk_size = getIntArgument(argc, argv, "--chunk_size", 1'000'000);
    config.keep_temp_files = getBoolArgument(argc, argv, "--keep_temp_files", false);
    config.common_moves_path = getArgument(argc, argv, "--common_moves", "common_moves.jsonl");
    config.write_common_moves = getBoolArgument(argc, argv, "--write_common_moves", false);
    config.skip_common_moves = getBoolArgument(argc, argv, "--skip_common_moves", false);
    config.run_phases = getIntArgument(argc, argv, "--run_phases", 3);
    config.max_games = getIntArgument(argc, argv, "--max_games", 0);
    config.num_threads = getIntArgument(argc, argv, "--threads", std::thread::hardware_concurrency());

    // Validate mutually exclusive options
    if (config.write_common_moves && config.skip_common_moves) {
        std::cerr << "Error: --write_common_moves and --skip_common_moves are mutually exclusive." << std::endl;
        return 1;
    }

    if (!config.write_common_moves && !config.skip_common_moves) {
        std::cerr << "Error: Either --write_common_moves or --skip_common_moves must be specified." << std::endl;
        return 1;
    }

    try {
        // Create converter and run conversion
        chessmimic::PgnToBagzConverter converter(config);
        converter.convert();
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
