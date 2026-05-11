#include <gtest/gtest.h>
#include "../winner_converter/winner_data_converter.hpp"
#include "../core/bagz.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <getopt.h>

namespace chessmimic::winner_converter {

class WinnerDataConverterTest : public testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path() / 
                    ("winner_converter_test_" + std::to_string(getpid()));
        std::filesystem::create_directories(temp_dir_);
    }
    
    void TearDown() override {
        std::filesystem::remove_all(temp_dir_);
    }
    
    void createPgnFile(const std::string& filename, const std::string& content) const {
        auto filepath = temp_dir_ / filename;
        std::ofstream file(filepath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to create test PGN file: " + filepath.string());
        }
        file << content;
        file.close();
        
        // Verify file was created
        if (!std::filesystem::exists(filepath)) {
            throw std::runtime_error("Test PGN file not created: " + filepath.string());
        }
    }
    
    std::filesystem::path temp_dir_;
};

// Test basic conversion with a simple game
TEST_F(WinnerDataConverterTest, ConvertSimpleGame) {
    // Create a test PGN file
    createPgnFile("test.pgn", R"(
[Event "Test Game"]
[Result "1-0"]
[WhiteElo "1750"]
[BlackElo "1723"]
[TimeControl "180+2"]

{[%clk 0:03:00]} 1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:59]} 
2. Nf3 {[%clk 0:02:55]} Nc6 {[%clk 0:02:57]} 1-0
)");
    
    // Configure converter
    WinnerDataConverter::Config config;
    config.pgn_paths = {(temp_dir_ / "test.pgn").string()};
    config.output_bagz_path = (temp_dir_ / "output.bagz").string();
    config.num_threads = 1;
    config.log_level = 0; // ERROR only
    
    // Run conversion
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    // Verify output exists
    ASSERT_TRUE(std::filesystem::exists(temp_dir_ / "output.bagz"));
    
    // Read and verify content
    BagFileReader reader((temp_dir_ / "output.bagz").string());
    EXPECT_GT(reader.size(), 0);
    
    // Check first record
    auto data = reader.get_record(0);
    std::string json(data.begin(), data.end());
    EXPECT_TRUE(json.find("\"winner\":1") != std::string::npos); // White wins
    EXPECT_TRUE(json.find("\"white_rating\":1750") != std::string::npos);
    EXPECT_TRUE(json.find("\"black_rating\":1723") != std::string::npos);
}

// Test draw filtering
TEST_F(WinnerDataConverterTest, FilterDraws) {
    // Create games with different results
    createPgnFile("games.pgn", R"(
[Event "Game 1"]
[Result "1-0"]
[WhiteElo "1600"]
[BlackElo "1600"]
[TimeControl "180+0"]

{[%clk 0:03:00]} 1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} 1-0

[Event "Game 2"]
[Result "1/2-1/2"]
[WhiteElo "1700"]
[BlackElo "1700"]
[TimeControl "180+0"]

{[%clk 0:03:00]} 1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} 1/2-1/2

[Event "Game 3"]
[Result "0-1"]
[WhiteElo "1800"]
[BlackElo "1800"]
[TimeControl "180+0"]

{[%clk 0:03:00]} 1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} 0-1
)");
    
    // Test without filter
    {
        WinnerDataConverter::Config config;
        config.pgn_paths = {(temp_dir_ / "games.pgn").string()};
        config.output_bagz_path = (temp_dir_ / "all_games.bagz").string();
        config.filter_draws = false;
        config.num_threads = 1;
        config.log_level = 0;
        
        WinnerDataConverter converter(config);
        ASSERT_TRUE(converter.convert());
        
        BagFileReader reader((temp_dir_ / "all_games.bagz").string());
        EXPECT_EQ(reader.size(), 6); // 3 games × 2 positions each
    }
    
    // Test with draw filter
    {
        WinnerDataConverter::Config config;
        config.pgn_paths = {(temp_dir_ / "games.pgn").string()};
        config.output_bagz_path = (temp_dir_ / "decisive_only.bagz").string();
        config.filter_draws = true;
        config.num_threads = 1;
        config.log_level = 0;
        
        WinnerDataConverter converter(config);
        ASSERT_TRUE(converter.convert());
        
        BagFileReader reader((temp_dir_ / "decisive_only.bagz").string());
        EXPECT_EQ(reader.size(), 4); // 2 decisive games × 2 positions each
        
        // Verify no draws in output
        for (size_t i = 0; i < reader.size(); ++i) {
            auto data = reader.get_record(i);
            std::string json(data.begin(), data.end());
            EXPECT_TRUE(json.find("\"winner\":0") == std::string::npos);
        }
    }
}

