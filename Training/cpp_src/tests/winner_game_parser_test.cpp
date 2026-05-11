#include <gtest/gtest.h>
#include "../winner_converter/winner_game_parser.hpp"
#include "../winner_converter/winner_position_record.hpp"
#include <sstream>
#include <vector>

namespace chessmimic::winner_converter {

class WinnerGameParserTest : public testing::Test {
protected:
    WinnerGameParser parser;
};

// Test parsing basic PGN with white win
TEST_F(WinnerGameParserTest, ParseWhiteWinWithClocks) {
    std::string pgn = R"([Event "Test Game"]
[White "Player1"]
[Black "Player2"]
[Result "1-0"]
[TimeControl "180+2"]
[WhiteElo "1750"]
[BlackElo "1723"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]}
2. Nf3 {[%clk 0:02:55]} Nc6 {[%clk 0:02:54]}
1-0)";

    std::vector<WinnerPositionRecord> records;
    bool success = parser.parseGame(pgn, records);
    
    ASSERT_TRUE(success);
    ASSERT_EQ(records.size(), 4);  // 4 positions after 4 half-moves
    
    // Check all positions have winner = 1 (white wins)
    for (const auto& record : records) {
        EXPECT_EQ(record.winner, 1);
        EXPECT_EQ(record.increment, 2.0);
    }
    
    // Check first position (after 1. e4)
    const auto& record1 = records[0];
    EXPECT_EQ(record1.white_rating, 1750);
    EXPECT_EQ(record1.black_rating, 1723);
    EXPECT_DOUBLE_EQ(record1.white_clock, 178.0);  // 180 - 2 = 178
    EXPECT_DOUBLE_EQ(record1.black_clock, 180.0);  // Black hasn't moved yet
    EXPECT_EQ(record1.recent_moves.size(), 1);
    EXPECT_EQ(record1.recent_moves[0], "e2e4");
}

// Test parsing black win
TEST_F(WinnerGameParserTest, ParseBlackWin) {
    std::string pgn = R"([Event "Test"]
[Result "0-1"]
[TimeControl "300+0"]
[WhiteElo "1600"]
[BlackElo "1650"]

1. d4 {[%clk 0:04:58]} d5 {[%clk 0:04:57]}
2. c4 {[%clk 0:04:55]} e6 {[%clk 0:04:55]}
0-1)";

    std::vector<WinnerPositionRecord> records;
    bool success = parser.parseGame(pgn, records);
    
    ASSERT_TRUE(success);
    ASSERT_FALSE(records.empty());
    
    // Check all positions have winner = -1 (black wins)
    for (const auto& record : records) {
        EXPECT_EQ(record.winner, -1);
        EXPECT_EQ(record.increment, 0.0);
    }
}

// Test parsing draw
TEST_F(WinnerGameParserTest, ParseDraw) {
    std::string pgn = R"([Event "Test"]
[Result "1/2-1/2"]
[TimeControl "180+2"]
[WhiteElo "1800"]
[BlackElo "1800"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:58]}
1/2-1/2)";

    std::vector<WinnerPositionRecord> records;
    bool success = parser.parseGame(pgn, records);
    
    ASSERT_TRUE(success);
    ASSERT_EQ(records.size(), 2);
    
    // Check all positions have winner = 0 (draw)
    for (const auto& record : records) {
        EXPECT_EQ(record.winner, 0);
    }
}

// Test parsing game without result
TEST_F(WinnerGameParserTest, ParseNoResult) {
    std::string pgn = R"([Event "Test"]
[Result "*"]
[TimeControl "180+2"]
[WhiteElo "1700"]
[BlackElo "1700"]

1. e4 e5
*)";

    std::vector<WinnerPositionRecord> records;
    bool success = parser.parseGame(pgn, records);
    
    // Should fail because result is not conclusive
    EXPECT_FALSE(success);
}

