#include <gtest/gtest.h>
#include "../clock_converter/clock_game_parser.hpp"
#include "../clock_converter/clock_position_record.hpp"
#include <sstream>
#include <vector>

namespace chessmimic::clock_converter {
class ClockGameParserTest : public testing::Test {
  protected:
    ClockGameParser parser;
};

// Test parsing basic PGN with clock annotations
TEST_F(ClockGameParserTest, ParseBasicPGNWithClocks) {
  std::string pgn = R"([Event "Test Game"]
[White "Player1"]
[Black "Player2"]
[Result "1-0"]
[TimeControl "180+2"]
[WhiteElo "1750"]
[BlackElo "1723"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]}
2. Nf3 {[%clk 0:02:55]} Nc6 {[%clk 0:02:54]}
3. Bb5 {[%clk 0:02:50]} a6 {[%clk 0:02:51]}
1-0)";

  std::vector<ClockPositionRecord> records;
  bool success = parser.parseGame(pgn, records);

  ASSERT_TRUE(success);
  ASSERT_FALSE(records.empty());

  // For 1. e4
  const auto& record1 = records[0];
  EXPECT_EQ(record1.rating, 1750); // White's rating
  EXPECT_FLOAT_EQ(record1.player_clock, 180.0f); // Clock BEFORE move (initial)
  EXPECT_FLOAT_EQ(record1.opponent_clock, 180.0f); // Initial time
  EXPECT_FLOAT_EQ(record1.increment, 2.0f);
  EXPECT_FLOAT_EQ(record1.thinking_time, 4.0f); // 180 + 2 - 178 = 4
  EXPECT_EQ(record1.recent_moves.size(), 1); // Includes current move e4
  EXPECT_EQ(record1.recent_moves[0], "e2e4");
  EXPECT_EQ(record1.fen, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"); // Initial position

  // For 1... e5
  const auto& record2 = records[1];
  EXPECT_EQ(record2.rating, 1723); // Black's rating
  EXPECT_FLOAT_EQ(record2.player_clock, 180.0f); // Clock BEFORE move (initial)
  EXPECT_FLOAT_EQ(record2.opponent_clock, 178.0f); // White's clock after e4
  EXPECT_FLOAT_EQ(record2.thinking_time, 5.0f); // 180 + 2 - 177 = 5
  EXPECT_EQ(record2.recent_moves.size(), 2); // Includes e4 and e5
  EXPECT_EQ(record2.recent_moves[0], "e2e4");
  EXPECT_EQ(record2.recent_moves[1], "e7e5");
}

// Test different clock formats
TEST_F(ClockGameParserTest, ParseDifferentClockFormats) {
  std::string pgn = R"([Event "Test"]
[TimeControl "300+0"]
[WhiteElo "1800"]
[BlackElo "1850"]

1. d4 {[%clk 4:58]} d5 {[%clk 0:04:57]}
2. c4 {[%clk 4:55.5]} e6 {[%clk 297.0]}
3. Nc3 {[%clk 293]} Nf6 {[%clk 4:55]}
*)";

  std::vector<ClockPositionRecord> records;
  bool success = parser.parseGame(pgn, records);

  ASSERT_TRUE(success);
  ASSERT_GE(records.size(), 6);

  // Test M:SS format - White's d4
  EXPECT_FLOAT_EQ(records[0].player_clock, 300.0f); // Clock BEFORE move (initial)

  // Test H:MM:SS format - Black's d5
  EXPECT_FLOAT_EQ(records[1].player_clock, 300.0f); // Clock BEFORE move (initial)

  // Test M:SS.s format - White's c4
  EXPECT_FLOAT_EQ(records[2].player_clock, 298.0f); // Clock BEFORE move (after d4)

  // Test pure seconds format - Black's e6
  EXPECT_FLOAT_EQ(records[3].player_clock, 297.0f); // Clock BEFORE move (after d5)

  // Test integer seconds - White's Nc3
  EXPECT_FLOAT_EQ(records[4].player_clock, 295.5f); // Clock BEFORE move (after c4)
}