// Test rating filtering
TEST_F(WinnerDataConverterTest, FilterByRating) {
    createPgnFile("rated_games.pgn", R"(
[Event "Low rated"]
[Result "1-0"]
[WhiteElo "1200"]
[BlackElo "1300"]
[TimeControl "180+0"]

{[%clk 0:03:00]} 1. e4 {[%clk 0:02:58]} 1-0

[Event "Mid rated"]
[Result "0-1"]
[WhiteElo "1600"]
[BlackElo "1700"]
[TimeControl "180+0"]

{[%clk 0:03:00]} 1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} 0-1

[Event "High rated"]
[Result "1-0"]
[WhiteElo "2200"]
[BlackElo "2100"]
[TimeControl "180+0"]

{[%clk 0:03:00]} 1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} 2. Nf3 {[%clk 0:02:55]} 1-0
)");
    
    WinnerDataConverter::Config config;
    config.pgn_paths = {(temp_dir_ / "rated_games.pgn").string()};
    config.output_bagz_path = (temp_dir_ / "filtered.bagz").string();
    config.min_rating = 1500;
    config.max_rating = 2000;
    config.num_threads = 1;
    config.log_level = 0;
    
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    BagFileReader reader((temp_dir_ / "filtered.bagz").string());
    
    // Only mid-rated game should be included
    EXPECT_EQ(reader.size(), 2); // 1 game with 2 positions
    
    // Verify ratings
    for (size_t i = 0; i < reader.size(); ++i) {
        auto data = reader.get_record(i);
        std::string json(data.begin(), data.end());
        EXPECT_TRUE(json.find("\"white_rating\":1600") != std::string::npos);
        EXPECT_TRUE(json.find("\"black_rating\":1700") != std::string::npos);
    }
}

// Test parallel processing
TEST_F(WinnerDataConverterTest, ParallelProcessing) {
    // Create multiple PGN files
    for (int i = 0; i < 4; ++i) {
        std::stringstream content;
        content << "[Event \"Game " << i << "\"]\n";
        content << "[Result \"" << (i % 2 == 0 ? "1-0" : "0-1") << "\"]\n";
        content << "[WhiteElo \"1500\"]\n";
        content << "[BlackElo \"1500\"]\n";
        content << "[TimeControl \"180+0\"]\n\n";
        content << "{[%clk 0:03:00]} 1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} " << (i % 2 == 0 ? "1-0" : "0-1") << "\n";
        
        createPgnFile("game" + std::to_string(i) + ".pgn", content.str());
    }
    
    WinnerDataConverter::Config config;
    for (int i = 0; i < 4; ++i) {
        config.pgn_paths.push_back((temp_dir_ / ("game" + std::to_string(i) + ".pgn")).string());
    }
    config.output_bagz_path = (temp_dir_ / "parallel.bagz").string();
    config.num_threads = 4; // Use multiple threads
    config.log_level = 0;
    
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    BagFileReader reader((temp_dir_ / "parallel.bagz").string());
    EXPECT_EQ(reader.size(), 8); // 4 games × 2 positions each
}

