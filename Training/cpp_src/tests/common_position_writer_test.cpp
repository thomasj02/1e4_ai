#include <gtest/gtest.h>
#include "../clock_converter/common_position_writer.hpp"
#include "../clock_converter/common_position_loader.hpp"
#include "../clock_converter/clock_position_record.hpp"
#include "../clock_converter/gzip_utils.hpp"
#include <filesystem>
#include <fstream>
#include <simdjson.h>

namespace chessmimic::clock_converter {

class CommonPositionWriterTest : public testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path() / 
                    ("common_position_writer_test_" + std::to_string(getpid()));
        std::filesystem::create_directories(temp_dir_);
    }
    
    void TearDown() override {
        std::filesystem::remove_all(temp_dir_);
    }
    
    // Helper to read JSONL file (supports both .jsonl and .jsonl.gz)
    static std::vector<std::string> readJsonlFile(const std::string& path) {
        std::vector<std::string> records;
        
        if (path.ends_with(".gz")) {
            // Read gzip file
            GzipReader reader(path);
            std::string line;
            while (reader.getline(line)) {
                if (!line.empty()) {
                    records.push_back(line);
                }
            }
        } else {
            // Read regular file
            std::ifstream file(path);
            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty()) {
                    records.push_back(line);
                }
            }
        }
        
        return records;
    }
    
    std::filesystem::path temp_dir_;
};

// Test writing positions with move history
TEST_F(CommonPositionWriterTest, WriteWithHistory) {
    std::string output_path = (temp_dir_ / "with_history.jsonl").string();
    CommonPositionWriter writer(output_path, false); // with_history = false (full key)
    
    // Create position group
    std::string position_key = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1|e2e4";
    std::vector<ClockPositionRecord> records;
    
    for (int i = 0; i < 5; ++i) {
        ClockPositionRecord record;
        record.fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1";
        record.recent_moves = {"e2e4"};
        record.rating = 1600 + i * 50;
        record.player_clock = 178.0f - i;
        record.opponent_clock = 180.0f;
        record.increment = 2.0f;
        record.thinking_time = 2.0f + i * 0.5f;
        records.push_back(record);
    }
    
    writer.writePositionGroup(position_key, records);
    writer.close();
    
    // Read and verify
    auto json_records = readJsonlFile(output_path);
    ASSERT_EQ(json_records.size(), 1);
    
    simdjson::dom::parser parser;
    simdjson::dom::element pos = parser.parse(json_records[0]);
    
    EXPECT_EQ(std::string(pos["position_key"]), position_key);
    EXPECT_EQ(static_cast<int64_t>(pos["count"]), 5);
    
    // Check thinking times array
    auto thinking_times = pos["thinking_times"].get_array();
    ASSERT_EQ(thinking_times.size(), 5);
    auto tt_iter = thinking_times.begin();
    EXPECT_FLOAT_EQ(static_cast<double>(*tt_iter), 2.0);
    std::advance(tt_iter, 4);
    EXPECT_FLOAT_EQ(static_cast<double>(*tt_iter), 4.0);
    
    // Check clock states
    auto clock_states = pos["clock_states"].get_array();
    ASSERT_EQ(clock_states.size(), 5);
    auto first_clock = *clock_states.begin();
    EXPECT_FLOAT_EQ(static_cast<double>(first_clock["player"]), 178.0);
    EXPECT_FLOAT_EQ(static_cast<double>(first_clock["opponent"]), 180.0);
    EXPECT_FLOAT_EQ(static_cast<double>(first_clock["increment"]), 2.0);
    
    // Check ratings
    auto ratings = pos["ratings"].get_array();
    ASSERT_EQ(ratings.size(), 5);
    auto r_iter = ratings.begin();
    EXPECT_EQ(static_cast<int64_t>(*r_iter), 1600);
    std::advance(r_iter, 4);
    EXPECT_EQ(static_cast<int64_t>(*r_iter), 1800);
}

