#include <gtest/gtest.h>
#include "../clock_converter/sorted_merge_writer.hpp"
#include "../clock_converter/clock_position_record.hpp"
#include "../core/bagz_record_writer.hpp"
#include "../pgn_converter/record_processor.hpp"
#include <filesystem>
#include <fstream>
#include <vector>

namespace chessmimic::clock_converter {

class SortedMergeWriterTest : public testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path() / 
                    ("sorted_merge_test_" + std::to_string(getpid()));
        std::filesystem::create_directories(temp_dir_);
    }
    
    void TearDown() override {
        std::filesystem::remove_all(temp_dir_);
    }
    
    // Helper to create a sorted chunk file
    void createSortedChunk(const std::string& filename, 
                          const std::vector<std::pair<std::string, ClockPositionRecord>>& records) const {
        std::ofstream file(temp_dir_ / filename);
        
        for (const auto& [key, record] : records) {
            file << key << "\t" << record.toJson() << "\n";
        }
        
        file.close();
    }
    
    // Helper to count records in BAGZ file
    static size_t countBagzRecords(const std::string& path) {
        if (!std::filesystem::exists(path)) {
            return 0;
        }
        BagFileReader reader(path);
        return reader.size();
    }
    
    // Helper to count lines in JSONL file
    static size_t countJsonlRecords(const std::string& path) {
        if (!std::filesystem::exists(path)) {
            return 0;
        }
        std::ifstream file(path);
        size_t count = 0;
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty()) {
                count++;
            }
        }
        return count;
    }
    
    std::filesystem::path temp_dir_;
};

// Test basic K-way merge
TEST_F(SortedMergeWriterTest, BasicKWayMerge) {
    // Create sorted chunks
    std::vector<std::pair<std::string, ClockPositionRecord>> chunk1;
    std::vector<std::pair<std::string, ClockPositionRecord>> chunk2;
    
    // Chunk 1
    ClockPositionRecord r1;
    r1.fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
    r1.rating = 1750;
    r1.player_clock = 180.0f;
    chunk1.emplace_back(r1.fen + "|", r1);
    
    ClockPositionRecord r2;
    r2.fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -";
    r2.recent_moves = {"e2e4"};
    r2.rating = 1750;
    r2.player_clock = 178.0f;
    chunk1.emplace_back(r2.getPositionKey(), r2);
    
    // Chunk 2
    ClockPositionRecord r3;
    r3.fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
    r3.rating = 1800;
    r3.player_clock = 180.0f;
    chunk2.emplace_back(r3.fen + "|", r3);
    
    ClockPositionRecord r4;
    r4.fen = "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -";
    r4.recent_moves = {"e2e4", "e7e5"};
    r4.rating = 1800;
    r4.player_clock = 177.0f;
    chunk2.emplace_back(r4.getPositionKey(), r4);
    
    createSortedChunk("chunk1.sorted", chunk1);
    createSortedChunk("chunk2.sorted", chunk2);
    
    // Configure merge
    SortedMergeWriter::Config config;
    config.max_positions_per_key = 10; // High threshold so nothing is common
    config.output_bagz_path = (temp_dir_ / "output.bagz").string();
    config.common_positions_with_history_path = (temp_dir_ / "common_with_history.jsonl").string();
    config.common_positions_fen_only_path = (temp_dir_ / "common_fen_only.jsonl").string();
    config.write_common_positions = true;
    
    SortedMergeWriter merger(config);
    
    std::vector chunk_files = {
        (temp_dir_ / "chunk1.sorted").string(),
        (temp_dir_ / "chunk2.sorted").string()
    };
    
    merger.mergeChunks(chunk_files);
    
    // All records should go to BAGZ (threshold not exceeded)
    EXPECT_EQ(countBagzRecords(config.output_bagz_path), 4);
    EXPECT_EQ(countJsonlRecords(config.common_positions_with_history_path), 0);
    EXPECT_EQ(countJsonlRecords(config.common_positions_fen_only_path), 0);
}