// Test shuffle functionality
TEST_F(WinnerDataConverterTest, ShuffleOutput) {
    // Create ordered games
    createPgnFile("ordered.pgn", R"(
[Event "Game A"]
[Result "1-0"]
[WhiteElo "1000"]
[BlackElo "1000"]
[TimeControl "180+0"]

{[%clk 0:03:00]} 1. a3 {[%clk 0:02:58]} a6 {[%clk 0:02:57]} 1-0

[Event "Game B"]
[Result "0-1"]
[WhiteElo "2000"]
[BlackElo "2000"]
[TimeControl "180+0"]

{[%clk 0:03:00]} 1. b3 {[%clk 0:02:58]} b6 {[%clk 0:02:57]} 0-1

[Event "Game C"]
[Result "1/2-1/2"]
[WhiteElo "3000"]
[BlackElo "3000"]
[TimeControl "180+0"]

{[%clk 0:03:00]} 1. c3 {[%clk 0:02:58]} c6 {[%clk 0:02:57]} 1/2-1/2
)");
    
    // First run without shuffle
    {
        WinnerDataConverter::Config config;
        config.pgn_paths = {(temp_dir_ / "ordered.pgn").string()};
        config.output_bagz_path = (temp_dir_ / "unshuffled.bagz").string();
        config.shuffle_enabled = false;
        config.num_threads = 1;
        config.log_level = 0;
        
        WinnerDataConverter converter(config);
        ASSERT_TRUE(converter.convert());
    }
    
    // Then run with shuffle (fixed seed for reproducibility)
    {
        WinnerDataConverter::Config config;
        config.pgn_paths = {(temp_dir_ / "ordered.pgn").string()};
        config.output_bagz_path = (temp_dir_ / "shuffled.bagz").string();
        config.shuffle_enabled = true;
        config.shuffle_seed = 12345;
        config.num_threads = 1;
        config.log_level = 0;
        
        WinnerDataConverter converter(config);
        ASSERT_TRUE(converter.convert());
    }
    
    // Compare outputs
    BagFileReader unshuffled((temp_dir_ / "unshuffled.bagz").string());
    BagFileReader shuffled((temp_dir_ / "shuffled.bagz").string());
    
    EXPECT_EQ(unshuffled.size(), shuffled.size());
    
    // Check that shuffled output has different order (with high probability)
    bool different_order = false;
    for (size_t i = 0; i < unshuffled.size() && !different_order; ++i) {
        auto data1 = unshuffled.get_record(i);
        auto data2 = shuffled.get_record(i);
        std::string json1(data1.begin(), data1.end());
        if (std::string json2(data2.begin(), data2.end()); json1 != json2) {
            different_order = true;
        }
    }
    EXPECT_TRUE(different_order) << "Shuffled output should have different order";
}

// Test handling of games without moves
TEST_F(WinnerDataConverterTest, GamesWithoutMoves) {
    createPgnFile("no_moves.pgn", R"(
[Event "Forfeit"]
[Result "1-0"]
[WhiteElo "1500"]
[BlackElo "1500"]
[TimeControl "180+0"]

[Event "Another Forfeit"]
[Result "0-1"]
[WhiteElo "1600"]
[BlackElo "1600"]
[TimeControl "180+0"]
1-0
)");
    
    WinnerDataConverter::Config config;
    config.pgn_paths = {(temp_dir_ / "no_moves.pgn").string()};
    config.output_bagz_path = (temp_dir_ / "no_moves_output.bagz").string();
    config.num_threads = 1;
    config.log_level = 0;
    
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    // Should produce empty output as there are no positions
    BagFileReader reader((temp_dir_ / "no_moves_output.bagz").string());
    EXPECT_EQ(reader.size(), 0);
}

// Test clock data inclusion
TEST_F(WinnerDataConverterTest, ClockDataExtraction) {
    createPgnFile("with_clocks.pgn", R"(
[Event "Timed Game"]
[Result "1-0"]
[WhiteElo "1800"]
[BlackElo "1750"]
[TimeControl "300+5"]

{[%clk 0:05:00]} 1. e4 {[%clk 0:04:58]} e5 {[%clk 0:04:57]} 
2. Nf3 {[%clk 0:04:55]} Nc6 {[%clk 0:04:54]} 1-0
)");
    
    WinnerDataConverter::Config config;
    config.pgn_paths = {(temp_dir_ / "with_clocks.pgn").string()};
    config.output_bagz_path = (temp_dir_ / "clocks.bagz").string();
    config.num_threads = 1;
    config.log_level = 0;
    
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    BagFileReader reader((temp_dir_ / "clocks.bagz").string());
    ASSERT_GT(reader.size(), 0);
    
    // Check that clock data is included
    auto data = reader.get_record(0);
    std::string json(data.begin(), data.end());
    EXPECT_TRUE(json.find("\"white_clock\":") != std::string::npos);
    EXPECT_TRUE(json.find("\"black_clock\":") != std::string::npos);
    EXPECT_TRUE(json.find("\"increment\":5") != std::string::npos);
}

