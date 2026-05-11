#pragma once

#include <string>
#include <vector>
#include <simdjson.h>

namespace chessmimic::winner_converter {

/**
 * Data structure representing a single chess position with winner information
 * for training the game outcome prediction model.
 */
class WinnerPositionRecord {
public:
    // Position data
    std::string fen;
    std::vector<std::string> recent_moves;
    
    // Game outcome
    int winner;  // 1 = white wins, 0 = draw, -1 = black wins
    
    // Player ratings
    int white_rating;
    int black_rating;
    
    // Clock data (important for predicting outcomes)
    double white_clock;      // Seconds remaining for white
    double black_clock;      // Seconds remaining for black
    double increment;        // Seconds added per move

    /**
     * Constructor
     */
    WinnerPositionRecord(
        std::string fen_,
        const std::vector<std::string>& recent_moves_,
        int winner_,
        int white_rating_,
        int black_rating_,
        double white_clock_,
        double black_clock_,
        double increment_
    );

    /**
     * Default constructor
     */
    WinnerPositionRecord() = default;

    /**
     * Generate position key in format: FEN|move1,move2,move3
     */
    [[nodiscard]] std::string getPositionKey() const;

    /**
     * Serialize to JSON string
     */
    [[nodiscard]] std::string toJson() const;

    /**
     * Create from JSON using a reusable parser
     */
    static WinnerPositionRecord fromJson(const std::string& json_str, simdjson::dom::parser& parser);

    /**
     * Comparison operators for sorting by position key
     */
    bool operator<(const WinnerPositionRecord& other) const {
        return getPositionKey() < other.getPositionKey();
    }
    
    bool operator>(const WinnerPositionRecord& other) const {
        return getPositionKey() > other.getPositionKey();
    }
};

} // namespace chessmimic::winner_converter