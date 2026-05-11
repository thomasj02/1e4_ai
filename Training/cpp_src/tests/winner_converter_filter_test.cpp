#include <gtest/gtest.h>
#include "../winner_converter/winner_data_converter.hpp"
#include "../winner_converter/winner_position_record.hpp"
#include "../core/bagz.hpp"
#include <filesystem>
#include <fstream>
#include <simdjson.h>

namespace chessmimic::winner_converter {

class WinnerConverterFilterTest : public testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path() / 
                    ("winner_filter_test_" + std::to_string(getpid()));
        std::filesystem::create_directories(temp_dir_);
    }
    
    void TearDown() override {
        std::filesystem::remove_all(temp_dir_);
    }
    
    std::filesystem::path temp_dir_;
};

// Test draw filtering with various draw notations
TEST_F(WinnerConverterFilterTest, DrawFilteringVariations) {
    std::ofstream file(temp_dir_ / "draws.pgn");
    
    // Standard 1/2-1/2 notation
    file << R"([Event "Draw 1"]
[Result "1/2-1/2"]
[WhiteElo "1500"]
[BlackElo "1500"]
[TimeControl "180+0"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} 1/2-1/2

)";
    
    // Alternative ½-½ notation
    file << R"([Event "Draw 2"]
[Result "½-½"]
[WhiteElo "1600"]
[BlackElo "1600"]
[TimeControl "180+0"]

1. d4 {[%clk 0:02:58]} d5 {[%clk 0:02:57]} ½-½

)";
    
    // Draw with 0.5-0.5 notation
    file << R"([Event "Draw 3"]
[Result "0.5-0.5"]
[WhiteElo "1700"]
[BlackElo "1700"]
[TimeControl "180+0"]

1. c4 {[%clk 0:02:58]} c5 {[%clk 0:02:57]} 0.5-0.5

)";
    
    // Decisive game for comparison
    file << R"([Event "Win"]
[Result "1-0"]
[WhiteElo "1800"]
[BlackElo "1800"]
[TimeControl "180+0"]

1. Nf3 {[%clk 0:02:58]} Nf6 {[%clk 0:02:57]} 1-0

)";
    
    file.close();
    
    // Test with draw filter enabled
    {
        WinnerDataConverter::Config config;
        config.pgn_paths = {(temp_dir_ / "draws.pgn").string()};
        config.output_bagz_path = (temp_dir_ / "no_draws.bagz").string();
        config.filter_draws = true;
        config.num_threads = 1;
        config.log_level = 0;
        
        WinnerDataConverter converter(config);
        ASSERT_TRUE(converter.convert());
        
        BagFileReader reader((temp_dir_ / "no_draws.bagz").string());
        
        // Only the decisive game should be included  
        // The game has 2 half-moves with clock annotations: 1. Nf3 {clock} Nf6 {clock}
        // Positions are generated after each move with a clock annotation
        EXPECT_EQ(reader.size(), 2); // 2 moves with clock annotations = 2 positions
        
        // Verify all positions are from decisive games
        simdjson::dom::parser parser;
        for (size_t i = 0; i < reader.size(); ++i) {
            auto data = reader.get_record(i);
            std::string json_str(data.begin(), data.end());
            WinnerPositionRecord record = WinnerPositionRecord::fromJson(json_str, parser);
            EXPECT_NE(record.winner, 0) << "No draws should be present";
        }
    }
}

// Test rating filter edge cases
TEST_F(WinnerConverterFilterTest, RatingFilterEdgeCases) {
    std::ofstream file(temp_dir_ / "ratings.pgn");
    
    // Game at exact minimum rating
    file << R"([Event "Min Rating"]
[Result "1-0"]
[WhiteElo "1600"]
[BlackElo "1400"]
[TimeControl "180+0"]

1. e4 {[%clk 0:02:58]} 1-0

)";
    
    // Game at exact maximum rating
    file << R"([Event "Max Rating"]
[Result "0-1"]
[WhiteElo "2100"]
[BlackElo "1900"]
[TimeControl "180+0"]

1. d4 {[%clk 0:02:58]} 0-1

)";
    
    // Game just below minimum
    file << R"([Event "Below Min"]
[Result "1-0"]
[WhiteElo "1500"]
[BlackElo "1398"]
[TimeControl "180+0"]

1. c4 {[%clk 0:02:58]} 1-0

)";
    
    // Game just above maximum
    file << R"([Event "Above Max"]
[Result "0-1"]
[WhiteElo "2101"]
[BlackElo "1901"]
[TimeControl "180+0"]

1. Nf3 {[%clk 0:02:58]} 0-1

)";
    
    // Game with missing ratings
    file << R"([Event "No Ratings"]