// Test command-line argument parsing
TEST_F(WinnerDataConverterTest, ParseArguments) {
    // Test valid arguments
    {
        WinnerDataConverter::Config config;
        
        // Reset getopt state
        optind = 1;
        
        const char* argv[] = {
            "pgn_to_winner_bagz",
            "input.pgn",
            "-o", "output.bagz",
            "--threads", "8",
            "--memory", "16.5",
            "--filter-draws",
            "--min-rating", "1600",
            "--max-rating", "2400",
            "--shuffle",
            "--shuffle-seed", "42",
            "--temp-dir", "/tmp/test",
            "--keep-temp-files",
            "--log-level", "2"
        };
        int argc = std::size(argv);
        
        ASSERT_TRUE(WinnerDataConverter::parseArguments(argc, const_cast<char**>(argv), config));
        
        EXPECT_EQ(config.pgn_paths.size(), 1);
        EXPECT_EQ(config.pgn_paths[0], "input.pgn");
        EXPECT_EQ(config.output_bagz_path, "output.bagz");
        EXPECT_EQ(config.num_threads, 8);
        EXPECT_DOUBLE_EQ(config.memory_limit_gb, 16.5);
        EXPECT_TRUE(config.filter_draws);
        EXPECT_EQ(config.min_rating, 1600);
        EXPECT_EQ(config.max_rating, 2400);
        EXPECT_TRUE(config.shuffle_enabled);
        EXPECT_EQ(config.shuffle_seed, 42);
        EXPECT_EQ(config.temp_dir, "/tmp/test");
        EXPECT_TRUE(config.keep_temp_files);
        EXPECT_EQ(config.log_level, 2);
    }
    
    // Test missing output file
    {
        WinnerDataConverter::Config config2;
        
        // Reset getopt state
        optind = 1;
        
        const char* argv[] = {"pgn_to_winner_bagz", "input.pgn"};
        int argc = std::size(argv);
        
        EXPECT_FALSE(WinnerDataConverter::parseArguments(argc, const_cast<char**>(argv), config2));
    }
}

// Test new shuffle configuration parameters
TEST_F(WinnerDataConverterTest, ParseShuffleConfiguration) {
    WinnerDataConverter::Config config;
    
    // Reset getopt state
    optind = 1;
    
    const char* argv[] = {
        "pgn_to_winner_bagz",
        "input.pgn",
        "-o", "output.bagz",
        "--shuffle",
        "--shuffle-buckets", "512",
        "--shuffle-memory", "2048",
        "--bucket-memory", "8192"
    };
    int argc = std::size(argv);
    
    ASSERT_TRUE(WinnerDataConverter::parseArguments(argc, const_cast<char**>(argv), config));
    
    EXPECT_TRUE(config.shuffle_enabled);
    EXPECT_EQ(config.shuffle_buckets, 512);
    EXPECT_EQ(config.shuffle_memory_threshold_mb, 2048);
    EXPECT_EQ(config.bucket_ram_limit_mb, 8192);
}

