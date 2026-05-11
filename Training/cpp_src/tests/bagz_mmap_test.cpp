#include <gtest/gtest.h>
#include "../core/bagz.hpp"
#include "../core/bagz_mmap.hpp"
#include <fstream>
#include <random>
#include <chrono>
#include <future>
#include <filesystem>
#include <simdjson.h>

using namespace chessmimic;

class BagzMMapTest : public testing::Test {
protected:
    void SetUp() override {
        // Create temporary directory for test files
        test_dir = "/tmp/bagz_mmap_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directory(test_dir);
    }
    
    void TearDown() override {
        // Clean up test files
        std::filesystem::remove_all(test_dir);
    }
    
    // Helper to create a test BAGZ file with known content
    std::string createTestBagz(const std::vector<std::string>& records, const std::string& filename) const {
        std::string path = test_dir + "/" + filename;
        BagWriter writer(path);
        
        for (const auto& record : records) {
            std::vector<uint8_t> data(record.begin(), record.end());
            writer.write(data);
        }
        
        writer.close();
        return path;
    }
    
    // Helper to generate random records
    static std::vector<std::string> generateRandomRecords(size_t count, size_t min_size, size_t max_size) {
        std::vector<std::string> records;
        std::mt19937 gen(42); // Fixed seed for reproducibility
        std::uniform_int_distribution<> size_dist(min_size, max_size);
        std::uniform_int_distribution<> char_dist('A', 'Z');
        
        for (size_t i = 0; i < count; ++i) {
            size_t size = size_dist(gen);
            std::string record;
            record.reserve(size);
            
            for (size_t j = 0; j < size; ++j) {
                record += static_cast<char>(char_dist(gen));
            }
            
            records.push_back(record);
        }
        
        return records;
    }
    
    std::string test_dir;
};

// Test 1: Basic compatibility - both readers return same data
TEST_F(BagzMMapTest, BasicCompatibility) {
    std::vector<std::string> test_records = {
        "First record",
        "Second record with more data",
        "Third",
        "",  // Empty record
        "Fifth record after empty"
    };
    
    std::string path = createTestBagz(test_records, "basic.bagz");
    
    BagFileReader original_reader(path);
    BagFileReaderMMap mmap_reader(path);
    
    // Check size
    EXPECT_EQ(original_reader.size(), mmap_reader.size());
    EXPECT_EQ(original_reader.size(), test_records.size());
    
    // Check each record
    for (size_t i = 0; i < test_records.size(); ++i) {
        auto original_data = original_reader.get_record(i);
        auto mmap_data = mmap_reader.get_record(i);
        
        EXPECT_EQ(original_data, mmap_data) << "Mismatch at record " << i;
        
        std::string original_str(original_data.begin(), original_data.end());
        EXPECT_EQ(original_str, test_records[i]) << "Content mismatch at record " << i;
    }
}

// Test 2: Large file handling
TEST_F(BagzMMapTest, LargeFileHandling) {
    constexpr size_t num_records = 10000;
    auto records = generateRandomRecords(num_records, 100, 1000);
    
    std::string path = createTestBagz(records, "large.bagz");
    
    BagFileReader original_reader(path);
    BagFileReaderMMap mmap_reader(path);
    
    EXPECT_EQ(original_reader.size(), mmap_reader.size());
    
    // Sample check - every 100th record
    for (size_t i = 0; i < num_records; i += 100) {
        auto original_data = original_reader.get_record(i);
        auto mmap_data = mmap_reader.get_record(i);
        
        EXPECT_EQ(original_data, mmap_data) << "Mismatch at record " << i;
    }
}

// Test 3: Edge cases
TEST_F(BagzMMapTest, EdgeCases) {
    std::vector edge_records = {
        std::string(1, 'A'),           // Single character
        std::string(10000, 'B'),       // Large record
        std::string(0, 'C'),           // Empty
        std::string(1000000, 'D'),     // Very large record
    };
    
    std::string path = createTestBagz(edge_records, "edge.bagz");
    
    BagFileReader original_reader(path);
    BagFileReaderMMap mmap_reader(path);
    
    for (size_t i = 0; i < edge_records.size(); ++i) {
        auto original_data = original_reader.get_record(i);
        auto mmap_data = mmap_reader.get_record(i);
        
        EXPECT_EQ(original_data, mmap_data) << "Mismatch at edge case " << i;
        
        std::string data_str(mmap_data.begin(), mmap_data.end());
        EXPECT_EQ(data_str, edge_records[i]) << "Content mismatch at edge case " << i;
    }
}

// Test 4: Random access pattern
TEST_F(BagzMMapTest, RandomAccess) {
    constexpr size_t num_records = 1000;
    auto records = generateRandomRecords(num_records, 50, 500);
    
    std::string path = createTestBagz(records, "random.bagz");
    
    BagFileReader original_reader(path);
    BagFileReaderMMap mmap_reader(path);
    
    // Access records in random order
    std::mt19937 gen(123);
    std::uniform_int_distribution<> dist(0, num_records - 1);
    
    for (int i = 0; i < 100; ++i) {
        size_t idx = dist(gen);
        
        auto original_data = original_reader.get_record(idx);
        auto mmap_data = mmap_reader.get_record(idx);
        
        EXPECT_EQ(original_data, mmap_data) << "Mismatch at random access " << idx;
    }
}

