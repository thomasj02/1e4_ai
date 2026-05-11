#pragma once

#include <string>
#include <vector>
#include <functional>
#include "external/chess.hpp"

namespace chessmimic::PgnParsingUtils {

/**
 * Parse clock time from various formats:
 * - Pure seconds: "297.0" or "293"
 * - H:MM:SS format: "1:30:45"
 * - M:SS or MM:SS format: "5:30" or "90:15"
 * - M:SS.s format with decimals: "5:30.5"
 *
 * @param clock_str The clock time string to parse
 * @return The time in seconds, or -1.0f if parsing fails
 */
float parseClockTime(const std::string& clock_str);

/**
 * Parse time control string in the format "initial+increment"
 * Examples: "300+0", "180+2"
 * Also handles single number format (no increment): "180"
 *
 * @param time_control The time control string to parse
 * @param increment Output parameter for the increment value in seconds
 * @return The initial time in seconds, or -1.0f if parsing fails
 */
float parseTimeControl(const std::string& time_control, float& increment);

/**
 * Extract clock annotation from a PGN move comment.
 * Looks for the [%clk ...] pattern and extracts the clock value.
 *
 * Examples:
 * - "[%clk 0:05:30]" -> "0:05:30"
 * - "{[%clk 1:30:45]}" -> "1:30:45"
 * - "{ some comment [%clk 297.0] }" -> "297.0"
 *
 * @param comment The move comment string to parse
 * @return The extracted clock value string, or empty string if not found
 */
std::string extractClockFromComment(const std::string& comment);

/**
 * Update recent moves list with a new move.
 * Converts the move to UCI format, appends it to the list, and keeps only
 * the last N moves as specified by max_recent_moves.
 *
 * @param recent_moves Vector of recent moves in UCI format (modified in-place)
 * @param move The chess move to add
 * @param max_recent_moves Maximum number of recent moves to keep
 */
void updateRecentMoves(
    std::vector<std::string>& recent_moves,
    const chess::Move& move,
    size_t max_recent_moves
);

/**
 * Struct to hold parsed PGN header information.
 * Used by parsePgnHeaders() to return extracted header data.
 */
struct PgnHeaders {
    int white_elo = 0;
    int black_elo = 0;
    float initial_time = 0.0f;
    float increment = 0.0f;
    std::string result;  // "1-0", "0-1", "1/2-1/2", or "*"

    bool has_white_elo = false;
    bool has_black_elo = false;
    bool has_time_control = false;
    bool has_result = false;
};

/**
 * Parse PGN headers from a PGN game string.
 * Extracts common headers: WhiteElo, BlackElo, TimeControl, and Result.
 *
 * This utility eliminates duplicated header parsing logic between ClockGameParser
 * and WinnerGameParser by providing a single, shared implementation.
 *
 * @param pgn The PGN game string to parse
 * @return PgnHeaders struct with extracted header values and validity flags
 */
PgnHeaders parsePgnHeaders(const std::string& pgn);

/**
 * Convert PGN result string to numeric winner value.
 *
 * @param result Result string from PGN ("1-0", "0-1", "1/2-1/2", "*")
 * @return 1 for White win, -1 for Black win, 0 for draw, -999 for invalid/ongoing
 */
int parseResult(const std::string& result);

/**
 * Parse multiple PGN games from a stream and invoke a callback for each game.
 *
 * This utility function handles the common pattern of:
 * 1. Accumulating lines until "[Event " marker (game boundary)
 * 2. Parsing each accumulated game
 * 3. Invoking callback with successfully parsed records
 *
 * Used by both ClockGameParser and WinnerGameParser to eliminate code duplication.
 *
 * @tparam RecordType The type of position records (e.g., ClockPositionRecord, WinnerPositionRecord)
 * @param stream Input stream containing PGN games
 * @param parseGameFunc Function that parses a single PGN game string into records
 *                      Should return true if parsing succeeded, false otherwise
 * @param callback Function called for each successfully parsed game with its records
 */
template<typename RecordType>
void parseGameStream(
    std::istream& stream,
    const std::function<bool(const std::string&, std::vector<RecordType>&)>& parseGameFunc,
    const std::function<void(std::vector<RecordType>&&)>& callback
) {
    std::string line;
    std::string current_game;

    while (std::getline(stream, line)) {
        // Check if this is the start of a new game
        if (line.starts_with("[Event ") && !current_game.empty()) {
            // Process the previous game
            if (std::vector<RecordType> records; parseGameFunc(current_game, records) && !records.empty()) {
                callback(std::move(records));
            }
            current_game.clear();
        }
        current_game += line + "\n";
    }

    // Process the last game
    if (!current_game.empty()) {
        if (std::vector<RecordType> records; parseGameFunc(current_game, records) && !records.empty()) {
            callback(std::move(records));
        }
    }
}

} // namespace chessmimic::PgnParsingUtils