// Test common position detection
TEST_F(SortedMergeWriterTest, CommonPositionDetection) {
    // Create chunks with repeated positions
    std::vector<std::pair<std::string, ClockPositionRecord>> chunk1;
    std::vector<std::pair<std::string, ClockPositionRecord>> chunk2;
    
    // Starting position appears many times
    std::string starting_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
    std::string starting_key = starting_fen + "|";
    
    // Add 15 instances of starting position across chunks
    for (int i = 0; i < 8; ++i) {
        ClockPositionRecord r;
        r.fen = starting_fen;
        r.rating = 1500 + i * 50;
        r.player_clock = 180.0f;
        r.thinking_time = i * 0.5f;
        chunk1.emplace_back(starting_key, r);
    }
    
    for (int i = 0; i < 7; ++i) {
        ClockPositionRecord r;
        r.fen = starting_fen;
        r.rating = 1600 + i * 50;
        r.player_clock = 180.0f;
        r.thinking_time = i * 0.3f;
        chunk2.emplace_back(starting_key, r);
    }
    
    createSortedChunk("chunk1.sorted", chunk1);
    createSortedChunk("chunk2.sorted", chunk2);
    
    // Configure with low threshold
    SortedMergeWriter::Config config;
    config.max_positions_per_key = 10; // Starting position exceeds this
    config.output_bagz_path = (temp_dir_ / "output.bagz").string();
    config.common_positions_with_history_path = (temp_dir_ / "common_with_history.jsonl").string();
    config.common_positions_fen_only_path = (temp_dir_ / "common_fen_only.jsonl").string();
    config.write_common_positions = true;
    
    SortedMergeWriter merger(config);
    
    std::vector chunk_files = {
        (temp_dir_ / "chunk1.sorted").string(),
        (temp_dir_ / "chunk2.sorted").string()
    };
    
    merger.mergeChunks(chunk_files);
    
    // Starting position should be in common files, not BAGZ
    EXPECT_EQ(countBagzRecords(config.output_bagz_path), 0);
    EXPECT_EQ(countJsonlRecords(config.common_positions_with_history_path), 1);
    EXPECT_EQ(countJsonlRecords(config.common_positions_fen_only_path), 1);
}

// Test FEN-level grouping
TEST_F(SortedMergeWriterTest, FenLevelGrouping) {
    std::vector<std::pair<std::string, ClockPositionRecord>> chunk;
    
    // Same position reached by different move sequences
    std::string fen = "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -";
    
    // Via e4 e5
    for (int i = 0; i < 6; ++i) {
        ClockPositionRecord r;
        r.fen = fen;
        r.recent_moves = {"e2e4", "e7e5"};
        r.rating = 1700 + i * 10;
        r.player_clock = 177.0f;
        chunk.emplace_back(r.getPositionKey(), r);
    }
    
    // Via e4 e6, e5 (transposition)
    for (int i = 0; i < 6; ++i) {
        ClockPositionRecord r;
        r.fen = fen;
        r.recent_moves = {"e2e4", "e7e6", "e6e5"};
        r.rating = 1750 + i * 10;
        r.player_clock = 175.0f;
        chunk.emplace_back(r.getPositionKey(), r);
    }
    
    createSortedChunk("chunk.sorted", chunk);
    
    // Configure with threshold between group sizes
    SortedMergeWriter::Config config;
    config.max_positions_per_key = 10; // FEN total (12) exceeds, but each path (6) doesn't
    config.output_bagz_path = (temp_dir_ / "output.bagz").string();
    config.common_positions_with_history_path = (temp_dir_ / "common_with_history.jsonl").string();
    config.common_positions_fen_only_path = (temp_dir_ / "common_fen_only.jsonl").string();
    config.write_common_positions = true;
    
    SortedMergeWriter merger(config);
    
    std::vector chunk_files = {
        (temp_dir_ / "chunk.sorted").string()
    };
    
    merger.mergeChunks(chunk_files);
    
    // Both paths should be in common (FEN-level decision)
    EXPECT_EQ(countBagzRecords(config.output_bagz_path), 0);
    EXPECT_EQ(countJsonlRecords(config.common_positions_with_history_path), 2); // Two different paths
    EXPECT_EQ(countJsonlRecords(config.common_positions_fen_only_path), 1); // One FEN
}

