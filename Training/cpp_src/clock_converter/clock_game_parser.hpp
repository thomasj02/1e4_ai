#pragma once

#include <string>
#include <vector>
#include <functional>
#include "clock_position_record.hpp"
#include "../external/chess.hpp"

namespace chessmimic::clock_converter {

/**
 * Parser for PGN games that extracts clock information and generates
 * ClockPositionRecord instances for each position with valid clock data.
 */
class ClockGameParser {
public:
    ClockGameParser();
    ~ClockGameParser() = default;
    
    /**
     * Parse a single PGN game string and extract clock position records.
     * 
     * @param pgn The PGN game string to parse
     * @param records Output vector of position records (cleared before parsing)
     * @return true if parsing succeeded, false if game lacks required data
     */
    bool parseGame(const std::string& pgn, std::vector<ClockPositionRecord>& records) const;
    
    /**
     * Parse multiple games from a stream.
     * 
     * @param stream Input stream containing PGN games
     * @param callback Function called for each parsed game with its records
     */
    void parseStream(std::istream& stream,
                    const std::function<void(std::vector<ClockPositionRecord>&&)>& callback) const;
    
    /**
     * Set the maximum number of recent moves to track.
     * 
     * @param max_moves Maximum number of recent moves (default: 12)
     */
    void setMaxRecentMoves(size_t max_moves) { max_recent_moves_ = max_moves; }
    
private:
    // Game header parsing
    static bool parseHeaders(const std::string& pgn,
                             int& white_elo,
                             int& black_elo,
                             float& initial_time,
                             float& increment);
    
    // Move parsing and position generation
    bool parseMoves(const std::string& movetext,
                   int white_elo,
                   int black_elo,
                   float initial_time,
                   float increment,
                   std::vector<ClockPositionRecord>& records) const;

    // Helper function to validate clock increases
    [[nodiscard]] static bool isValidClockIncrease(float current_clock,
                                                   float previous_clock,
                                                   float increment,
                                                   bool first_move);
    
    size_t max_recent_moves_ = 12;  // 6 full moves = 12 half-moves
};

} // namespace chessmimic::clock_converter