// Test writing FEN-only positions
TEST_F(CommonPositionWriterTest, WriteFenOnly) {
    std::string output_path = (temp_dir_ / "fen_only.jsonl").string();
    CommonPositionWriter writer(output_path, true); // strip_move_history = true
    
    // Create positions with different move histories
    std::vector<ClockPositionRecord> records;
    
    // Same position via e4 e5
    for (int i = 0; i < 3; ++i) {
        ClockPositionRecord record;
        record.fen = "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2";
        record.recent_moves = {"e2e4", "e7e5"};
        record.rating = 1700 + i * 25;
        record.player_clock = 177.0f;
        record.thinking_time = 3.0f + i;
        records.push_back(record);
    }
    
    // Same position via different moves (transposition)
    for (int i = 0; i < 2; ++i) {
        ClockPositionRecord record;
        record.fen = "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2";
        record.recent_moves = {"e2e4", "e7e6", "e6e5"};
        record.rating = 1650 + i * 50;
        record.player_clock = 175.0f;
        record.thinking_time = 5.0f + i;
        records.push_back(record);
    }
    
    // Write with FEN as key (move history stripped)
    std::string fen_without_clocks = "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -";
    writer.writePositionGroup(fen_without_clocks, records);
    writer.close();
    
    // Read and verify
    auto json_records = readJsonlFile(output_path);
    ASSERT_EQ(json_records.size(), 1);
    
    simdjson::dom::parser parser;
    simdjson::dom::element pos = parser.parse(json_records[0]);
    
    EXPECT_EQ(std::string(pos["fen"]), fen_without_clocks);
    EXPECT_EQ(static_cast<int64_t>(pos["count"]), 5); // All records combined
    
    // Check that all thinking times are included
    auto thinking_times = pos["thinking_times"].get_array();
    ASSERT_EQ(thinking_times.size(), 5);
}

// Test multiple position groups
TEST_F(CommonPositionWriterTest, MultiplePositionGroups) {
    std::string output_path = (temp_dir_ / "multiple.jsonl").string();
    CommonPositionWriter writer(output_path, false);
    
    // First position group
    std::string key1 = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -|";
    std::vector<ClockPositionRecord> group1;
    for (int i = 0; i < 10; ++i) {
        ClockPositionRecord record;
        record.fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
        record.rating = 1500 + i * 10;
        record.player_clock = 180.0f;
        record.thinking_time = 0.0f;
        group1.push_back(record);
    }
    writer.writePositionGroup(key1, group1);
    
    // Second position group
    std::string key2 = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -|e2e4";
    std::vector<ClockPositionRecord> group2;
    for (int i = 0; i < 5; ++i) {
        ClockPositionRecord record;
        record.fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1";
        record.recent_moves = {"e2e4"};
        record.rating = 1600 + i * 20;
        record.player_clock = 178.0f;
        record.thinking_time = 2.0f;
        group2.push_back(record);
    }
    writer.writePositionGroup(key2, group2);
    
    writer.close();
    
    // Read and verify
    auto json_records = readJsonlFile(output_path);
    ASSERT_EQ(json_records.size(), 2);
    
    simdjson::dom::parser parser;
    // Check both groups
    simdjson::dom::element pos1 = parser.parse(json_records[0]);
    EXPECT_EQ(static_cast<int64_t>(pos1["count"]), 10);
    simdjson::dom::element pos2 = parser.parse(json_records[1]);
    EXPECT_EQ(static_cast<int64_t>(pos2["count"]), 5);
}

// Test append mode
TEST_F(CommonPositionWriterTest, AppendMode) {
    std::string output_path = (temp_dir_ / "append.jsonl").string();
    
    // Write first batch
    {
        CommonPositionWriter writer(output_path, false);
        
        std::string key = "test_position|move1";
        std::vector<ClockPositionRecord> records;
        ClockPositionRecord record;
        record.fen = "test_position";
        record.recent_moves = {"move1"};
        record.rating = 1700;
        record.thinking_time = 3.0f;
        records.push_back(record);
        
        writer.writePositionGroup(key, records);
        writer.close();
    }
    
    // Append second batch
    {
        CommonPositionWriter writer(output_path, false, true); // append = true
        
        std::string key = "another_position|move2";
        std::vector<ClockPositionRecord> records;
        ClockPositionRecord record;
        record.fen = "another_position";
        record.recent_moves = {"move2"};
        record.rating = 1800;
        record.thinking_time = 2.5f;
        records.push_back(record);
        
        writer.writePositionGroup(key, records);
        writer.close();
    }
    
    // Read and verify both records exist
    auto json_records = readJsonlFile(output_path);
    ASSERT_EQ(json_records.size(), 2);
    
    simdjson::dom::parser parser;
    simdjson::dom::element pos1 = parser.parse(json_records[0]);
    EXPECT_EQ(std::string(pos1["position_key"]), "test_position|move1");
    simdjson::dom::element pos2 = parser.parse(json_records[1]);
    EXPECT_EQ(std::string(pos2["position_key"]), "another_position|move2");
}

