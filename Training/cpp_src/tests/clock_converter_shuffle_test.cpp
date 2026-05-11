#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <algorithm>
#include <sstream>
#include <cstring>
#include "../clock_converter/clock_data_converter.hpp"
#include "../core/bagz.hpp"
#include "../pgn_converter/shuffle_manager.hpp"
#include "../shuffle/bucket_manager.hpp"
#include <simdjson.h>
#include <lz4frame.h>

namespace chessmimic::test {

class ClockConverterShuffleTest : public testing::Test {
    protected:
        void SetUp() override {
            // Create temporary directory for tests
            temp_dir = std::filesystem::temp_directory_path() / "clock_converter_shuffle_test";
            std::filesystem::create_directories(temp_dir);
        }

        void TearDown() override {
            // Clean up temporary directory
            std::filesystem::remove_all(temp_dir);
        }

        // Helper function to create a test records file for bucket shuffle
        static void createKeyedRecordsFile(const std::string& path, size_t num_records) {
            std::ofstream out(path, std::ios::binary);

            for (size_t i = 0; i < num_records; ++i) {
                // Create a key (position identifier)
                std::string key = "position_" + std::to_string(i);

                // Create the record data
                std::ostringstream oss;
                oss << "{\"fen\":\"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\","
                        << "\"recent_moves\":[],"
                        << "\"rating\":" << (1500 + i) << ","
                        << "\"player_clock\":300.0,"
                        << "\"opponent_clock\":300.0,"
                        << "\"increment\":3.0,"
                        << "\"thinking_time\":" << (1.0 + (i % 10) * 0.1) << ","
                        << "\"record_id\":" << i << "}";

                std::string json_str = oss.str();

                // Calculate sizes
                uint32_t key_len = static_cast<uint32_t>(key.size());
                uint32_t record_len = static_cast<uint32_t>(json_str.size());
                uint32_t total_size = sizeof(key_len) + key_len + sizeof(record_len) + record_len;

                // Write in the format expected by RecordProcessor
                out.write(reinterpret_cast<const char*>(&total_size), sizeof(total_size));
                out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
                out.write(key.data(), key.size());
                out.write(reinterpret_cast<const char*>(&record_len), sizeof(record_len));
                out.write(json_str.data(), json_str.size());
            }

            out.close();
        }

        // Helper function to read records from BAGZ format
        static std::vector<int> readBagzRecordIds(const std::string& path) {
            BagFileReader reader(path);
            std::vector<int> record_ids;
            simdjson::dom::parser parser;

            for (size_t i = 0; i < reader.size(); ++i) {
                auto record_data = reader.get_record(i);
                std::string json_str(record_data.begin(), record_data.end());
                simdjson::dom::element doc = parser.parse(json_str);
                record_ids.push_back(static_cast<int>(doc["record_id"].get_int64()));
            }

            return record_ids;
        }