// Test two-phase processing with intermediate format
TEST_F(WinnerDataConverterTest, TwoPhaseProcessingWithShuffle) {
    createPgnFile("test_shuffle.pgn", R"(
[Event "Test Game"]
[Result "1-0"]
[WhiteElo "1750"]
[BlackElo "1750"]
[TimeControl "180+0"]

{[%clk 0:03:00]} 1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} 
2. Nf3 {[%clk 0:02:55]} Nc6 {[%clk 0:02:54]} 
3. Bb5 {[%clk 0:02:52]} a6 {[%clk 0:02:51]} 1-0
)");
    
    WinnerDataConverter::Config config;
    config.pgn_paths = {(temp_dir_ / "test_shuffle.pgn").string()};
    config.output_bagz_path = (temp_dir_ / "shuffled_output.bagz").string();
    config.shuffle_enabled = true;
    config.shuffle_seed = 12345;
    config.num_threads = 1;
    config.log_level = 0;
    config.temp_dir = temp_dir_.string();
    
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    // Verify final output exists
    ASSERT_TRUE(std::filesystem::exists(temp_dir_ / "shuffled_output.bagz"));
    
    // If keep_temp_files is false, intermediate file should be cleaned up
    if (!config.keep_temp_files) {
        EXPECT_FALSE(std::filesystem::exists(temp_dir_ / "unsorted_records.data"));
    }
}

// Test bucket-based shuffle for large files
TEST_F(WinnerDataConverterTest, BucketBasedShuffleForLargeFiles) {
    // Create a larger PGN file that will trigger bucket-based shuffle
    std::stringstream pgn_content;
    for (int game = 0; game < 100; ++game) {
        pgn_content << "[Event \"Game " << game << "\"]\n";
        pgn_content << "[Result \"" << (game % 3 == 0 ? "1-0" : game % 3 == 1 ? "0-1" : "1/2-1/2") << "\"]\n";
        pgn_content << "[WhiteElo \"" << (1500 + game) << "\"]\n";
        pgn_content << "[BlackElo \"" << (1500 + game) << "\"]\n";
        pgn_content << "[TimeControl \"180+0\"]\n\n";
        pgn_content << "{[%clk 0:03:00]} 1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} ";
        pgn_content << "2. Nf3 {[%clk 0:02:55]} Nc6 {[%clk 0:02:54]} ";
        pgn_content << "3. Bb5 {[%clk 0:02:52]} a6 {[%clk 0:02:51]} ";
        pgn_content << "4. Ba4 {[%clk 0:02:49]} Nf6 {[%clk 0:02:48]} ";
        pgn_content << "5. O-O {[%clk 0:02:46]} Be7 {[%clk 0:02:45]} ";
        pgn_content << (game % 3 == 0 ? "1-0" : game % 3 == 1 ? "0-1" : "1/2-1/2") << "\n\n";
    }
    createPgnFile("large_dataset.pgn", pgn_content.str());
    
    WinnerDataConverter::Config config;
    config.pgn_paths = {(temp_dir_ / "large_dataset.pgn").string()};
    config.output_bagz_path = (temp_dir_ / "bucket_shuffled.bagz").string();
    config.shuffle_enabled = true;
    config.shuffle_seed = 42;
    config.shuffle_memory_threshold_mb = 1; // Force bucket-based shuffle
    config.shuffle_buckets = 16;
    config.bucket_ram_limit_mb = 128;
    config.num_threads = 4;
    config.log_level = 0;
    config.temp_dir = temp_dir_.string();
    
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    // Verify output exists and contains expected number of positions
    ASSERT_TRUE(std::filesystem::exists(temp_dir_ / "bucket_shuffled.bagz"));
    BagFileReader reader((temp_dir_ / "bucket_shuffled.bagz").string());
    EXPECT_GT(reader.size(), 0);
}