// Test 5: Concurrent access (most important for our use case)
TEST_F(BagzMMapTest, ConcurrentAccess) {
    // Test concurrent access with mmap reader
    constexpr size_t num_records = 5000;
    auto records = generateRandomRecords(num_records, 100, 1000);
    std::string path = createTestBagz(records, "concurrent.bagz");
    constexpr size_t num_threads = 8;
    BagFileReaderMMap mmap_reader(path);

    std::vector<std::future<bool>> futures;

    auto worker = [&mmap_reader, &records](size_t start, size_t end) {
        for (size_t i = start; i < end; ++i) {
            auto data = mmap_reader.get_record(i);
            if (std::string data_str(data.begin(), data.end()); data_str != records[i]) {
                return false;
            }
        }
        return true;
    };

    size_t chunk_size = num_records / num_threads;
    for (size_t t = 0; t < num_threads; ++t) {
        size_t start = t * chunk_size;
        size_t end = (t == num_threads - 1) ? num_records : start + chunk_size;

        futures.push_back(std::async(std::launch::async, worker, start, end));
    }

    for (auto& future : futures) {
        EXPECT_TRUE(future.get()) << "Concurrent access failed";
    }
}

// Test 6: Error handling
TEST_F(BagzMMapTest, ErrorHandling) {
    // Test non-existent file
    EXPECT_THROW(BagFileReaderMMap("non_existent.bagz"), std::runtime_error);
    
    // Test out of bounds access
    std::vector<std::string> records = {"test"};
    std::string path = createTestBagz(records, "bounds.bagz");
    
    BagFileReaderMMap reader(path);
    EXPECT_THROW(reader.get_record(-1), std::out_of_range);
    EXPECT_THROW(reader.get_record(1), std::out_of_range);
    EXPECT_THROW(reader.get_record(999), std::out_of_range);
}

// Test 7: Binary data compatibility
TEST_F(BagzMMapTest, BinaryDataCompatibility) {
    std::vector<std::string> binary_records;
    
    // Create records with all possible byte values
    for (int i = 0; i < 256; ++i) {
        std::string record;
        for (int j = 0; j < 10; ++j) {
            record += static_cast<char>(i);
        }
        binary_records.push_back(record);
    }
    
    std::string path = createTestBagz(binary_records, "binary.bagz");
    
    BagFileReader original_reader(path);
    BagFileReaderMMap mmap_reader(path);
    
    for (size_t i = 0; i < binary_records.size(); ++i) {
        auto original_data = original_reader.get_record(i);
        auto mmap_data = mmap_reader.get_record(i);
        
        EXPECT_EQ(original_data, mmap_data) << "Binary data mismatch at record " << i;
    }
}

// Test 8: Multiple readers on same file
TEST_F(BagzMMapTest, MultipleReaders) {
    constexpr size_t num_records = 1000;
    auto records = generateRandomRecords(num_records, 100, 500);
    
    std::string path = createTestBagz(records, "multiple.bagz");
    
    // Create multiple mmap readers
    BagFileReaderMMap reader1(path);
    BagFileReaderMMap reader2(path);
    BagFileReaderMMap reader3(path);
    
    // They should all return the same data
    for (size_t i = 0; i < 100; ++i) {
        auto data1 = reader1.get_record(i);
        auto data2 = reader2.get_record(i);
        auto data3 = reader3.get_record(i);
        
        EXPECT_EQ(data1, data2);
        EXPECT_EQ(data2, data3);
    }
}

// Test 9: Stress test with real chess data
TEST_F(BagzMMapTest, ChessDataCompatibility) {
    // Create records that look like chess position data
    std::vector<std::string> chess_records;
    
    for (int i = 0; i < 100; ++i) {
        std::ostringstream oss;
        oss << "{\"recent_and_fen\":["
            << "[\"e2e4\",\"e7e5\",\"g1f3\"],"
            << "\"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\"],"
            << "\"moves\":{\"Nf3\":{\"180\":{\"1500\":10}}}}";
        
        chess_records.push_back(oss.str());
    }
    
    std::string path = createTestBagz(chess_records, "chess.bagz");
    
    BagFileReader original_reader(path);
    BagFileReaderMMap mmap_reader(path);
    
    for (size_t i = 0; i < chess_records.size(); ++i) {
        auto original_data = original_reader.get_record(i);
        auto mmap_data = mmap_reader.get_record(i);
        
        EXPECT_EQ(original_data, mmap_data);
        
        // Verify JSON is still valid
        std::string data_str(mmap_data.begin(), mmap_data.end());
        simdjson::dom::parser parser;
        EXPECT_NO_THROW(parser.parse(data_str));
    }
}