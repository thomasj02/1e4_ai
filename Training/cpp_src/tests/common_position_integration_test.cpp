#include <gtest/gtest.h>
#include "../common_position/common_position_extractor.hpp"
#include "../common_position/position_data.hpp"
#include "../core/bagz.hpp"
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <simdjson.h>

using namespace chessmimic;

class CommonPositionIntegrationTest : public testing::Test {
protected:
    void SetUp() override {
        // Create a temporary directory for test files
        test_dir_ = std::filesystem::temp_directory_path() / 
                    ("test_common_pos_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(test_dir_);
        
        bagz_path_ = test_dir_ / "test.bagz";
        output_path_ = test_dir_ / "output.jsonl";
    }
    
    void TearDown() override {
        // Clean up test directory
        std::filesystem::remove_all(test_dir_);
    }
    
    // Helper to create a test bagz file
    void createTestBagz(const std::vector<std::string>& records) const {
        BagWriter writer(bagz_path_.string());
        
        for (const auto& record_str : records) {
            writer.write(std::vector<uint8_t>(record_str.begin(), record_str.end()));
        }
        
        writer.close();
    }
    
    // Helper to create a test record
    static auto createRecord(const std::string& fen,
                             const std::vector<std::string>& recent_moves,
                             const std::string& move,
                             int count,
                             float clock_time = 60.0,
                             int rating = 1500) -> std::string {
        std::stringstream json;
        json << "{\"recent_and_fen\":[";
        
        // Add recent moves array
        json << "[";
        for (size_t i = 0; i < recent_moves.size(); ++i) {
            json << "\"" << recent_moves[i] << "\"";
            if (i < recent_moves.size() - 1) json << ",";
        }
        json << "],";
        
        // Add FEN
        json << "\"" << fen << "\"],";
        
        // Add moves object
        json << "\"moves\":{";
        json << "\"" << move << "\":{";
        json << "\"" << std::to_string(clock_time) << "\":{";
        json << "\"" << std::to_string(rating) << "\":" << count;
        json << "}}}";
        json << "}";
        
        return json.str();
    }
    
    // Helper to read output file
    std::vector<PositionData> readOutput() const {
        std::vector<PositionData> positions;
        std::ifstream in(output_path_);
        std::string line;
        
        while (std::getline(in, line)) {
            positions.push_back(PositionData::fromJson(line));
        }
        
        return positions;
    }

    std::filesystem::path test_dir_;
    std::filesystem::path bagz_path_;
    std::filesystem::path output_path_;
};

// Test basic extraction with a small dataset
TEST_F(CommonPositionIntegrationTest, BasicExtraction) {
    // Create test data with valid FEN strings
    // Using starting position and a variation
    const std::string startPos = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    const std::string afterE4 = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";
    
    std::vector records = {
        createRecord(startPos, {"e2e4"}, "Nf3", 10),
        createRecord(startPos, {"e2e4", "e7e5"}, "Nc3", 5),
        createRecord(afterE4, {"d2d4"}, "e5", 3),
        createRecord(startPos, {"d2d4"}, "Nf3", 2),
    };
    
    createTestBagz(records);
    
    // Run extraction
    CommonPositionExtractor::Config config;
    config.bagz_path = bagz_path_.string();
    config.output_path = output_path_.string();
    config.temp_dir = test_dir_ / "temp";
    config.threshold = 5;
    config.chunk_size = 2;
    config.num_threads = 2;
    
    CommonPositionExtractor extractor(config);
    extractor.extract();
    
    // Verify output
    auto positions = readOutput();
    ASSERT_EQ(positions.size(), 1); // Only startPos should exceed threshold
    
    // The extractor strips move clocks, so we should get the position without them
    EXPECT_EQ(positions[0].fen, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -");
    EXPECT_EQ(positions[0].total, 17); // 10 + 5 + 2
    EXPECT_EQ(positions[0].moves["Nf3"], 12); // 10 + 2
    EXPECT_EQ(positions[0].moves["Nc3"], 5);
}

// Test with many unique positions to verify memory efficiency
TEST_F(CommonPositionIntegrationTest, ManyUniquePositions) {
    std::vector<std::string> records;
    
    // Create 1000 unique positions (vary the move number to make them unique)
    for (int i = 0; i < 1000; ++i) {
        // Create unique positions by varying the fullmove number
        std::string fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 " + std::to_string(i + 1);
        records.push_back(createRecord(fen, {}, "e4", 1));
    }
    
    // Add a few common positions that exceed threshold
    const std::string common_fen1 = "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2";
    const std::string common_fen2 = "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq d3 0 1";
    
    for (int i = 0; i < 10; ++i) {
        records.push_back(createRecord(common_fen1, {"move" + std::to_string(i)}, "Nf3", 3));
        records.push_back(createRecord(common_fen2, {"move" + std::to_string(i)}, "d4", 2));
    }
    
    createTestBagz(records);
    
    // Run extraction with small chunks to test merging
    CommonPositionExtractor::Config config;
    config.bagz_path = bagz_path_.string();
    config.output_path = output_path_.string();
    config.temp_dir = test_dir_ / "temp";
    config.threshold = 10;
    config.chunk_size = 100;
    config.num_threads = 4;
    
    CommonPositionExtractor extractor(config);
    extractor.extract();
    
    // Verify output
    auto positions = readOutput();
    // We expect 3 positions: 2 common ones and the starting position (all positions with fullmove 1-1000 get stripped to same FEN)
    ASSERT_EQ(positions.size(), 3);
    
    // Find and verify common positions
    std::unordered_map<std::string, PositionData> pos_map;
    for (const auto& pos : positions) {
        pos_map[pos.fen] = pos;
    }
    
    // The extractor strips move clocks, so we need to look for the stripped versions
    const std::string stripped_fen1 = "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -";
    const std::string stripped_fen2 = "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq d3";
    
    ASSERT_TRUE(pos_map.contains(stripped_fen1));
    EXPECT_EQ(pos_map[stripped_fen1].total, 30); // 10 * 3
    EXPECT_EQ(pos_map[stripped_fen1].moves["Nf3"], 30);
    
    ASSERT_TRUE(pos_map.contains(stripped_fen2));
    EXPECT_EQ(pos_map[stripped_fen2].total, 20); // 10 * 2
    EXPECT_EQ(pos_map[stripped_fen2].moves["d4"], 20);
}

// Test with different time controls and ratings
TEST_F(CommonPositionIntegrationTest, MultipleTimeControlsAndRatings) {
    std::vector<std::string> records;
    
    // Same position with different time controls and ratings
    std::string fen = "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3";
    
    // Blitz games
    records.push_back(createRecord(fen, {"e2e4", "e7e5", "Nf3", "Nc6", "Bc4"}, "Bc5", 5, 180.0, 1600));
    records.push_back(createRecord(fen, {"e2e4", "e7e5", "Nf3", "Nc6", "Bc4"}, "Bc5", 3, 179.5, 1650));
    records.push_back(createRecord(fen, {"e2e4", "e7e5", "Nf3", "Nc6", "Bc4"}, "f5", 2, 180.0, 1600));
    
    // Rapid games
    records.push_back(createRecord(fen, {"e2e4", "e7e5", "Nf3", "Nc6", "Bc4"}, "Bc5", 8, 600.0, 1700));
    records.push_back(createRecord(fen, {"e2e4", "e7e5", "Nf3", "Nc6", "Bc4"}, "Nf6", 4, 598.0, 1750));
    
    createTestBagz(records);
    
    // Run extraction
    CommonPositionExtractor::Config config;
    config.bagz_path = bagz_path_.string();
    config.output_path = output_path_.string();
    config.temp_dir = test_dir_ / "temp";
    config.threshold = 10;
    
    CommonPositionExtractor extractor(config);
    extractor.extract();
    
    // Verify output
    auto positions = readOutput();
    ASSERT_EQ(positions.size(), 1);
    
    auto& [pos_fen, moves, total] = positions[0];
    // Expect the stripped FEN (without move clocks)
    EXPECT_EQ(pos_fen, "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq -");
    EXPECT_EQ(total, 22); // 5+3+2+8+4
    EXPECT_EQ(moves["Bc5"], 16); // 5+3+8
    EXPECT_EQ(moves["f5"], 2);
    EXPECT_EQ(moves["Nf6"], 4);
}

// Test max_records limit
TEST_F(CommonPositionIntegrationTest, MaxRecordsLimit) {
    std::vector<std::string> records;
    
    // Create 100 records
    for (int i = 0; i < 100; ++i) {
        // Every 10th record is the same position
        std::string fen = (i % 10 == 0) ? 
            "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1" : 
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 " + std::to_string(i);
        records.push_back(createRecord(fen, {}, "e4", 2));
    }
    
    createTestBagz(records);
    
    // Run extraction with max_records = 50
    CommonPositionExtractor::Config config;
    config.bagz_path = bagz_path_.string();
    config.output_path = output_path_.string();
    config.temp_dir = test_dir_ / "temp";
    config.threshold = 5;
    config.max_records = 50;
    
    CommonPositionExtractor extractor(config);
    extractor.extract();
    
    // Verify output
    auto positions = readOutput();
    // We processed 50 records, which includes positions at indices 0,10,20,30,40 (common) and other unique positions
    // But many "unique" positions have the same base position, just different move numbers
    ASSERT_GE(positions.size(), 1);
    
    // Find the common position
    const std::string common_stripped = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3";
    auto it = std::ranges::find_if(positions,
                                   [&](const auto& p) { return p.fen == common_stripped; });
    ASSERT_NE(it, positions.end());
    EXPECT_EQ(it->total, 10); // 5 * 2
}

// Test parallel processing correctness
TEST_F(CommonPositionIntegrationTest, ParallelProcessingCorrectness) {
    std::vector<std::string> records;
    
    // Create overlapping data that will be distributed across chunks
    std::vector<std::string> common_fens = {
        "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
        "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq d3 0 1",
        "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKBNR w KQkq - 2 3"
    };
    std::mt19937 rng(12345); // Fixed seed for reproducibility
    std::uniform_int_distribution fen_dist(0, 2);
    std::uniform_int_distribution count_dist(1, 5);
    
    for (int i = 0; i < 1000; ++i) {
        std::string fen = common_fens[fen_dist(rng)];
        std::string move = "move" + std::to_string(i % 10);
        int count = count_dist(rng);
        records.push_back(createRecord(fen, {}, move, count));
    }
    
    createTestBagz(records);
    
    // Run extraction with different thread counts
    std::vector thread_counts = {1, 4, 8};
    std::vector<std::vector<PositionData>> results;
    
    for (int threads : thread_counts) {
        CommonPositionExtractor::Config config;
        config.bagz_path = bagz_path_.string();
        config.output_path = test_dir_ / ("output_" + std::to_string(threads) + ".jsonl");
        config.temp_dir = test_dir_ / ("temp_" + std::to_string(threads));
        config.threshold = 100;
        config.chunk_size = 100;
        config.num_threads = threads;
        
        CommonPositionExtractor extractor(config);
        extractor.extract();
        
        // Read results
        std::ifstream in(config.output_path);
        std::string line;
        std::vector<PositionData> positions;
        
        while (std::getline(in, line)) {
            positions.push_back(PositionData::fromJson(line));
        }
        
        // Sort by FEN for comparison
        std::ranges::sort(positions,
                          [](const PositionData& a, const PositionData& b) {
                              return a.fen < b.fen;
                          });
        
        results.push_back(positions);
    }
    
    // Verify all thread counts produce identical results
    for (size_t i = 1; i < results.size(); ++i) {
        ASSERT_EQ(results[i].size(), results[0].size()) 
            << "Different result count with " << thread_counts[i] << " threads";
        
        for (size_t j = 0; j < results[0].size(); ++j) {
            EXPECT_EQ(results[i][j].fen, results[0][j].fen);
            EXPECT_EQ(results[i][j].total, results[0][j].total);
            EXPECT_EQ(results[i][j].moves.size(), results[0][j].moves.size());
            
            // Verify move counts match
            for (const auto& [move, count] : results[0][j].moves) {
                EXPECT_EQ(results[i][j].moves[move], count);
            }
        }
    }
}