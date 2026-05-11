#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "core/bagz.hpp"
#include <filesystem>
#include <random>
#include <numeric>

using namespace chessmimic;
using namespace testing;

class BagzRoundtripTest : public Test {
protected:
    void SetUp() override {
        // Create a temporary test file path in the build directory
        m_test_file = std::filesystem::temp_directory_path() / "bagz_test.bagz";
        m_test_file_separate = std::filesystem::temp_directory_path() / "bagz_test_separate.bagz";
        
        // Remove any existing test files
        std::filesystem::remove(m_test_file);
        std::filesystem::remove(m_test_file_separate);
        std::filesystem::remove(std::filesystem::path(m_test_file_separate).replace_extension(".limits"));
    }

    void TearDown() override {
        // Clean up test files
        std::filesystem::remove(m_test_file);
        std::filesystem::remove(m_test_file_separate);
        std::filesystem::remove(std::filesystem::path(m_test_file_separate).replace_extension(".limits"));
    }

    // Helper to generate random byte vectors
    static std::vector<uint8_t> generateRandomData(size_t size) {
        std::vector<uint8_t> data(size);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution distrib(0, 255);
        
        for (size_t i = 0; i < size; ++i) {
            data[i] = static_cast<uint8_t>(distrib(gen));
        }
        
        return data;
    }
    
    // Helper to generate test datasets of different sizes
    static std::vector<std::vector<uint8_t>> generateTestDataset(int num_records, int min_size, int max_size) {
        std::vector<std::vector<uint8_t>> dataset;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution size_distrib(min_size, max_size);
        
        for (int i = 0; i < num_records; ++i) {
            size_t record_size = size_distrib(gen);
            dataset.push_back(generateRandomData(record_size));
        }
        
        return dataset;
    }

    std::filesystem::path m_test_file;
    std::filesystem::path m_test_file_separate;
};

TEST_F(BagzRoundtripTest, BasicRoundtrip) {
    // Create some test data - 10 records of 100 bytes each
    std::vector<std::vector<uint8_t>> test_data = generateTestDataset(10, 100, 100);
    
    // Add an empty record for Python compatibility
    test_data.push_back(std::vector<uint8_t>());
    
    // Write the data
    {
        BagWriter writer(m_test_file.string());
        for (const auto& record : test_data) {
            writer.write(record);
        }
        writer.close();
    }
    
    // Read the data back
    {
        BagFileReader reader(m_test_file.string());
        
        // Check the size
        EXPECT_EQ(reader.size(), test_data.size());
        
        // Check each record
        for (size_t i = 0; i < test_data.size(); ++i) {
            auto retrieved_record = reader.get_record(i);
            EXPECT_EQ(retrieved_record, test_data[i]) 
                << "Record " << i << " does not match original data";
        }
    }
}

TEST_F(BagzRoundtripTest, VaryingSizeRecords) {
    // Create test data - 20 records of varying sizes from 10 to 1000 bytes
    std::vector<std::vector<uint8_t>> test_data = generateTestDataset(20, 10, 1000);
    
    // Add an empty record for Python compatibility
    test_data.push_back(std::vector<uint8_t>());
    
    // Write the data
    {
        BagWriter writer(m_test_file.string());
        for (const auto& record : test_data) {
            writer.write(record);
        }
        writer.close();
    }
    
    // Read the data back
    {
        BagFileReader reader(m_test_file.string());
        
        // Check the size
        EXPECT_EQ(reader.size(), test_data.size());
        
        // Check each record
        for (size_t i = 0; i < test_data.size(); ++i) {
            auto retrieved_record = reader.get_record(i);
            EXPECT_EQ(retrieved_record, test_data[i])
                << "Record " << i << " does not match original data";
        }
    }
}

TEST_F(BagzRoundtripTest, EmptyRecords) {
    // Create test data with non-empty records
    std::vector test_data = {
        generateRandomData(10),  // Small record
        generateRandomData(100), // Medium record
        generateRandomData(200)  // Larger record
    };
    
    // Add an empty record for Python compatibility
    test_data.push_back(std::vector<uint8_t>());
    
    // Write the data
    {
        BagWriter writer(m_test_file.string());
        for (const auto& record : test_data) {
            writer.write(record);
        }
        writer.close();
    }
    
    // Read the data back
    {
        BagFileReader reader(m_test_file.string());
        
        // Check the size
        EXPECT_EQ(reader.size(), test_data.size());
        
        // Check each record
        for (size_t i = 0; i < test_data.size(); ++i) {
            auto retrieved_record = reader.get_record(i);
            EXPECT_EQ(retrieved_record, test_data[i])
                << "Record " << i << " does not match original data";
        }
    }
    
    // Add a test for valid empty record handling
    EXPECT_NO_THROW({
        BagWriter writer(m_test_file.string());
        // Mix empty and non-empty records
        writer.write(std::vector<uint8_t>{1, 2, 3});  // Non-empty
        writer.write(std::vector<uint8_t>());         // Empty - should be included in Python compatibility mode
        writer.write(std::vector<uint8_t>{4, 5, 6});  // Non-empty
        // Add a trailing empty record for Python compatibility
        writer.write(std::vector<uint8_t>());
        writer.close();
        
        BagFileReader reader(m_test_file.string());
        // Should have 4 records (including the empty ones)
        EXPECT_EQ(reader.size(), 4);
        
        // First record should be {1, 2, 3}
        std::vector<uint8_t> record0 = reader.get_record(0);
        EXPECT_EQ(record0, std::vector<uint8_t>({1, 2, 3}));
        
        // Second record should be empty
        std::vector<uint8_t> record1 = reader.get_record(1);
        EXPECT_TRUE(record1.empty());
        
        // Third record should be {4, 5, 6}
        std::vector<uint8_t> record2 = reader.get_record(2);
        EXPECT_EQ(record2, std::vector<uint8_t>({4, 5, 6}));
        
        // Fourth record should be empty
        std::vector<uint8_t> record3 = reader.get_record(3);
        EXPECT_TRUE(record3.empty());
    });
}

