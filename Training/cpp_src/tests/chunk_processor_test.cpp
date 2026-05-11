#include <gtest/gtest.h>
#include "../clock_converter/chunk_processor.hpp"
#include "../clock_converter/clock_position_record.hpp"
#include "../clock_converter/clock_game_parser.hpp"
#include <filesystem>
#include <fstream>
#include <vector>

namespace chessmimic::clock_converter {

class ChunkProcessorTest : public testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path() / 
                    ("chunk_processor_test_" + std::to_string(getpid()));
        std::filesystem::create_directories(temp_dir_);
    }
    
    void TearDown() override {
        std::filesystem::remove_all(temp_dir_);
    }
    
    // Helper to create a test PGN file
    void createPgnFile(const std::string& filename, const std::string& content) const {
        std::ofstream file(temp_dir_ / filename);
        file << content;
        file.close();
    }
    
    std::filesystem::path temp_dir_;
};

// Test basic chunk processing
TEST_F(ChunkProcessorTest, ProcessSingleChunk) {
    // Create test PGN file
    std::string pgn_content = R"([Event "Test"]
[TimeControl "180+2"]
[WhiteElo "1750"]
[BlackElo "1723"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]}
2. Nf3 {[%clk 0:02:55]} Nc6 {[%clk 0:02:54]}
*)";
    
    createPgnFile("test.pgn", pgn_content);
    
    // Process file
    ChunkProcessor processor(1024 * 1024); // 1MB chunk size
    
    std::vector pgn_files = {(temp_dir_ / "test.pgn").string()};
    auto chunk_files = processor.processFiles(pgn_files, temp_dir_.string());
    
    ASSERT_EQ(chunk_files.size(), 1);
    
    // Verify chunk is sorted
    std::ifstream chunk_file(chunk_files[0]);
    std::vector<std::pair<std::string, ClockPositionRecord>> records;
    
    std::string line;
    simdjson::dom::parser parser; // Reusable parser for performance
    while (std::getline(chunk_file, line)) {
        if (!line.empty()) {
            size_t tab_pos = line.find('\t');
            ASSERT_NE(tab_pos, std::string::npos);
            
            std::string key = line.substr(0, tab_pos);
            std::string json = line.substr(tab_pos + 1);
            
            try {
                ClockPositionRecord record = ClockPositionRecord::fromJson(json, parser);
                records.emplace_back(key, record);
            } catch (const std::exception& e) {
                FAIL() << "Failed to parse JSON: " << e.what();
            }
        }
    }
    
    EXPECT_EQ(records.size(), 4); // 4 moves in the game
    
    // Verify records are sorted by key
    for (size_t i = 1; i < records.size(); ++i) {
        EXPECT_LE(records[i-1].first, records[i].first);
    }
}

// Test chunk size limits
TEST_F(ChunkProcessorTest, RespectChunkSizeLimit) {
    // Create large PGN content
    std::stringstream pgn;
    
    // Generate many games
    for (int game = 0; game < 100; ++game) {
        pgn << "[Event \"Game " << game << "\"]\n";
        pgn << "[TimeControl \"180+2\"]\n";
        pgn << "[WhiteElo \"" << (1500 + game) << "\"]\n";
        pgn << "[BlackElo \"" << (1500 + game) << "\"]\n\n";
        
        // Add some moves
        for (int move = 1; move <= 10; ++move) {
            pgn << move << ". e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} ";
        }
        pgn << "*\n\n";
    }
    
    createPgnFile("large.pgn", pgn.str());
    
    // Process with small chunk size
    ChunkProcessor processor(10 * 1024); // 10KB chunk size
    
    std::vector pgn_files = {(temp_dir_ / "large.pgn").string()};
    auto chunk_files = processor.processFiles(pgn_files, temp_dir_.string());
    
    // Should create multiple chunks
    EXPECT_GT(chunk_files.size(), 1);
    
    // Verify each chunk is sorted
    for (const auto& chunk_path : chunk_files) {
        std::ifstream chunk_file(chunk_path);
        std::string prev_key;
        std::string line;
        
        while (std::getline(chunk_file, line)) {
            if (!line.empty()) {
                size_t tab_pos = line.find('\t');
                std::string key = line.substr(0, tab_pos);
                
                if (!prev_key.empty()) {
                    EXPECT_LE(prev_key, key) << "Chunk not sorted";
                }
                prev_key = key;
            }
        }
    }
}

// Test parallel processing
TEST_F(ChunkProcessorTest, ParallelProcessing) {
    // Create multiple PGN files
    for (int i = 0; i < 4; ++i) {
        std::stringstream pgn;
        pgn << "[Event \"Game " << i << "\"]\n";
        pgn << "[TimeControl \"300+0\"]\n";
        pgn << "[WhiteElo \"1600\"]\n";
        pgn << "[BlackElo \"1650\"]\n\n";
        pgn << "1. d4 {[%clk 0:04:58]} d5 {[%clk 0:04:57]} ";
        pgn << "2. c4 {[%clk 0:04:55]} e6 {[%clk 0:04:54]} *\n";
        
        createPgnFile("game" + std::to_string(i) + ".pgn", pgn.str());
    }
    
    // Process files in parallel
    ChunkProcessor processor(1024 * 1024, 4); // 1MB chunks, 4 threads
    
    std::vector<std::string> pgn_files;
    for (int i = 0; i < 4; ++i) {
        pgn_files.push_back((temp_dir_ / ("game" + std::to_string(i) + ".pgn")).string());
    }
    
    auto chunk_files = processor.processFiles(pgn_files, temp_dir_.string());
    
    // Verify all games were processed
    size_t total_records = 0;
    for (const auto& chunk_path : chunk_files) {
        std::ifstream chunk_file(chunk_path);
        std::string line;
        while (std::getline(chunk_file, line)) {
            if (!line.empty()) {
                total_records++;
            }
        }
    }
    
    EXPECT_EQ(total_records, 16); // 4 games * 4 positions each
}

