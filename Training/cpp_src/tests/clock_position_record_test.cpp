#include <gtest/gtest.h>
#include <simdjson.h>
#include <sstream>
#include "../clock_converter/clock_position_record.hpp"

namespace chessmimic::test {

class ClockPositionRecordTest : public testing::Test {
protected:
    void SetUp() override {
        // Common test data
        test_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
        test_recent_moves = {"e2e4", "e7e5", "g1f3"};
        test_rating = 1750;
        test_player_clock = 178.5;
        test_opponent_clock = 175.0;
        test_increment = 2.0;
        test_thinking_time = 4.5;
    }

    std::string test_fen;
    std::vector<std::string> test_recent_moves;
    int test_rating = 0;
    double test_player_clock = 0.0;
    double test_opponent_clock = 0.0;
    double test_increment = 0.0;
    double test_thinking_time = 0.0;
};

TEST_F(ClockPositionRecordTest, ConstructorInitializesFields) {
    ClockPositionRecord record(
        test_fen,
        test_recent_moves,
        test_rating,
        test_player_clock,
        test_opponent_clock,
        test_increment,
        test_thinking_time
    );

    EXPECT_EQ(record.fen, test_fen);
    EXPECT_EQ(record.recent_moves, test_recent_moves);
    EXPECT_EQ(record.rating, test_rating);
    EXPECT_DOUBLE_EQ(record.player_clock, test_player_clock);
    EXPECT_DOUBLE_EQ(record.opponent_clock, test_opponent_clock);
    EXPECT_DOUBLE_EQ(record.increment, test_increment);
    EXPECT_DOUBLE_EQ(record.thinking_time, test_thinking_time);
}

TEST_F(ClockPositionRecordTest, GeneratesCorrectPositionKey) {
    ClockPositionRecord record(
        test_fen,
        test_recent_moves,
        test_rating,
        test_player_clock,
        test_opponent_clock,
        test_increment,
        test_thinking_time
    );

    std::string expected_key = test_fen + "|e2e4,e7e5,g1f3";
    EXPECT_EQ(record.getPositionKey(), expected_key);
}

TEST_F(ClockPositionRecordTest, GeneratesCorrectPositionKeyEmptyMoves) {
    ClockPositionRecord record(
        test_fen,
        {},  // Empty recent moves
        test_rating,
        test_player_clock,
        test_opponent_clock,
        test_increment,
        test_thinking_time
    );

    std::string expected_key = test_fen + "|";
    EXPECT_EQ(record.getPositionKey(), expected_key);
}

TEST_F(ClockPositionRecordTest, ExtractsFenFromKey) {
    std::string key = test_fen + "|e2e4,e7e5,g1f3";
    std::string extracted_fen = ClockPositionRecord::extractFenFromKey(key);
    EXPECT_EQ(extracted_fen, test_fen);
}

TEST_F(ClockPositionRecordTest, StripsMoveClockFromFen) {
    // FEN with move clocks
    std::string fen_with_clocks = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    std::string expected = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
    
    EXPECT_EQ(ClockPositionRecord::stripMoveClockFromFen(fen_with_clocks), expected);
}

TEST_F(ClockPositionRecordTest, SerializesToJson) {
    ClockPositionRecord record(
        test_fen,
        test_recent_moves,
        test_rating,
        test_player_clock,
        test_opponent_clock,
        test_increment,
        test_thinking_time
    );

    std::string json_str = record.toJson();
    
    // Parse the JSON string using simdjson
    simdjson::dom::parser parser;
    simdjson::dom::element doc = parser.parse(json_str);

    EXPECT_EQ(doc["fen"].get_string().value(), test_fen);
    
    auto recent_moves_array = doc["recent_moves"].get_array().value();
    EXPECT_EQ(recent_moves_array.size(), 3);
    
    size_t idx = 0;
    for (auto move : recent_moves_array) {
        EXPECT_EQ(move.get_string().value(), test_recent_moves[idx]);
        idx++;
    }
    
    EXPECT_EQ(doc["rating"].get_int64().value(), test_rating);
    EXPECT_DOUBLE_EQ(doc["player_clock"].get_double().value(), test_player_clock);
    EXPECT_DOUBLE_EQ(doc["opponent_clock"].get_double().value(), test_opponent_clock);
    EXPECT_DOUBLE_EQ(doc["increment"].get_double().value(), test_increment);
    EXPECT_DOUBLE_EQ(doc["thinking_time"].get_double().value(), test_thinking_time);
}

TEST_F(ClockPositionRecordTest, DeserializesFromJson) {
    // Build a JSON string
    std::ostringstream json_stream;
    json_stream << "{";
    json_stream << R"("fen":")" << test_fen << R"(",)";
    json_stream << R"("recent_moves":[)";
    for (size_t i = 0; i < test_recent_moves.size(); ++i) {
        json_stream << "\"" << test_recent_moves[i] << "\"";
        if (i < test_recent_moves.size() - 1) {
            json_stream << ",";
        }
    }
    json_stream << "],";
    json_stream << R"("rating":)" << test_rating << ",";
    json_stream << R"("player_clock":)" << test_player_clock << ",";
    json_stream << R"("opponent_clock":)" << test_opponent_clock << ",";
    json_stream << R"("increment":)" << test_increment << ",";
    json_stream << R"("thinking_time":)" << test_thinking_time;
    json_stream << "}";
    
    std::string json_str = json_stream.str();
    simdjson::dom::parser parser;
    ClockPositionRecord record = ClockPositionRecord::fromJson(json_str, parser);

    EXPECT_EQ(record.fen, test_fen);
    EXPECT_EQ(record.recent_moves, test_recent_moves);
    EXPECT_EQ(record.rating, test_rating);
    EXPECT_DOUBLE_EQ(record.player_clock, test_player_clock);
    EXPECT_DOUBLE_EQ(record.opponent_clock, test_opponent_clock);
    EXPECT_DOUBLE_EQ(record.increment, test_increment);
    EXPECT_DOUBLE_EQ(record.thinking_time, test_thinking_time);
}

TEST_F(ClockPositionRecordTest, ComparesCorrectlyByKey) {
    ClockPositionRecord record1(
        test_fen,
        test_recent_moves,
        test_rating,
        test_player_clock,
        test_opponent_clock,
        test_increment,
        test_thinking_time
    );

    // Different FEN should compare differently
    ClockPositionRecord record2(
        "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
        test_recent_moves,
        test_rating,
        test_player_clock,
        test_opponent_clock,
        test_increment,
        test_thinking_time
    );

    EXPECT_GT(record1, record2);  // First FEN comes after second alphabetically ("8/8" > "4P3/8")
}

TEST_F(ClockPositionRecordTest, JsonCachingOptimization) {
    // Create a record from JSON - this should cache the JSON
    std::string original_json = R"({"fen":"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1","recent_moves":["e2e4","e7e5"],"rating":1750,"player_clock":180.000000,"opponent_clock":179.500000,"increment":2.000000,"thinking_time":0.500000})";
    
    simdjson::dom::parser parser;
    ClockPositionRecord record = ClockPositionRecord::fromJson(original_json, parser);
    
    // When we call toJson, it should return the cached version
    std::string json_output = record.toJson();
    
    // The output should be identical to the input
    EXPECT_EQ(json_output, original_json);
    
    // Verify the values were parsed correctly
    EXPECT_EQ(record.fen, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    EXPECT_EQ(record.recent_moves.size(), 2);
    EXPECT_EQ(record.recent_moves[0], "e2e4");
    EXPECT_EQ(record.recent_moves[1], "e7e5");
    EXPECT_EQ(record.rating, 1750);
    EXPECT_DOUBLE_EQ(record.player_clock, 180.0);
    EXPECT_DOUBLE_EQ(record.opponent_clock, 179.5);
    EXPECT_DOUBLE_EQ(record.increment, 2.0);
    EXPECT_DOUBLE_EQ(record.thinking_time, 0.5);
}

} // namespace chessmimic::test