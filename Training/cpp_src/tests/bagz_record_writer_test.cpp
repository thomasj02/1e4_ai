#include <gtest/gtest.h>
#include "../core/bagz_record_writer.hpp"
#include "../clock_converter/clock_position_record.hpp"
#include "../core/bagz.hpp"
#include <filesystem>
#include <vector>

namespace chessmimic {

class BagzRecordWriterTest : public testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path() / 
                    ("bagz_record_test_" + std::to_string(getpid()));
        std::filesystem::create_directories(temp_dir_);
    }
    
    void TearDown() override {
        std::filesystem::remove_all(temp_dir_);
    }
    
    std::filesystem::path temp_dir_;
};

// Test basic writing functionality
TEST_F(BagzRecordWriterTest, WriteBasicRecords) {
    std::string output_path = (temp_dir_ / "test.bagz").string();
    BagzRecordWriter writer(output_path);
    
    // Create test records
    std::vector<ClockPositionRecord> records;
    
    ClockPositionRecord record1;
    record1.fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1";
    record1.recent_moves = {"e2e4"};
    record1.rating = 1750;
    record1.player_clock = 178.0f;
    record1.opponent_clock = 180.0f;
    record1.increment = 2.0f;
    record1.thinking_time = 4.0f;
    records.push_back(record1);
    
    ClockPositionRecord record2;
    record2.fen = "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2";
    record2.recent_moves = {"e2e4", "e7e5"};
    record2.rating = 1723;
    record2.player_clock = 177.0f;
    record2.opponent_clock = 178.0f;
    record2.increment = 2.0f;
    record2.thinking_time = 5.0f;
    records.push_back(record2);
    
    // Write records
    for (const auto& record : records) {
        writer.writeRecord(record);
    }
    
    // Finalize writing
    writer.close();
    
    // Verify file exists
    ASSERT_TRUE(std::filesystem::exists(output_path));
    
    // Read back and verify
    BagFileReader reader(output_path);
    ASSERT_EQ(reader.size(), 2);
    
    // Check first record
    auto data1 = reader.get_record(0);
    std::string json1(data1.begin(), data1.end());
    EXPECT_TRUE(json1.find("\"fen\":\"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1\"") != std::string::npos);
    EXPECT_TRUE(json1.find("\"rating\":1750") != std::string::npos);
    EXPECT_TRUE(json1.find("\"player_clock\":178") != std::string::npos);
    
    // Check second record
    auto data2 = reader.get_record(1);
    std::string json2(data2.begin(), data2.end());
    EXPECT_TRUE(json2.find("\"rating\":1723") != std::string::npos);
}

// Test writing empty records
TEST_F(BagzRecordWriterTest, WriteEmptyFile) {
    std::string output_path = (temp_dir_ / "empty.bagz").string();
    BagzRecordWriter writer(output_path);
    
    // Close without writing anything
    writer.close();
    
    // File should still be created
    ASSERT_TRUE(std::filesystem::exists(output_path));
    
    // Should have 0 records
    BagFileReader reader(output_path);
    EXPECT_EQ(reader.size(), 0);
}

// Test writing many records
TEST_F(BagzRecordWriterTest, WriteManyRecords) {
    std::string output_path = (temp_dir_ / "many.bagz").string();
    BagzRecordWriter writer(output_path);

    constexpr size_t num_records = 1000;
    
    // Write many records
    for (size_t i = 0; i < num_records; ++i) {
        ClockPositionRecord record;
        record.fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
        record.rating = 1500 + (i % 500);
        record.player_clock = 180.0f - (i % 60);
        record.opponent_clock = 180.0f;
        record.increment = (i % 3) + 1.0f;
        record.thinking_time = (i % 10) / 2.0f;
        
        writer.writeRecord(record);
    }
    
    writer.close();
    
    // Verify all records were written
    BagFileReader reader(output_path);
    EXPECT_EQ(reader.size(), num_records);
    
    // Spot check a few records
    auto data = reader.get_record(500);
    std::string json(data.begin(), data.end());
    EXPECT_TRUE(json.find("\"rating\":1500") != std::string::npos); // 1500 + (500 % 500 = 0)
}