// Test statistics tracking
TEST_F(CommonPositionWriterTest, StatisticsTracking) {
    std::string output_path = (temp_dir_ / "stats.jsonl").string();
    CommonPositionWriter writer(output_path, false);
    
    // Write several groups
    for (int group = 0; group < 3; ++group) {
        std::string key = "position" + std::to_string(group) + "|";
        std::vector<ClockPositionRecord> records;
        
        for (int i = 0; i < 5 + group * 2; ++i) {
            ClockPositionRecord record;
            record.fen = "position" + std::to_string(group);
            record.rating = 1600 + i * 10;
            record.thinking_time = 1.0f + i;
            records.push_back(record);
        }
        
        writer.writePositionGroup(key, records);
    }
    
    auto [total_position_groups, total_records, total_bytes_written] = writer.getStatistics();
    EXPECT_EQ(total_position_groups, 3);
    EXPECT_EQ(total_records, 5 + 7 + 9); // 5, 7, 9 records per group
    EXPECT_GT(total_bytes_written, 0);
    
    writer.close();
}

// Test error handling
TEST_F(CommonPositionWriterTest, ErrorHandling) {
    // Invalid path
    std::string invalid_path = "/invalid/path/output.jsonl";
    
    EXPECT_THROW({
        CommonPositionWriter writer(invalid_path, false);
    }, std::runtime_error);
}

// Test empty group handling
TEST_F(CommonPositionWriterTest, EmptyGroupHandling) {
    std::string output_path = (temp_dir_ / "empty_group.jsonl").string();
    CommonPositionWriter writer(output_path, false);
    
    // Try to write empty group
    std::string key = "empty_position|";
    std::vector<ClockPositionRecord> empty_records;
    
    // Should handle gracefully (either skip or write with count=0)
    writer.writePositionGroup(key, empty_records);
    writer.close();

    // Either no records or record with count=0
    if (auto json_records = readJsonlFile(output_path); !json_records.empty()) {
        simdjson::dom::parser parser;
        simdjson::dom::element pos = parser.parse(json_records[0]);
        EXPECT_EQ(static_cast<int64_t>(pos["count"]), 0);
    }
}

// Test large thinking time arrays
TEST_F(CommonPositionWriterTest, LargeThinkingTimeArrays) {
    std::string output_path = (temp_dir_ / "large_arrays.jsonl").string();
    CommonPositionWriter writer(output_path, false);
    
    std::string key = "popular_position|e2e4";
    std::vector<ClockPositionRecord> records;
    
    // Create many records
    for (int i = 0; i < 1000; ++i) {
        ClockPositionRecord record;
        record.fen = "popular_position";
        record.recent_moves = {"e2e4"};
        record.rating = 1400 + (i % 400);
        record.player_clock = 180.0f - (i % 180);
        record.thinking_time = (i % 30) / 10.0f;
        records.push_back(record);
    }
    
    writer.writePositionGroup(key, records);
    writer.close();
    
    // Verify large arrays are handled
    auto json_records = readJsonlFile(output_path);
    ASSERT_EQ(json_records.size(), 1);
    
    simdjson::dom::parser parser;
    simdjson::dom::element pos = parser.parse(json_records[0]);
    EXPECT_EQ(int64_t(pos["count"]), 1000);
    
    auto thinking_times = pos["thinking_times"].get_array();
    EXPECT_EQ(thinking_times.size(), 1000);
    auto clock_states = pos["clock_states"].get_array();
    EXPECT_EQ(clock_states.size(), 1000);
    auto ratings = pos["ratings"].get_array();
    EXPECT_EQ(ratings.size(), 1000);
}