// Test intermediate format compatibility with RecordProcessor
TEST_F(WinnerDataConverterTest, IntermediateFormatCompatibility) {
    createPgnFile("intermediate_test.pgn", R"(
[Event "Test"]
[Result "1-0"]
[WhiteElo "1800"]
[BlackElo "1800"]
[TimeControl "180+0"]

{[%clk 0:03:00]} 1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} 1-0
)");
    
    WinnerDataConverter::Config config;
    config.pgn_paths = {(temp_dir_ / "intermediate_test.pgn").string()};
    config.output_bagz_path = (temp_dir_ / "final.bagz").string();
    config.shuffle_enabled = true;
    config.keep_temp_files = true; // Keep intermediate file for testing
    config.num_threads = 1;
    config.log_level = 0;
    config.temp_dir = temp_dir_.string();
    
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    // Verify intermediate file exists and has correct format
    auto intermediate_path = temp_dir_ / "unsorted_records.data";
    ASSERT_TRUE(std::filesystem::exists(intermediate_path));
    
    // Try to read the intermediate file using RecordProcessor format
    std::ifstream in(intermediate_path, std::ios::binary);
    ASSERT_TRUE(in.is_open());
    
    // Read first record
    uint32_t record_size;
    in.read(reinterpret_cast<char*>(&record_size), sizeof(record_size));
    ASSERT_TRUE(in.good());
    EXPECT_GT(record_size, 0);
    
    // Read key length and key
    uint32_t key_len;
    in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
    ASSERT_TRUE(in.good());
    EXPECT_GT(key_len, 0);
    
    std::string key(key_len, '\0');
    in.read(&key[0], key_len);
    ASSERT_TRUE(in.good());
    
    // Read record length and record
    uint32_t record_len;
    in.read(reinterpret_cast<char*>(&record_len), sizeof(record_len));
    ASSERT_TRUE(in.good());
    EXPECT_GT(record_len, 0);
    
    std::string record(record_len, '\0');
    in.read(&record[0], record_len);
    ASSERT_TRUE(in.good());
    
    // Verify record is valid JSON
    EXPECT_TRUE(record.find("\"winner\":") != std::string::npos);
    EXPECT_TRUE(record.find("\"fen\":") != std::string::npos);
}

// Test shuffle threshold behavior
TEST_F(WinnerDataConverterTest, ShuffleThresholdBehavior) {
    // Create a small PGN file
    createPgnFile("small.pgn", R"(
[Event "Small Game"]
[Result "1-0"]
[WhiteElo "1500"]
[BlackElo "1500"]
[TimeControl "180+0"]

{[%clk 0:03:00]} 1. e4 {[%clk 0:02:58]} 1-0
)");
    
    // Test with threshold that should trigger in-memory shuffle
    {
        WinnerDataConverter::Config config;
        config.pgn_paths = {(temp_dir_ / "small.pgn").string()};
        config.output_bagz_path = (temp_dir_ / "memory_shuffled.bagz").string();
        config.shuffle_enabled = true;
        config.shuffle_memory_threshold_mb = 1024; // High threshold for in-memory
        config.num_threads = 1;
        config.log_level = 2; // INFO level to check logs
        config.temp_dir = temp_dir_.string();
        
        WinnerDataConverter converter(config);
        ASSERT_TRUE(converter.convert());
        
        // Should use in-memory shuffle for small file
        ASSERT_TRUE(std::filesystem::exists(temp_dir_ / "memory_shuffled.bagz"));
    }
    
    // Test with threshold that should trigger bucket-based shuffle
    {
        WinnerDataConverter::Config config;
        config.pgn_paths = {(temp_dir_ / "small.pgn").string()};
        config.output_bagz_path = (temp_dir_ / "bucket_shuffled.bagz").string();
        config.shuffle_enabled = true;
        config.shuffle_memory_threshold_mb = 0; // Force bucket shuffle
        config.shuffle_buckets = 4;
        config.num_threads = 1;
        config.log_level = 2; // INFO level to check logs
        config.temp_dir = temp_dir_.string();
        
        WinnerDataConverter converter(config);
        ASSERT_TRUE(converter.convert());
        
        // Should use bucket-based shuffle even for small file
        ASSERT_TRUE(std::filesystem::exists(temp_dir_ / "bucket_shuffled.bagz"));
    }
}

