#include <gtest/gtest.h>
#include "../winner_converter/winner_data_converter.hpp"
#include "../winner_converter/winner_position_record.hpp"
#include "../core/bagz.hpp"
#include <filesystem>
#include <fstream>
#include <simdjson.h>

namespace chessmimic::winner_converter {

class WinnerConverterIntegrationTest : public testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path() / 
                    ("winner_integration_test_" + std::to_string(getpid()));
        std::filesystem::create_directories(temp_dir_);
    }
    
    void TearDown() override {
        std::filesystem::remove_all(temp_dir_);
    }
    
    void createComplexPgnFile(const std::string& filename) const {
        std::ofstream file(temp_dir_ / filename);
        
        // Write all games as a single string to ensure proper formatting
        std::string pgn_content = R"([Event "Test Tournament"]
[Site "Test"]
[Date "2024.01.01"]
[Round "1"]
[White "Player A"]
[Black "Player B"]
[Result "1-0"]
[WhiteElo "2100"]
[BlackElo "2050"]
[TimeControl "300+3"]

{[%clk 0:05:00]} 1. e4 {[%clk 0:04:57]} c5 {[%clk 0:04:58]} 
2. Nf3 {[%clk 0:04:54]} d6 {[%clk 0:04:55]}
3. d4 {[%clk 0:04:50]} cxd4 {[%clk 0:04:52]}
4. Nxd4 {[%clk 0:04:48]} Nf6 {[%clk 0:04:49]}
5. Nc3 {[%clk 0:04:45]} a6 {[%clk 0:04:46]}
6. Be3 {[%clk 0:04:42]} e5 {[%clk 0:04:43]}
7. Nb3 {[%clk 0:04:39]} Be6 {[%clk 0:04:40]} 1-0

[Event "Test Tournament"]
[Site "Test"]
[Date "2024.01.01"]
[Round "2"]
[White "Player C"]
[Black "Player D"]
[Result "1/2-1/2"]
[WhiteElo "1850"]
[BlackElo "1900"]
[TimeControl "180+2"]

{[%clk 0:03:00]} 1. d4 {[%clk 0:02:58]} Nf6 {[%clk 0:02:59]}
2. c4 {[%clk 0:02:56]} e6 {[%clk 0:02:57]} 3. Nf3 {[%clk 0:02:54]} b6 {[%clk 0:02:55]} 
4. g3 {[%clk 0:02:52]} Ba6 {[%clk 0:02:53]} 5. b3 {[%clk 0:02:50]} Bb4+ {[%clk 0:02:51]} 
6. Bd2 {[%clk 0:02:48]} Be7 {[%clk 0:02:49]} 1/2-1/2

[Event "Test Tournament"]
[Site "Test"]
[Date "2024.01.01"]
[Round "3"]
[White "Player E"]
[Black "Player F"]
[Result "0-1"]
[WhiteElo "1600"]
[BlackElo "1750"]
[TimeControl "180+2"]

{[%clk 0:03:00]} 1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:59]} 
2. Nf3 {[%clk 0:02:56]} Nc6 {[%clk 0:02:57]} 3. Bc4 {[%clk 0:02:54]} Bc5 {[%clk 0:02:55]} 
4. b4 {[%clk 0:02:52]} Bxb4 {[%clk 0:02:53]} 5. c3 {[%clk 0:02:50]} Ba5 {[%clk 0:02:51]} 
6. d4 {[%clk 0:02:48]} exd4 {[%clk 0:02:49]} 7. O-O {[%clk 0:02:46]} b5 {[%clk 0:02:47]} 0-1

[Event "Test Tournament"]
[Site "Test"]
[Date "2024.01.01"]
[Round "4"]
[White "Player G"]
[Black "Player H"]
[Result "*"]
[WhiteElo "1700"]
[BlackElo "1700"]
[TimeControl "180+2"]

{[%clk 0:03:00]} 1. e4 {[%clk 0:02:58]} c5 {[%clk 0:02:59]} 
2. Nf3 {[%clk 0:02:56]} e6 {[%clk 0:02:57]} 3. d4 {[%clk 0:02:54]} cxd4 {[%clk 0:02:55]} 
4. Nxd4 {[%clk 0:02:52]} *
)";
        
        file << pgn_content;
        
        file.close();
        
        // Debug: print the file content
        std::ifstream debug_file(temp_dir_ / filename);
        std::string line;
        int line_count = 0;
        std::cout << "\nDEBUG: PGN file content:\n";
        while (std::getline(debug_file, line) && line_count < 100) {
            std::cout << line << "\n";
            line_count++;
        }
        std::cout << "\nDEBUG: Total lines: " << line_count << "\n\n";
    }
    
    std::filesystem::path temp_dir_;
};