[Result "1/2-1/2"]
[TimeControl "180+0"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} 1/2-1/2

)";
    
    file.close();
    
    WinnerDataConverter::Config config;
    config.pgn_paths = {(temp_dir_ / "ratings.pgn").string()};
    config.output_bagz_path = (temp_dir_ / "rating_filtered.bagz").string();
    config.min_rating = 1500;
    config.max_rating = 2000;
    config.num_threads = 1;
    config.log_level = 0;
    
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    BagFileReader reader((temp_dir_ / "rating_filtered.bagz").string());
    
    // Games 1 and 2 should be included (average ratings 1500 and 2000)
    // Games 3, 4 should be excluded (averages 1449 and 2001)
    // Game 5 has no ratings, defaults to 0, so excluded
    // Each game has only 1 move with clock annotation
    EXPECT_EQ(reader.size(), 2); // 2 games × 1 position each
    
    // Verify ratings in output
    simdjson::dom::parser parser;
    for (size_t i = 0; i < reader.size(); ++i) {
        auto data = reader.get_record(i);
        std::string json_str(data.begin(), data.end());
        WinnerPositionRecord record = WinnerPositionRecord::fromJson(json_str, parser);
        
        int avg_rating = (record.white_rating + record.black_rating) / 2;
        EXPECT_GE(avg_rating, 1500);
        EXPECT_LE(avg_rating, 2000);
    }
}

// Test games without TimeControl header
TEST_F(WinnerConverterFilterTest, MissingTimeControl) {
    std::ofstream file(temp_dir_ / "no_time_control.pgn");
    
    // Game without TimeControl header
    file << R"([Event "No Time Control"]
[Result "1-0"]
[WhiteElo "1700"]
[BlackElo "1700"]

1. e4 {[%clk 0:10:00]} e5 {[%clk 0:09:58]} 1-0

)";
    
    // Game with TimeControl
    file << R"([Event "With Time Control"]
[Result "0-1"]
[WhiteElo "1800"]
[BlackElo "1800"]
[TimeControl "600+10"]

1. d4 {[%clk 0:10:00]} d5 {[%clk 0:09:55]} 0-1

)";
    
    file.close();
    
    WinnerDataConverter::Config config;
    config.pgn_paths = {(temp_dir_ / "no_time_control.pgn").string()};
    config.output_bagz_path = (temp_dir_ / "time_control_output.bagz").string();
    config.num_threads = 1;
    config.log_level = 0;
    
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    BagFileReader reader((temp_dir_ / "time_control_output.bagz").string());
    // Game 1: No TimeControl header = skipped
    // Game 2: Has TimeControl header and 2 moves with clock annotations
    EXPECT_EQ(reader.size(), 2); // 1 valid game × 2 positions
    
    // Check that only the second game is included
    simdjson::dom::parser parser;
    bool found_ten_increment = false;
    
    for (size_t i = 0; i < reader.size(); ++i) {
        auto data = reader.get_record(i);
        std::string json_str(data.begin(), data.end());

        if (WinnerPositionRecord record = WinnerPositionRecord::fromJson(json_str, parser); record.increment == 10.0) found_ten_increment = true;
    }
    
    EXPECT_TRUE(found_ten_increment) << "Game with TimeControl should have correct increment";
}

// Test filtering of unfinished games
TEST_F(WinnerConverterFilterTest, UnfinishedGames) {
    std::ofstream file(temp_dir_ / "unfinished.pgn");
    
    // Unfinished game with * result
    file << R"([Event "Unfinished 1"]
[Result "*"]
[WhiteElo "1500"]
[BlackElo "1500"]
[TimeControl "180+0"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} 2. Nf3 {[%clk 0:02:55]} *

)";
    
    // Game with no result header
    file << R"([Event "No Result"]
[WhiteElo "1600"]
[BlackElo "1600"]
[TimeControl "180+0"]

1. d4 {[%clk 0:02:58]} d5 {[%clk 0:02:57]}

)";
    
    // Game with invalid result
    file << R"([Event "Invalid Result"]
[Result "???"]
[WhiteElo "1700"]
[BlackElo "1700"]
[TimeControl "180+0"]

1. c4 {[%clk 0:02:58]} c5 {[%clk 0:02:57]}

)";
    
    // Valid finished game
    file << R"([Event "Finished"]
[Result "1-0"]
[WhiteElo "1800"]
[BlackElo "1800"]
[TimeControl "180+0"]