// Test gzip compression
TEST_F(CommonPositionWriterTest, GzipCompression) {
    std::string output_path = (temp_dir_ / "compressed.jsonl.gz").string();
    CommonPositionWriter writer(output_path, false);
    
    // Create position group
    std::string position_key = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1|e2e4";
    std::vector<ClockPositionRecord> records;
    
    for (int i = 0; i < 10; ++i) {
        ClockPositionRecord record;
        record.fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1";
        record.recent_moves = {"e2e4"};
        record.rating = 1600 + i * 50;
        record.player_clock = 178.0f - i;
        record.opponent_clock = 180.0f;
        record.increment = 2.0f;
        record.thinking_time = 2.0f + i * 0.5f;
        records.push_back(record);
    }
    
    writer.writePositionGroup(position_key, records);
    writer.close();
    
    // Verify file exists and is a valid gzip file
    ASSERT_TRUE(std::filesystem::exists(output_path));
    
    // Read and verify using gzip reader
    auto json_records = readJsonlFile(output_path);
    ASSERT_EQ(json_records.size(), 1);
    
    simdjson::dom::parser parser;
    simdjson::dom::element pos = parser.parse(json_records[0]);
    EXPECT_EQ(std::string(pos["position_key"]), position_key);
    EXPECT_EQ(static_cast<int64_t>(pos["count"]), 10);
    
    auto thinking_times = pos["thinking_times"].get_array();
    EXPECT_EQ(thinking_times.size(), 10);
    
    // Check that gzip file is smaller than uncompressed would be
    auto compressed_size = std::filesystem::file_size(output_path);
    EXPECT_GT(compressed_size, 0);
    
    // Rough estimate: JSON data should compress significantly
    // (this is a loose check since compression ratio varies)
    std::string uncompressed_path = (temp_dir_ / "uncompressed.jsonl").string();
    CommonPositionWriter uncompressed_writer(uncompressed_path, false);
    uncompressed_writer.writePositionGroup(position_key, records);
    uncompressed_writer.close();
    
    auto uncompressed_size = std::filesystem::file_size(uncompressed_path);
    EXPECT_LT(compressed_size, uncompressed_size); // Gzip should be smaller
}

// Test gzip with FEN-only format
TEST_F(CommonPositionWriterTest, GzipFenOnly) {
    std::string output_path = (temp_dir_ / "fen_only_compressed.jsonl.gz").string();
    CommonPositionWriter writer(output_path, true); // strip_move_history = true
    
    std::vector<ClockPositionRecord> records;
    for (int i = 0; i < 5; ++i) {
        ClockPositionRecord record;
        record.fen = "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2";
        record.recent_moves = {"e2e4", "e7e5"};
        record.rating = 1700 + i * 25;
        record.thinking_time = 3.0f + i;
        records.push_back(record);
    }
    
    std::string fen_without_clocks = "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -";
    writer.writePositionGroup(fen_without_clocks, records);
    writer.close();
    
    // Read and verify
    auto json_records = readJsonlFile(output_path);
    ASSERT_EQ(json_records.size(), 1);
    
    simdjson::dom::parser parser;
    simdjson::dom::element pos = parser.parse(json_records[0]);
    EXPECT_EQ(std::string(pos["fen"]), fen_without_clocks);
    EXPECT_EQ(static_cast<int64_t>(pos["count"]), 5);
}

// Test that append mode throws error for gzip files
TEST_F(CommonPositionWriterTest, GzipAppendError) {
    std::string output_path = (temp_dir_ / "append_error.jsonl.gz").string();
    
    EXPECT_THROW({
        CommonPositionWriter writer(output_path, false, true); // append = true
    }, std::runtime_error);
}

// Test round-trip: write gzip then read with CommonPositionLoader
TEST_F(CommonPositionWriterTest, GzipRoundTrip) {
    std::string output_path = (temp_dir_ / "roundtrip.jsonl.gz").string();
    
    // Write some FEN-only positions
    {
        CommonPositionWriter writer(output_path, true);
        
        // Add multiple FENs
        std::vector<std::string> fens = {
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",
            "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -",
            "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -"
        };
        
        for (const auto& fen : fens) {
            std::vector<ClockPositionRecord> records;
            ClockPositionRecord record;
            record.fen = fen;
            record.rating = 1700;
            record.thinking_time = 3.0f;
            records.push_back(record);
            
            writer.writePositionGroup(fen, records);
        }
        
        writer.close();
    }
    
    // Read with CommonPositionLoader
    CommonPositionLoader loader(output_path);
    EXPECT_EQ(loader.size(), 3);
    
    // Test position checks
    EXPECT_TRUE(loader.isCommon("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -|"));
    EXPECT_TRUE(loader.isCommon("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -|e2e4"));
    EXPECT_TRUE(loader.isCommon("rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -|e2e4,e7e5"));
    
    // Test non-existent position
    EXPECT_FALSE(loader.isCommon("8/8/8/8/8/8/8/8 w - -|"));
}

} // namespace chessmimic::clock_converter