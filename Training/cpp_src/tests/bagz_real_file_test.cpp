#include <gtest/gtest.h>
#include "../core/bagz.hpp"
#include "../core/bagz_mmap.hpp"
#include <simdjson.h>
#include <filesystem>
#include <iostream>
#include <random>
#include <chrono>
#include <future>
#include <cstdlib>

using namespace chessmimic;

class BagzRealFileTest : public testing::Test {
protected:
    static std::string getEnvPath(const char* name) {
        const char* value = std::getenv(name);
        return value ? std::string(value) : std::string();
    }

    // Test with the sample BAGZ file if it exists
    const std::string sample_bagz = "../sample_data/train.tiny.bagz";
    const std::string large_bagz = getEnvPath("CHESSMIMIC_LARGE_BAGZ");

    static bool fileExists(const std::string& path) {
        return std::filesystem::exists(path);
    }
};

// Test with sample BAGZ file
TEST_F(BagzRealFileTest, SampleFileCompatibility) {
    if (!fileExists(sample_bagz)) {
        GTEST_SKIP() << "Sample BAGZ file not found: " << sample_bagz;
    }
    
    BagFileReader original_reader(sample_bagz);
    BagFileReaderMMap mmap_reader(sample_bagz);
    
    // Check sizes match
    ASSERT_EQ(original_reader.size(), mmap_reader.size());
    
    size_t num_records = original_reader.size();
    std::cout << "Testing with " << num_records << " records from sample file" << std::endl;
    
    // Check all records
    for (size_t i = 0; i < num_records; ++i) {
        auto original_data = original_reader.get_record(i);
        auto mmap_data = mmap_reader.get_record(i);
        
        ASSERT_EQ(original_data.size(), mmap_data.size()) 
            << "Size mismatch at record " << i;
        
        ASSERT_EQ(original_data, mmap_data) 
            << "Content mismatch at record " << i;
        
        // Try to parse as JSON to ensure data integrity
        std::string data_str(mmap_data.begin(), mmap_data.end());
        simdjson::dom::parser parser;
        simdjson::dom::element record;
        ASSERT_NO_THROW(record = parser.parse(data_str))
            << "Invalid JSON at record " << i;
        
        // Verify expected structure
        EXPECT_NO_THROW(record["recent_and_fen"]);
        EXPECT_NO_THROW(record["moves"]);
        auto recent_and_fen = record["recent_and_fen"].get_array();
        EXPECT_EQ(recent_and_fen.size(), 2);
        // Check that second element (FEN) is a string by accessing it
        EXPECT_NO_THROW(std::string(recent_and_fen.at(1)));
    }
}

// Test with first N records from large file
TEST_F(BagzRealFileTest, LargeFilePartialCompatibility) {
    if (!fileExists(large_bagz)) {
        GTEST_SKIP() << "Large BAGZ file not found: " << large_bagz;
    }

    constexpr size_t test_records = 1000; // Test first 1000 records
    
    BagFileReader original_reader(large_bagz);
    BagFileReaderMMap mmap_reader(large_bagz);
    
    ASSERT_EQ(original_reader.size(), mmap_reader.size());
    
    size_t num_records = std::min(test_records, original_reader.size());
    std::cout << "Testing with first " << num_records << " records from large file" << std::endl;
    
    // Check records
    for (size_t i = 0; i < num_records; ++i) {
        auto original_data = original_reader.get_record(i);
        auto mmap_data = mmap_reader.get_record(i);
        
        ASSERT_EQ(original_data.size(), mmap_data.size()) 
            << "Size mismatch at record " << i;
        
        ASSERT_EQ(original_data, mmap_data) 
            << "Content mismatch at record " << i;
        
        // Sample check - parse every 100th record
        if (i % 100 == 0) {
            std::string data_str(mmap_data.begin(), mmap_data.end());
            simdjson::dom::parser parser;
            ASSERT_NO_THROW(parser.parse(data_str))
                << "Invalid JSON at record " << i;
        }
    }
}

// Test random access pattern on large file
TEST_F(BagzRealFileTest, LargeFileRandomAccess) {
    if (!fileExists(large_bagz)) {
        GTEST_SKIP() << "Large BAGZ file not found: " << large_bagz;
    }
    
    BagFileReader original_reader(large_bagz);
    BagFileReaderMMap mmap_reader(large_bagz);
    
    size_t total_records = original_reader.size();
    
    // Test 100 random positions
    std::mt19937 gen(42);
    std::uniform_int_distribution<size_t> dist(0, std::min(total_records - 1, static_cast<size_t>(1000000)));
    
    for (int i = 0; i < 100; ++i) {
        size_t idx = dist(gen);
        
        auto original_data = original_reader.get_record(idx);
        auto mmap_data = mmap_reader.get_record(idx);
        
        ASSERT_EQ(original_data, mmap_data) 
            << "Mismatch at random position " << idx;
    }
}

