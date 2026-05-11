#include <ranges>
#include <gtest/gtest.h>
#include <unordered_map>
#include <set>
#include "../bucket_record.hpp"

using namespace chessmimic;

class BucketDistributionTest : public testing::Test {
protected:
    // Helper to create a test record
    static BucketRecord createTestRecord(const std::string& key, const std::string& data) {
        BucketRecord record;
        record.key_len = key.size();
        record.key = key;
        record.record_len = data.size();
        record.record = data;
        record.record_size = sizeof(record.record_size) + sizeof(record.key_len) + 
                            record.key_len + sizeof(record.record_len) + record.record_len;
        return record;
    }
};

// Test current behavior: identical keys go to same bucket
TEST_F(BucketDistributionTest, CurrentBehavior_IdenticalKeysGoToSameBucket) {
    const std::string identical_key = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";
    
    // Create multiple records with identical keys
    std::set<uint32_t> bucket_indices;
    for (int i = 0; i < 100; i++) {
        constexpr uint32_t num_buckets = 16;
        std::string data = R"({"index":)" + std::to_string(i) + R"(})";
        auto record = createTestRecord(identical_key, data);
        uint32_t bucket_idx = record.getBucketIndex(num_buckets);
        bucket_indices.insert(bucket_idx);
    }
    
    // Current behavior: all records with identical keys go to the same bucket
    EXPECT_EQ(bucket_indices.size(), 1) 
        << "Current implementation puts all identical keys in the same bucket";
}

// Test that different keys go to different buckets (mostly)
TEST_F(BucketDistributionTest, CurrentBehavior_DifferentKeysDistribute) {
    // Create records with different keys
    std::unordered_map<uint32_t, int> bucket_counts;
    for (int i = 0; i < 100; i++) {
        constexpr uint32_t num_buckets = 16;
        std::string key = "position_" + std::to_string(i);
        auto record = createTestRecord(key, "data");
        uint32_t bucket_idx = record.getBucketIndex(num_buckets);
        bucket_counts[bucket_idx]++;
    }
    
    // Should use multiple buckets
    EXPECT_GT(bucket_counts.size(), 1) 
        << "Different keys should distribute across multiple buckets";
}

// Test desired behavior: identical keys with different record indices distribute across buckets
TEST_F(BucketDistributionTest, DesiredBehavior_IdenticalKeysDistributeWithRecordIndex) {
    const std::string identical_key = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";
    
    // This test will fail initially - that's expected in TDD
    std::set<uint32_t> bucket_indices;
    for (size_t i = 0; i < 100; i++) {
        constexpr uint32_t num_buckets = 16;
        std::string data = R"({"index":)" + std::to_string(i) + R"(})";
        auto record = createTestRecord(identical_key, data);
        
        // Use the new API with record index
        uint32_t bucket_idx = record.getBucketIndex(num_buckets, i);  // Pass record index
        bucket_indices.insert(bucket_idx);
    }
    
    // Desired behavior: records with identical keys but different indices 
    // should distribute across multiple buckets
    EXPECT_GT(bucket_indices.size(), 1) 
        << "Identical keys with different record indices should distribute across buckets"
        << " (this test is expected to fail until we implement the fix)";
}

// Test the distribution quality when using record index
TEST_F(BucketDistributionTest, DesiredBehavior_GoodDistributionWithRecordIndex) {
    // Test with multiple different keys
    std::vector<std::string> test_keys = {
        "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
        "r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 4 4",
        "rnbqkb1r/pp1ppppp/5n2/2p5/2P5/5N2/PP1PPPPP/RNBQKB1R w KQkq c6 0 3"
    };
    
    for (const auto& key : test_keys) {
        constexpr uint32_t num_buckets = 16;
        constexpr int num_records_per_key = 1000;
        std::unordered_map<uint32_t, int> bucket_counts;
        
        for (size_t i = 0; i < num_records_per_key; i++) {
            auto record = createTestRecord(key, "data");
            
            // Use new API with record index
            uint32_t bucket_idx = record.getBucketIndex(num_buckets, i);
            
            bucket_counts[bucket_idx]++;
        }
        
        // Check that we use a good number of buckets
        EXPECT_GT(bucket_counts.size(), num_buckets / 2) 
            << "Should use at least half of available buckets for key: " << key;
        
        // Check for reasonable distribution
        double expected_per_bucket = static_cast<double>(num_records_per_key) / num_buckets;
        int min_count = INT_MAX;
        int max_count = 0;
        
        for (const auto& count: bucket_counts | std::views::values) {
            min_count = std::min(min_count, count);
            max_count = std::max(max_count, count);
        }
        
        // The distribution should be reasonably balanced
        double imbalance = static_cast<double>(max_count - min_count) / expected_per_bucket;
        EXPECT_LT(imbalance, 2.0) 
            << "Distribution should be reasonably balanced for key: " << key
            << " (max=" << max_count << ", min=" << min_count << ")";
    }
}

// Test that the new bucket assignment is still deterministic
TEST_F(BucketDistributionTest, DesiredBehavior_DeterministicWithRecordIndex) {
    const std::string test_key = "test_position_key";
    
    // For each record index, the bucket assignment should be deterministic
    for (size_t record_idx = 0; record_idx < 10; record_idx++) {
        constexpr uint32_t num_buckets = 256;
        auto record = createTestRecord(test_key, "data");
        
        // Use new API
        uint32_t first_bucket = record.getBucketIndex(num_buckets, record_idx);
        
        // Verify it's deterministic
        for (int i = 0; i < 5; i++) {
            uint32_t bucket = record.getBucketIndex(num_buckets, record_idx);
            EXPECT_EQ(first_bucket, bucket)
                << "Bucket assignment should be deterministic for record_idx=" << record_idx;
        }
    }
}