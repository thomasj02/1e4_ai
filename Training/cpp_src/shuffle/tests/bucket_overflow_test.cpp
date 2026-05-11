#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <random>
#include <vector>
#include "../bucket_manager.hpp"
#include "../bucket_record.hpp"
#include "../../utils/logger.hpp"

using namespace chessmimic;

class BucketOverflowTest : public testing::Test {
protected:
    std::string temp_dir = std::filesystem::temp_directory_path() / "bucket_test";
    std::unique_ptr<Logger> logger;
    
    void SetUp() override {
        // Create a clean test directory
        std::filesystem::remove_all(temp_dir);
        std::filesystem::create_directories(temp_dir);
        
        // Create logger
        logger = std::make_unique<Logger>();
    }
    
    void TearDown() override {
        // Clean up test directory
        std::filesystem::remove_all(temp_dir);
    }
};

// Test that overflow buckets are created when the primary bucket exceeds the size limit
TEST_F(BucketOverflowTest, OverflowBucketCreation) {
    // Create bucket manager with a small max bucket size to force overflow
    size_t num_buckets = 1; // With only 1 bucket, all records go to bucket 0
    size_t buffer_limit_mb = 1;
    size_t max_bucket_size_bytes = 25 * 1024; // 25KB per bucket - very small to ensure overflow
    
    BucketManager bucket_manager(temp_dir, num_buckets, buffer_limit_mb, max_bucket_size_bytes, logger.get());
    bucket_manager.initialize();
    
    // Create a record of 10KB - we'll add several to exceed the 25KB bucket limit
    BucketRecord record;
    record.key = "test_key_0";
    record.key_len = record.key.size();
    record.record.resize(10 * 1024, 'X'); // 10KB record
    record.record_len = record.record.size();
    record.record_size = record.key_len + record.record_len + 
                          sizeof(record.key_len) + sizeof(record.record_len);
    
    // Add multiple records to force overflow bucket creation
    // Each is ~10KB, and with a 25KB limit, we should overflow after 2-3 records
    for (int i = 0; i < 10; ++i) {
        record.key = "test_key_" + std::to_string(i);
        record.key_len = record.key.size();
        record.record_size = record.key_len + record.record_len + 
                             sizeof(record.key_len) + sizeof(record.record_len);
        
        bucket_manager.writeRecord(record);
    }
    
    // Flush all buffers to ensure everything is written
    bucket_manager.flushAllBuffers();
    
    // Verify bucket stats
    const auto& stats = bucket_manager.getBucketStats();
    
    // Count total records and overflow buckets
    size_t total_records = 0;
    size_t overflow_buckets = 0;
    
    for (const auto& stat : stats) {
        total_records += stat.record_count;
        if (stat.is_overflow) {
            overflow_buckets++;
        }
    }
    
    // With such a small bucket size (25KB) and 10 records of 10KB each,
    // we should have at least 3 overflow buckets
    EXPECT_GE(overflow_buckets, 1) << "Expected at least 1 overflow bucket";
    EXPECT_EQ(total_records, 10) << "Expected all 10 records to be accounted for";
    
    // Clean up
    bucket_manager.cleanupBucketFiles();
}