// Test parsing game without time control
TEST_F(WinnerGameParserTest, ParseNoTimeControl) {
    std::string pgn = R"([Event "Test"]
[Result "1-0"]
[WhiteElo "1700"]
[BlackElo "1700"]

1. e4 e5
1-0)";

    std::vector<WinnerPositionRecord> records;
    bool success = parser.parseGame(pgn, records);
    
    // Should fail because no time control
    EXPECT_FALSE(success);
}

// Test parsing game without clock annotations
TEST_F(WinnerGameParserTest, ParseNoClockAnnotations) {
    std::string pgn = R"([Event "Test"]
[Result "1-0"]
[TimeControl "180+2"]
[WhiteElo "1700"]
[BlackElo "1700"]

1. e4 e5
2. Nf3 Nc6
1-0)";

    std::vector<WinnerPositionRecord> records;
    bool success = parser.parseGame(pgn, records);
    
    // Should fail because no clock annotations
    EXPECT_FALSE(success);
}

// Test recent moves tracking
TEST_F(WinnerGameParserTest, RecentMovesTracking) {
    std::string pgn = R"([Event "Test"]
[Result "1-0"]
[TimeControl "180+2"]
[WhiteElo "1700"]
[BlackElo "1700"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]}
2. Nf3 {[%clk 0:02:55]} Nc6 {[%clk 0:02:54]}
3. Bb5 {[%clk 0:02:50]} a6 {[%clk 0:02:51]}
1-0)";

    std::vector<WinnerPositionRecord> records;
    bool success = parser.parseGame(pgn, records);
    
    ASSERT_TRUE(success);
    ASSERT_EQ(records.size(), 6);
    
    // Check recent moves for position after 3... a6
    // With default of 12 moves, we should have all 6 moves including the latest
    const auto& last_record = records[5];
    EXPECT_EQ(last_record.recent_moves.size(), 6);
    EXPECT_EQ(last_record.recent_moves[0], "e2e4");
    EXPECT_EQ(last_record.recent_moves[1], "e7e5");
    EXPECT_EQ(last_record.recent_moves[2], "g1f3");
    EXPECT_EQ(last_record.recent_moves[3], "b8c6");
    EXPECT_EQ(last_record.recent_moves[4], "f1b5");
    EXPECT_EQ(last_record.recent_moves[5], "a7a6");
}

// Test stream parsing with multiple games
TEST_F(WinnerGameParserTest, ParseStream) {
    std::stringstream stream(R"([Event "Game 1"]
[Result "1-0"]
[TimeControl "180+2"]
[WhiteElo "1700"]
[BlackElo "1700"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]}
1-0

[Event "Game 2"]
[Result "0-1"]
[TimeControl "180+2"]
[WhiteElo "1800"]
[BlackElo "1850"]

1. d4 {[%clk 0:02:58]} d5 {[%clk 0:02:57]}
0-1

[Event "Game 3"]
[Result "1/2-1/2"]
[TimeControl "300+0"]
[WhiteElo "2000"]
[BlackElo "2000"]

1. Nf3 {[%clk 0:04:58]} Nf6 {[%clk 0:04:57]}
1/2-1/2)");

    size_t game_count = 0;
    std::vector<int> winners;
    
    parser.parseStream(stream, [&](std::vector<WinnerPositionRecord>&& records) {
        game_count++;
        if (!records.empty()) {
            winners.push_back(records[0].winner);
        }
    });
    
    EXPECT_EQ(game_count, 3);
    ASSERT_EQ(winners.size(), 3);
    EXPECT_EQ(winners[0], 1);    // White win
    EXPECT_EQ(winners[1], -1);   // Black win
    EXPECT_EQ(winners[2], 0);    // Draw
}

