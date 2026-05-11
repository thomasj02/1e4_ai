#include "winner_game_parser.hpp"
#include "../pgn/pgn_parsing_utils.hpp"
#include <regex>
#include <cmath>

namespace chessmimic::winner_converter {

// Constructor
WinnerGameParser::WinnerGameParser() = default;

// Parse single game
bool WinnerGameParser::parseGame(
    const std::string& pgn, 
    std::vector<WinnerPositionRecord>& records
) const {
    records.clear();
    
    // Parse headers
    int winner;
    int white_elo, black_elo;
    float initial_time, increment;
    
    if (!parseHeaders(pgn, winner, white_elo, black_elo, initial_time, increment)) {
        return false;
    }
    
    // Find move section (after headers)
    size_t move_start = pgn.find("\n\n");
    if (move_start == std::string::npos) {
        return false;
    }
    
    std::string movetext = pgn.substr(move_start + 2);
    
    // Parse moves and generate records
    return parseMoves(movetext, winner, white_elo, black_elo, initial_time, increment, records);
}

// Parse stream with multiple games
void WinnerGameParser::parseStream(
    std::istream& stream,
    const std::function<void(std::vector<WinnerPositionRecord>&&)>& callback
) const {
    // Use shared utility function to eliminate code duplication
    auto parseFunc = [this](const std::string& pgn, std::vector<WinnerPositionRecord>& records) {
        return this->parseGame(pgn, records);
    };
    PgnParsingUtils::parseGameStream<WinnerPositionRecord>(stream, parseFunc, callback);
}

// Parse headers from PGN
bool WinnerGameParser::parseHeaders(
    const std::string& pgn,
    int& winner,
    int& white_elo,
    int& black_elo,
    float& initial_time,
    float& increment
) {
    // Use shared utility to extract headers
    PgnParsingUtils::PgnHeaders headers = PgnParsingUtils::parsePgnHeaders(pgn);

    // Extract values
    white_elo = headers.white_elo;
    black_elo = headers.black_elo;
    initial_time = headers.initial_time;
    increment = headers.increment;

    // Parse result to winner value
    winner = PgnParsingUtils::parseResult(headers.result);

    // Validate required headers
    bool has_result = winner != -999;  // Valid results are -1, 0, 1
    return has_result && headers.has_time_control && headers.has_white_elo && headers.has_black_elo;
}

// Parse moves and generate position records
bool WinnerGameParser::parseMoves(
    const std::string& movetext,
    int winner,
    int white_elo,
    int black_elo,
    float initial_time,
    float increment,
    std::vector<WinnerPositionRecord>& records
) const {
    chess::Board board;
    std::vector<std::string> recent_moves;
    
    // Track clock states for both players
    float white_clock = initial_time;
    float black_clock = initial_time;
    
    // Parse moves
    std::regex move_regex(R"raw((\d+\.(?:\.\.)?\s*)?([a-hKQRBNO][a-h0-9xO\-\+\#\=]+)\s*(\{[^\}]*\})?)raw");
    std::sregex_iterator it(movetext.begin(), movetext.end(), move_regex);
    std::sregex_iterator end;
    
    bool has_any_clock = false;
    
    while (it != end) {
        std::string move_str = (*it)[2].str();
        std::string comment = (*it)[3].str();
        
        // Parse the move
        chess::Movelist moves;
        chess::movegen::legalmoves(moves, board);
        
        chess::Move move = chess::Move::NO_MOVE;
        for (const auto& legal_move : moves) {
            // Convert to SAN and compare
            if (std::string san = chess::uci::moveToSan(board, legal_move); san == move_str) {
                move = legal_move;
                break;
            }
        }
        
        if (move == chess::Move::NO_MOVE) {
            ++it;
            continue;  // Skip invalid moves
        }
        
        // Apply the move
        board.makeMove(move);

        // Update recent moves to include this move
        PgnParsingUtils::updateRecentMoves(recent_moves, move, max_recent_moves_);

        // Check if we have clock data
        if (std::string clock_str = PgnParsingUtils::extractClockFromComment(comment); !clock_str.empty()) {
            if (float clock_time = PgnParsingUtils::parseClockTime(clock_str); clock_time >= 0) {
                has_any_clock = true;
                
                // Update the appropriate player's clock
                if (board.sideToMove() == chess::Color::BLACK) {
                    // White just moved
                    white_clock = clock_time;
                } else {
                    // Black just moved
                    black_clock = clock_time;
                }
                
                // Create position record AFTER the move
                WinnerPositionRecord record(
                    board.getFen(),
                    recent_moves,
                    winner,
                    white_elo,
                    black_elo,
                    white_clock,
                    black_clock,
                    increment
                );
                
                records.push_back(record);
            }
        }
        
        ++it;
    }
    
    // Must have at least one clock annotation
    return has_any_clock && !records.empty();
}

} // namespace chessmimic::winner_converter