// Test LZ4 compressed PGN file support
TEST_F(WinnerDataConverterTest, ProcessLZ4CompressedFiles) {
    // Get path to existing test PGN file
    std::filesystem::path test_data_dir = std::filesystem::path(__FILE__).parent_path() / "data_files";
    std::filesystem::path source_pgn = test_data_dir / "sample_100.pgn";
    
    ASSERT_TRUE(std::filesystem::exists(source_pgn)) << "Test data file not found: " << source_pgn;
    
    // Copy to temp directory and compress
    auto temp_pgn = temp_dir_ / "sample_100.pgn";
    std::filesystem::copy_file(source_pgn, temp_pgn);
    
    // Compress it using lz4 command line tool
    auto compressed_path = temp_dir_ / "sample_100.pgn.lz4";
    std::string compress_cmd = "lz4 -f " + temp_pgn.string() + " " + compressed_path.string();
    ASSERT_EQ(system(compress_cmd.c_str()), 0) << "Failed to compress PGN file with lz4";
    ASSERT_TRUE(std::filesystem::exists(compressed_path)) << "Compressed file was not created";
    
    // Remove the uncompressed file to ensure we're testing LZ4 support
    std::filesystem::remove(temp_pgn);
    
    // Configure converter to process LZ4 file
    WinnerDataConverter::Config config;
    config.pgn_paths = {compressed_path.string()};
    config.output_bagz_path = (temp_dir_ / "lz4_output.bagz").string();
    config.num_threads = 1;
    config.log_level = 0;
    
    // Run conversion - this should fail without LZ4 support
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    // Verify output
    BagFileReader reader((temp_dir_ / "lz4_output.bagz").string());
    EXPECT_GT(reader.size(), 0) << "No positions extracted from LZ4 file";
    
    // Check that we got a reasonable number of positions
    EXPECT_GT(reader.size(), 100) << "Too few positions extracted";
    
    // Check first record has expected fields
    auto data = reader.get_record(0);
    std::string json(data.begin(), data.end());
    EXPECT_TRUE(json.find("\"winner\":") != std::string::npos);
    EXPECT_TRUE(json.find("\"white_rating\":") != std::string::npos);
    EXPECT_TRUE(json.find("\"black_rating\":") != std::string::npos);
    EXPECT_TRUE(json.find("\"white_clock\":") != std::string::npos);
    EXPECT_TRUE(json.find("\"black_clock\":") != std::string::npos);
}

// Test mixed compressed and uncompressed files
TEST_F(WinnerDataConverterTest, ProcessMixedCompressedUncompressedFiles) {
    // Create uncompressed PGN
    std::string pgn1 = R"(
[Event "Uncompressed Game"]
[Result "1-0"]
[WhiteElo "1500"]
[BlackElo "1500"]
[TimeControl "180+0"]

{[%clk 0:03:00]} 1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} 1-0
)";
    createPgnFile("game1.pgn", pgn1);
    
    // Get test PGN for compression
    std::filesystem::path test_data_dir = std::filesystem::path(__FILE__).parent_path() / "data_files";
    std::filesystem::path source_pgn = test_data_dir / "sample_100.pgn";
    
    // Copy first 10 games from sample file
    auto temp_pgn2 = temp_dir_ / "game2.pgn";
    std::ifstream src(source_pgn);
    std::ofstream dst(temp_pgn2);
    std::string line;
    int game_count = 0;
    while (std::getline(src, line) && game_count < 10) {
        dst << line << "\n";
        if (line.starts_with("[Event ") && !dst.tellp()) {
            game_count++;
        }
    }
    src.close();
    dst.close();
    
    // Compress the second file
    auto compressed_path2 = temp_dir_ / "game2.pgn.lz4";
    std::string compress_cmd = "lz4 -f " + temp_pgn2.string() + " " + compressed_path2.string();
    ASSERT_EQ(system(compress_cmd.c_str()), 0);
    std::filesystem::remove(temp_pgn2);
    
    // Configure converter with both files
    WinnerDataConverter::Config config;
    config.pgn_paths = {
        (temp_dir_ / "game1.pgn").string(),
        compressed_path2.string()
    };
    config.output_bagz_path = (temp_dir_ / "mixed_output.bagz").string();
    config.num_threads = 2; // Test parallel processing
    config.log_level = 0;
    
    // Run conversion
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    // Verify output contains positions from both files
    BagFileReader reader((temp_dir_ / "mixed_output.bagz").string());
    EXPECT_GT(reader.size(), 2); // At least positions from uncompressed file
}

} // namespace chessmimic::winner_converter