// Test maximum recent moves configuration
TEST_F(WinnerGameParserTest, MaxRecentMovesConfig) {
    parser.setMaxRecentMoves(2);  // Only track 2 recent moves
    
    std::string pgn = R"([Event "Test"]
[Result "1-0"]
[TimeControl "180+2"]
[WhiteElo "1700"]
[BlackElo "1700"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]}
2. Nf3 {[%clk 0:02:55]} Nc6 {[%clk 0:02:54]}
3. Bb5 {[%clk 0:02:50]} a6 {[%clk 0:02:51]}
1-0)";

    std::vector<WinnerPositionRecord> records;
    bool success = parser.parseGame(pgn, records);
    
    ASSERT_TRUE(success);
    
    // Check that only 2 recent moves are tracked
    const auto& last_record = records.back();
    EXPECT_EQ(last_record.recent_moves.size(), 2);
    EXPECT_EQ(last_record.recent_moves[0], "f1b5");
    EXPECT_EQ(last_record.recent_moves[1], "a7a6");
}

// Test that recent_moves includes the latest move that led to the current position
TEST_F(WinnerGameParserTest, RecentMovesIncludesLatestMove) {
    std::string pgn = R"([Event "Test"]
[Result "1-0"]
[TimeControl "180+2"]
[WhiteElo "1700"]
[BlackElo "1700"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]}
2. Nf3 {[%clk 0:02:55]} Nc6 {[%clk 0:02:54]}
3. Bb5 {[%clk 0:02:50]} a6 {[%clk 0:02:51]}
1-0)";

    std::vector<WinnerPositionRecord> records;
    bool success = parser.parseGame(pgn, records);
    
    ASSERT_TRUE(success);
    ASSERT_EQ(records.size(), 6);
    
    // Check positions and their recent moves
    // Position after 1. e4
    EXPECT_EQ(records[0].recent_moves.size(), 1);
    EXPECT_EQ(records[0].recent_moves[0], "e2e4");
    
    // Position after 1... e5
    EXPECT_EQ(records[1].recent_moves.size(), 2);
    EXPECT_EQ(records[1].recent_moves[0], "e2e4");
    EXPECT_EQ(records[1].recent_moves[1], "e7e5");
    
    // Position after 2. Nf3
    EXPECT_EQ(records[2].recent_moves.size(), 3);
    EXPECT_EQ(records[2].recent_moves[0], "e2e4");
    EXPECT_EQ(records[2].recent_moves[1], "e7e5");
    EXPECT_EQ(records[2].recent_moves[2], "g1f3");
    
    // Position after 2... Nc6
    EXPECT_EQ(records[3].recent_moves.size(), 4);
    EXPECT_EQ(records[3].recent_moves[0], "e2e4");
    EXPECT_EQ(records[3].recent_moves[1], "e7e5");
    EXPECT_EQ(records[3].recent_moves[2], "g1f3");
    EXPECT_EQ(records[3].recent_moves[3], "b8c6");
    
    // Position after 3. Bb5
    EXPECT_EQ(records[4].recent_moves.size(), 5);
    EXPECT_EQ(records[4].recent_moves[0], "e2e4");
    EXPECT_EQ(records[4].recent_moves[1], "e7e5");
    EXPECT_EQ(records[4].recent_moves[2], "g1f3");
    EXPECT_EQ(records[4].recent_moves[3], "b8c6");
    EXPECT_EQ(records[4].recent_moves[4], "f1b5");
    
    // Position after 3... a6 - This should include the latest move a7a6
    EXPECT_EQ(records[5].recent_moves.size(), 6);
    EXPECT_EQ(records[5].recent_moves[0], "e2e4");
    EXPECT_EQ(records[5].recent_moves[1], "e7e5");
    EXPECT_EQ(records[5].recent_moves[2], "g1f3");
    EXPECT_EQ(records[5].recent_moves[3], "b8c6");
    EXPECT_EQ(records[5].recent_moves[4], "f1b5");
    EXPECT_EQ(records[5].recent_moves[5], "a7a6");  // The latest move should be included!
}

} // namespace chessmimic::winner_converter