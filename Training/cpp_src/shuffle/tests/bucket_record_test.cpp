#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include "../../pgn_converter/pgn_to_bagz_converter.hpp"

using namespace chessmimic;

// Test fixture for BucketRecord tests
class BucketRecordTest : public testing::Test {
protected:
    void SetUp() override {
        // Create a temporary directory for test files
        temp_dir_ = std::filesystem::temp_directory_path() / "bucket_record_test";
        std::filesystem::create_directories(temp_dir_);
    }

    void TearDown() override {
        // Clean up temporary files
        std::filesystem::remove_all(temp_dir_);
    }

    // Helper method to create a test record
    static BucketRecord createTestRecord(const std::string& key, const std::string& record_data) {
        BucketRecord record;
        record.key_len = key.size();
        record.key = key;
        record.record_len = record_data.size();
        record.record = record_data;
        
        // Calculate total record size
        record.record_size = sizeof(record.record_size) + sizeof(record.key_len) + 
                            record.key_len + sizeof(record.record_len) + record.record_len;
        
        return record;
    }

    std::filesystem::path temp_dir_;
};

// Test serialization and deserialization of a BucketRecord
TEST_F(BucketRecordTest, SerializeDeserializeRoundTrip) {
    // Create a test record
    const std::string test_key = "testPositionKey123";
    const std::string test_record = R"({"moves":{"e2e4":{"1000":{"1500":5}}},"recent_and_fen":"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"})";
    
    auto original_record = createTestRecord(test_key, test_record);
    
    // Create a temporary file for the test
    std::string temp_file = (temp_dir_ / "test_record.dat").string();
    
    // Serialize the record to the file
    {
        std::ofstream out(temp_file, std::ios::binary);
        ASSERT_TRUE(out.is_open()) << "Failed to open output file: " << temp_file;
        original_record.serialize(out);
    }
    
    // Deserialize the record from the file
    BucketRecord deserialized_record;
    {
        std::ifstream in(temp_file, std::ios::binary);
        ASSERT_TRUE(in.is_open()) << "Failed to open input file: " << temp_file;
        deserialized_record = BucketRecord::deserialize(in);
    }
    
    // Verify the deserialized record matches the original
    EXPECT_EQ(original_record.record_size, deserialized_record.record_size);
    EXPECT_EQ(original_record.key_len, deserialized_record.key_len);
    EXPECT_EQ(original_record.key, deserialized_record.key);
    EXPECT_EQ(original_record.record_len, deserialized_record.record_len);
    EXPECT_EQ(original_record.record, deserialized_record.record);
}

// Test serializing and deserializing multiple records
TEST_F(BucketRecordTest, MultipleRecords) {
    // Create test records
    std::vector<BucketRecord> original_records;
    
    for (int i = 0; i < 10; i++) {
        std::string key = "key" + std::to_string(i);
        std::string record = R"({"index":)" + std::to_string(i) + R"(,"data":"test" + )" + std::to_string(i) + R"("})";
        original_records.push_back(createTestRecord(key, record));
    }
    
    // Create a temporary file for the test
    std::string temp_file = (temp_dir_ / "test_records.dat").string();
    
    // Serialize all records to the file
    {
        std::ofstream out(temp_file, std::ios::binary);
        ASSERT_TRUE(out.is_open()) << "Failed to open output file: " << temp_file;
        for (const auto& record : original_records) {
            record.serialize(out);
        }
    }
    
    // Deserialize all records from the file
    std::vector<BucketRecord> deserialized_records;
    {
        std::ifstream in(temp_file, std::ios::binary);
        ASSERT_TRUE(in.is_open()) << "Failed to open input file: " << temp_file;
        
        while (in) {
            auto record = BucketRecord::deserialize(in);
            if (in) {
                deserialized_records.push_back(record);
            }
        }
    }
    
    // Verify the number of records
    EXPECT_EQ(original_records.size(), deserialized_records.size());
    
    // Compare each record
    for (size_t i = 0; i < original_records.size(); i++) {
        EXPECT_EQ(original_records[i].record_size, deserialized_records[i].record_size);
        EXPECT_EQ(original_records[i].key_len, deserialized_records[i].key_len);
        EXPECT_EQ(original_records[i].key, deserialized_records[i].key);
        EXPECT_EQ(original_records[i].record_len, deserialized_records[i].record_len);
        EXPECT_EQ(original_records[i].record, deserialized_records[i].record);
    }
}

// Test bucket hash distribution
TEST_F(BucketRecordTest, BucketHashDistribution) {
    constexpr uint32_t num_buckets = 16;
    constexpr int num_keys = 1000;
    
    // Generate test keys and count distribution
    std::vector<uint32_t> bucket_counts(num_buckets, 0);
    
    for (int i = 0; i < num_keys; i++) {
        // Create keys with some randomness but also some pattern
        std::string key = "key" + std::to_string(i) + "_" + std::to_string(rand() % 1000);
        
        auto record = createTestRecord(key, "test");
        uint32_t bucket = record.getBucketIndex(num_buckets);
        
        // Ensure bucket index is valid
        EXPECT_LT(bucket, num_buckets);
        
        // Count distribution
        bucket_counts[bucket]++;
    }
    
    // Check distribution - we expect roughly even distribution
    double expected_per_bucket = static_cast<double>(num_keys) / num_buckets;
    double tolerance = 0.3 * expected_per_bucket; // Allow 30% deviation
    
    for (uint32_t i = 0; i < num_buckets; i++) {
        double deviation = std::abs(bucket_counts[i] - expected_per_bucket);
        double relative_deviation = deviation / expected_per_bucket;
        
        // We expect each bucket to be within tolerance of the expected count
        EXPECT_LT(relative_deviation, tolerance) 
            << "Bucket " << i << " has count " << bucket_counts[i] 
            << ", expected around " << expected_per_bucket;
    }
}

// Test determinism of bucket hash function
TEST_F(BucketRecordTest, BucketHashDeterminism) {
    // Create some test keys
    std::vector<std::string> test_keys = {
        "key1", "key2", "key3", "position1", "position2", "longPositionKeyWithMoreData",
        "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
        "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2"
    };
    
    // Test each key multiple times to ensure the hash is deterministic
    for (const auto& key : test_keys) {
        constexpr uint32_t num_buckets = 256;
        auto record = createTestRecord(key, "test");
        
        // Hash the key multiple times and verify the result is consistent
        uint32_t first_bucket = record.getBucketIndex(num_buckets);
        
        for (int i = 0; i < 5; i++) {
            EXPECT_EQ(first_bucket, record.getBucketIndex(num_buckets))
                << "Hash function not deterministic for key: " << key;
        }
    }
}