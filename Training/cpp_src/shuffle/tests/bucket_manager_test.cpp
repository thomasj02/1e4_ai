#include <gtest/gtest.h>
#include <filesystem>
#include <vector>
#include <string>
#include <thread>
#include <random>
#include "../../pgn_converter/pgn_to_bagz_converter.hpp"

using namespace chessmimic;

// Test fixture for BucketManager tests
class BucketManagerTest : public testing::Test {
protected:
    void SetUp() override {
        // Create a temporary directory for test files
        temp_dir_ = std::filesystem::temp_directory_path() / "bucket_manager_test";
        std::filesystem::create_directories(temp_dir_);

        // Create logger for testing
        logger_ = std::make_shared<Logger>();

        // Create a BucketManager instance for testing
        bucket_manager_ = std::make_unique<BucketManager>(
            temp_dir_.string(),
            num_buckets_,
            buffer_limit_mb_,
            max_bucket_size_bytes_,
            logger_.get()  // Pass the raw pointer from shared_ptr
        );

        bucket_manager_->initialize();
    }

    void TearDown() override {
        // Clean up bucket files
        bucket_manager_->cleanupBucketFiles();

        // Reset manager before cleaning up temp directory
        bucket_manager_.reset();

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
    std::unique_ptr<BucketManager> bucket_manager_;
    const size_t num_buckets_ = 16;
    const size_t buffer_limit_mb_ = 1; // Small limit for testing
    const size_t max_bucket_size_bytes_ = 64 * 1024 * 1024; // 64MB default max bucket size
};

// Test basic functionality: initialize, write records, and get statistics
TEST_F(BucketManagerTest, BasicFunctionality) {
    // Create some test records
    auto record1 = createTestRecord("key1", R"({"value": 1})");
    auto record2 = createTestRecord("key2", R"({"value": 2})");
    auto record3 = createTestRecord("key3", R"({"value": 3})");

    // Write records
    bucket_manager_->writeRecord(record1);
    bucket_manager_->writeRecord(record2);
    bucket_manager_->writeRecord(record3);

    // Flush buffers to ensure everything is written to disk
    bucket_manager_->flushAllBuffers();

    // Get statistics
    size_t total_records = bucket_manager_->getTotalRecordCount();
    size_t total_bytes = bucket_manager_->getTotalSizeBytes();
    const auto& bucket_stats = bucket_manager_->getBucketStats();

    // Verify total record count
    EXPECT_EQ(total_records, 3);

    // Verify total bytes is non-zero
    EXPECT_GT(total_bytes, 0);

    // Verify bucket stats
    size_t bucket_record_sum = 0;
    size_t bucket_bytes_sum = 0;
    for (const auto& stat : bucket_stats) {
        bucket_record_sum += stat.record_count;
        bucket_bytes_sum += stat.total_bytes;
    }

    EXPECT_EQ(bucket_record_sum, 3);
    EXPECT_EQ(bucket_bytes_sum, total_bytes);
}

// Test buffer management and flushing
TEST_F(BucketManagerTest, BufferManagement) {
    // Create a large number of records to force buffer flushing
    constexpr int num_records = 1000;
    std::string base_record = R"({"large_field": ")";

    // Add a string of 1000 bytes to make the record large
    std::string padding(1000, 'x');
    base_record += padding + "\"}";

    // Write many records to trigger buffer flushing
    for (int i = 0; i < num_records; i++) {
        std::string key = "key" + std::to_string(i);
        auto record = createTestRecord(key, base_record);
        bucket_manager_->writeRecord(record);
    }

    // Final flush to ensure everything is written
    bucket_manager_->flushAllBuffers();

    // Verify record count
    EXPECT_EQ(bucket_manager_->getTotalRecordCount(), num_records);

    // Verify that buffers were flushed (buffer sizes should be 0 after flush)
    for (const auto& stats = bucket_manager_->getBucketStats(); const auto& stat : stats) {
        if (stat.record_count > 0) {
            EXPECT_EQ(stat.buffer_size, 0) << "Buffer for bucket with "
                                         << stat.record_count
                                         << " records should be empty after flush";
        }
    }
}

// Test thread safety by writing records from multiple threads
TEST_F(BucketManagerTest, ThreadSafety) {
    constexpr int threads = 4;
    constexpr int records_per_thread = 250;
    std::vector<std::thread> thread_pool;

    // Function for each thread to execute
    auto write_records = [this](int thread_id, int count) {
        std::mt19937 rng(thread_id); // Deterministic seed based on thread ID

        for (int i = 0; i < count; i++) {
            std::string key = "thread" + std::to_string(thread_id) +
                "_record" + std::to_string(i);
            std::string record_data = R"({"thread": )" + std::to_string(thread_id) +
                R"(, "record": )" + std::to_string(i) + "}";

            auto record = createTestRecord(key, record_data);
            bucket_manager_->writeRecord(record);

            // Small random delay to increase chance of thread interleaving
            if (i % 10 == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(rng() % 5));
            }
        }
    };

    // Start worker threads
    for (int t = 0; t < threads; t++) {
        thread_pool.emplace_back(write_records, t, records_per_thread);
    }

    // Wait for all threads to complete
    for (auto& thread : thread_pool) {
        thread.join();
    }

    // Flush buffers and check results
    bucket_manager_->flushAllBuffers();

    // Verify total record count
    EXPECT_EQ(bucket_manager_->getTotalRecordCount(), threads * records_per_thread);
}

// Test error handling by attempting to write to an invalid directory
TEST_F(BucketManagerTest, ErrorHandling) {
    // Create a BucketManager with an invalid path (non-existent root directory)
    auto invalid_path = "/nonexistent/directory/that/should/not/exist";

    auto invalid_manager = std::make_unique<BucketManager>(
        invalid_path,
        num_buckets_,
        buffer_limit_mb_,
        max_bucket_size_bytes_,
        logger_.get()  // Pass the raw pointer from shared_ptr
    );

    // Initialization should throw an exception
    EXPECT_THROW(invalid_manager->initialize(), std::exception);
}

// Test bucket file path generation
TEST_F(BucketManagerTest, BucketFilePaths) {
    // Create records that will hash to different buckets
    constexpr int test_records = 100;
    std::unordered_map<std::string, bool> file_paths_seen;

    for (int i = 0; i < test_records; i++) {
        std::string key = "unique_key_" + std::to_string(i);
        auto record = createTestRecord(key, R"({"index": )" + std::to_string(i) + "}");
        bucket_manager_->writeRecord(record);
    }

    bucket_manager_->flushAllBuffers();

    // Check that bucket files were created
    const auto& stats = bucket_manager_->getBucketStats();
    int files_with_records = 0;

    for (const auto& stat : stats) {
        if (stat.record_count > 0) {
            files_with_records++;

            // Check file exists if it has records
            EXPECT_TRUE(std::filesystem::exists(stat.file_path))
                << "File " << stat.file_path << " should exist with "
                << stat.record_count << " records";
        }
    }

    // Verify multiple buckets were used
    EXPECT_GT(files_with_records, 1)
        << "Records should be distributed across multiple buckets";
}
