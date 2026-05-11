#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <simdjson.h>
#include "pgn_converter/record_processor.hpp"

using namespace chessmimic;

// Test fixture for RecordProcessor tests
class RecordProcessorTest : public testing::Test {
protected:
    void SetUp() override {
        // Create a temporary directory for test files
        temp_dir_ = std::filesystem::temp_directory_path() / "record_processor_test";
        std::filesystem::create_directories(temp_dir_);
    }

    void TearDown() override {
        // Clean up temporary files
        std::filesystem::remove_all(temp_dir_);
    }

    // Helper method to write a test record to a file
    static void writeTestRecordToFile(const std::string& file_path,
                                      const std::string& key,
                                      const std::string& record_json) {
        std::ofstream out(file_path, std::ios::binary);
        ASSERT_TRUE(out.is_open()) << "Failed to open output file: " << file_path;

        // Write record size
        uint32_t record_size = sizeof(uint32_t) + key.size() + sizeof(uint32_t) + record_json.size();
        out.write(reinterpret_cast<const char*>(&record_size), sizeof(record_size));

        // Write key
        uint32_t key_len = key.size();
        out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        out.write(key.data(), key_len);

        // Write record
        uint32_t record_len = record_json.size();
        out.write(reinterpret_cast<const char*>(&record_len), sizeof(record_len));
        out.write(record_json.data(), record_len);
    }

    std::filesystem::path temp_dir_;
};

// Test readRecord with valid input
TEST_F(RecordProcessorTest, ReadValidRecord) {
    // Create a test file with a valid record
    std::string test_file = (temp_dir_ / "valid_record.dat").string();
    std::string test_key = "testPositionKey123";
    std::string test_record_json =
        R"({"moves":{"e2e4":{"1000":{"1500":5}}},"recent_and_fen":[[],  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"]})";

    writeTestRecordToFile(test_file, test_key, test_record_json);

    // Read the record using RecordProcessor
    std::ifstream in(test_file, std::ios::binary);
    ASSERT_TRUE(in.is_open()) << "Failed to open input file: " << test_file;

    std::string key, record;
    bool result = RecordProcessor::readRecord(in, key, record);

    // Verify the result
    EXPECT_TRUE(result);
    EXPECT_EQ(test_key, key);
    EXPECT_EQ(test_record_json, record);
}

// Test readRecord with an empty file
TEST_F(RecordProcessorTest, ReadEmptyFile) {
    // Create an empty file
    std::string test_file = (temp_dir_ / "empty_record.dat").string();
    std::ofstream out(test_file, std::ios::binary);
    out.close();

    // Try to read from the empty file
    std::ifstream in(test_file, std::ios::binary);
    ASSERT_TRUE(in.is_open()) << "Failed to open input file: " << test_file;

    std::string key, record;
    bool result = RecordProcessor::readRecord(in, key, record);

    // Expect failure
    EXPECT_FALSE(result);
}

// Test readRecord with an invalid (truncated) file
TEST_F(RecordProcessorTest, ReadTruncatedFile) {
    // Create a test file with a truncated record
    std::string test_file = (temp_dir_ / "truncated_record.dat").string();
    std::ofstream out(test_file, std::ios::binary);

    uint32_t record_size = 100; // Some arbitrary size
    out.write(reinterpret_cast<const char*>(&record_size), sizeof(record_size));

    uint32_t key_len = 10;
    out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));

    // Write only part of the key (truncated)
    std::string partial_key = "truncated";
    out.write(partial_key.data(), partial_key.size());
    out.close();

    // Try to read from the truncated file
    std::ifstream in(test_file, std::ios::binary);
    ASSERT_TRUE(in.is_open()) << "Failed to open input file: " << test_file;

    std::string key, record;
    bool result = RecordProcessor::readRecord(in, key, record);

    // Expect failure
    EXPECT_FALSE(result);
}

// Test mergeRecord with empty target
TEST_F(RecordProcessorTest, MergeRecordEmptyTarget) {
    // Create source record as JSON string
    std::string source = R"({
        "recent_and_fen": [[], "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"],
        "moves": {
            "e2e4": {
                "1000": {
                    "1500": 5
                }
            }
        }
    })";

    // Create empty target
    AggregatedRecord target;

    // Merge records
    RecordProcessor::mergeRecord(target, source);
    
    // Convert to JSON for verification
    std::string result = RecordProcessor::aggregatedRecordToJson(target);

    // Parse result to verify
    try {
        simdjson::dom::parser parser;
        simdjson::dom::element doc = parser.parse(result);

        // Verify result - recent_and_fen is now an array [[moves], fen]
        auto recent_and_fen_array = doc["recent_and_fen"].get_array();
        EXPECT_EQ(recent_and_fen_array.at(0).get_array().size(), 0);  // Empty moves array
        EXPECT_EQ(recent_and_fen_array.at(1).get_string().value(), "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        EXPECT_EQ(doc["moves"]["e2e4"]["1000"]["1500"].get_int64().value(), 5);
    } catch (const simdjson::simdjson_error& e) {
        FAIL() << "JSON parsing error: " << e.what() << " - Result was: " << result;
    }
}

