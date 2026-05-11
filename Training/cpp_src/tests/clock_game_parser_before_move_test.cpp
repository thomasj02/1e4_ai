#include <gtest/gtest.h>
#include "../clock_converter/clock_game_parser.hpp"

using namespace chessmimic::clock_converter;
using chessmimic::ClockPositionRecord;

class ClockGameParserBeforeMoveTest : public testing::Test {
protected:
    ClockGameParser parser;
};

TEST_F(ClockGameParserBeforeMoveTest, TestPlayerClockIsBeforeMove) {
    // Test that player_clock stores the time BEFORE the move, not after
    std::string pgn = R"([Event "Test"]
[White "Player1"]
[Black "Player2"]
[WhiteElo "1800"]
[BlackElo "1750"]
[TimeControl "180+2"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]}
2. Nf3 {[%clk 0:02:55]} Nc6 {[%clk 0:02:54]}
)";

    std::vector<ClockPositionRecord> records;
    bool success = parser.parseGame(pgn, records);
    ASSERT_TRUE(success);
    ASSERT_GE(records.size(), 4);

    // Check first move (White's e4)
    // Initial time: 180s, after move: 178s (180 - 4 + 2)
    // player_clock should be 180 (before move), not 178 (after move)
    EXPECT_FLOAT_EQ(records[0].player_clock, 180.0f);  // Clock BEFORE move
    EXPECT_FLOAT_EQ(records[0].thinking_time, 4.0f);   // Time spent thinking
    EXPECT_FLOAT_EQ(records[0].increment, 2.0f);
    
    // Verify the relationship: clock_before - thinking_time + increment = clock_after
    // 180 - 4 + 2 = 178 (which matches the PGN annotation)

    // Check second move (Black's e5)
    // Initial time: 180s, after move: 177s (180 - 5 + 2)
    EXPECT_FLOAT_EQ(records[1].player_clock, 180.0f);  // Clock BEFORE move
    EXPECT_FLOAT_EQ(records[1].thinking_time, 5.0f);
    
    // Check third move (White's Nf3)
    // Previous clock: 178s, after move: 175s (178 - 5 + 2)
    EXPECT_FLOAT_EQ(records[2].player_clock, 178.0f);  // Clock BEFORE move
    EXPECT_FLOAT_EQ(records[2].thinking_time, 5.0f);
    
    // Check fourth move (Black's Nc6)
    // Previous clock: 177s, after move: 174s (177 - 5 + 2)
    EXPECT_FLOAT_EQ(records[3].player_clock, 177.0f);  // Clock BEFORE move
    EXPECT_FLOAT_EQ(records[3].thinking_time, 5.0f);
}

TEST_F(ClockGameParserBeforeMoveTest, TestThinkingTimeCalculation) {
    // Test that thinking time is correctly calculated from before/after clocks
    std::string pgn = R"([Event "Test"]
[White "Player1"]
[Black "Player2"]
[WhiteElo "1800"]
[BlackElo "1750"]
[TimeControl "300+5"]

1. d4 {[%clk 0:04:55]} d5 {[%clk 0:04:50]}
)";

    std::vector<ClockPositionRecord> records;
    bool success = parser.parseGame(pgn, records);
    ASSERT_TRUE(success);
    ASSERT_GE(records.size(), 2);

    // White's first move: 300s -> 295s (300 - 10 + 5)
    EXPECT_FLOAT_EQ(records[0].player_clock, 300.0f);     // Before move
    EXPECT_FLOAT_EQ(records[0].thinking_time, 10.0f);     // Thinking time
    EXPECT_FLOAT_EQ(records[0].increment, 5.0f);
    
    // Black's first move: 300s -> 290s (300 - 15 + 5)
    EXPECT_FLOAT_EQ(records[1].player_clock, 300.0f);     // Before move
    EXPECT_FLOAT_EQ(records[1].thinking_time, 15.0f);     // Thinking time
}

TEST_F(ClockGameParserBeforeMoveTest, TestOpponentClockTracking) {
    // Test that opponent clock is correctly tracked
    std::string pgn = R"([Event "Test"]
[White "Player1"]
[Black "Player2"]
[WhiteElo "1800"]
[BlackElo "1750"]
[TimeControl "60+1"]

1. e4 {[%clk 0:00:58]} e5 {[%clk 0:00:55]}
2. Nf3 {[%clk 0:00:56]} 
)";

    std::vector<ClockPositionRecord> records;
    bool success = parser.parseGame(pgn, records);
    ASSERT_TRUE(success);
    ASSERT_GE(records.size(), 3);

    // After White's e4, Black hasn't moved yet
    EXPECT_FLOAT_EQ(records[0].opponent_clock, 60.0f);    // Black's initial time
    
    // After Black's e5, White's clock should be 58s
    EXPECT_FLOAT_EQ(records[1].opponent_clock, 58.0f);    // White's clock after e4
    
    // After White's Nf3, Black's clock should be 55s
    EXPECT_FLOAT_EQ(records[2].opponent_clock, 55.0f);    // Black's clock after e5
}

TEST_F(ClockGameParserBeforeMoveTest, TestZeroIncrementGames) {
    // Test games without increment
    std::string pgn = R"([Event "Test"]
[White "Player1"]
[Black "Player2"]
[WhiteElo "1800"]
[BlackElo "1750"]
[TimeControl "180+0"]

1. e4 {[%clk 0:02:55]} e5 {[%clk 0:02:50]}
)";

    std::vector<ClockPositionRecord> records;
    bool success = parser.parseGame(pgn, records);
    ASSERT_TRUE(success);
    ASSERT_GE(records.size(), 2);

    // White's move: 180s -> 175s (no increment)
    EXPECT_FLOAT_EQ(records[0].player_clock, 180.0f);
    EXPECT_FLOAT_EQ(records[0].thinking_time, 5.0f);
    EXPECT_FLOAT_EQ(records[0].increment, 0.0f);
    
    // Black's move: 180s -> 170s (no increment)
    EXPECT_FLOAT_EQ(records[1].player_clock, 180.0f);
    EXPECT_FLOAT_EQ(records[1].thinking_time, 10.0f);
}

TEST_F(ClockGameParserBeforeMoveTest, TestFENReflectsPositionBeforeMove) {
    // Verify that FEN is the position BEFORE the move
    std::string pgn = R"([Event "Test"]
[White "Player1"]
[Black "Player2"]
[WhiteElo "1800"]
[BlackElo "1750"]
[TimeControl "180+2"]

1. e4 {[%clk 0:02:58]}
)";

    std::vector<ClockPositionRecord> records;
    bool success = parser.parseGame(pgn, records);
    ASSERT_TRUE(success);
    ASSERT_GE(records.size(), 1);

    // FEN should be the initial position (before e4 is played)
    EXPECT_EQ(records[0].fen, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    
    // Clock should be before the move
    EXPECT_FLOAT_EQ(records[0].player_clock, 180.0f);
    
    // Recent moves should include e4
    EXPECT_EQ(records[0].recent_moves.size(), 1);
    EXPECT_EQ(records[0].recent_moves[0], "e2e4");
}