// Test missing clock data handling
TEST_F(ClockGameParserTest, HandleMissingClockData) {
  std::string pgn = R"([Event "Test"]
[TimeControl "180+2"]
[WhiteElo "1700"]
[BlackElo "1700"]

1. e4 e5
2. Nf3 {[%clk 0:02:55]} Nc6 {[%clk 0:02:54]}
3. Bb5 a6
1-0)";

  std::vector<ClockPositionRecord> records;
  bool success = parser.parseGame(pgn, records);

  ASSERT_TRUE(success);
  // Should only have records for moves with clock data
  EXPECT_EQ(records.size(), 2); // Only moves 2.Nf3 and 2...Nc6

  // First record should be for 2.Nf3
  const auto& record1 = records[0];
  EXPECT_FLOAT_EQ(record1.player_clock, 180.0f); // Clock BEFORE move (initial)
  EXPECT_EQ(record1.recent_moves.size(), 3); // e4, e5, Nf3
}

// Test extraction of time control and increment
TEST_F(ClockGameParserTest, ExtractTimeControlAndIncrement) {
  struct TestCase {
    std::string time_control;
    float expected_initial;
    float expected_increment;
    std::string clock_time;
    float expected_clock_seconds;
  };

  std::vector<TestCase> test_cases = {
    {"180+2", 180.0f, 2.0f, "0:02:58", 178.0f},
    {"300+0", 300.0f, 0.0f, "0:04:59", 299.0f},
    {"600+5", 600.0f, 5.0f, "0:09:58", 598.0f},
    {"5400+30", 5400.0f, 30.0f, "1:29:58", 5398.0f},
    {"60+1", 60.0f, 1.0f, "0:00:58", 58.0f}
  };

  for (const auto& [time_control, expected_initial, expected_increment, clock_time, expected_clock_seconds]:
       test_cases) {
    std::string pgn = "[TimeControl \"" + time_control + "\"]\n"
                      "[WhiteElo \"1750\"]\n"
                      "[BlackElo \"1750\"]\n\n"
                      "1. e4 {[%clk " + clock_time + "]} *";

    std::vector<ClockPositionRecord> records;
    bool success = parser.parseGame(pgn, records);

    ASSERT_TRUE(success);
    ASSERT_FALSE(records.empty());
    EXPECT_FLOAT_EQ(records[0].increment, expected_increment);
    EXPECT_FLOAT_EQ(records[0].player_clock, expected_initial); // Clock BEFORE move

    // Verify thinking time calculation
    float expected_thinking = expected_initial + expected_increment - expected_clock_seconds;
    EXPECT_FLOAT_EQ(records[0].thinking_time, expected_thinking);
  }
}

// Test default recent moves tracking (12 moves)
TEST_F(ClockGameParserTest, TrackRecentMovesDefault) {
  std::string pgn = R"([Event "Test Default"]
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
  // Don't set max recent moves - use default of 12
  bool success = parser.parseGame(pgn, records);

  ASSERT_TRUE(success);
  ASSERT_GE(records.size(), 14);

  // Check recent moves for 7...d6 (move 14)
  const auto& last_record = records[13];
  EXPECT_EQ(last_record.recent_moves.size(), 12); // Should have 12 moves

  // Verify the last 12 moves are tracked correctly (moves 3-14, including d6)
  EXPECT_EQ(last_record.recent_moves[0], "g1f3"); // 2. Nf3
  EXPECT_EQ(last_record.recent_moves[1], "b8c6"); // 2... Nc6
  EXPECT_EQ(last_record.recent_moves[2], "f1b5"); // 3. Bb5
  EXPECT_EQ(last_record.recent_moves[3], "a7a6"); // 3... a6
  EXPECT_EQ(last_record.recent_moves[4], "b5a4"); // 4. Ba4
  EXPECT_EQ(last_record.recent_moves[5], "g8f6"); // 4... Nf6
  EXPECT_EQ(last_record.recent_moves[6], "e1g1"); // 5. O-O
  EXPECT_EQ(last_record.recent_moves[7], "f8e7"); // 5... Be7
  EXPECT_EQ(last_record.recent_moves[8], "f1e1"); // 6. Re1
  EXPECT_EQ(last_record.recent_moves[9], "b7b5"); // 6... b5
  EXPECT_EQ(last_record.recent_moves[10], "a4b3"); // 7. Bb3
  EXPECT_EQ(last_record.recent_moves[11], "d7d6"); // 7... d6 (current move)
}

