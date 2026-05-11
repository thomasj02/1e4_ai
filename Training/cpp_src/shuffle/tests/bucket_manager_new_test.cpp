#include <gtest/gtest.h>
#include <filesystem>
#include <vector>
#include <string>
#include "../../pgn_converter/pgn_to_bagz_converter.hpp"

using namespace chessmimic;

// Test fixture for new BucketManager tests
class BucketManagerNewTest : public testing::Test {
protected:
    void SetUp() override {
        // Create a temporary directory for test files
        temp_dir_ = std::filesystem::temp_directory_path() / "bucket_manager_new_test";
        std::filesystem::create_directories(temp_dir_);

        // Create logger for testing
        logger_ = std::make_shared<Logger>();
    }

    void TearDown() override {
        // Clean up temporary directory
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
    std::shared_ptr<Logger> logger_;
};

// Test that overflow buckets are created in the same directory as primary buckets
TEST_F(BucketManagerNewTest, OverflowBucketsInSameDirectory) {
    // Create BucketManager with small bucket size to force overflow
    constexpr size_t num_buckets = 2;
    constexpr size_t total_memory_mb = 2;  // Total memory limit instead of per-bucket
    constexpr size_t max_bucket_size_bytes = 10 * 1024; // 10KB to force overflow
    
    // NOTE: This constructor signature will need to change
    auto bucket_manager = std::make_unique<BucketManager>(
        temp_dir_.string(),
        num_buckets,
        total_memory_mb,  // This is now TOTAL memory, not per-bucket buffer
        max_bucket_size_bytes,
        logger_.get()
    );
    
    bucket_manager->initialize();
    
    // Create records that will force overflow
    for (int i = 0; i < 20; i++) {
        std::string key = "key_" + std::to_string(i);
        std::string data(5 * 1024, 'X'); // 5KB records
        auto record = createTestRecord(key, data);
        bucket_manager->writeRecord(record);
    }
    
    bucket_manager->flushAllBuffers();
    
    // Get all bucket paths
    auto bucket_paths = bucket_manager->getAllBucketPaths();
    
    // Check that overflow buckets exist
    bool has_overflow = false;
    for (const auto& stats = bucket_manager->getBucketStats(); const auto& stat : stats) {
        if (stat.is_overflow) {
            has_overflow = true;
            
            // Check that overflow bucket is NOT in an "overflow" subdirectory
            EXPECT_FALSE(stat.file_path.find("/overflow/") != std::string::npos)
                << "Overflow bucket should not be in separate overflow directory: " 
                << stat.file_path;
            
            // Check that overflow bucket IS in the buckets directory
            EXPECT_TRUE(stat.file_path.find("/buckets/") != std::string::npos)
                << "Overflow bucket should be in the buckets directory: " 
                << stat.file_path;
        }
    }
    
    EXPECT_TRUE(has_overflow) << "Test should have created overflow buckets";
    
    // Verify overflow directory does not exist
    std::filesystem::path overflow_dir = temp_dir_ / "buckets" / "overflow";
    EXPECT_FALSE(std::filesystem::exists(overflow_dir))
        << "Overflow subdirectory should not exist";
}

// Test dynamic buffer allocation based on total memory
TEST_F(BucketManagerNewTest, DynamicBufferAllocation) {
    constexpr size_t num_buckets = 4;
    constexpr size_t total_memory_mb = 8;  // 8MB total memory
    constexpr size_t max_bucket_size_bytes = 100 * 1024 * 1024; // 100MB (won't overflow)
    
    auto bucket_manager = std::make_unique<BucketManager>(
        temp_dir_.string(),
        num_buckets,
        total_memory_mb,
        max_bucket_size_bytes,
        logger_.get()
    );
    
    bucket_manager->initialize();
    
    // Expected per-bucket buffer should be total_memory / num_buckets = 8MB / 4 = 2MB
    size_t expected_per_bucket_bytes = total_memory_mb * 1024 * 1024 / num_buckets;
    
    // Write records to different buckets
    for (int i = 0; i < 40; i++) {
        std::string key = "key_" + std::to_string(i);
        std::string data(100 * 1024, 'X'); // 100KB records
        auto record = createTestRecord(key, data);
        bucket_manager->writeRecord(record);
    }
    
    // Check buffer allocation through some method (this will need implementation)
    // For now, we'll check that buffers don't exceed their limits
    for (const auto& stats = bucket_manager->getBucketStats(); const auto& stat : stats) {
        if (!stat.is_overflow && stat.buffer_size > 0) {
            EXPECT_LE(stat.buffer_size, expected_per_bucket_bytes)
                << "Buffer size should not exceed per-bucket allocation";
        }
    }
}

// Test that creating overflow buckets triggers buffer reallocation
TEST_F(BucketManagerNewTest, BufferReallocationOnOverflow) {
    constexpr size_t num_buckets = 2;
    constexpr size_t total_memory_mb = 4;  // 4MB total
    constexpr size_t max_bucket_size_bytes = 1024 * 1024; // 1MB to force overflow
    
    auto bucket_manager = std::make_unique<BucketManager>(
        temp_dir_.string(),
        num_buckets,
        total_memory_mb,
        max_bucket_size_bytes,
        logger_.get()
    );
    
    bucket_manager->initialize();
    
    // Initial per-bucket allocation: 4MB / 2 = 2MB each
    size_t initial_per_bucket = total_memory_mb * 1024 * 1024 / num_buckets;
    
    // Force creation of overflow buckets
    for (int i = 0; i < 50; i++) {
        std::string key = "key_" + std::to_string(i);
        std::string data(50 * 1024, 'X'); // 50KB records
        auto record = createTestRecord(key, data);
        bucket_manager->writeRecord(record);
    }
    
    bucket_manager->flushAllBuffers();
    
    // Count total buckets (primary + overflow)
    size_t total_buckets = 0;
    for (const auto& stats = bucket_manager->getBucketStats(); const auto& stat : stats) {
        if (stat.record_count > 0) {
            total_buckets++;
        }
    }
    
    EXPECT_GT(total_buckets, num_buckets) 
        << "Overflow buckets should have been created";
    
    // New per-bucket allocation should be: 4MB / total_buckets
    size_t expected_new_per_bucket = total_memory_mb * 1024 * 1024 / total_buckets;
    
    EXPECT_LT(expected_new_per_bucket, initial_per_bucket)
        << "Per-bucket memory should decrease when more buckets are created";
    
    // This test assumes BucketManager will expose a method to check current buffer limits
    // or we can infer it from behavior when writing more data
}

// Test that total memory usage stays within limits
TEST_F(BucketManagerNewTest, TotalMemoryLimit) {
    constexpr size_t num_buckets = 10;
    constexpr size_t total_memory_mb = 5;  // 5MB total - relatively small
    constexpr size_t max_bucket_size_bytes = 50 * 1024 * 1024; // 50MB
    
    auto bucket_manager = std::make_unique<BucketManager>(
        temp_dir_.string(),
        num_buckets,
        total_memory_mb,
        max_bucket_size_bytes,
        logger_.get()
    );
    
    bucket_manager->initialize();
    
    // Write many small records across all buckets
    for (int i = 0; i < 1000; i++) {
        std::string key = "distributed_key_" + std::to_string(i);
        std::string data(5 * 1024, 'A' + (i % 26)); // 5KB records
        auto record = createTestRecord(key, data);
        bucket_manager->writeRecord(record);
    }
    
    // Get current buffer usage across all buckets
    size_t total_buffer_usage = 0;
    for (const auto& stats = bucket_manager->getBucketStats(); const auto& stat : stats) {
        total_buffer_usage += stat.buffer_size;
    }
    
    // Total buffer usage should not exceed the configured limit
    size_t total_memory_bytes = total_memory_mb * 1024 * 1024;
    EXPECT_LE(total_buffer_usage, total_memory_bytes)
        << "Total buffer usage across all buckets should not exceed configured limit";
}