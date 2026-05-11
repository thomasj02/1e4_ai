#include <iostream>
#include <chrono>
#include <memory>
#include <random>
#include <thread>
#include <atomic>
#include "cpp_src/common_position/common_position_extractor.hpp"
#include "cpp_src/common_position/bagz_reader_impl.hpp"

using namespace chessmimic;

void createTestBagz(const std::string& path, size_t num_records) {
    // Create a simple test BAGZ file with dummy records
    std::ofstream out(path, std::ios::binary);
    
    // Write header
    uint32_t version = 1;
    uint64_t num_records_64 = num_records;
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(&num_records_64), sizeof(num_records_64));
    
    // Write index table
    uint64_t offset = sizeof(version) + sizeof(num_records_64) + num_records * sizeof(uint64_t);
    for (size_t i = 0; i < num_records; ++i) {
        out.write(reinterpret_cast<const char*>(&offset), sizeof(offset));
        // Each record will be ~200 bytes
        offset += 200;
    }
    
    // Write records
    for (size_t i = 0; i < num_records; ++i) {
        std::string record = R"({
            "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "recent_moves": ["e2e4", "e7e5"],
            "response": {"Nf3": 5, "Bc4": 3},
            "rating_bin": 1500,
            "time_control": "180+0"
        })";
        
        // Pad to 200 bytes
        while (record.size() < 200) {
            record += " ";
        }
        
        out.write(record.data(), record.size());
    }
    
    out.close();
}

int main() {
    const std::string test_file = "/tmp/perf_test.bagz";
    const size_t num_records = 1'000'000;
    
    std::cout << "Creating test BAGZ file with " << num_records << " records..." << std::endl;
    createTestBagz(test_file, num_records);
    
    CommonPositionExtractor::Config config;
    config.bagz_path = test_file;
    config.output_path = "/tmp/perf_output.jsonl";
    config.temp_dir = "/tmp/perf_temp";
    config.threshold = 1000;
    config.chunk_size = 100'000;
    config.num_threads = 8;
    config.keep_temp_files = false;
    
    // Test with reader pool (new implementation)
    std::cout << "\nTesting with reader pool (8 threads)..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();
    
    try {
        CommonPositionExtractor extractor(config);
        extractor.extract();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Time taken: " << duration.count() << " ms" << std::endl;
    std::cout << "Records per second: " << (num_records * 1000.0 / duration.count()) << std::endl;
    
    // Clean up
    std::filesystem::remove(test_file);
    std::filesystem::remove(config.output_path);
    std::filesystem::remove_all(config.temp_dir);
    
    return 0;
}