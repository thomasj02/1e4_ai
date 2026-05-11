#include <gtest/gtest.h>
#include "../winner_converter/winner_position_record.hpp"
#include <vector>
#include <string>

namespace chessmimic::winner_converter {

class WinnerPositionRecordTest : public testing::Test {
protected:
    void SetUp() override {
        // Common test data
        test_fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3";
        test_moves = {"e2e4"};
    }

    std::string test_fen;
    std::vector<std::string> test_moves;
};

// Test basic construction and field access
TEST_F(WinnerPositionRecordTest, ConstructionAndFieldAccess) {
    WinnerPositionRecord record(
        test_fen,
        test_moves,
        1,          // white wins
        1750,       // white rating
        1723,       // black rating
        178.0,      // white clock
        180.0,      // black clock
        2.0         // increment
    );

    EXPECT_EQ(record.fen, test_fen);
    EXPECT_EQ(record.recent_moves, test_moves);
    EXPECT_EQ(record.winner, 1);
    EXPECT_EQ(record.white_rating, 1750);
    EXPECT_EQ(record.black_rating, 1723);
    EXPECT_DOUBLE_EQ(record.white_clock, 178.0);
    EXPECT_DOUBLE_EQ(record.black_clock, 180.0);
    EXPECT_DOUBLE_EQ(record.increment, 2.0);
}

// Test all three outcome values
TEST_F(WinnerPositionRecordTest, AllOutcomeValues) {
    // White win
    WinnerPositionRecord white_win(test_fen, test_moves, 1, 1800, 1800, 100.0, 100.0, 0.0);
    EXPECT_EQ(white_win.winner, 1);

    // Draw
    WinnerPositionRecord draw(test_fen, test_moves, 0, 1800, 1800, 100.0, 100.0, 0.0);
    EXPECT_EQ(draw.winner, 0);

    // Black win
    WinnerPositionRecord black_win(test_fen, test_moves, -1, 1800, 1800, 100.0, 100.0, 0.0);
    EXPECT_EQ(black_win.winner, -1);
}

// Test position key generation
TEST_F(WinnerPositionRecordTest, PositionKeyGeneration) {
    std::vector<std::string> moves = {"e2e4", "e7e5", "g1f3"};
    WinnerPositionRecord record(test_fen, moves, 1, 1750, 1723, 178.0, 180.0, 2.0);

    std::string expected_key = test_fen + "|e2e4,e7e5,g1f3";
    EXPECT_EQ(record.getPositionKey(), expected_key);
}

// Test position key with no moves
TEST_F(WinnerPositionRecordTest, PositionKeyNoMoves) {
    std::vector<std::string> no_moves;
    WinnerPositionRecord record(test_fen, no_moves, 1, 1750, 1723, 180.0, 180.0, 2.0);

    std::string expected_key = test_fen + "|";
    EXPECT_EQ(record.getPositionKey(), expected_key);
}

// Test JSON serialization
TEST_F(WinnerPositionRecordTest, JsonSerialization) {
    WinnerPositionRecord record(
        test_fen,
        test_moves,
        1,
        1750,
        1723,
        178.0,
        180.0,
        2.0
    );

    std::string json = record.toJson();
    
    // Check that JSON contains all required fields
    EXPECT_NE(json.find("\"fen\":\"" + test_fen + "\""), std::string::npos);
    EXPECT_NE(json.find("\"winner\":1"), std::string::npos);
    EXPECT_NE(json.find("\"white_rating\":1750"), std::string::npos);
    EXPECT_NE(json.find("\"black_rating\":1723"), std::string::npos);
    EXPECT_NE(json.find("\"white_clock\":178.0"), std::string::npos);
    EXPECT_NE(json.find("\"black_clock\":180.0"), std::string::npos);
    EXPECT_NE(json.find("\"increment\":2.0"), std::string::npos);
    EXPECT_NE(json.find("\"recent_moves\":[\"e2e4\"]"), std::string::npos);
}

// Test JSON deserialization
TEST_F(WinnerPositionRecordTest, JsonDeserialization) {
    std::string json = R"({
        "fen": "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3",
        "recent_moves": ["e2e4"],
        "winner": 0,
        "white_rating": 1750,
        "black_rating": 1723,
        "white_clock": 178.0,
        "black_clock": 180.0,
        "increment": 2.0
    })";

    simdjson::dom::parser parser;
    WinnerPositionRecord record = WinnerPositionRecord::fromJson(json, parser);

    EXPECT_EQ(record.fen, test_fen);
    EXPECT_EQ(record.recent_moves, test_moves);
    EXPECT_EQ(record.winner, 0);
    EXPECT_EQ(record.white_rating, 1750);
    EXPECT_EQ(record.black_rating, 1723);
    EXPECT_DOUBLE_EQ(record.white_clock, 178.0);
    EXPECT_DOUBLE_EQ(record.black_clock, 180.0);
    EXPECT_DOUBLE_EQ(record.increment, 2.0);
}

// Test round-trip JSON serialization
TEST_F(WinnerPositionRecordTest, JsonRoundTrip) {
    std::vector<std::string> moves = {"d2d4", "d7d5", "c2c4"};
    WinnerPositionRecord original(
        "rnbqkbnr/ppp1pppp/8/3p4/2PP4/8/PP2PPPP/RNBQKBNR b KQkq c3",
        moves,
        -1,   // black wins
        2100,
        2050,
        295.5,
        297.0,
        3.0
    );

    std::string json = original.toJson();
    simdjson::dom::parser parser;
    WinnerPositionRecord deserialized = WinnerPositionRecord::fromJson(json, parser);

    EXPECT_EQ(deserialized.fen, original.fen);
    EXPECT_EQ(deserialized.recent_moves, original.recent_moves);
    EXPECT_EQ(deserialized.winner, original.winner);
    EXPECT_EQ(deserialized.white_rating, original.white_rating);
    EXPECT_EQ(deserialized.black_rating, original.black_rating);
    EXPECT_DOUBLE_EQ(deserialized.white_clock, original.white_clock);
    EXPECT_DOUBLE_EQ(deserialized.black_clock, original.black_clock);
    EXPECT_DOUBLE_EQ(deserialized.increment, original.increment);
}

// Test comparison operators for sorting
TEST_F(WinnerPositionRecordTest, ComparisonOperators) {
    WinnerPositionRecord record1(
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",
        {}, 1, 1500, 1500, 300.0, 300.0, 0.0
    );

    WinnerPositionRecord record2(
        "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3",
        {}, 1, 1500, 1500, 300.0, 300.0, 0.0
    );

    // Different FENs should result in different position keys
    EXPECT_TRUE(record1 < record2 || record2 < record1);
    EXPECT_NE(record1.getPositionKey(), record2.getPositionKey());
}

} // namespace chessmimic::winner_converter