// Test mergeRecord with non-empty target
TEST_F(RecordProcessorTest, MergeRecordNonEmptyTarget) {
    // Create source record with one move
    std::string source = R"({
        "recent_and_fen": [[], "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"],
        "moves": {
            "e2e4": {
                "1000": {
                    "1500": 5
                }
            }
        }
    })";

    // Create target record with a different move
    AggregatedRecord target;
    target.fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    target.moves[MoveKey("d2d4", "2000", "1600")] = 3;

    // Merge records
    RecordProcessor::mergeRecord(target, source);
    
    // Convert to JSON for verification
    std::string result = RecordProcessor::aggregatedRecordToJson(target);

    // Parse result to verify
    simdjson::dom::parser parser;
    simdjson::dom::element doc = parser.parse(result);

    // Verify result - should have both moves
    EXPECT_EQ(doc["moves"]["e2e4"]["1000"]["1500"].get_int64().value(), 5);
    EXPECT_EQ(doc["moves"]["d2d4"]["2000"]["1600"].get_int64().value(), 3);
}

// Test mergeRecord with overlapping moves
TEST_F(RecordProcessorTest, MergeRecordOverlappingMoves) {
    // Create source record
    std::string source = R"({
        "recent_and_fen": [[], "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"],
        "moves": {
            "e2e4": {
                "1000": {
                    "1500": 5
                }
            }
        }
    })";

    // Create target record with the same move but different count
    AggregatedRecord target;
    target.fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    target.moves[MoveKey("e2e4", "1000", "1500")] = 3;

    // Merge records
    RecordProcessor::mergeRecord(target, source);
    
    // Convert to JSON for verification
    std::string result = RecordProcessor::aggregatedRecordToJson(target);

    // Parse result to verify
    simdjson::dom::parser parser;
    simdjson::dom::element doc = parser.parse(result);

    // Verify result - counts should be summed
    EXPECT_EQ(doc["moves"]["e2e4"]["1000"]["1500"].get_int64().value(), 8);
}

// Test countTotalMoves with various record structures
TEST_F(RecordProcessorTest, CountTotalMoves) {
    // Test case 1: Empty record
    AggregatedRecord empty_record;
    empty_record.fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    EXPECT_EQ(0, RecordProcessor::countTotalMoves(empty_record));

    // Test case 2: Single move, single count
    AggregatedRecord single_move_record;
    single_move_record.fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    single_move_record.moves[MoveKey("e2e4", "1000", "1500")] = 5;
    EXPECT_EQ(5, RecordProcessor::countTotalMoves(single_move_record));

    // Test case 3: Multiple moves, multiple time controls, multiple ratings
    AggregatedRecord complex_record;
    complex_record.fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    complex_record.moves[MoveKey("e2e4", "1000", "1500")] = 5;
    complex_record.moves[MoveKey("e2e4", "1000", "1600")] = 3;
    complex_record.moves[MoveKey("e2e4", "2000", "1700")] = 2;
    complex_record.moves[MoveKey("d2d4", "1000", "1500")] = 4;
    EXPECT_EQ(14, RecordProcessor::countTotalMoves(complex_record));
}

