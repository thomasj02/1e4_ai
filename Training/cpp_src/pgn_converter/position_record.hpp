#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <simdjson.h>

namespace chessmimic {
// Position record structure
struct PositionRecord {
    std::vector<std::string> recent_moves;
    std::string fen;
    std::unordered_map<
        std::string, // move
        std::unordered_map<
            std::string, // clock_time
            std::unordered_map<
                std::string, // rating
                int // count
            >
        >
    > moves;

    // Convert to JSON
    std::string toJson() const {
        std::ostringstream oss;
        oss << "{\"recent_and_fen\":[[";
        
        // Write recent moves array
        for (size_t i = 0; i < recent_moves.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "\"" << recent_moves[i] << "\"";
        }
        
        oss << "],\"" << fen << "\"],\"moves\":{";
        
        // Write moves map
        bool first_move = true;
        for (const auto& [move, clock_map] : moves) {
            if (!first_move) oss << ",";
            oss << "\"" << move << "\":{";
            
            bool first_clock = true;
            for (const auto& [clock_time, rating_map] : clock_map) {
                if (!first_clock) oss << ",";
                oss << "\"" << clock_time << "\":{";
                
                bool first_rating = true;
                for (const auto& [rating, count] : rating_map) {
                    if (!first_rating) oss << ",";
                    oss << "\"" << rating << "\":" << count;
                    first_rating = false;
                }
                
                oss << "}";
                first_clock = false;
            }
            
            oss << "}";
            first_move = false;
        }
        
        oss << "}}";
        return oss.str();
    }

    // Parse from JSON
    static PositionRecord fromJson(const std::string& json_str) {
        PositionRecord record;
        simdjson::dom::parser parser;
        simdjson::dom::element doc = parser.parse(json_str);
        
        auto recent_and_fen = doc["recent_and_fen"];
        for (auto move : recent_and_fen.at(0)) {
            record.recent_moves.emplace_back(move);
        }
        record.fen = std::string(recent_and_fen.at(1));

        for (auto moves_obj = doc["moves"].get_object(); auto [move_key, clock_obj] : moves_obj) {
            auto move = std::string(move_key);
            for (auto [clock_key, rating_obj] : clock_obj.get_object()) {
                auto clock_time = std::string(clock_key);
                for (auto [rating_key, count] : rating_obj.get_object()) {
                    auto rating = std::string(rating_key);
                    record.moves[move][clock_time][rating] = static_cast<int>(count.get_int64());
                }
            }
        }
        
        return record;
    }
};
} // namespace chessmimic