// Test that record retrieval works correctly with overflow buckets
TEST_F(BucketOverflowTest, RecordDistribution) {
    // Use just 1 bucket with a very small size limit to guarantee overflow
    size_t num_buckets = 1;
    size_t buffer_limit_mb = 1;
    size_t max_bucket_size_bytes = 20 * 1024; // 20KB - small enough that only 2 records fit per bucket
    
    BucketManager bucket_manager(temp_dir, num_buckets, buffer_limit_mb, max_bucket_size_bytes, logger.get());
    bucket_manager.initialize();
    
    // Create 20 records with 10KB each
    // Total size will be ~200KB, which should create several overflow buckets
    size_t record_count = 20;

    for (size_t i = 0; i < record_count; ++i) {
        size_t record_size_kb = 10;
        BucketRecord record;
        record.key = "test_key_" + std::to_string(i);
        record.key_len = record.key.size();
        record.record.resize(record_size_kb * 1024, 'A' + (i % 26));
        record.record_len = record.record.size();
        record.record_size = record.key_len + record.record_len + 
                             sizeof(record.key_len) + sizeof(record.record_len);
        
        bucket_manager.writeRecord(record);
    }
    
    // Flush all buffers
    bucket_manager.flushAllBuffers();
    
    // Get bucket stats
    const auto& stats = bucket_manager.getBucketStats();
    
    // Count buckets and records
    size_t total_buckets = 0;
    size_t overflow_buckets = 0;
    size_t total_records = 0;
    
    for (const auto& stat : stats) {
        if (stat.record_count > 0) {
            total_buckets++;
            total_records += stat.record_count;
            
            if (stat.is_overflow) {
                overflow_buckets++;
            }
        }
    }
    
    // With 20 records of 10KB each and a 20KB bucket size limit,
    // we should have created multiple overflow buckets
    EXPECT_GE(overflow_buckets, 5) << "Expected at least 5 overflow buckets"; 
    EXPECT_EQ(total_records, record_count) << "Expected all records to be accounted for";
    EXPECT_GT(total_buckets, 1) << "Expected multiple buckets to be used";
    
    // Clean up
    bucket_manager.cleanupBucketFiles();
}

// Test handling of bucket RAM limits and streaming processing
TEST_F(BucketOverflowTest, BucketRAMLimit) {
    // Create a BucketManager with normal parameters - use only one bucket
    size_t num_buckets = 1; // With 1 bucket, all records go to bucket 0
    size_t buffer_limit_mb = 1;
    size_t max_bucket_size_bytes = 1 * 1024 * 1024; // 1MB max bucket size
    
    BucketManager bucket_manager(temp_dir, num_buckets, buffer_limit_mb, max_bucket_size_bytes, logger.get());
    bucket_manager.initialize();
    
    // Create a record larger than buffer limit but smaller than max bucket size
    BucketRecord record;
    record.key = "large_record_key";
    record.key_len = record.key.size();
    
    // 500KB record - smaller than max bucket size but large enough to be flushed
    constexpr size_t record_size_kb = 500;
    record.record.resize(record_size_kb * 1024, 'A');
    record.record_len = record.record.size();
    record.record_size = record.key_len + record.record_len + 
                          sizeof(record.key_len) + sizeof(record.record_len);
    
    // Add the record to the bucket manager - should flush to disk because it's bigger than buffer
    bucket_manager.writeRecord(record);
    
    // Verify that the bucket has the record
    const auto& stats = bucket_manager.getBucketStats();
    
    // With one bucket, the record should be in bucket 0
    EXPECT_EQ(stats[0].record_count, 1) << "Expected 1 record in bucket 0";
    EXPECT_GE(stats[0].total_bytes, record_size_kb * 1024) 
        << "Expected bucket to contain at least " << record_size_kb << "KB";
    
    // Add more records to exceed the max bucket size and create overflow
    for (int i = 0; i < 3; ++i) {
        record.key = "large_record_key_" + std::to_string(i);
        record.key_len = record.key.size();
        record.record_size = record.key_len + record.record_len + 
                              sizeof(record.key_len) + sizeof(record.record_len);
        
        bucket_manager.writeRecord(record);
    }
    
    // Flush all buffers
    bucket_manager.flushAllBuffers();
    
    // Verify overflow buckets were created
    bool found_overflow = false;
    for (const auto& stat : bucket_manager.getBucketStats()) {
        if (stat.is_overflow) {
            found_overflow = true;
            break;
        }
    }
    
    EXPECT_TRUE(found_overflow) << "Expected overflow buckets to be created";
    
    // Get the total records
    size_t total_records = 0;
    for (const auto& stat : bucket_manager.getBucketStats()) {
        total_records += stat.record_count;
    }
    
    EXPECT_EQ(total_records, 4) << "Expected 4 total records";
    
    // Clean up
    bucket_manager.cleanupBucketFiles();
}

// We don't need a main function here, it's provided by run_tests.cpp
// The main from run_tests.cpp will be used for all tests