// Test statistics tracking
TEST_F(BagzRecordWriterTest, TrackStatistics) {
    std::string output_path = (temp_dir_ / "stats.bagz").string();
    BagzRecordWriter writer(output_path);
    
    // Write records with various properties
    std::vector<ClockPositionRecord> records;
    
    // Different ratings
    for (int rating : {1500, 1600, 1700, 1800, 1900}) {
        ClockPositionRecord record;
        record.fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
        record.rating = rating;
        record.player_clock = 180.0f;
        record.opponent_clock = 180.0f;
        record.increment = 2.0f;
        record.thinking_time = 2.0f;
        records.push_back(record);
    }
    
    for (const auto& record : records) {
        writer.writeRecord(record);
    }
    
    auto [total_records, total_bytes_uncompressed] = writer.getStatistics();
    EXPECT_EQ(total_records, 5);
    EXPECT_EQ(total_bytes_uncompressed, writer.getTotalBytesWritten());
    EXPECT_GT(total_bytes_uncompressed, 0);
    
    writer.close();
    
    // Check compression ratio
    size_t file_size = std::filesystem::file_size(output_path);
    EXPECT_LT(file_size, total_bytes_uncompressed); // Should be compressed
}

// Test handling of special characters in JSON
TEST_F(BagzRecordWriterTest, HandleSpecialCharacters) {
    std::string output_path = (temp_dir_ / "special.bagz").string();
    BagzRecordWriter writer(output_path);
    
    ClockPositionRecord record;
    // FEN with quotes and backslashes should be properly escaped
    record.fen = "r\"n\\bqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    record.recent_moves = {"e2e4", "e7e5"};
    record.rating = 1750;
    record.player_clock = 180.0f;
    record.opponent_clock = 180.0f;
    record.increment = 0.0f;
    record.thinking_time = 0.0f;
    
    writer.writeRecord(record);
    writer.close();
    
    // Read back and verify JSON is valid
    BagFileReader reader(output_path);
    ASSERT_EQ(reader.size(), 1);
    
    auto data = reader.get_record(0);
    std::string json(data.begin(), data.end());
    
    // Try to parse as JSON to ensure it's valid
    try {
        simdjson::dom::parser parser;
        ClockPositionRecord parsed = ClockPositionRecord::fromJson(json, parser);
        // If we get here, parsing succeeded
        EXPECT_EQ(parsed.fen, record.fen);
    } catch (const std::exception& e) {
        FAIL() << "Failed to parse JSON: " << e.what();
    }
}

// Test concurrent writing (should fail or be protected)
TEST_F(BagzRecordWriterTest, NoConcurrentWrites) {
    std::string output_path = (temp_dir_ / "concurrent.bagz").string();
    BagzRecordWriter writer(output_path);
    
    // Create records for concurrent writing
    std::vector<ClockPositionRecord> records;
    for (int i = 0; i < 100; ++i) {
        ClockPositionRecord record;
        record.fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
        record.rating = 1500 + i;
        record.player_clock = 180.0f;
        record.opponent_clock = 180.0f;
        record.increment = 0.0f;
        record.thinking_time = 1.0f;
        records.push_back(record);
    }
    
    // Note: BagzRecordWriter should either:
    // 1. Be thread-safe (with internal locking)
    // 2. Document that it's not thread-safe
    // For now, we'll just write sequentially
    for (const auto& record : records) {
        writer.writeRecord(record);
    }
    
    writer.close();
    
    BagFileReader reader(output_path);
    EXPECT_EQ(reader.size(), 100);
}

// Test error handling
TEST_F(BagzRecordWriterTest, HandleWriteErrors) {
    // Try to write to an invalid path
    std::string invalid_path = "/invalid/path/that/does/not/exist/test.bagz";
    
    EXPECT_THROW({
        BagzRecordWriter writer(invalid_path);
    }, std::runtime_error);
}

} // namespace chessmimic