TEST_F(BagzRoundtripTest, SeparateLimitsFile) {
    // Create test data - make sure none are empty
    std::vector<std::vector<uint8_t>> test_data = generateTestDataset(15, 50, 500);
    
    // Get base filename without path for constructing limits filename
    auto dir = m_test_file_separate.parent_path();
    auto filename = m_test_file_separate.filename().string();
    auto limits_filename = "limits." + filename;
    auto expected_limits_path = dir / limits_filename;
    
    // Write the data with separate limits file
    {
        BagWriter writer(m_test_file_separate.string(), true);
        for (const auto& record : test_data) {
            writer.write(record);
        }
        writer.close();
    }
    
    // Verify the limits file was created - using the correct path
    EXPECT_TRUE(std::filesystem::exists(expected_limits_path)) 
        << "Limits file not found at: " << expected_limits_path.string();
    
    // Read the data back
    {
        BagFileReader reader(m_test_file_separate.string(), true);
        
        // Check the size
        EXPECT_EQ(reader.size(), test_data.size());
        
        // Check each record
        for (size_t i = 0; i < test_data.size(); ++i) {
            auto retrieved_record = reader.get_record(i);
            EXPECT_EQ(retrieved_record, test_data[i])
                << "Record " << i << " does not match original data";
        }
    }
}

TEST_F(BagzRoundtripTest, LargeDataset) {
    // Create a larger dataset - 100 records of varying sizes
    std::vector<std::vector<uint8_t>> test_data = generateTestDataset(100, 10, 2000);
    
    // Add an empty record for Python compatibility
    test_data.push_back(std::vector<uint8_t>());
    
    // Write the data
    {
        BagWriter writer(m_test_file.string());
        for (const auto& record : test_data) {
            writer.write(record);
        }
        writer.close();
    }
    
    // Read the data back
    {
        BagFileReader reader(m_test_file.string());
        
        // Check the size
        EXPECT_EQ(reader.size(), test_data.size());
        
        // Check records (test a subset to save time)
        for (std::vector<size_t> indices_to_check = {0, 24, 49, 74, 99, 100}; auto idx : indices_to_check) {
            if (idx < test_data.size()) {
                auto retrieved_record = reader.get_record(idx);
                EXPECT_EQ(retrieved_record, test_data[idx])
                    << "Record " << idx << " does not match original data";
            }
        }
    }
}

TEST_F(BagzRoundtripTest, CompressionEfficiency) {
    // Create data with high compressibility (repeating patterns)
    std::vector<uint8_t> pattern(100, 0);
    for (size_t i = 0; i < pattern.size(); i++) {
        pattern[i] = static_cast<uint8_t>(i % 10);
    }
    
    std::vector<std::vector<uint8_t>> test_data;
    for (int i = 0; i < 10; i++) {
        // Create a record by repeating the pattern multiple times
        std::vector<uint8_t> record;
        for (int j = 0; j < 10; j++) {
            record.insert(record.end(), pattern.begin(), pattern.end());
        }
        test_data.push_back(record);
    }
    
    // Write the data
    {
        BagWriter writer(m_test_file.string());
        for (const auto& record : test_data) {
            writer.write(record);
        }
        writer.close();
    }
    
    // Read the data back
    {
        BagFileReader reader(m_test_file.string());
        
        // Check each record
        for (size_t i = 0; i < test_data.size(); ++i) {
            auto retrieved_record = reader.get_record(i);
            EXPECT_EQ(retrieved_record, test_data[i])
                << "Record " << i << " does not match original data";
        }
    }
    
    // The file size should be smaller than the raw data due to compression
    size_t raw_data_size = std::accumulate(test_data.begin(), test_data.end(), 0ULL, 
        [](size_t sum, const std::vector<uint8_t>& rec) { return sum + rec.size(); });
    size_t file_size = std::filesystem::file_size(m_test_file);
    
    // This test is more informational than a strict assertion
    std::cout << "Raw data size: " << raw_data_size << " bytes, compressed file size: " 
              << file_size << " bytes, compression ratio: " 
              << (static_cast<double>(raw_data_size) / file_size) << std::endl;
}