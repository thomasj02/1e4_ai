#include <gtest/gtest.h>
#include <filesystem>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include "../bucket_manager.hpp"
#include "../bucket_record.hpp"
#include "../../utils/logger.hpp"

using namespace chessmimic;

// Test specifically for the bucket_stats_ vector race condition
class BucketStatsRaceTest : public testing::Test {
protected:
    std::string temp_dir = std::filesystem::temp_directory_path() / "bucket_stats_race_test";
    std::unique_ptr<Logger> logger;
    
    void SetUp() override {
        std::filesystem::remove_all(temp_dir);
        std::filesystem::create_directories(temp_dir);
        logger = std::make_unique<Logger>();
    }
    
    void TearDown() override {
        std::filesystem::remove_all(temp_dir);
    }

    static BucketRecord createRecord(const std::string& key, size_t size) {
        BucketRecord record;
        record.key = key;
        record.key_len = key.size();
        record.record.resize(size, 'X');
        record.record_len = size;
        record.record_size = sizeof(record.record_size) + sizeof(record.key_len) +
                            record.key_len + sizeof(record.record_len) + record.record_len;
        return record;
    }
};

// Test the specific race condition between bucket_stats_ resize and read
TEST_F(BucketStatsRaceTest, ConcurrentResizeAndRead) {
    // Create bucket manager with small buckets to force frequent overflows
    constexpr size_t num_buckets = 2;
    constexpr size_t total_memory_mb = 1;
    constexpr size_t max_bucket_size_bytes = 5 * 1024; // 5KB - very small to force many overflows
    
    auto bucket_manager = std::make_unique<BucketManager>(
        temp_dir, num_buckets, total_memory_mb, max_bucket_size_bytes, logger.get());
    bucket_manager->initialize();
    
    std::atomic stop_readers{false};
    std::atomic<size_t> read_count{0};
    std::atomic<size_t> write_count{0};
    
    // Reader threads that continuously call getBucketStats() and access the vector
    auto reader_func = [&] {
        while (!stop_readers) {
            try {
                // This accesses bucket_stats_ vector
                // Simulate reading through the stats (which was unsafe before the fix)
                for (const auto& stats = bucket_manager->getBucketStats(); const auto& stat : stats) {
                    // Access various fields to increase chance of catching memory issues
                    volatile bool is_overflow = stat.is_overflow;
                    volatile size_t buffer_size = stat.buffer_size;
                    volatile size_t total_bytes = stat.total_bytes;
                    (void)is_overflow;
                    (void)buffer_size;
                    (void)total_bytes;
                }
                ++read_count;
                
                // Also call getAllBucketPaths which iterates bucket_stats_
                auto paths = bucket_manager->getAllBucketPaths();
                
            } catch (const std::exception& e) {
                FAIL() << "Reader thread crashed: " << e.what();
            }
        }
    };
    
    // Writer threads that create records forcing overflow buckets (vector resize)
    auto writer_func = [&](int thread_id) {
        try {
            // Each thread writes records that will force overflow bucket creation
            for (int i = 0; i < 50; ++i) {
                std::string key = "thread" + std::to_string(thread_id) + "_rec" + std::to_string(i);
                auto record = createRecord(key, 2 * 1024); // 2KB records
                bucket_manager->writeRecord(record);
                ++write_count;
                
                // Small delay to spread out the writes
                if (i % 5 == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
            }
        } catch (const std::exception& e) {
            FAIL() << "Writer thread " << thread_id << " crashed: " << e.what();
        }
    };
    
    // Start multiple reader threads
    constexpr int num_readers = 4;
    std::vector<std::thread> reader_threads;
    for (int i = 0; i < num_readers; ++i) {
        reader_threads.emplace_back(reader_func);
    }
    
    // Start multiple writer threads
    constexpr int num_writers = 4;
    std::vector<std::thread> writer_threads;
    for (int i = 0; i < num_writers; ++i) {
        writer_threads.emplace_back(writer_func, i);
    }
    
    // Wait for writers to complete
    for (auto& t : writer_threads) {
        t.join();
    }
    
    // Stop readers
    stop_readers = true;
    for (auto& t : reader_threads) {
        t.join();
    }
    
    // Verify results
    bucket_manager->flushAllBuffers();
    
    // Check that we created overflow buckets
    const auto& final_stats = bucket_manager->getBucketStats();
    size_t overflow_count = 0;
    size_t total_records = 0;
    
    for (const auto& stat : final_stats) {
        if (stat.is_overflow) {
            overflow_count++;
        }
        total_records += stat.record_count;
    }
    
    EXPECT_GT(overflow_count, 0) << "Test should have created overflow buckets";
    EXPECT_EQ(total_records, write_count.load()) << "All written records should be accounted for";
    EXPECT_GT(read_count.load(), 100) << "Readers should have performed many reads during the test";
    
    // If we got here without crashing, the race condition is fixed
}

// Test many threads creating overflow buckets simultaneously
TEST_F(BucketStatsRaceTest, SimultaneousOverflowCreation) {
    // Create bucket manager with very small buckets
    constexpr size_t num_buckets = 4;
    constexpr size_t total_memory_mb = 1;
    constexpr size_t max_bucket_size_bytes = 1024; // 1KB - extremely small
    
    auto bucket_manager = std::make_unique<BucketManager>(
        temp_dir, num_buckets, total_memory_mb, max_bucket_size_bytes, logger.get());
    bucket_manager->initialize();
    
    std::atomic<size_t> overflow_created{0};
    
    // Many threads all trying to create overflow buckets at once
    auto overflow_creator = [&](int thread_id) {
        try {
            // Write enough data to force overflow
            for (int i = 0; i < 10; ++i) {
                std::string key = "overflow_" + std::to_string(thread_id) + "_" + std::to_string(i);
                auto record = createRecord(key, 512); // 512 byte records
                bucket_manager->writeRecord(record);
            }
            ++overflow_created;
        } catch (const std::exception& e) {
            FAIL() << "Thread " << thread_id << " failed: " << e.what();
        }
    };
    
    // Launch many threads simultaneously
    constexpr int num_threads = 20;
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(overflow_creator, i);
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    // Verify
    bucket_manager->flushAllBuffers();
    
    const auto& stats = bucket_manager->getBucketStats();
    size_t overflow_buckets = 0;
    
    for (const auto& stat : stats) {
        if (stat.is_overflow) {
            overflow_buckets++;
        }
    }
    
    EXPECT_GT(overflow_buckets, num_buckets) << "Should have created many overflow buckets";
    EXPECT_EQ(overflow_created.load(), num_threads) << "All threads should have completed";
}