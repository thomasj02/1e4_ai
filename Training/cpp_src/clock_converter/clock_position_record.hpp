#pragma once

#include <string>
#include <vector>
#include <simdjson.h>

namespace chessmimic {

/**
 * Data structure representing a single chess position with clock information
 * for training the clock prediction model.
 */
class ClockPositionRecord {
public:
    // Position data
    std::string fen;
    std::vector<std::string> recent_moves;
    
    // Player data
    int rating;
    
    // Clock data
    double player_clock;      // Seconds remaining
    double opponent_clock;    // Seconds remaining
    double increment;         // Seconds per move
    double thinking_time;     // Seconds used for this move
    
private:
    // Cached JSON representation for performance
    mutable std::string cached_json;

public:
    /**
     * Constructor
     */
    ClockPositionRecord(
        std::string  fen,
        const std::vector<std::string>& recent_moves,
        int rating,
        double player_clock,
        double opponent_clock,
        double increment,
        double thinking_time
    );

    /**
     * Default constructor
     */
    ClockPositionRecord() : rating(0), player_clock(0.0), opponent_clock(0.0), 
                           increment(0.0), thinking_time(0.0) {}

    /**
     * Generate position key in format: FEN|move1,move2,move3
     */
    [[nodiscard]] std::string getPositionKey() const;

    /**
     * Extract FEN from a position key
     */
    static std::string extractFenFromKey(const std::string& key);

    /**
     * Strip move clocks (halfmove and fullmove) from FEN
     * Example: "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
     * becomes: "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -"
     */
    static std::string stripMoveClockFromFen(const std::string& fen);

    /**
     * Serialize to JSON string
     */
    [[nodiscard]] std::string toJson() const;

    /**
     * Create from JSON using a reusable parser (more efficient for batch processing).
     * The parser instance can be reused across multiple calls, saving ~150ns per parse.
     * This method also caches the JSON string for efficient serialization.
     */
    static ClockPositionRecord fromJson(const std::string& json_str, simdjson::dom::parser& parser);

    /**
     * Comparison operators for sorting by position key
     */
    bool operator<(const ClockPositionRecord& other) const {
        return getPositionKey() < other.getPositionKey();
    }
    
    bool operator>(const ClockPositionRecord& other) const {
        return getPositionKey() > other.getPositionKey();
    }
};

} // namespace chessmimic