1. Nf3 {[%clk 0:02:58]} Nf6 {[%clk 0:02:57]} 1-0

)";
    
    file.close();
    
    WinnerDataConverter::Config config;
    config.pgn_paths = {(temp_dir_ / "unfinished.pgn").string()};
    config.output_bagz_path = (temp_dir_ / "finished_only.bagz").string();
    config.num_threads = 1;
    config.log_level = 0;
    
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    BagFileReader reader((temp_dir_ / "finished_only.bagz").string());
    
    // Only the finished game should be included
    // Game has 2 moves with clock annotations: Nf3 {clock} Nf6 {clock}
    EXPECT_EQ(reader.size(), 2); // 1 finished game × 2 positions
    
    // Verify all positions have valid winners
    simdjson::dom::parser parser;
    for (size_t i = 0; i < reader.size(); ++i) {
        auto data = reader.get_record(i);
        std::string json_str(data.begin(), data.end());
        WinnerPositionRecord record = WinnerPositionRecord::fromJson(json_str, parser);
        
        EXPECT_TRUE(record.winner == -1 || record.winner == 0 || record.winner == 1)
            << "Winner should be valid (-1, 0, or 1)";
    }
}

// Test clock data filtering
TEST_F(WinnerConverterFilterTest, ClockDataPresence) {
    std::ofstream file(temp_dir_ / "clocks.pgn");
    
    // Game with complete clock annotations
    file << R"([Event "Full Clocks"]
[Result "1-0"]
[WhiteElo "1700"]
[BlackElo "1700"]
[TimeControl "300+5"]

1. e4 {[%clk 0:04:58]} e5 {[%clk 0:04:57]} 
2. Nf3 {[%clk 0:04:55]} Nc6 {[%clk 0:04:54]} 1-0

)";
    
    // Game with partial clock annotations
    file << R"([Event "Partial Clocks"]
[Result "0-1"]
[WhiteElo "1800"]
[BlackElo "1800"]
[TimeControl "180+2"]

1. d4 {[%clk 0:02:58]} d5 2. c4 e6 0-1

)";
    
    // Game with no clock annotations
    file << R"([Event "No Clocks"]
[Result "1/2-1/2"]
[WhiteElo "1900"]
[BlackElo "1900"]
[TimeControl "600+0"]

1. c4 c5 2. Nc3 Nc6 1/2-1/2

)";
    
    file.close();
    
    WinnerDataConverter::Config config;
    config.pgn_paths = {(temp_dir_ / "clocks.pgn").string()};
    config.output_bagz_path = (temp_dir_ / "clock_output.bagz").string();
    config.num_threads = 1;
    config.log_level = 0;
    
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    BagFileReader reader((temp_dir_ / "clock_output.bagz").string());
    
    // Game 1: Has initial clock + 4 moves with clocks = 4 positions
    // Game 2: No initial clock, 1 move with clock = 1 position  
    // Game 3: No clocks at all = excluded (requires TimeControl + at least one clock)
    EXPECT_EQ(reader.size(), 5); // 4 + 1 = 5 positions total
    
    // Check clock data variety
    simdjson::dom::parser parser;
    bool found_with_clocks = false;
    
    for (size_t i = 0; i < reader.size(); ++i) {
        auto data = reader.get_record(i);
        std::string json_str(data.begin(), data.end());

        // All positions should have clock data since games without clocks are excluded
        if (WinnerPositionRecord record = WinnerPositionRecord::fromJson(json_str, parser); record.white_clock > 0 && record.black_clock > 0) {
            found_with_clocks = true;
        }
    }
    
    EXPECT_TRUE(found_with_clocks) << "Should have positions with clock data";
    // Note: Games without clock annotations are excluded entirely by WinnerGameParser
}

// Test multiple filter combinations
TEST_F(WinnerConverterFilterTest, MultipleFilterInteraction) {
    std::ofstream file(temp_dir_ / "multi_filter.pgn");
    
    // Draw + Low rating (filtered by both)
    file << R"([Event "Draw Low"]
[Result "1/2-1/2"]
[WhiteElo "1200"]
[BlackElo "1200"]
[TimeControl "180+0"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} 1/2-1/2

)";
    
    // Draw + Good rating (filtered by draw)
    file << R"([Event "Draw Good"]
[Result "1/2-1/2"]
[WhiteElo "1700"]
[BlackElo "1700"]
[TimeControl "180+0"]

1. d4 {[%clk 0:02:58]} d5 {[%clk 0:02:57]} 1/2-1/2

)";
    
    // Win + Low rating (filtered by rating)
    file << R"([Event "Win Low"]
[Result "1-0"]
[WhiteElo "1300"]
[BlackElo "1300"]
[TimeControl "180+0"]