// Test full pipeline with complex PGN file
TEST_F(WinnerConverterIntegrationTest, FullPipelineTest) {
    createComplexPgnFile("complex.pgn");
    
    WinnerDataConverter::Config config;
    config.pgn_paths = {(temp_dir_ / "complex.pgn").string()};
    config.output_bagz_path = (temp_dir_ / "output.bagz").string();
    config.num_threads = 2;
    config.log_level = 1; // INFO level for integration test
    
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    // Verify output
    ASSERT_TRUE(std::filesystem::exists(temp_dir_ / "output.bagz"));
    
    BagFileReader reader((temp_dir_ / "output.bagz").string());
    
    // Count positions by outcome
    int white_wins = 0, draws = 0, black_wins = 0, unfinished = 0;
    std::set<std::string> unique_fens;
    
    simdjson::dom::parser parser;
    
    for (size_t i = 0; i < reader.size(); ++i) {
        auto data = reader.get_record(i);
        std::string json_str(data.begin(), data.end());
        
        WinnerPositionRecord record = WinnerPositionRecord::fromJson(json_str, parser);
        
        unique_fens.insert(record.fen);
        
        if (record.winner == 1) white_wins++;
        else if (record.winner == 0) draws++;
        else if (record.winner == -1) black_wins++;
        else unfinished++;
    }
    
    // Verify counts (games 1-3 should be included, game 4 excluded as unfinished)
    EXPECT_GT(white_wins, 0) << "Should have white win positions";
    EXPECT_GT(draws, 0) << "Should have draw positions";
    EXPECT_GT(black_wins, 0) << "Should have black win positions";
    EXPECT_EQ(unfinished, 0) << "Should not have unfinished game positions";
    
    // Verify we have unique positions
    EXPECT_GT(unique_fens.size(), 10) << "Should have multiple unique positions";
}

// Test with real sample PGN data
TEST_F(WinnerConverterIntegrationTest, RealSampleData) {
    // Check if sample data exists
    std::filesystem::path sample_pgn = "../cpp_src/tests/data_files/sample_100.pgn";
    if (!std::filesystem::exists(sample_pgn)) {
        GTEST_SKIP() << "Sample PGN file not found";
    }
    
    WinnerDataConverter::Config config;
    config.pgn_paths = {sample_pgn.string()};
    config.output_bagz_path = (temp_dir_ / "sample_output.bagz").string();
    config.shuffle_enabled = true;
    config.shuffle_seed = 42;
    config.num_threads = 4;
    config.log_level = 1;
    
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    // Verify output
    BagFileReader reader((temp_dir_ / "sample_output.bagz").string());
    EXPECT_GT(reader.size(), 100) << "Sample file should produce many positions";
    
    // Verify data integrity
    simdjson::dom::parser parser;
    for (size_t i = 0; i < std::min(static_cast<size_t>(10), reader.size()); ++i) {
        auto data = reader.get_record(i);
        std::string json_str(data.begin(), data.end());
        
        // Should parse without errors
        ASSERT_NO_THROW({
            WinnerPositionRecord record = WinnerPositionRecord::fromJson(json_str, parser);
            
            // Basic validation
            EXPECT_FALSE(record.fen.empty());
            EXPECT_GE(record.winner, -1);
            EXPECT_LE(record.winner, 1);
            EXPECT_GE(record.white_rating, 0);
            EXPECT_GE(record.black_rating, 0);
        });
    }
}