// Test tracking recent moves with custom limit
TEST_F(ClockGameParserTest, TrackRecentMovesCustom) {
  std::string pgn = R"([Event "Test"]
[TimeControl "180+2"]
[WhiteElo "1750"]
[BlackElo "1750"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]}
2. Nf3 {[%clk 0:02:55]} Nc6 {[%clk 0:02:54]}
3. Bb5 {[%clk 0:02:50]} a6 {[%clk 0:02:51]}
4. Ba4 {[%clk 0:02:48]} Nf6 {[%clk 0:02:49]}
*)";

  std::vector<ClockPositionRecord> records;
  parser.setMaxRecentMoves(3); // Configure to track last 3 moves (testing non-default)
  bool success = parser.parseGame(pgn, records);

  ASSERT_TRUE(success);
  ASSERT_GE(records.size(), 8);

  // Check recent moves for 4...Nf6 (including the current move)
  const auto& last_record = records[7];
  EXPECT_EQ(last_record.recent_moves.size(), 3);
  EXPECT_EQ(last_record.recent_moves[0], "a7a6"); // 3... a6
  EXPECT_EQ(last_record.recent_moves[1], "b5a4"); // 4. Ba4
  EXPECT_EQ(last_record.recent_moves[2], "g8f6"); // 4... Nf6 (current move)
}

// Test FEN generation including move numbers
TEST_F(ClockGameParserTest, FENIncludesMoveNumbers) {
  std::string pgn = R"([Event "Test"]
[TimeControl "180+2"]
[WhiteElo "1750"]
[BlackElo "1750"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]}
2. Nf3 {[%clk 0:02:55]} *)";

  std::vector<ClockPositionRecord> records;
  bool success = parser.parseGame(pgn, records);

  ASSERT_TRUE(success);
  ASSERT_GE(records.size(), 3);

  // Check FEN includes proper move numbers
  // Before 1. e4, it's White's turn, halfmove clock = 0, fullmove = 1
  EXPECT_TRUE(records[0].fen.find(" 0 1") != std::string::npos) << "FEN: " << records[0].fen;
  // Before 1... e5 (after e4), it's Black's turn, halfmove clock = 0, fullmove = 1
  EXPECT_TRUE(records[1].fen.find(" 0 1") != std::string::npos) << "FEN: " << records[1].fen;
  // Before 2. Nf3 (after e5), it's White's turn, halfmove clock = 0, fullmove = 2
  EXPECT_TRUE(records[2].fen.find(" 0 2") != std::string::npos) << "FEN: " << records[2].fen;
}

// Test handling of games without time control
TEST_F(ClockGameParserTest, HandleMissingTimeControl) {
  std::string pgn = R"([Event "Test"]
[WhiteElo "1750"]
[BlackElo "1750"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]}
*)";

  std::vector<ClockPositionRecord> records;
  bool success = parser.parseGame(pgn, records);

  // Should fail or return empty when no time control
  EXPECT_FALSE(success) << "Should fail without time control";
}

// Test handling of games without ratings
TEST_F(ClockGameParserTest, HandleMissingRatings) {
  std::string pgn = R"([Event "Test"]
[TimeControl "180+2"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]}
*)";

  std::vector<ClockPositionRecord> records;
  bool success = parser.parseGame(pgn, records);

  // Should fail or return empty when no ratings
  EXPECT_FALSE(success) << "Should fail without ratings";
}