1. c4 {[%clk 0:02:58]} c5 {[%clk 0:02:57]} 1-0

)";
    
    // Win + Good rating (passes all filters)
    file << R"([Event "Win Good"]
[Result "0-1"]
[WhiteElo "1800"]
[BlackElo "1800"]
[TimeControl "180+0"]

1. Nf3 {[%clk 0:02:58]} Nf6 {[%clk 0:02:57]} 0-1

)";
    
    // Win + High rating (filtered by rating)
    file << R"([Event "Win High"]
[Result "1-0"]
[WhiteElo "2500"]
[BlackElo "2500"]
[TimeControl "180+0"]

1. e4 {[%clk 0:02:58]} c5 {[%clk 0:02:57]} 1-0

)";
    
    file.close();
    
    WinnerDataConverter::Config config;
    config.pgn_paths = {(temp_dir_ / "multi_filter.pgn").string()};
    config.output_bagz_path = (temp_dir_ / "multi_filtered.bagz").string();
    config.filter_draws = true;
    config.min_rating = 1500;
    config.max_rating = 2000;
    config.num_threads = 1;
    config.log_level = 0;
    
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    BagFileReader reader((temp_dir_ / "multi_filtered.bagz").string());
    
    // Only "Win Good" should pass all filters
    // Game has 2 moves with clock annotations: Nf3 {clock} Nf6 {clock}
    EXPECT_EQ(reader.size(), 2); // 1 game × 2 positions
    
    // Verify the included game
    simdjson::dom::parser parser;
    for (size_t i = 0; i < reader.size(); ++i) {
        auto data = reader.get_record(i);
        std::string json_str(data.begin(), data.end());
        WinnerPositionRecord record = WinnerPositionRecord::fromJson(json_str, parser);
        
        EXPECT_EQ(record.winner, -1) << "Should be black win";
        EXPECT_EQ(record.white_rating, 1800);
        EXPECT_EQ(record.black_rating, 1800);
    }
}

// Test that games with missing Elo headers are skipped
TEST_F(WinnerConverterFilterTest, MissingEloHeaders) {
    std::ofstream file(temp_dir_ / "missing_elo.pgn");
    
    // Game with only WhiteElo (missing BlackElo - should be skipped)
    file << R"([Event "White Elo Only"]
[Result "1-0"]
[WhiteElo "1700"]
[TimeControl "180+0"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} 1-0

)";
    
    // Game with only BlackElo (missing WhiteElo - should be skipped)
    file << R"([Event "Black Elo Only"]
[Result "0-1"]
[BlackElo "1800"]
[TimeControl "180+0"]

1. d4 {[%clk 0:02:58]} d5 {[%clk 0:02:57]} 0-1

)";
    
    // Game with no Elo headers (both missing - should be skipped)
    file << R"([Event "No Elo"]
[Result "1/2-1/2"]
[TimeControl "180+0"]

1. c4 {[%clk 0:02:58]} c5 {[%clk 0:02:57]} 1/2-1/2

)";
    
    // Game with both Elo headers (should be included)
    file << R"([Event "Both Elos"]
[Result "1-0"]
[WhiteElo "1600"]
[BlackElo "1650"]
[TimeControl "180+0"]

1. Nf3 {[%clk 0:02:58]} Nf6 {[%clk 0:02:57]} 1-0

)";
    
    file.close();
    
    WinnerDataConverter::Config config;
    config.pgn_paths = {(temp_dir_ / "missing_elo.pgn").string()};
    config.output_bagz_path = (temp_dir_ / "elo_defaults.bagz").string();
    config.min_rating = 0; // Include games with any ratings including 0
    config.num_threads = 1;
    config.log_level = 0;
    
    WinnerDataConverter converter(config);
    ASSERT_TRUE(converter.convert());
    
    BagFileReader reader((temp_dir_ / "elo_defaults.bagz").string());
    
    // Only the game with both Elo headers should be included
    // Games with missing Elo headers are skipped by WinnerGameParser
    // The valid game has 2 moves with clock annotations
    EXPECT_EQ(reader.size(), 2); // 1 valid game × 2 positions
    
    // Verify only the valid game is included
    simdjson::dom::parser parser;
    for (size_t i = 0; i < reader.size(); ++i) {
        auto data = reader.get_record(i);
        std::string json_str(data.begin(), data.end());
        WinnerPositionRecord record = WinnerPositionRecord::fromJson(json_str, parser);
        
        // Only the game with both Elo headers is included
        // This game has WhiteElo="1600" and BlackElo="1650"
        EXPECT_EQ(record.white_rating, 1600);
        EXPECT_EQ(record.black_rating, 1650);
    }
}

} // namespace chessmimic::winner_converter