// Test skip common positions mode
TEST_F(SortedMergeWriterTest, SkipCommonPositions) {
    // Create common positions file with FEN-only format
    std::ofstream common_file(temp_dir_ / "existing_common.jsonl");
    common_file << R"({"fen":"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -","count":100})" << "\n";
    common_file.close();
    
    // Create chunk with both common and uncommon positions
    std::vector<std::pair<std::string, ClockPositionRecord>> chunk;
    
    // Common position (should be skipped)
    ClockPositionRecord r1;
    r1.fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
    r1.rating = 1750;
    r1.player_clock = 180.0f;
    chunk.emplace_back(r1.fen + "|", r1);
    
    // Uncommon position (should be kept)
    ClockPositionRecord r2;
    r2.fen = "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -";
    r2.recent_moves = {"e2e4", "e7e5"};
    r2.rating = 1750;
    r2.player_clock = 177.0f;
    chunk.emplace_back(r2.getPositionKey(), r2);
    
    createSortedChunk("chunk.sorted", chunk);
    
    // Configure to skip common positions
    SortedMergeWriter::Config config;
    config.max_positions_per_key = 100;
    config.output_bagz_path = (temp_dir_ / "output.bagz").string();
    config.common_positions_fen_only_path = (temp_dir_ / "existing_common.jsonl").string();
    config.skip_common_positions = true;
    
    SortedMergeWriter merger(config);
    
    std::vector chunk_files = {
        (temp_dir_ / "chunk.sorted").string()
    };
    
    merger.mergeChunks(chunk_files);
    
    // Only uncommon position should be written
    EXPECT_EQ(countBagzRecords(config.output_bagz_path), 1);
}

// Test statistics tracking
TEST_F(SortedMergeWriterTest, StatisticsTracking) {
    // Create chunks with various positions
    std::vector<std::pair<std::string, ClockPositionRecord>> chunk;
    
    // Add some records
    for (int i = 0; i < 20; ++i) {
        ClockPositionRecord r;
        r.fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
        r.rating = 1500 + i * 10;
        r.player_clock = 180.0f - i;
        chunk.emplace_back(r.fen + "|", r);
    }
    
    createSortedChunk("chunk.sorted", chunk);
    
    // Configure
    SortedMergeWriter::Config config;
    config.max_positions_per_key = 15;
    config.output_bagz_path = (temp_dir_ / "output.bagz").string();
    config.common_positions_with_history_path = (temp_dir_ / "common_with_history.jsonl").string();
    config.common_positions_fen_only_path = (temp_dir_ / "common_fen_only.jsonl").string();
    config.write_common_positions = true;
    
    SortedMergeWriter merger(config);
    
    std::vector chunk_files = {
        (temp_dir_ / "chunk.sorted").string()
    };
    
    merger.mergeChunks(chunk_files);
    
    auto stats = merger.getStatistics();
    EXPECT_EQ(stats.total_records_processed, 20);
    EXPECT_EQ(stats.unique_positions, 1);
    EXPECT_EQ(stats.common_positions_found, 1);
    EXPECT_EQ(stats.records_written_to_bagz, 0);
    EXPECT_EQ(stats.records_written_to_common, 20);
}

// Test empty chunks
TEST_F(SortedMergeWriterTest, HandleEmptyChunks) {
    // Create empty chunk
    std::ofstream file(temp_dir_ / "empty.sorted");
    file.close();
    
    // Configure
    SortedMergeWriter::Config config;
    config.max_positions_per_key = 100;
    config.output_bagz_path = (temp_dir_ / "output.bagz").string();
    
    SortedMergeWriter merger(config);
    
    std::vector chunk_files = {
        (temp_dir_ / "empty.sorted").string()
    };
    
    merger.mergeChunks(chunk_files);
    
    // Should handle gracefully
    EXPECT_EQ(countBagzRecords(config.output_bagz_path), 0);
}