// Test clock state tracking throughout game
TEST_F(ClockGameParserTest, TrackClockStateThroughGame) {
  std::string pgn = R"([Event "Test"]
[TimeControl "60+1"]
[WhiteElo "1750"]
[BlackElo "1750"]

1. e4 {[%clk 0:00:58]} e5 {[%clk 0:00:57]}
2. Nf3 {[%clk 0:00:56]} Nc6 {[%clk 0:00:55]}
*)";

  std::vector<ClockPositionRecord> records;
  bool success = parser.parseGame(pgn, records);

  ASSERT_TRUE(success);
  ASSERT_EQ(records.size(), 4);

  // Verify opponent clock tracking
  EXPECT_FLOAT_EQ(records[0].opponent_clock, 60.0f); // Black hasn't moved yet
  EXPECT_FLOAT_EQ(records[1].opponent_clock, 58.0f); // White's clock after e4
  EXPECT_FLOAT_EQ(records[2].opponent_clock, 57.0f); // Black's clock after e5
  EXPECT_FLOAT_EQ(records[3].opponent_clock, 56.0f); // White's clock after Nf3
}

// Test parseStream for processing multiple games
TEST_F(ClockGameParserTest, ParseMultipleGamesFromStream) {
  std::stringstream stream(R"([Event "Game 1"]
[TimeControl "180+2"]
[WhiteElo "1750"]
[BlackElo "1750"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} *

[Event "Game 2"]
[TimeControl "300+0"]
[WhiteElo "1800"]
[BlackElo "1850"]

1. d4 {[%clk 0:04:58]} d5 {[%clk 0:04:57]} *)");

  std::vector<ClockPositionRecord> all_records;
  parser.parseStream(stream, [&all_records](std::vector<ClockPositionRecord>&& records) {
    all_records.insert(all_records.end(),
                       std::make_move_iterator(records.begin()),
                       std::make_move_iterator(records.end()));
  });

  ASSERT_GE(all_records.size(), 4);

  // Check records from first game
  EXPECT_EQ(all_records[0].rating, 1750);
  EXPECT_FLOAT_EQ(all_records[0].increment, 2.0f);

  // Check records from second game
  EXPECT_EQ(all_records[2].rating, 1800);
  EXPECT_FLOAT_EQ(all_records[2].increment, 0.0f);
}

// Test with real PGN format from sample file
TEST_F(ClockGameParserTest, ParseRealPGNFormat) {
  // Real game from lichess with actual formatting
  std::string pgn = R"([Event "Rated Blitz game"]
[Site "https://lichess.org/yrxai40U"]
[Date "2024.11.01"]
[Round "-"]
[White "Sergio7203"]
[Black "cardus76"]
[Result "0-1"]
[UTCDate "2024.11.01"]
[UTCTime "00:00:24"]
[WhiteElo "1741"]
[BlackElo "1782"]
[WhiteRatingDiff "-5"]
[BlackRatingDiff "+5"]
[ECO "B20"]
[Opening "Sicilian Defense: Bowdler Attack"]
[TimeControl "300+0"]
[Termination "Normal"]

1. e4 { [%clk 0:05:00] } 1... c5 { [%clk 0:05:00] } 2. Bc4 { [%clk 0:04:59] } 2... Nc6 { [%clk 0:04:59] } 3. Bxf7+ { [%clk 0:04:58] } 3... Kxf7 { [%clk 0:04:57] } *)";

  std::vector<ClockPositionRecord> records;
  bool success = parser.parseGame(pgn, records);

  ASSERT_TRUE(success);
  ASSERT_EQ(records.size(), 6); // 6 moves with clock data

  // Check first move
  const auto& record1 = records[0];
  EXPECT_EQ(record1.rating, 1741); // White's rating
  EXPECT_FLOAT_EQ(record1.player_clock, 300.0f); // 5:00
  EXPECT_FLOAT_EQ(record1.opponent_clock, 300.0f); // Black hasn't moved
  EXPECT_FLOAT_EQ(record1.increment, 0.0f); // 300+0
  EXPECT_FLOAT_EQ(record1.thinking_time, 0.0f); // 300 - 300 = 0

  // Check Black's first move
  const auto& record2 = records[1];
  EXPECT_EQ(record2.rating, 1782); // Black's rating
  EXPECT_FLOAT_EQ(record2.player_clock, 300.0f); // 5:00
  EXPECT_FLOAT_EQ(record2.opponent_clock, 300.0f); // White's clock
  EXPECT_FLOAT_EQ(record2.thinking_time, 0.0f);

  // Check a later move with actual time used
  const auto& record5 = records[4]; // 3. Bxf7+
  EXPECT_EQ(record5.rating, 1741); // White's rating
  EXPECT_FLOAT_EQ(record5.player_clock, 299.0f); // Clock BEFORE move
  EXPECT_FLOAT_EQ(record5.thinking_time, 1.0f); // Lost 1 second (299 - 298)
}

