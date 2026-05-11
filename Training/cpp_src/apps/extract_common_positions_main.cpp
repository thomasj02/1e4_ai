#include <iostream>
#include <string>
#include "common_position/common_position_extractor.hpp"

using namespace chessmimic;

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <bagz_file> <output_file> [options]\n";
        std::cerr << "Options:\n";
        std::cerr << "  --threshold <n>       Minimum move count (default: 25)\n";
        std::cerr << "  --chunk-size <n>      Records per chunk (default: 1000000)\n";
        std::cerr << "  --max-records <n>     Maximum records to process (default: all)\n";
        std::cerr << "  --temp-dir <path>     Temporary directory\n";
        std::cerr << "  --threads <n>         Number of threads (default: auto)\n";
        std::cerr << "  --keep-temp-files     Keep temporary files\n";
        return 1;
    }

    CommonPositionExtractor::Config config;
    config.bagz_path = argv[1];
    config.output_path = argv[2];

    // Parse optional arguments
    for (int i = 3; i < argc; i += 2) {
        std::string arg = argv[i];
        if (i + 1 < argc) {
            std::string value = argv[i + 1];
            if (arg == "--threshold") {
                config.threshold = std::stoi(value);
            } else if (arg == "--chunk-size") {
                config.chunk_size = std::stoull(value);
            } else if (arg == "--max-records") {
                config.max_records = std::stoull(value);
            } else if (arg == "--temp-dir") {
                config.temp_dir = value;
            } else if (arg == "--threads") {
                config.num_threads = std::stoull(value);
            }
        }
        if (arg == "--keep-temp-files") {
            config.keep_temp_files = true;
            i--; // This flag doesn't have a value
        }
    }

    try {
        CommonPositionExtractor extractor(config);
        extractor.extract();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}