// Test malformed chunk handling
TEST_F(SortedMergeWriterTest, HandleMalformedChunks) {
    // Create chunk with malformed data
    std::ofstream file(temp_dir_ / "malformed.sorted");
    file << "no_tab_here\n";
    file << "valid_key\t{\"invalid_json\": }\n";
    file.close();
    
    // Configure
    SortedMergeWriter::Config config;
    config.max_positions_per_key = 100;
    config.output_bagz_path = (temp_dir_ / "output.bagz").string();
    
    SortedMergeWriter merger(config);
    
    std::vector chunk_files = {
        (temp_dir_ / "malformed.sorted").string()
    };
    
    // Should throw or handle gracefully
    EXPECT_THROW({
        merger.mergeChunks(chunk_files);
    }, std::runtime_error);
}

// Test intermediate records format compatibility with RecordProcessor
TEST_F(SortedMergeWriterTest, IntermediateRecordsFormatCompatibility) {
    // Create sorted chunks
    std::vector<std::pair<std::string, ClockPositionRecord>> chunk;
    
    // Add some test records
    ClockPositionRecord r1;
    r1.fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
    r1.rating = 1750;
    r1.player_clock = 180.0f;
    r1.opponent_clock = 180.0f;
    r1.increment = 0.0f;
    r1.thinking_time = 2.5f;
    chunk.emplace_back(r1.fen + "|", r1);
    
    ClockPositionRecord r2;
    r2.fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -";
    r2.recent_moves = {"e2e4"};
    r2.rating = 1750;
    r2.player_clock = 178.0f;
    r2.opponent_clock = 179.0f;
    r2.increment = 0.0f;
    r2.thinking_time = 1.8f;
    chunk.emplace_back(r2.getPositionKey(), r2);
    
    createSortedChunk("chunk.sorted", chunk);
    
    // Configure to write intermediate records format (not BAGZ)
    SortedMergeWriter::Config config;
    config.max_positions_per_key = 100;
    config.output_bagz_path = "";  // Empty = don't write BAGZ
    config.output_records_path = (temp_dir_ / "intermediate.records").string();
    
    SortedMergeWriter merger(config);
    
    std::vector chunk_files = {
        (temp_dir_ / "chunk.sorted").string()
    };
    
    merger.mergeChunks(chunk_files);
    
    // Get statistics to debug
    auto stats = merger.getStatistics();
    std::cout << "DEBUG: Records processed: " << stats.total_records_processed << std::endl;
    std::cout << "DEBUG: Unique positions: " << stats.unique_positions << std::endl;
    std::cout << "DEBUG: Records written to BAGZ: " << stats.records_written_to_bagz << std::endl;
    
    // Check if the file was created
    ASSERT_TRUE(std::filesystem::exists(config.output_records_path)) 
        << "Intermediate records file was not created: " << config.output_records_path;
    
    // Check file size
    size_t file_size = std::filesystem::file_size(config.output_records_path);
    EXPECT_GT(file_size, 0) << "Intermediate records file is empty";
    
    // Now try to read the intermediate file with RecordProcessor::readRecord
    std::ifstream in(config.output_records_path, std::ios::binary);
    ASSERT_TRUE(in.is_open()) << "Failed to open intermediate records file";
    
    // Try to read first record
    std::string key1, record1;
    bool result1 = RecordProcessor::readRecord(in, key1, record1);
    EXPECT_TRUE(result1) << "Failed to read first record";
    EXPECT_EQ(r1.fen + "|", key1) << "First record key mismatch";
    
    // Verify the JSON contains expected data
    simdjson::dom::parser parser;
    simdjson::dom::element doc1 = parser.parse(record1);
    EXPECT_EQ(doc1["fen"].get_string().value(), r1.fen);
    EXPECT_EQ(doc1["rating"].get_int64().value(), r1.rating);
    EXPECT_NEAR(doc1["player_clock"].get_double().value(), r1.player_clock, 0.01);
    
    // Try to read second record
    std::string key2, record2;
    bool result2 = RecordProcessor::readRecord(in, key2, record2);
    EXPECT_TRUE(result2) << "Failed to read second record";
    EXPECT_EQ(r2.getPositionKey(), key2) << "Second record key mismatch";
    
    // Verify no more records
    std::string key3, record3;
    bool result3 = RecordProcessor::readRecord(in, key3, record3);
    EXPECT_FALSE(result3) << "Expected no more records";
}

} // namespace chessmimic::clock_converter