// Test clock increase filtering - normal increment behavior (should be kept)
TEST_F(ClockGameParserTest, ClockIncreaseFilteringNormalIncrement) {
  std::string pgn = R"([Event "Test Normal Increment"]
[TimeControl "180+2"]
[WhiteElo "1750"]
[BlackElo "1750"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:59]}
2. Nf3 {[%clk 0:02:59]} Nc6 {[%clk 0:02:58]}
*)";

  std::vector<ClockPositionRecord> records;
  bool success = parser.parseGame(pgn, records);

  ASSERT_TRUE(success);
  ASSERT_EQ(records.size(), 4); // All moves should be kept

  // Check that all records were processed normally
  EXPECT_FLOAT_EQ(records[0].player_clock, 180.0f); // Clock BEFORE White's e4
  EXPECT_FLOAT_EQ(records[1].player_clock, 180.0f); // Clock BEFORE Black's e5
  EXPECT_FLOAT_EQ(records[2].player_clock, 178.0f); // Clock BEFORE White's Nf3
  EXPECT_FLOAT_EQ(records[3].player_clock, 179.0f); // Clock BEFORE Black's Nc6
}

// Test clock increase filtering - artificial time addition (should be filtered)
TEST_F(ClockGameParserTest, ClockIncreaseFilteringExcessiveIncrease) {
  std::string pgn = R"([Event "Test Excessive Time Addition"]
[TimeControl "180+2"]
[WhiteElo "1750"]
[BlackElo "1750"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]}
2. Nf3 {[%clk 0:02:55]} Nc6 {[%clk 0:03:05]}
3. Bb5 {[%clk 0:02:53]} a6 {[%clk 0:02:51]}
*)";

  std::vector<ClockPositionRecord> records;
  bool success = parser.parseGame(pgn, records);

  // Game should be rejected due to artificial time addition
  // Black's clock went from 2:57 to 3:05 (8 second increase) with only 2 second increment
  EXPECT_FALSE(success) << "Game should be rejected due to clock time increase > 2x increment";
}

// Test clock increase filtering - zero increment with time addition (should be filtered)
TEST_F(ClockGameParserTest, ClockIncreaseFilteringZeroIncrementWithIncrease) {
  std::string pgn = R"([Event "Test Zero Increment Time Addition"]
[TimeControl "300+0"]
[WhiteElo "1750"]
[BlackElo "1750"]

1. e4 {[%clk 0:04:58]} e5 {[%clk 0:04:57]}
2. Nf3 {[%clk 0:04:56]} Nc6 {[%clk 0:04:59]}
*)";

  std::vector<ClockPositionRecord> records;
  bool success = parser.parseGame(pgn, records);

  // Game should be rejected - any time increase with zero increment is invalid
  EXPECT_FALSE(success) << "Game should be rejected due to clock time increase with zero increment";
}

