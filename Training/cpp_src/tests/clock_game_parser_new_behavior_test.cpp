#include <gtest/gtest.h>
#include "../clock_converter/clock_game_parser.hpp"
#include "../clock_converter/clock_position_record.hpp"
#include <vector>

namespace chessmimic::clock_converter {

class ClockGameParserNewBehaviorTest : public testing::Test {
protected:
    ClockGameParser parser;
};

// Test that FEN is the position BEFORE the move and recent_moves includes the current move
TEST_F(ClockGameParserNewBehaviorTest, FENBeforeMoveAndIncludeCurrentMove) {
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

    std::vector<ClockPositionRecord> records;
    bool success = parser.parseGame(pgn, records);
    
    ASSERT_TRUE(success);
    ASSERT_GE(records.size(), 4);
    
    // First record: For White's e4 move
    const auto& record1 = records[0];
    // FEN should be initial position (before e4)
    EXPECT_EQ(record1.fen, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    // recent_moves should include e4
    EXPECT_EQ(record1.recent_moves.size(), 1);
    EXPECT_EQ(record1.recent_moves[0], "e2e4");
    // Clock and thinking time remain the same
    EXPECT_EQ(record1.rating, 1750);  // White's rating
    EXPECT_FLOAT_EQ(record1.player_clock, 180.0f);  // Clock BEFORE move
    EXPECT_FLOAT_EQ(record1.thinking_time, 4.0f);   // Time spent on e4
    
    // Second record: For Black's e5 move
    const auto& record2 = records[1];
    // FEN should be position after e4 (before e5)
    EXPECT_TRUE(record2.fen.find("e4") != std::string::npos || 
                record2.fen.find("4P3") != std::string::npos);
    // recent_moves should include e4 and e5
    EXPECT_EQ(record2.recent_moves.size(), 2);
    EXPECT_EQ(record2.recent_moves[0], "e2e4");
    EXPECT_EQ(record2.recent_moves[1], "e7e5");
    EXPECT_EQ(record2.rating, 1723);  // Black's rating
    EXPECT_FLOAT_EQ(record2.player_clock, 180.0f);  // Clock BEFORE move
    EXPECT_FLOAT_EQ(record2.thinking_time, 5.0f);   // Time spent on e5
    
    // Third record: For White's Nf3 move
    const auto& record3 = records[2];
    // FEN should be position after e5 (before Nf3)
    EXPECT_TRUE(record3.fen.find("e5") != std::string::npos || 
                record3.fen.find("4p3") != std::string::npos);
    // recent_moves should include e4, e5, and Nf3
    EXPECT_EQ(record3.recent_moves.size(), 3);
    EXPECT_EQ(record3.recent_moves[0], "e2e4");
    EXPECT_EQ(record3.recent_moves[1], "e7e5");
    EXPECT_EQ(record3.recent_moves[2], "g1f3");
    EXPECT_EQ(record3.rating, 1750);  // White's rating
    EXPECT_FLOAT_EQ(record3.player_clock, 178.0f);  // Clock BEFORE move (after increment)
    EXPECT_FLOAT_EQ(record3.thinking_time, 5.0f);   // Time spent on Nf3
}

// Test recent moves limit with new behavior
TEST_F(ClockGameParserNewBehaviorTest, RecentMovesLimitWithCurrentMove) {
    std::string pgn = R"([Event "Test"]
[TimeControl "180+2"]
[WhiteElo "1750"]
[BlackElo "1750"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]}
2. Nf3 {[%clk 0:02:55]} Nc6 {[%clk 0:02:54]}
3. Bb5 {[%clk 0:02:50]} a6 {[%clk 0:02:51]}
4. Ba4 {[%clk 0:02:48]} Nf6 {[%clk 0:02:49]}
5. O-O {[%clk 0:02:46]} Be7 {[%clk 0:02:47]}
6. Re1 {[%clk 0:02:44]} b5 {[%clk 0:02:45]}
7. Bb3 {[%clk 0:02:42]} d6 {[%clk 0:02:43]}
*)";