// Test writeRecord with and without move filtering
TEST_F(RecordProcessorTest, WriteRecordWithFiltering) {
    std::string test_file = (temp_dir_ / "write_record_test.dat").string();
    std::string test_key = "testPositionKey123";

    // Create a record with many moves
    AggregatedRecord many_moves_record;
    many_moves_record.fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    many_moves_record.moves[MoveKey("e2e4", "1000", "1500")] = 15;
    many_moves_record.moves[MoveKey("e2e4", "1000", "1600")] = 10;
    many_moves_record.moves[MoveKey("d2d4", "1000", "1500")] = 8;

    // Case 1: No filtering (max_moves_per_position = 0)
    {
        std::ofstream out(test_file, std::ios::binary);
        ASSERT_TRUE(out.is_open()) << "Failed to open output file: " << test_file;

        RecordProcessor::writeRecord(out, test_key, many_moves_record, 0);
        out.close();

        // Verify record was written
        std::ifstream in(test_file, std::ios::binary);
        std::string key, record;
        bool result = RecordProcessor::readRecord(in, key, record);

        EXPECT_TRUE(result);
        EXPECT_EQ(test_key, key);

        // Verify JSON content by parsing
        simdjson::dom::parser parser;
        simdjson::dom::element doc = parser.parse(record);
        EXPECT_EQ(doc["moves"]["e2e4"]["1000"]["1500"].get_int64().value(), 15);
        EXPECT_EQ(doc["moves"]["e2e4"]["1000"]["1600"].get_int64().value(), 10);
        EXPECT_EQ(doc["moves"]["d2d4"]["1000"]["1500"].get_int64().value(), 8);
    }

    // Case 2: With filtering (max_moves_per_position = 20) - record should be written
    {
        std::ofstream out(test_file, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open()) << "Failed to open output file: " << test_file;

        RecordProcessor::writeRecord(out, test_key, many_moves_record, 40);
        out.close();

        // Verify record was written
        std::ifstream in(test_file, std::ios::binary);
        std::string key, record;
        bool result = RecordProcessor::readRecord(in, key, record);

        EXPECT_TRUE(result);
        EXPECT_EQ(test_key, key);
    }

    // Case 3: With strict filtering (max_moves_per_position = 10) - record should be skipped
    {
        std::ofstream out(test_file, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open()) << "Failed to open output file: " << test_file;

        RecordProcessor::writeRecord(out, test_key, many_moves_record, 10);
        out.close();

        // Verify file is empty (no record written due to filtering)
        std::ifstream in(test_file, std::ios::binary);
        std::string key, record;
        bool result = RecordProcessor::readRecord(in, key, record);

        EXPECT_FALSE(result); // No record should be present
    }
}

// Test malformed JSON handling in mergeRecord
TEST_F(RecordProcessorTest, HandleMalformedJson) {
    // Create a well-formed source record
    std::string source = R"({
        "recent_and_fen": [[], "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"],
        "moves": {
            "e2e4": {
                "1000": {
                    "1500": 5
                }
            }
        }
    })";

    // Create a target with no moves
    AggregatedRecord target;
    target.fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    // Merge should handle the case gracefully
    RecordProcessor::mergeRecord(target, source);
    
    // Convert to JSON for verification
    std::string result = RecordProcessor::aggregatedRecordToJson(target);

    // Parse result to verify
    simdjson::dom::parser parser;
    simdjson::dom::element doc = parser.parse(result);

    // Verify result
    EXPECT_TRUE(doc["moves"].error() != simdjson::NO_SUCH_FIELD);
    EXPECT_EQ(doc["moves"]["e2e4"]["1000"]["1500"].get_int64().value(), 5);
}

// Test multiple readRecords sequentially
TEST_F(RecordProcessorTest, ReadMultipleRecordsSequentially) {
    // Create a test file with multiple records
    std::string test_file = (temp_dir_ / "multiple_records.dat").string();

    // Create several test records
    struct TestRecord {
        std::string key;
        std::string json;
    };

    std::vector<TestRecord> test_records = {
        {"key1", R"({"moves":{"e2e4":{"1000":{"1500":5}}},"recent_and_fen":[[], "fen1"]})"},
        {"key2", R"({"moves":{"d2d4":{"2000":{"1600":3}}},"recent_and_fen":[[], "fen2"]})"},
        {"key3", R"({"moves":{"g1f3":{"3000":{"1700":7}}},"recent_and_fen":[[], "fen3"]})"}
    };

    // Write all records to the file
    {
        std::ofstream out(test_file, std::ios::binary);
        ASSERT_TRUE(out.is_open()) << "Failed to open output file: " << test_file;

        for (const auto& [key, json] : test_records) {
            // Write directly to the stream instead of reopening the file each time
            uint32_t record_size = sizeof(uint32_t) + key.size() + sizeof(uint32_t) + json.size();
            out.write(reinterpret_cast<const char*>(&record_size), sizeof(record_size));

            uint32_t key_len = key.size();
            out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
            out.write(key.data(), key_len);

            uint32_t record_len = json.size();
            out.write(reinterpret_cast<const char*>(&record_len), sizeof(record_len));
            out.write(json.data(), record_len);
        }
    }

    // Read all records sequentially
    std::ifstream in(test_file, std::ios::binary);
    ASSERT_TRUE(in.is_open()) << "Failed to open input file: " << test_file;

    for (const auto& [expected_key, expected_json] : test_records) {
        std::string key, record;
        bool result = RecordProcessor::readRecord(in, key, record);

        EXPECT_TRUE(result);
        EXPECT_EQ(expected_key, key);
        EXPECT_EQ(expected_json, record);
    }

    // Verify we've reached the end of the file
    std::string key, record;
    bool result = RecordProcessor::readRecord(in, key, record);
    EXPECT_FALSE(result);
}