// Test memory-efficient processing of large files
TEST_F(WinnerConverterIntegrationTest, LargeFileProcessing) {
    // Create a moderately large PGN file
    std::ofstream file(temp_dir_ / "large.pgn");
    
    // Generate 100 games
    for (int game = 0; game < 100; ++game) {
        file << "[Event \"Game " << game << "\"]\n";
        file << "[Result \"" << (game % 3 == 0 ? "1-0" : game % 3 == 1 ? "0-1" : "1/2-1/2") << "\"]\n";
        file << "[WhiteElo \"" << (1500 + (game * 7) % 500) << "\"]\n";
        file << "[BlackElo \"" << (1500 + (game * 11) % 500) << "\"]\n";
        file << "[TimeControl \"300+5\"]\n\n";
        
        // Add some moves with clock annotations
        file << "{[%clk 0:05:00]} 1. e4 {[%clk 0:04:58]} e5 {[%clk 0:04:57]} ";
        file << "2. Nf3 {[%clk 0:04:55]} Nc6 {[%clk 0:04:54]} ";
        file << "3. Bb5 {[%clk 0:04:52]} a6 {[%clk 0:04:51]} ";
        file << "4. Ba4 {[%clk 0:04:49]} Nf6 {[%clk 0:04:48]} ";
        file << (game % 3 == 0 ? "1-0" : game % 3 == 1 ? "0-1" : "1/2-1/2") << "\n\n";
    }
    file.close();
    
    WinnerDataConverter::Config config;
    config.pgn_paths = {(temp_dir_ / "large.pgn").string()};
    config.output_bagz_path = (temp_dir_ / "large_output.bagz").string();
    config.memory_limit_gb = 0.1; // Limit to 100MB to test memory efficiency
    config.num_threads = 4;
    config.log_level = 0;
    
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    BagFileReader reader((temp_dir_ / "large_output.bagz").string());
    EXPECT_EQ(reader.size(), 800); // 100 games × 8 positions each
}

// Test error handling for corrupted PGN
TEST_F(WinnerConverterIntegrationTest, CorruptedPgnHandling) {
    std::ofstream file(temp_dir_ / "corrupted.pgn");
    
    // Valid game
    file << R"([Event "Valid"]
[Result "1-0"]
[WhiteElo "1500"]
[BlackElo "1500"]
[TimeControl "180+0"]

{[%clk 0:03:00]} 1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} 1-0

)";
    
    // Corrupted game (missing closing bracket)
    file << R"([Event "Corrupted"
[Result "0-1"]
[WhiteElo "1600"
[BlackElo "1600"]
[TimeControl "180+0"]
{[%clk 0:03:00]} 1. d4 {[%clk 0:02:58]} d5 {[%clk 0:02:57]} 0-1

)";
    
    // Another valid game
    file << R"([Event "Valid 2"]
[Result "1/2-1/2"]
[WhiteElo "1700"]
[BlackElo "1700"]
[TimeControl "180+0"]

{[%clk 0:03:00]} 1. c4 {[%clk 0:02:58]} c5 {[%clk 0:02:57]} 1/2-1/2

)";
    
    file.close();
    
    WinnerDataConverter::Config config;
    config.pgn_paths = {(temp_dir_ / "corrupted.pgn").string()};
    config.output_bagz_path = (temp_dir_ / "corrupted_output.bagz").string();
    config.num_threads = 1;
    config.log_level = 0;
    
    WinnerDataConverter converter(config);
    
    // Should still succeed, skipping corrupted games
    ASSERT_TRUE(converter.convert());
    
    BagFileReader reader((temp_dir_ / "corrupted_output.bagz").string());
    // Should have positions from the two valid games only
    EXPECT_EQ(reader.size(), 4); // 2 valid games × 2 positions each
}

// Test directory processing
TEST_F(WinnerConverterIntegrationTest, DirectoryProcessing) {
    // Create multiple PGN files in a subdirectory
    auto pgn_dir = temp_dir_ / "pgn_files";
    std::filesystem::create_directories(pgn_dir);
    
    for (int i = 0; i < 3; ++i) {
        std::ofstream file(pgn_dir / ("game" + std::to_string(i) + ".pgn"));
        file << "[Event \"Game " << i << "\"]\n";
        file << "[Result \"1-0\"]\n";
        file << "[WhiteElo \"1500\"]\n";
        file << "[BlackElo \"1500\"]\n";
        file << "[TimeControl \"180+0\"]\n\n";
        file << "{[%clk 0:03:00]} 1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} 1-0\n";
        file.close();
    }
    
    // Also create a non-PGN file that should be ignored
    std::ofstream other(pgn_dir / "readme.txt");
    other << "This should be ignored";
    other.close();
    
    WinnerDataConverter::Config config;
    config.pgn_paths = {pgn_dir.string()};
    config.output_bagz_path = (temp_dir_ / "dir_output.bagz").string();
    config.num_threads = 2;
    config.log_level = 0;
    
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    BagFileReader reader((temp_dir_ / "dir_output.bagz").string());
    EXPECT_EQ(reader.size(), 6); // 3 games × 2 positions each
}

// Test combined filtering options
TEST_F(WinnerConverterIntegrationTest, CombinedFiltering) {
    std::ofstream file(temp_dir_ / "mixed.pgn");
    
    // Game 1: Draw, low rating (should be filtered)
    file << R"([Event "Game 1"]
[Result "1/2-1/2"]
[WhiteElo "1200"]
[BlackElo "1300"]
[TimeControl "180+0"]

{[%clk 0:03:00]} 1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} 1/2-1/2

)";
    
    // Game 2: Win, good rating (should be included)
    file << R"([Event "Game 2"]