    std::vector<ClockPositionRecord> records;
    bool success = parser.parseGame(pgn, records);
    
    ASSERT_TRUE(success);
    ASSERT_GE(records.size(), 14);
    
    // Check the 14th record (for Black's d6 move)
    const auto& last_record = records[13];
    // Should have 12 moves (including current move d6)
    EXPECT_EQ(last_record.recent_moves.size(), 12);
    
    // The moves should be: Nf3, Nc6, Bb5, a6, Ba4, Nf6, O-O, Be7, Re1, b5, Bb3, d6
    // (moves 3-14, as we can only store 12 moves)
    EXPECT_EQ(last_record.recent_moves[0], "g1f3");   // 2. Nf3
    EXPECT_EQ(last_record.recent_moves[1], "b8c6");   // 2... Nc6
    EXPECT_EQ(last_record.recent_moves[2], "f1b5");   // 3. Bb5
    EXPECT_EQ(last_record.recent_moves[3], "a7a6");   // 3... a6
    EXPECT_EQ(last_record.recent_moves[4], "b5a4");   // 4. Ba4
    EXPECT_EQ(last_record.recent_moves[5], "g8f6");   // 4... Nf6
    EXPECT_EQ(last_record.recent_moves[6], "e1g1");   // 5. O-O
    EXPECT_EQ(last_record.recent_moves[7], "f8e7");   // 5... Be7
    EXPECT_EQ(last_record.recent_moves[8], "f1e1");   // 6. Re1
    EXPECT_EQ(last_record.recent_moves[9], "b7b5");   // 6... b5
    EXPECT_EQ(last_record.recent_moves[10], "a4b3");  // 7. Bb3
    EXPECT_EQ(last_record.recent_moves[11], "d7d6");  // 7... d6 (current move)
}

// Test position key generation with new behavior
TEST_F(ClockGameParserNewBehaviorTest, PositionKeyWithCurrentMove) {
    std::string pgn = R"([Event "Test"]
[TimeControl "180+2"]
[WhiteElo "1750"]
[BlackElo "1750"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]}
2. Nf3 {[%clk 0:02:55]}
*)";

    std::vector<ClockPositionRecord> records;
    bool success = parser.parseGame(pgn, records);
    
    ASSERT_TRUE(success);
    ASSERT_GE(records.size(), 3);
    
    // Check third record (White's Nf3)
    const auto& record = records[2];
    std::string position_key = record.getPositionKey();
    
    // Position key should be: FEN|e2e4,e7e5,g1f3
    // Where FEN is the position before Nf3 (after Black's e5)
    EXPECT_TRUE(position_key.find("|e2e4,e7e5,g1f3") != std::string::npos);
}

// Test that opponent clock tracking still works correctly
TEST_F(ClockGameParserNewBehaviorTest, OpponentClockWithNewBehavior) {
    std::string pgn = R"([Event "Test"]
[TimeControl "60+1"]
[WhiteElo "1800"]
[BlackElo "1750"]

1. e4 {[%clk 0:00:58]} e5 {[%clk 0:00:55]}
2. Nf3 {[%clk 0:00:56]} 
*)";

    std::vector<ClockPositionRecord> records;
    bool success = parser.parseGame(pgn, records);
    
    ASSERT_TRUE(success);
    ASSERT_GE(records.size(), 3);
    
    // First record (White's e4)
    EXPECT_FLOAT_EQ(records[0].opponent_clock, 60.0f);    // Black hasn't moved yet
    
    // Second record (Black's e5)
    EXPECT_FLOAT_EQ(records[1].opponent_clock, 58.0f);    // White's clock after e4
    
    // Third record (White's Nf3)
    EXPECT_FLOAT_EQ(records[2].opponent_clock, 55.0f);    // Black's clock after e5
}

} // namespace chessmimic::clock_converter