        std::filesystem::path temp_dir;
};

// Test that shuffle configuration can be parsed from command line
TEST_F(ClockConverterShuffleTest, ParseShuffleArguments) {
    const char* argv[] = {
        "pgn_to_clock_bagz",
        "file1.pgn.lz4",
        "--output", "output.bagz",
        "--shuffle",
        "--shuffle-seed", "42",
        "--shuffle-buckets", "128",
        "--bucket-size-mb", "1024",
        "--bucket-ram-limit-mb", "2048"
    };
    int argc = std::size(argv);

    ClockDataConverter::Config config = ClockDataConverter::parseArguments(argc, const_cast<char**>(argv));

    EXPECT_TRUE(config.shuffle_enabled);
    EXPECT_EQ(config.shuffle_seed, 42);
    EXPECT_EQ(config.shuffle_buckets, 128);
    EXPECT_EQ(config.max_bucket_size_mb, 1024);
    EXPECT_EQ(config.shuffle_memory_threshold_mb, 2048);  // Should map deprecated param
}

// Test that shuffle is disabled by default
TEST_F(ClockConverterShuffleTest, ShuffleDisabledByDefault) {
    const char* argv[] = {
        "pgn_to_clock_bagz",
        "file1.pgn.lz4",
        "--output", "output.bagz"
    };
    int argc = std::size(argv);

    ClockDataConverter::Config config = ClockDataConverter::parseArguments(argc, const_cast<char**>(argv));

    EXPECT_FALSE(config.shuffle_enabled);
    EXPECT_EQ(config.shuffle_seed, 0);  // 0 means random seed
    EXPECT_EQ(config.shuffle_buckets, 1024);  // Default value
}

// Test in-memory shuffle functionality
TEST_F(ClockConverterShuffleTest, InMemoryShuffleWorks) {
    // Create test records file (simple format)
    auto input_records = temp_dir / "input.records";
    size_t num_records = 1000;
    createKeyedRecordsFile(input_records.string(), num_records);

    // Create shuffle manager
    auto logger = std::make_unique<Logger>(Logger::Level::INFO);
    auto thread_pool = std::make_unique<ThreadPool>(1);

    ShuffleManager shuffle_manager(
        temp_dir.string(),
        42,  // Fixed seed
        false,  // Don't keep temp files
        thread_pool.get(),
        logger.get()
    );

    // Perform shuffle
    std::string shuffled_file = shuffle_manager.shuffleRecords(input_records.string());

    // Read records from shuffled file
    std::vector<int> record_ids = readBagzRecordIds(shuffled_file);

    // Verify all records are present
    EXPECT_EQ(record_ids.size(), num_records);

    // Verify records are shuffled (not in original order)
    bool is_shuffled = false;
    for (size_t i = 0; i < record_ids.size(); ++i) {
        if (record_ids[i] != static_cast<int>(i)) {
            is_shuffled = true;
            break;
        }
    }
    EXPECT_TRUE(is_shuffled) << "Records are still in original order";

    // Verify all IDs are present
    std::unordered_set seen_ids(record_ids.begin(), record_ids.end());
    EXPECT_EQ(seen_ids.size(), num_records);
}

// Test bucket shuffle functionality
TEST_F(ClockConverterShuffleTest, BucketShuffleWorks) {
    // Create test records file (keyed format)
    auto input_records = temp_dir / "input.records";
    size_t num_records = 1000;
    createKeyedRecordsFile(input_records.string(), num_records);

    // Create managers
    auto logger = std::make_unique<Logger>(Logger::Level::INFO);
    auto thread_pool = std::make_unique<ThreadPool>(2);

    ShuffleManager shuffle_manager(
        temp_dir.string(),
        999,  // Fixed seed
        false,  // Don't keep temp files
        thread_pool.get(),
        logger.get()
    );

    // Create buckets subdirectory
    std::filesystem::create_directories(temp_dir / "buckets");

    BucketManager bucket_manager(
        temp_dir.string(),
        8,  // Number of buckets
        10,  // Buffer limit MB - smaller for test
        100 * 1024 * 1024,  // Max bucket size bytes (100MB)
        logger.get()
    );

    // Initialize bucket manager
    bucket_manager.initialize();

    // Specify output path with .bagz extension
    auto output_bagz = temp_dir / "shuffled.bagz";

    // Perform bucket shuffle
    std::string shuffled_file = shuffle_manager.bucketShuffleRecords(
        input_records.string(),
        bucket_manager,
        output_bagz.string()
    );

    // Verify the shuffled file exists and has content
    ASSERT_TRUE(std::filesystem::exists(shuffled_file));
    size_t file_size = std::filesystem::file_size(shuffled_file);
    ASSERT_GT(file_size, 0) << "Shuffled file is empty";

    // The bucket shuffle outputs BAGZ format, so read it as BAGZ
    BagFileReader reader(shuffled_file);
    size_t actual_record_count = reader.size();

    // Verify we got records
    ASSERT_GT(actual_record_count, 0) << "No records were read from shuffled BAGZ file";

    // Verify all records are present
    EXPECT_EQ(actual_record_count, num_records)
        << "Record count mismatch: " << actual_record_count << " vs " << num_records;

    // Read record IDs to verify shuffling
    std::vector<int> record_ids;
    simdjson::dom::parser parser;
    for (size_t i = 0; i < std::min(static_cast<size_t>(100), actual_record_count); ++i) {
        auto record_data = reader.get_record(i);
        std::string json_str(record_data.begin(), record_data.end());
        simdjson::dom::element doc = parser.parse(json_str);
        record_ids.push_back(static_cast<int>(doc["record_id"].get_int64()));
    }

    // Verify records are shuffled
    bool is_shuffled = false;
    int sequential_count = 0;
    for (size_t i = 1; i < record_ids.size(); ++i) {
        if (record_ids[i] == record_ids[i-1] + 1) {
            sequential_count++;
        } else {
            is_shuffled = true;
        }
    }

    // If more than 90% are sequential, it's probably not shuffled
    EXPECT_TRUE(is_shuffled || sequential_count < 90)
        << "Records appear to be in mostly sequential order";

    // Verify all unique IDs are present in a larger sample
    std::unordered_set<int> seen_ids;
    size_t sample_size = std::min(actual_record_count, static_cast<size_t>(500));
    simdjson::dom::parser parser2;
    for (size_t i = 0; i < sample_size; ++i) {
        auto record_data = reader.get_record(i);
        std::string json_str(record_data.begin(), record_data.end());
        simdjson::dom::element doc = parser2.parse(json_str);
        seen_ids.insert(static_cast<int>(doc["record_id"].get_int64()));
    }
    EXPECT_EQ(seen_ids.size(), sample_size) << "Duplicate records found in shuffled output";
}

// Test deterministic shuffling with same seed
TEST_F(ClockConverterShuffleTest, DeterministicShuffleWithSeed) {
    // Create test records file (simple format)
    auto input_records = temp_dir / "input.records";
    size_t num_records = 500;
    createKeyedRecordsFile(input_records.string(), num_records);

    auto logger = std::make_unique<Logger>(Logger::Level::INFO);
    auto thread_pool = std::make_unique<ThreadPool>(1);

    std::vector<std::vector<int>> runs;

    // Run shuffle twice with same seed
    for (int run = 0; run < 2; ++run) {
        ShuffleManager shuffle_manager(
            temp_dir.string(),
            12345,  // Same seed
            false,
            thread_pool.get(),
            logger.get()
        );

        std::string shuffled_file = shuffle_manager.shuffleRecords(input_records.string());

        // Read record order
        std::vector<int> record_ids = readBagzRecordIds(shuffled_file);
        runs.push_back(record_ids);

        // Recreate input file for next run
        if (run == 0) {
            createKeyedRecordsFile(input_records.string(), num_records);
        }
    }

    // Verify both runs produced the same order
    ASSERT_EQ(runs[0].size(), runs[1].size());
    for (size_t i = 0; i < runs[0].size(); ++i) {
        EXPECT_EQ(runs[0][i], runs[1][i]) << "Order differs at position " << i;
    }
}

// Test that different seeds produce different shuffles
TEST_F(ClockConverterShuffleTest, DifferentSeedsProduceDifferentShuffles) {
    // Create test records file (simple format)
    auto input_records = temp_dir / "input.records";

    auto logger = std::make_unique<Logger>(Logger::Level::INFO);
    auto thread_pool = std::make_unique<ThreadPool>(1);

    std::vector seeds = {123, 456};
    std::vector<std::vector<int>> orders;

    for (int seed : seeds) {
        size_t num_records = 500;
        createKeyedRecordsFile(input_records.string(), num_records);

        ShuffleManager shuffle_manager(
            temp_dir.string(),
            seed,
            false,
            thread_pool.get(),
            logger.get()
        );

        std::string shuffled_file = shuffle_manager.shuffleRecords(input_records.string());

        // Read the order
        std::vector<int> order = readBagzRecordIds(shuffled_file);
        orders.push_back(order);
    }

    // Verify that the orders are different
    ASSERT_EQ(orders[0].size(), orders[1].size());
    bool orders_differ = false;
    for (size_t i = 0; i < orders[0].size(); ++i) {
        if (orders[0][i] != orders[1][i]) {
            orders_differ = true;
            break;
        }
    }

    EXPECT_TRUE(orders_differ) << "Different seeds produced the same shuffle order";
}

// Test that shuffle now directly produces BAGZ
TEST_F(ClockConverterShuffleTest, ShuffleDirectlyProducesBagz) {
    // Create test records file (simple format)
    auto input_records = temp_dir / "input.records";
    auto output_bagz = temp_dir / "output.bagz";
    size_t num_records = 100;
    createKeyedRecordsFile(input_records.string(), num_records);

    // Create shuffle manager and shuffle to BAGZ
    auto logger = std::make_unique<Logger>(Logger::Level::INFO);
    auto thread_pool = std::make_unique<ThreadPool>(1);

    ShuffleManager shuffle_manager(
        temp_dir.string(),
        123,  // seed
        false, // keep temp files
        thread_pool.get(),
        logger.get()
    );

    // Shuffle directly to BAGZ format
    std::string result = shuffle_manager.shuffleRecords(input_records.string(), output_bagz.string());
    EXPECT_EQ(result, output_bagz.string());

    // Verify BAGZ file
    ASSERT_TRUE(std::filesystem::exists(output_bagz));

    BagFileReader reader(output_bagz.string());
    EXPECT_EQ(reader.size(), num_records);

    // Verify records
    simdjson::dom::parser parser3;
    for (size_t i = 0; i < std::min(static_cast<size_t>(10), reader.size()); ++i) {
        auto record_data = reader.get_record(i);
        std::string json_str(record_data.begin(), record_data.end());
        simdjson::dom::element doc = parser3.parse(json_str);

        // Check if keys exist by attempting to access them
        EXPECT_NO_THROW(doc["fen"]);
        EXPECT_NO_THROW(doc["record_id"]);
    }
}

}