[Result "1-0"]
[WhiteElo "1800"]
[BlackElo "1750"]
[TimeControl "180+0"]

{[%clk 0:03:00]} 1. d4 {[%clk 0:02:58]} d5 {[%clk 0:02:57]} 
2. c4 {[%clk 0:02:55]} 1-0

)";
    
    // Game 3: Win, low rating (should be filtered)
    file << R"([Event "Game 3"]
[Result "0-1"]
[WhiteElo "1100"]
[BlackElo "1150"]
[TimeControl "180+0"]

{[%clk 0:03:00]} 1. f4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} 0-1

)";
    
    // Game 4: Win, high rating (should be filtered)
    file << R"([Event "Game 4"]
[Result "1-0"]
[WhiteElo "2500"]
[BlackElo "2600"]
[TimeControl "180+0"]

{[%clk 0:03:00]} 1. e4 {[%clk 0:02:58]} c5 {[%clk 0:02:57]} 1-0

)";
    
    file.close();
    
    WinnerDataConverter::Config config;
    config.pgn_paths = {(temp_dir_ / "mixed.pgn").string()};
    config.output_bagz_path = (temp_dir_ / "filtered.bagz").string();
    config.filter_draws = true;
    config.min_rating = 1500;
    config.max_rating = 2000;
    config.num_threads = 1;
    config.log_level = 0;
    
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    BagFileReader reader((temp_dir_ / "filtered.bagz").string());
    // Only game 2 should pass all filters
    EXPECT_EQ(reader.size(), 3); // 1 game × 3 positions
}

// Test integration with standard ShuffleManager
TEST_F(WinnerConverterIntegrationTest, StandardShuffleManagerIntegration) {
    // Create a test PGN file with multiple games
    createComplexPgnFile("shuffle_test.pgn");
    
    WinnerDataConverter::Config config;
    config.pgn_paths = {(temp_dir_ / "shuffle_test.pgn").string()};
    config.output_bagz_path = (temp_dir_ / "shuffled.bagz").string();
    config.shuffle_enabled = true;
    config.shuffle_seed = 99999;
    config.shuffle_buckets = 8;
    config.num_threads = 2;
    config.log_level = 0;
    config.temp_dir = temp_dir_.string();
    
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    // Verify output exists
    ASSERT_TRUE(std::filesystem::exists(temp_dir_ / "shuffled.bagz"));
    
    // Read output and verify it's been shuffled
    BagFileReader reader((temp_dir_ / "shuffled.bagz").string());
    EXPECT_GT(reader.size(), 0);
    
    // Collect all positions to verify shuffle
    std::vector<std::string> positions;
    simdjson::dom::parser parser;
    
    for (size_t i = 0; i < reader.size(); ++i) {
        auto data = reader.get_record(i);
        std::string json(data.begin(), data.end());
        simdjson::dom::element doc = parser.parse(json);
        auto fen = std::string(doc["fen"]);
        positions.push_back(fen);
    }
    
    // With a fixed seed, verify that positions are not in their original order
    // (This is probabilistic but should work for a reasonable number of positions)
    bool is_shuffled = false;
    for (size_t i = 1; i < positions.size(); ++i) {
        if (positions[i] < positions[i-1]) {
            is_shuffled = true;
            break;
        }
    }
    EXPECT_TRUE(is_shuffled) << "Positions should be shuffled";
}

