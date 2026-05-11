#include <iostream>
#include <chrono>
#include <filesystem>
#include <cstdlib>
#include "cpp_src/common_position/common_position_extractor.hpp"

using namespace chessmimic;

int main() {
    const char* configured_path = std::getenv("CHESSMIMIC_COMMON_POSITIONS_BAGZ");
    const std::string bagz_file = configured_path ? configured_path : "data/data_with_clocks/train.bagz";
    
    // Check if file exists
    if (!std::filesystem::exists(bagz_file)) {
        std::cerr << "Error: BAGZ file not found: " << bagz_file << std::endl;
        return 1;
    }
    
    std::cout << "File size: " << std::filesystem::file_size(bagz_file) / (1024.0 * 1024.0 * 1024.0) 
              << " GB" << std::endl;
    
    CommonPositionExtractor::Config config;
    config.bagz_path = bagz_file;
    config.output_path = "/tmp/common_positions_test.jsonl";
    config.temp_dir = "/tmp/common_pos_temp";
    config.threshold = 100;  // Only output positions that appear 100+ times
    config.chunk_size = 100'000;  // Process 100k records per chunk
    config.num_threads = 8;
    config.keep_temp_files = false;
    config.max_records = 1'000'000;  // Limit to 1M records for testing
    
    std::cout << "\nExtracting common positions from first " << config.max_records 
              << " records..." << std::endl;
    std::cout << "Using " << config.num_threads << " threads with chunk size " 
              << config.chunk_size << std::endl;
    std::cout << "Output threshold: " << config.threshold << " occurrences" << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    try {
        CommonPositionExtractor extractor(config);
        extractor.extract();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    
    std::cout << "\nExtraction completed in " << duration.count() << " seconds" << std::endl;
    std::cout << "Processing rate: " << (config.max_records / duration.count()) 
              << " records/second" << std::endl;
    
    // Check output file
    if (std::filesystem::exists(config.output_path)) {
        auto output_size = std::filesystem::file_size(config.output_path);
        std::cout << "Output file size: " << output_size / 1024.0 << " KB" << std::endl;
        
        // Count lines in output
        std::ifstream infile(config.output_path);
        size_t line_count = std::count(std::istreambuf_iterator<char>(infile), 
                                      std::istreambuf_iterator<char>(), '\n');
        std::cout << "Found " << line_count << " common positions exceeding threshold" << std::endl;
        
        // Show first few positions
        infile.clear();
        infile.seekg(0);
        std::cout << "\nFirst 5 positions:" << std::endl;
        std::string line;
        for (int i = 0; i < 5 && std::getline(infile, line); ++i) {
            std::cout << line << std::endl;
        }
    }
    
    // Clean up
    std::filesystem::remove_all(config.temp_dir);
    
    return 0;
}
