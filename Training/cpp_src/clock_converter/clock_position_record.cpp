#include "clock_position_record.hpp"
#include <sstream>
#include <utility>
#include <iomanip>

namespace chessmimic {

// Helper function to escape JSON strings
static std::string escapeJsonString(const std::string& input) {
    std::ostringstream escaped;
    for (char c : input) {
        switch (c) {
            case '"': escaped << "\\\""; break;
            case '\\': escaped << "\\\\"; break;
            case '\b': escaped << "\\b"; break;
            case '\f': escaped << "\\f"; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default:
                if (c >= 0 && c <= 0x1f) {
                    // Control characters
                    escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                } else {
                    escaped << c;
                }
                break;
        }
    }
    return escaped.str();
}
ClockPositionRecord::ClockPositionRecord(
    std::string fen,
    const std::vector<std::string>& recent_moves,
    int rating,
    double player_clock,
    double opponent_clock,
    double increment,
    double thinking_time
)
    : fen(std::move(fen)),
      recent_moves(recent_moves),
      rating(rating),
      player_clock(player_clock),
      opponent_clock(opponent_clock),
      increment(increment),
      thinking_time(thinking_time) {
}

std::string ClockPositionRecord::getPositionKey() const {
    std::stringstream ss;
    ss << fen << "|";

    for (size_t i = 0; i < recent_moves.size(); ++i) {
        if (i > 0) {
            ss << ",";
        }
        ss << recent_moves[i];
    }

    return ss.str();
}

std::string ClockPositionRecord::extractFenFromKey(const std::string& key) {
    if (size_t delimiter_pos = key.find('|'); delimiter_pos != std::string::npos) {
        return key.substr(0, delimiter_pos);
    }
    return key; // If no delimiter, assume entire string is FEN
}

std::string ClockPositionRecord::stripMoveClockFromFen(const std::string& fen) {
    // FEN format: pieces castling ep halfmove fullmove
    // We want to remove halfmove and fullmove

    size_t last_space = fen.rfind(' ');
    if (last_space == std::string::npos) {
        return fen; // Invalid FEN, return as-is
    }

    size_t second_last_space = fen.rfind(' ', last_space - 1);
    if (second_last_space == std::string::npos) {
        return fen; // Invalid FEN, return as-is
    }

    // Return everything up to (but not including) the halfmove clock
    return fen.substr(0, second_last_space);
}

std::string ClockPositionRecord::toJson() const {
    // Use cached JSON if available
    if (!cached_json.empty()) {
        return cached_json;
    }
    
    // Otherwise build it
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);
    oss << R"({"fen":")" << escapeJsonString(fen) << R"(","recent_moves":[)";
    
    for (size_t i = 0; i < recent_moves.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << escapeJsonString(recent_moves[i]) << "\"";
    }
    
    oss << "],\"rating\":" << rating
        << ",\"player_clock\":" << player_clock
        << ",\"opponent_clock\":" << opponent_clock
        << ",\"increment\":" << increment
        << ",\"thinking_time\":" << thinking_time << "}";
    
    cached_json = oss.str();
    return cached_json;
}

ClockPositionRecord ClockPositionRecord::fromJson(const std::string& json_str, simdjson::dom::parser& parser) {
    simdjson::dom::element doc = parser.parse(json_str);
    
    std::vector<std::string> moves;
    for (auto move : doc["recent_moves"]) {
        moves.emplace_back(move);
    }
    
    ClockPositionRecord record{
        std::string(doc["fen"]),
        moves,
        static_cast<int>(doc["rating"].get_int64()),
        doc["player_clock"].get_double(),
        doc["opponent_clock"].get_double(),
        doc["increment"].get_double(),
        doc["thinking_time"].get_double()
    };
    
    // Cache the original JSON for efficient serialization
    record.cached_json = json_str;
    
    return record;
}

} // namespace chessmimic