// Test large dataset with bucket-based shuffle
TEST_F(WinnerConverterIntegrationTest, LargeDatasetBucketShuffle) {
    // Create multiple PGN files to simulate a large dataset
    std::vector<std::string> pgn_files;
    
    for (int file_idx = 0; file_idx < 5; ++file_idx) {
        std::stringstream pgn_content;
        for (int game = 0; game < 20; ++game) {
            pgn_content << "[Event \"Large Dataset Game " << (file_idx * 20 + game) << "\"]\n";
            pgn_content << "[Result \"" << (game % 3 == 0 ? "1-0" : game % 3 == 1 ? "0-1" : "1/2-1/2") << "\"]\n";
            pgn_content << "[WhiteElo \"" << (1600 + game * 10) << "\"]\n";
            pgn_content << "[BlackElo \"" << (1600 + game * 10) << "\"]\n";
            pgn_content << "[TimeControl \"180+2\"]\n\n";
            
            // Add some moves with clock annotations
            pgn_content << "{[%clk 0:03:00]} 1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} ";
            pgn_content << "2. Nf3 {[%clk 0:02:55]} Nc6 {[%clk 0:02:54]} ";
            pgn_content << "3. Bb5 {[%clk 0:02:52]} a6 {[%clk 0:02:51]} ";
            pgn_content << "4. Ba4 {[%clk 0:02:49]} Nf6 {[%clk 0:02:48]} ";
            pgn_content << "5. O-O {[%clk 0:02:46]} Be7 {[%clk 0:02:45]} ";
            pgn_content << (game % 3 == 0 ? "1-0" : game % 3 == 1 ? "0-1" : "1/2-1/2") << "\n\n";
        }
        
        std::string filename = "large_dataset_" + std::to_string(file_idx) + ".pgn";
        std::ofstream file(temp_dir_ / filename);
        file << pgn_content.str();
        file.close();
        
        pgn_files.push_back((temp_dir_ / filename).string());
    }
    
    WinnerDataConverter::Config config;
    config.pgn_paths = pgn_files;
    config.output_bagz_path = (temp_dir_ / "large_bucket_shuffle.bagz").string();
    config.shuffle_enabled = true;
    config.shuffle_seed = 54321;
    config.shuffle_memory_threshold_mb = 1; // Force bucket-based shuffle
    config.shuffle_buckets = 32;
    config.bucket_ram_limit_mb = 256;
    config.num_threads = 4;
    config.log_level = 0;
    config.temp_dir = temp_dir_.string();
    config.filter_draws = true; // Filter draws to reduce dataset size
    
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    // Verify output
    ASSERT_TRUE(std::filesystem::exists(temp_dir_ / "large_bucket_shuffle.bagz"));
    BagFileReader reader((temp_dir_ / "large_bucket_shuffle.bagz").string());
    
    // Should have positions from multiple games (excluding draws)
    EXPECT_GT(reader.size(), 50);
    
    // Verify data integrity - sample a few records
    simdjson::dom::parser parser;
    for (size_t i = 0; i < std::min<size_t>(10, reader.size()); i += reader.size() / 10) {
        auto data = reader.get_record(i);
        std::string json(data.begin(), data.end());
        simdjson::dom::element doc = parser.parse(json);
        
        // Verify all required fields are present
        EXPECT_NO_THROW({
            [[maybe_unused]] auto  fen = std::string(doc["fen"]);
            [[maybe_unused]] int winner_val = doc["winner"].get_int64();
            [[maybe_unused]] int64_t white_rating = doc["white_rating"].get_int64();
            [[maybe_unused]] int64_t black_rating = doc["black_rating"].get_int64();
            [[maybe_unused]] double white_clock = doc["white_clock"].get_double();
            [[maybe_unused]] double black_clock = doc["black_clock"].get_double();
            [[maybe_unused]] double increment = doc["increment"].get_double();
        });
        
        // Verify winner is not 0 (draw) since we filtered them
        int winner = doc["winner"].get_int64();
        EXPECT_NE(winner, 0);
    }
}

// Test two-phase processing with keep_temp_files
TEST_F(WinnerConverterIntegrationTest, TwoPhaseWithTempFiles) {
    createComplexPgnFile("temp_files_test.pgn");
    
    WinnerDataConverter::Config config;
    config.pgn_paths = {(temp_dir_ / "temp_files_test.pgn").string()};
    config.output_bagz_path = (temp_dir_ / "final_with_temps.bagz").string();
    config.shuffle_enabled = true;
    config.keep_temp_files = true; // Keep intermediate files
    config.num_threads = 1;
    config.log_level = 0;
    config.temp_dir = temp_dir_.string();
    
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    // Verify both final output and intermediate file exist
    ASSERT_TRUE(std::filesystem::exists(temp_dir_ / "final_with_temps.bagz"));
    ASSERT_TRUE(std::filesystem::exists(temp_dir_ / "unsorted_records.data"));
    
    // Verify intermediate file format
    std::ifstream intermediate(temp_dir_ / "unsorted_records.data", std::ios::binary);
    ASSERT_TRUE(intermediate.is_open());
    
    // Read and validate at least one record from intermediate file
    uint32_t record_size;
    intermediate.read(reinterpret_cast<char*>(&record_size), sizeof(record_size));
    ASSERT_TRUE(intermediate.good());
    EXPECT_GT(record_size, 0);
    EXPECT_LT(record_size, 10000); // Reasonable size limit
}

} // namespace chessmimic::winner_converter
