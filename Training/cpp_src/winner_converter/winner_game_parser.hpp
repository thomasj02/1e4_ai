#pragma once

#include <string>
#include <vector>
#include <functional>
#include "winner_position_record.hpp"
#include "../external/chess.hpp"

namespace chessmimic::winner_converter {

/**
 * Parser for PGN games that extracts winner information and generates
 * WinnerPositionRecord instances for each position with valid clock data.
 */
class WinnerGameParser {
public:
    WinnerGameParser();
    ~WinnerGameParser() = default;
    
    /**
     * Parse a single PGN game string and extract winner position records.
     * 
     * @param pgn The PGN game string to parse
     * @param records Output vector of position records (cleared before parsing)
     * @return true if parsing succeeded, false if game lacks required data
     */
    bool parseGame(const std::string& pgn, std::vector<WinnerPositionRecord>& records) const;
    
    /**
     * Parse multiple games from a stream.
     * 
     * @param stream Input stream containing PGN games
     * @param callback Function called for each parsed game with its records
     */
    void parseStream(std::istream& stream,
                    const std::function<void(std::vector<WinnerPositionRecord>&&)>& callback) const;
    
    /**
     * Set the maximum number of recent moves to track.
     * 
     * @param max_moves Maximum number of recent moves (default: 12)
     */
    void setMaxRecentMoves(size_t max_moves) { max_recent_moves_ = max_moves; }

private:
    // Game header parsing
    static bool parseHeaders(const std::string& pgn,
                            int& winner,
                            int& white_elo,
                            int& black_elo,
                            float& initial_time,
                            float& increment);
    
    // Move parsing and position generation
    bool parseMoves(const std::string& movetext,
                   int winner,
                   int white_elo,
                   int black_elo,
                   float initial_time,
                   float increment,
                   std::vector<WinnerPositionRecord>& records) const;

    size_t max_recent_moves_ = 12;  // 6 full moves = 12 half-moves (matches pgn_to_bagz and chessmimic_core.cpp)
};

} // namespace chessmimic::winner_converter