// Test concurrent access on real file
TEST_F(BagzRealFileTest, ConcurrentRealFileAccess) {
    std::string test_file = fileExists(sample_bagz) ? sample_bagz : 
                           (fileExists(large_bagz) ? large_bagz : "");
    
    if (test_file.empty()) {
        GTEST_SKIP() << "No BAGZ file available for testing";
    }

    constexpr size_t num_threads = 4;
    constexpr size_t records_per_thread = 100;
    
    BagFileReaderMMap mmap_reader(test_file);

    if (size_t total_records = mmap_reader.size(); total_records < num_threads * records_per_thread) {
        GTEST_SKIP() << "File too small for concurrent test";
    }
    
    std::vector<std::future<bool>> futures;
    
    auto worker = [&test_file](size_t start, size_t count) {
        // Each thread creates its own readers to avoid concurrent access issues
        BagFileReaderMMap thread_mmap_reader(test_file);
        BagFileReader thread_original_reader(test_file);
        
        for (size_t i = 0; i < count; ++i) {
            size_t idx = start + i;
            auto mmap_data = thread_mmap_reader.get_record(idx);
            if (auto orig_data = thread_original_reader.get_record(idx); mmap_data != orig_data) {
                return false;
            }
        }
        return true;
    };
    
    // Launch threads
    for (size_t t = 0; t < num_threads; ++t) {
        size_t start = t * records_per_thread;
        futures.push_back(std::async(std::launch::async, worker, start, records_per_thread));
    }
    
    // Check results
    for (auto& future : futures) {
        EXPECT_TRUE(future.get()) << "Concurrent access verification failed";
    }
}

// Benchmark comparison
TEST_F(BagzRealFileTest, PerformanceBenchmark) {
    std::string test_file = fileExists(sample_bagz) ? sample_bagz : 
                           (fileExists(large_bagz) ? large_bagz : "");
    
    if (test_file.empty()) {
        GTEST_SKIP() << "No BAGZ file available for benchmarking";
    }

    // Sequential access benchmark
    {
        constexpr size_t test_records = 10000;
        BagFileReader original_reader(test_file);
        BagFileReaderMMap mmap_reader(test_file);
        
        size_t num_records = std::min(test_records, original_reader.size());
        
        // Time original reader
        auto start = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < num_records; ++i) {
            auto data = original_reader.get_record(i);
        }
        auto original_time = std::chrono::high_resolution_clock::now() - start;
        
        // Time mmap reader
        start = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < num_records; ++i) {
            auto data = mmap_reader.get_record(i);
        }
        auto mmap_time = std::chrono::high_resolution_clock::now() - start;
        
        auto original_ms = std::chrono::duration_cast<std::chrono::milliseconds>(original_time).count();
        auto mmap_ms = std::chrono::duration_cast<std::chrono::milliseconds>(mmap_time).count();
        
        std::cout << "\nSequential access (" << num_records << " records):" << std::endl;
        std::cout << "  Original: " << original_ms << "ms" << std::endl;
        std::cout << "  MMap:     " << mmap_ms << "ms" << std::endl;
        std::cout << "  Speedup:  " << static_cast<double>(original_ms) / mmap_ms << "x" << std::endl;
    }
    
    // Random access benchmark
    {
        BagFileReader original_reader(test_file);
        BagFileReaderMMap mmap_reader(test_file);
        
        size_t total_records = original_reader.size();
        size_t num_accesses = std::min(static_cast<size_t>(1000), total_records);
        
        std::mt19937 gen(42);
        std::uniform_int_distribution<size_t> dist(0, total_records - 1);
        std::vector<size_t> indices;
        for (size_t i = 0; i < num_accesses; ++i) {
            indices.push_back(dist(gen));
        }
        
        // Time original reader
        auto start = std::chrono::high_resolution_clock::now();
        for (size_t idx : indices) {
            auto data = original_reader.get_record(idx);
        }
        auto original_time = std::chrono::high_resolution_clock::now() - start;
        
        // Time mmap reader
        start = std::chrono::high_resolution_clock::now();
        for (size_t idx : indices) {
            auto data = mmap_reader.get_record(idx);
        }
        auto mmap_time = std::chrono::high_resolution_clock::now() - start;
        
        auto original_ms = std::chrono::duration_cast<std::chrono::milliseconds>(original_time).count();
        auto mmap_ms = std::chrono::duration_cast<std::chrono::milliseconds>(mmap_time).count();
        
        std::cout << "\nRandom access (" << num_accesses << " accesses):" << std::endl;
        std::cout << "  Original: " << original_ms << "ms" << std::endl;
        std::cout << "  MMap:     " << mmap_ms << "ms" << std::endl;
        std::cout << "  Speedup:  " << static_cast<double>(original_ms) / mmap_ms << "x" << std::endl;
    }
}