// Test clock increase filtering - boundary case exactly 2x increment
TEST_F(ClockGameParserTest, ClockIncreaseFilteringBoundaryCase) {
  std::string pgn = R"([Event "Test Boundary Case"]
[TimeControl "180+2"]
[WhiteElo "1750"]
[BlackElo "1750"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]}
2. Nf3 {[%clk 0:02:55]} Nc6 {[%clk 0:03:01]}
*)";

  std::vector<ClockPositionRecord> records;
  bool success = parser.parseGame(pgn, records);

  // Game should be rejected - exactly 2x increment increase (4 seconds) should be invalid
  // Black's clock: 2:57 → 3:01 = 4 second increase, increment is 2, so 4 >= 2*2 is true
  // Using >= boundary condition as specified in test
  EXPECT_FALSE(success) << "Game should be rejected due to clock time increase >= 2x increment";
}

// Test clock increase filtering - small timing variance allowed
TEST_F(ClockGameParserTest, ClockIncreaseFilteringTimingVarianceAllowed) {
  std::string pgn = R"([Event "Test Timing Variance"]
[TimeControl "180+2"]
[WhiteElo "1750"]
[BlackElo "1750"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]}
2. Nf3 {[%clk 0:02:55]} Nc6 {[%clk 0:03:00]}
*)";

  std::vector<ClockPositionRecord> records;
  bool success = parser.parseGame(pgn, records);

  // Game should be accepted - 3 second increase is less than 2x increment (4 seconds)
  ASSERT_TRUE(success);
  ASSERT_EQ(records.size(), 4);

  // Verify the record with slight timing variance was processed
  EXPECT_FLOAT_EQ(records[3].player_clock, 177.0f); // Clock BEFORE Black's Nc6
}

// Test clock increase filtering - multiple violations in one game
TEST_F(ClockGameParserTest, ClockIncreaseFilteringMultipleViolations) {
  std::string pgn = R"([Event "Test Multiple Violations"]
[TimeControl "180+2"]
[WhiteElo "1750"]
[BlackElo "1750"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]}
2. Nf3 {[%clk 0:03:10]} Nc6 {[%clk 0:03:15]}
*)";

  std::vector<ClockPositionRecord> records;
  bool success = parser.parseGame(pgn, records);

  // Game should be rejected due to multiple artificial time additions
  EXPECT_FALSE(success) << "Game should be rejected due to multiple clock time violations";
}

// Test clock increase filtering - normal game should still work
TEST_F(ClockGameParserTest, ClockIncreaseFilteringNormalGameUnaffected) {
  std::string pgn = R"([Event "Test Normal Game"]
[TimeControl "180+2"]
[WhiteElo "1750"]
[BlackElo "1750"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]}
2. Nf3 {[%clk 0:02:55]} Nc6 {[%clk 0:02:54]}
3. Bb5 {[%clk 0:02:50]} a6 {[%clk 0:02:51]}
*)";

  std::vector<ClockPositionRecord> records;
  bool success = parser.parseGame(pgn, records);

  ASSERT_TRUE(success);
  ASSERT_EQ(records.size(), 6);

  // Verify normal clock progression
  EXPECT_FLOAT_EQ(records[0].thinking_time, 4.0f); // 180 + 2 - 178 = 4
  EXPECT_FLOAT_EQ(records[1].thinking_time, 5.0f); // 180 + 2 - 177 = 5
  EXPECT_FLOAT_EQ(records[2].thinking_time, 5.0f); // 178 + 2 - 175 = 5
}

// Test clock increase filtering - first moves are not filtered, but second moves are
TEST_F(ClockGameParserTest, ClockIncreaseFilteringFirstMoveExemption) {
  std::string pgn = R"([Event "Test First Move Exemption"]
[TimeControl "180+2"]
[WhiteElo "1750"]
[BlackElo "1750"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]}
2. Nf3 {[%clk 0:03:10]} Nc6 {[%clk 0:02:54]}
*)";

  std::vector<ClockPositionRecord> records;
  bool success = parser.parseGame(pgn, records);

  // Game should be rejected due to White's second move showing excessive time increase
  // White: 2:58 → 3:10 = 12 second increase with 2 second increment (12 > 4)
  EXPECT_FALSE(success) << "Game should be rejected due to clock time increase on White's second move";
}
} // namespace chessmimic::clock_converter
