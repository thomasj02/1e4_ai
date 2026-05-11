#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include "pgn_converter/shuffle_manager.hpp"
#include "pgn_converter/pgn_to_bagz_converter.hpp"
#include "pgn_converter/record_processor.hpp"
#include "../core/bagz.hpp"
#include "../utils/thread_pool.hpp"
#include "../utils/logger.hpp"

using namespace chessmimic;

class ShuffleDistributionTest : public testing::Test {
protected:
    void SetUp() override {
        // Create temp directory
        temp_dir_ = std::filesystem::temp_directory_path() / "shuffle_distribution_test";
        std::filesystem::create_directories(temp_dir_);
        
        // Create logger and thread pool
        logger_ = std::make_unique<Logger>(Logger::Level::DEBUG);
        thread_pool_ = std::make_unique<ThreadPool>(4);
    }

    void TearDown() override {
        // Clean up
        std::filesystem::remove_all(temp_dir_);
    }

    // Create a test input file with multiple records having identical keys
    std::string createTestInputFile(const std::string& filename, 
                                  const std::string& key, 
                                  int num_records) const {
        std::string filepath = (temp_dir_ / filename).string();
        std::ofstream out(filepath, std::ios::binary);
        
        for (int i = 0; i < num_records; i++) {
            std::string record = R"({"index":)" + std::to_string(i) + R"(,"data":"test"})";
            RecordProcessor::writeRawRecord(out, key, record);
        }
        
        out.close();
        return filepath;
    }

    // Analyze bucket distribution after shuffle pass 1
    static std::unordered_map<size_t, int> analyzeBucketDistribution(
        const std::string& bucket_dir, 
        const std::string& target_key) {
        
        std::unordered_map<size_t, int> bucket_counts;
        
        // Iterate through all bucket files
        for (const auto& entry : std::filesystem::directory_iterator(bucket_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".dat") {
                // Extract bucket index from filename (e.g., "bucket_00001.dat")
                std::string filename = entry.path().stem().string();
                size_t bucket_idx = 0;
                if (sscanf(filename.c_str(), "bucket_%zu", &bucket_idx) == 1 ||
                    sscanf(filename.c_str(), "bucket_%zu_overflow", &bucket_idx) == 1) {
                    
                    // Read records from this bucket
                    std::ifstream in(entry.path(), std::ios::binary);
                    std::string key, record;
                    
                    while (RecordProcessor::readRecord(in, key, record)) {
                        if (key == target_key) {
                            bucket_counts[bucket_idx]++;
                        }
                    }
                }
            }
        }
        
        return bucket_counts;
    }

    std::filesystem::path temp_dir_;
    std::unique_ptr<Logger> logger_;
    std::unique_ptr<ThreadPool> thread_pool_;
};

// Test that identical keys are distributed across buckets
TEST_F(ShuffleDistributionTest, IdenticalKeysDistributeAcrossBuckets) {
    const std::string test_key = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";
    constexpr int num_records = 1000;
    constexpr size_t num_buckets = 16;
    
    // Create input file with identical keys
    std::string input_file = createTestInputFile("identical_keys.records", test_key, num_records);
    
    // Create shuffle manager
    ShuffleManager shuffle_manager(
        temp_dir_.string(),
        42,  // seed
        true, // keep temp files for analysis
        thread_pool_.get(),
        logger_.get()
    );
    
    // Create bucket manager
    BucketManager bucket_manager(
        temp_dir_.string(),
        num_buckets,
        256,  // 256MB total memory
        512 * 1024 * 1024,  // 512MB max bucket size
        logger_.get()
    );
    
    // Initialize bucket manager
    bucket_manager.initialize();
    
    // Run bucket shuffle - this internally calls distributeToBuckets
    std::string output_file = shuffle_manager.bucketShuffleRecords(input_file, bucket_manager);
    
    // Analyze distribution from the intermediate bucket files
    std::string bucket_dir = temp_dir_.string() + "/buckets";
    auto distribution = analyzeBucketDistribution(bucket_dir, test_key);
    
    // Verify records are distributed across multiple buckets
    EXPECT_GT(distribution.size(), 1) 
        << "Records with identical keys should be distributed across multiple buckets";
    
    // Verify all records are accounted for
    int total_records_found = 0;
    for (const auto& [bucket, count] : distribution) {
        total_records_found += count;
        CM_LOG_INFO("Bucket {}: {} records", bucket, count);
    }
    EXPECT_EQ(total_records_found, num_records) 
        << "All records should be accounted for";
    
    // Check distribution quality - should use at least 25% of buckets
    EXPECT_GT(distribution.size(), num_buckets / 4) 
        << "Should use at least 25% of available buckets";
    
    // Also verify the output file exists and is a valid BAGZ file
    EXPECT_TRUE(std::filesystem::exists(output_file));
    
    // Read back the output and verify all records are present
    BagFileReader reader(output_file);
    EXPECT_EQ(reader.size(), static_cast<size_t>(num_records));
}

// Test mixed keys still distribute well
TEST_F(ShuffleDistributionTest, MixedKeysDistributeWell) {
    constexpr int records_per_key = 100;
    constexpr size_t num_buckets = 16;
    
    // Create input file with multiple different keys
    std::string filepath = (temp_dir_ / "mixed_keys.records").string();
    std::ofstream out(filepath, std::ios::binary);
    
    std::vector<std::string> test_keys = {
        "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
        "r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 4 4",
        "rnbqkb1r/pp1ppppp/5n2/2p5/2P5/5N2/PP1PPPPP/RNBQKB1R w KQkq c6 0 3"
    };
    
    // Write records with alternating keys
    for (size_t i = 0; i < static_cast<size_t>(records_per_key) * test_keys.size(); i++) {
        std::string key = test_keys[i % test_keys.size()];
        std::string record = R"({"index":)" + std::to_string(i) + R"(})";
        RecordProcessor::writeRawRecord(out, key, record);
    }
    out.close();
    
    // Create managers
    ShuffleManager shuffle_manager(
        temp_dir_.string(),
        42,  // seed
        true, // keep temp files
        thread_pool_.get(),
        logger_.get()
    );
    
    BucketManager bucket_manager(
        temp_dir_.string(),
        num_buckets,
        256,  // 256MB total memory
        512 * 1024 * 1024,  // 512MB max bucket size
        logger_.get()
    );
    
    bucket_manager.initialize();
    
    // Run bucket shuffle
    std::string output_file = shuffle_manager.bucketShuffleRecords(filepath, bucket_manager);
    
    // Analyze distribution for each key
    std::string bucket_dir = temp_dir_.string() + "/buckets";
    
    for (const auto& key : test_keys) {
        auto distribution = analyzeBucketDistribution(bucket_dir, key);
        
        // Each key should be distributed across multiple buckets
        EXPECT_GT(distribution.size(), 1) 
            << "Key '" << key << "' should be distributed across multiple buckets";
        
        // Verify correct count
        int total = 0;
        for (const auto& count : distribution | std::views::values) {
            total += count;
        }
        EXPECT_EQ(total, records_per_key) 
            << "All records for key '" << key << "' should be found";
    }
}