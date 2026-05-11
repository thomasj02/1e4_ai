#include "winner_position_record.hpp"
#include <sstream>
#include <iomanip>

namespace chessmimic::winner_converter {

// Constructor - stub implementation
WinnerPositionRecord::WinnerPositionRecord(
    std::string fen_,
    const std::vector<std::string>& recent_moves_,
    int winner_,
    int white_rating_,
    int black_rating_,
    double white_clock_,
    double black_clock_,
    double increment_
) : fen(std::move(fen_)),
    recent_moves(recent_moves_),
    winner(winner_),
    white_rating(white_rating_),
    black_rating(black_rating_),
    white_clock(white_clock_),
    black_clock(black_clock_),
    increment(increment_) {
    // Basic initialization done, but other methods still stubs
}

// Generate position key - FEN|move1,move2,move3
std::string WinnerPositionRecord::getPositionKey() const {
    std::string key = fen + "|";
    
    for (size_t i = 0; i < recent_moves.size(); ++i) {
        if (i > 0) {
            key += ",";
        }
        key += recent_moves[i];
    }
    
    return key;
}

// Serialize to JSON
std::string WinnerPositionRecord::toJson() const {
    std::ostringstream oss;
    oss << "{";
    
    // FEN
    oss << R"("fen":")" << fen << "\",";
    
    // Recent moves array
    oss << "\"recent_moves\":[";
    for (size_t i = 0; i < recent_moves.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << recent_moves[i] << "\"";
    }
    oss << "],";
    
    // Winner (now an integer)
    oss << "\"winner\":" << winner << ",";
    
    // Ratings
    oss << "\"white_rating\":" << white_rating << ",";
    oss << "\"black_rating\":" << black_rating << ",";
    
    // Clock data
    oss << "\"white_clock\":" << std::fixed << std::setprecision(1) << white_clock << ",";
    oss << "\"black_clock\":" << std::fixed << std::setprecision(1) << black_clock << ",";
    oss << "\"increment\":" << std::fixed << std::setprecision(1) << increment;
    
    oss << "}";
    return oss.str();
}

// Create from JSON
WinnerPositionRecord WinnerPositionRecord::fromJson(
    const std::string& json_str, 
    simdjson::dom::parser& parser
) {
    simdjson::dom::element doc = parser.parse(json_str);
    
    WinnerPositionRecord record;
    
    // Extract basic fields
    record.fen = std::string(doc["fen"]);
    record.winner = static_cast<int>(doc["winner"].get_int64());
    record.white_rating = static_cast<int>(doc["white_rating"].get_int64());
    record.black_rating = static_cast<int>(doc["black_rating"].get_int64());
    record.white_clock = static_cast<double>(doc["white_clock"]);
    record.black_clock = static_cast<double>(doc["black_clock"]);
    record.increment = static_cast<double>(doc["increment"]);
    
    // Extract recent moves array
    record.recent_moves.clear();
    for (auto move : doc["recent_moves"]) {
        record.recent_moves.emplace_back(move);
    }
    
    return record;
}

} // namespace chessmimic::winner_converter