// Test memory tracking
TEST_F(ChunkProcessorTest, MemoryTracking) {
    // Create PGN file
    std::string pgn_content = R"([Event "Test"]
[TimeControl "60+1"]
[WhiteElo "1800"]
[BlackElo "1850"]

1. e4 {[%clk 0:00:58]} c5 {[%clk 0:00:57]}
2. Nf3 {[%clk 0:00:56]} d6 {[%clk 0:00:55]}
3. d4 {[%clk 0:00:54]} cxd4 {[%clk 0:00:53]}
*)";
    
    createPgnFile("memory_test.pgn", pgn_content);
    
    // Process with tracking
    ChunkProcessor processor(50 * 1024); // 50KB chunk
    
    std::vector pgn_files = {(temp_dir_ / "memory_test.pgn").string()};
    
    auto stats = processor.getStatistics();
    EXPECT_EQ(stats.total_records_processed, 0);
    EXPECT_EQ(stats.total_chunks_created, 0);
    
    auto chunk_files = processor.processFiles(pgn_files, temp_dir_.string());
    
    stats = processor.getStatistics();
    EXPECT_GT(stats.total_records_processed, 0);
    EXPECT_GT(stats.total_chunks_created, 0);
    EXPECT_GT(stats.peak_memory_usage, 0);
}

// Test error handling
TEST_F(ChunkProcessorTest, HandleInvalidFiles) {
    ChunkProcessor processor(1024 * 1024);
    
    // Non-existent file
    std::vector<std::string> pgn_files = {"/non/existent/file.pgn"};
    
    EXPECT_THROW({
        processor.processFiles(pgn_files, temp_dir_.string());
    }, std::runtime_error);
}

// Test chunk file naming
TEST_F(ChunkProcessorTest, ChunkFileNaming) {
    std::string pgn_content = R"([Event "Test"]
[TimeControl "180+2"]
[WhiteElo "1750"]
[BlackElo "1723"]

1. e4 {[%clk 0:02:58]} *
)";
    
    createPgnFile("test.pgn", pgn_content);
    
    ChunkProcessor processor(1024);
    
    std::vector pgn_files = {(temp_dir_ / "test.pgn").string()};
    auto chunk_files = processor.processFiles(pgn_files, temp_dir_.string());
    
    ASSERT_FALSE(chunk_files.empty());
    
    // Check chunk file naming pattern
    std::filesystem::path chunk_path(chunk_files[0]);
    std::string filename = chunk_path.filename().string();
    
    // Should contain thread id and chunk number
    EXPECT_TRUE(filename.find("chunk_") != std::string::npos);
    EXPECT_TRUE(filename.find(".sorted") != std::string::npos);
}

// Test position key generation
TEST_F(ChunkProcessorTest, CorrectPositionKeys) {
    std::string pgn_content = R"([Event "Test"]
[TimeControl "180+2"]
[WhiteElo "1750"]
[BlackElo "1723"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]}
2. Nf3 {[%clk 0:02:55]} *
)";
    
    createPgnFile("keys.pgn", pgn_content);
    
    ChunkProcessor processor(1024 * 1024);
    
    std::vector pgn_files = {(temp_dir_ / "keys.pgn").string()};
    auto chunk_files = processor.processFiles(pgn_files, temp_dir_.string());
    
    ASSERT_EQ(chunk_files.size(), 1);
    
    // Read and verify keys
    std::ifstream chunk_file(chunk_files[0]);
    std::vector<std::string> keys;
    std::string line;
    
    while (std::getline(chunk_file, line)) {
        if (!line.empty()) {
            size_t tab_pos = line.find('\t');
            keys.push_back(line.substr(0, tab_pos));
        }
    }
    
    ASSERT_EQ(keys.size(), 3);
    
    // Check that we have the expected patterns somewhere in the keys
    // With new behavior: moves include the current move
    bool has_e2e4_only = false;
    bool has_e2e4_e7e5_only = false;
    bool has_e2e4_e7e5_g1f3 = false;
    
    for (const auto& key : keys) {
        // Check for pattern with just e2e4
        if (key.find("|e2e4") != std::string::npos && 
            key.find("|e2e4,") == std::string::npos) {
            has_e2e4_only = true;
        }
        // Check for pattern with e2e4,e7e5 but not g1f3
        if (key.find("|e2e4,e7e5") != std::string::npos &&
            key.find(",g1f3") == std::string::npos) {
            has_e2e4_e7e5_only = true;
        }
        // Check for pattern with e2e4,e7e5,g1f3
        if (key.find("|e2e4,e7e5,g1f3") != std::string::npos) {
            has_e2e4_e7e5_g1f3 = true;
        }
    }
    
    EXPECT_TRUE(has_e2e4_only) << "Should have a position with just e2e4 (White's first move)";
    EXPECT_TRUE(has_e2e4_e7e5_only) << "Should have a position with e2e4,e7e5 (Black's first move)";
    EXPECT_TRUE(has_e2e4_e7e5_g1f3) << "Should have a position with e2e4,e7e5,g1f3 (White's second move)";
}

} // namespace chessmimic::clock_converter