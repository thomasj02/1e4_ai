#include "clock_game_parser.hpp"
#include "../pgn/pgn_parsing_utils.hpp"
#include <regex>
#include <cmath>

namespace chessmimic::clock_converter {
ClockGameParser::ClockGameParser() = default;

bool ClockGameParser::parseGame(const std::string &pgn, std::vector<ClockPositionRecord> &records) const {
  records.clear();

  // Parse headers
  int white_elo, black_elo;
  float initial_time, increment;

  if (!parseHeaders(pgn, white_elo, black_elo, initial_time, increment)) {
    return false;
  }

  // Find move section (after headers)
  size_t move_start = pgn.find("\n\n");
  if (move_start == std::string::npos) {
    return false;
  }

  std::string movetext = pgn.substr(move_start + 2);

  // Parse moves and generate records
  return parseMoves(movetext, white_elo, black_elo, initial_time, increment, records);
}

void ClockGameParser::parseStream(std::istream &stream,
                                  const std::function<void(std::vector<ClockPositionRecord> &&)> &callback) const {
  // Use shared utility function to eliminate code duplication
  auto parseFunc = [this](const std::string &pgn, std::vector<ClockPositionRecord> &records) {
    return this->parseGame(pgn, records);
  };
  PgnParsingUtils::parseGameStream<ClockPositionRecord>(stream, parseFunc, callback);
}

bool ClockGameParser::parseHeaders(const std::string &pgn,
                                   int &white_elo,
                                   int &black_elo,
                                   float &initial_time,
                                   float &increment) {
  // Use shared utility to extract headers
  PgnParsingUtils::PgnHeaders headers = PgnParsingUtils::parsePgnHeaders(pgn);

  // Extract values
  white_elo = headers.white_elo;
  black_elo = headers.black_elo;
  initial_time = headers.initial_time;
  increment = headers.increment;

  // Validate required headers
  return headers.has_time_control && headers.has_white_elo && headers.has_black_elo;
}

bool ClockGameParser::parseMoves(const std::string &movetext,
                                 int white_elo,
                                 int black_elo,
                                 float initial_time,
                                 float increment,
                                 std::vector<ClockPositionRecord> &records) const {
  chess::Board board;
  std::vector<std::string> recent_moves;

  // Track clock states for both players
  float white_clock = initial_time;
  float black_clock = initial_time;
  float prev_white_clock = initial_time;
  float prev_black_clock = initial_time;

  // Track if this is the first move for each player
  bool white_first_move = true;
  bool black_first_move = true;

  // Parse moves
  std::regex move_regex(R"raw((\d+\.(?:\.\.)?\s*)?([a-hKQRBNO][a-h0-9xO\-\+\#\=]+)\s*(\{[^\}]*\})?)raw");
  std::sregex_iterator it(movetext.begin(), movetext.end(), move_regex);
  std::sregex_iterator end;

  while (it != end) {
    std::string move_str = (*it)[2].str();
    std::string comment = (*it)[3].str();

    // Parse the move
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);

    chess::Move move = chess::Move::NO_MOVE;
    for (const auto &legal_move: moves) {
      // Convert to SAN and compare
      if (std::string san = chess::uci::moveToSan(board, legal_move); san == move_str) {
        move = legal_move;
        break;
      }
    }

    if (move == chess::Move::NO_MOVE) {
      ++it;
      continue; // Skip invalid moves
    }

    // Check if we have clock data
    if (std::string clock_str = PgnParsingUtils::extractClockFromComment(comment); !clock_str.empty()) {
      if (float clock_time = PgnParsingUtils::parseClockTime(clock_str); clock_time >= 0) {
        // Save FEN BEFORE the move
        std::string fen_before_move = board.getFen();

        // Create a copy of recent_moves and add the current move
        std::vector<std::string> moves_including_current = recent_moves;
        moves_including_current.push_back(chess::uci::moveToUci(move));

        // Keep only the last N moves
        if (moves_including_current.size() > max_recent_moves_) {
          moves_including_current.erase(moves_including_current.begin());
        }

        // Create position record with FEN BEFORE the move
        ClockPositionRecord record;
        record.fen = fen_before_move;
        record.recent_moves = moves_including_current;

        // Determine whose move it is (based on current board state BEFORE the move)

        if (board.sideToMove() == chess::Color::WHITE) {
          // White is about to move
          // Validate clock increase before updating
          if (!isValidClockIncrease(clock_time, prev_white_clock, increment, white_first_move)) {
            return false;
          }

          white_clock = clock_time;
          record.rating = white_elo;
          record.player_clock = prev_white_clock; // Clock BEFORE the move
          record.opponent_clock = black_clock;
          record.thinking_time = prev_white_clock + increment - white_clock;
          prev_white_clock = white_clock;
          white_first_move = false; // Mark that White has made their first move
        }
        else {
          // Black is about to move
          // Validate clock increase before updating
          if (!isValidClockIncrease(clock_time, prev_black_clock, increment, black_first_move)) {
            return false;
          }

          black_clock = clock_time;
          record.rating = black_elo;
          record.player_clock = prev_black_clock; // Clock BEFORE the move
          record.opponent_clock = white_clock;
          record.thinking_time = prev_black_clock + increment - black_clock;
          prev_black_clock = black_clock;
          black_first_move = false; // Mark that Black has made their first move
        }

        record.increment = increment;

        // Ensure thinking time is non-negative
        if (record.thinking_time < 0) {
          record.thinking_time = 0;
        }

        records.push_back(record);
      }
    }

    // Apply the move
    board.makeMove(move);

    // Update recent moves
    PgnParsingUtils::updateRecentMoves(recent_moves, move, max_recent_moves_);

    ++it;
  }

  return !records.empty();
}

bool ClockGameParser::isValidClockIncrease(float current_clock,
                                           float previous_clock,
                                           float increment,
                                           bool first_move) {
  if (first_move) {
    return true; // Skip first move (no previous clock to compare)
  }

  // Check for invalid clock increases
  if (float clock_increase = current_clock - previous_clock; (increment == 0.0f && clock_increase > 0.0f) ||
                                                             (increment > 0.0f && clock_increase >= 2.0f * increment)) {
    return false;
  }

  return true;
}
} // namespace chessmimic::clock_converter
