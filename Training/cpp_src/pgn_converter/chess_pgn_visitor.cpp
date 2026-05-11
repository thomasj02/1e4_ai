#include "chess_pgn_visitor.hpp"
#include <iomanip>
#include <sstream>
#include "../utils/logger.hpp"

namespace chessmimic {
ChessPgnVisitor::ChessPgnVisitor(int min_rating, int max_rating, int min_ply,
                                 int recent_moves_ply, int min_clock_seconds)
    : min_rating_(min_rating), max_rating_(max_rating), min_ply_(min_ply),
      recent_moves_ply_(recent_moves_ply), min_clock_seconds_(min_clock_seconds) {
}

void ChessPgnVisitor::startPgn() {
    // Reset state for new game
    board_ = chess::Board();
    headers_.clear();
    move_history_.clear();
    recent_moves_.clear();
    position_data_.clear();
    valid_game_ = true;
}

void ChessPgnVisitor::header(std::string_view key, std::string_view value) {
    headers_[std::string(key)] = std::string(value);
}

void ChessPgnVisitor::startMoves() {
    // Check if game meets rating criteria
    if (headers_.contains("WhiteElo") && headers_.contains("BlackElo")) {
        try {
            int white_elo = std::stoi(headers_["WhiteElo"]);
            if (int black_elo = std::stoi(headers_["BlackElo"]); white_elo < min_rating_ || white_elo > max_rating_ ||
                black_elo < min_rating_ || black_elo > max_rating_) {
                valid_game_ = false;
                skipPgn(true);
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Caught exception parinsg headers: " << e.what() << std::endl;
            valid_game_ = false;
            skipPgn(true);
        }
    }
    else {
        valid_game_ = false;
        skipPgn(true);
    }

    // Check game termination
    if (!headers_.contains("Termination") ||
        (headers_["Termination"] != "Normal" && headers_["Termination"] != "Time forfeit")) {
        valid_game_ = false;
        skipPgn(true);
    }
}

void ChessPgnVisitor::move(std::string_view move_sv, std::string_view comment_sv) {
    std::string comment(comment_sv);

    // Parse clock time from comment if available
    float clock_time = 0.0f;

    // Look for [%clk 0:01:23] pattern
    std::string clk_pattern = "[%clk ";
    if (auto clk_pos = comment.find(clk_pattern); clk_pos != std::string::npos) {
        auto time_start = clk_pos + clk_pattern.length();
        if (auto time_end = comment.find(']', time_start); time_end != std::string::npos) {
            std::string time_str = comment.substr(time_start, time_end - time_start);

            // Parse time in format H:MM:SS
            auto first_colon = time_str.find(':');
            if (auto second_colon = time_str.find(':', first_colon + 1); first_colon != std::string::npos &&
                second_colon != std::string::npos) {
                try {
                    int hours = std::stoi(time_str.substr(0, first_colon));
                    int minutes = std::stoi(time_str.substr(first_colon + 1, second_colon - first_colon - 1));
                    float seconds = std::stof(time_str.substr(second_colon + 1));

                    clock_time = static_cast<float>(hours) * 3600 + static_cast<float>(minutes) * 60 + seconds;
                }
                catch (const std::exception& e) {
                    // If parsing fails, use default 0 and log a warning
                    g_logger.warning("Failed to parse clock time in comment: [{}] - {}", comment, e.what());
                    clock_time = 0.0f;
                }
            }
        }
    }

    // Record this move
    recordMove(std::string(move_sv), clock_time);
}

void ChessPgnVisitor::endPgn() {
    // Reset skip flag
    skipPgn(false);

    // Call the callback if it's set
    if (end_pgn_callback_) {
        end_pgn_callback_(*this);
    }
}

bool ChessPgnVisitor::isValidGame() const {
    return valid_game_;
}


std::vector<std::pair<std::string, PositionRecord>> ChessPgnVisitor::getPositionRecords() const {
    std::vector<std::pair<std::string, PositionRecord>> records;
    records.reserve(position_data_.size());

    for (const auto& [key, record] : position_data_) {
        records.emplace_back(key, record);
    }

    return records;
}

void ChessPgnVisitor::recordMove(const std::string& move_str, float clock_time) {
    // Create a string copy of the move
    std::string original_move_str = move_str; // Keep the original move string for logging

    // Get current state before trying to make the move
    std::string fen = board_.getFen();
    std::vector recent_moves_vec(recent_moves_.begin(), recent_moves_.end());

    // Try to parse the move
    std::string move_uci; // UCI notation for move if successful

    // First try to parse as SAN notation
    auto move = chess::uci::parseSan(board_, move_str);
    if (move != chess::Move::NO_MOVE) {
        // Successfully parsed, get the UCI notation
        move_uci = chess::uci::moveToUci(move);
    }

    // If SAN parsing failed, try UCI notation
    if (move == chess::Move::NO_MOVE) {
        try {
            move = chess::uci::uciToMove(board_, move_str);
            if (move != chess::Move::NO_MOVE) {
                move_uci = move_str; // It's already in UCI format
            }
        }
        catch (const std::exception& e) {
            // UCI parsing failed, log a warning
            g_logger.warning("Failed to parse move in UCI format: [{}] - {}", move_str, e.what());
        }
    }

    // Special case for castling
    if (move == chess::Move::NO_MOVE && (move_str == "O-O" || move_str == "0-0")) {
        // Try to find kingside castling move
        chess::Movelist legal_moves;
        chess::movegen::legalmoves(legal_moves, board_);

        for (auto legal_move : legal_moves) {
            if (legal_move.typeOf() == chess::Move::CASTLING &&
                legal_move.to() > legal_move.from()) {
                move = legal_move;
                move_uci = chess::uci::moveToUci(move);
                break;
            }
        }
    }
    else if (move == chess::Move::NO_MOVE && (move_str == "O-O-O" || move_str == "0-0-0")) {
        // Try to find queenside castling move
        chess::Movelist legal_moves;
        chess::movegen::legalmoves(legal_moves, board_);

        for (auto legal_move : legal_moves) {
            if (legal_move.typeOf() == chess::Move::CASTLING &&
                legal_move.to() < legal_move.from()) {
                move = legal_move;
                move_uci = chess::uci::moveToUci(move);
                break;
            }
        }
    }

    // Determine which format to use for move history
    std::string history_move;
    bool use_uci = !move_uci.empty(); // whether we should use UCI format

    // For consistency, we'll use UCI format everywhere if we have it
    if (use_uci) {
        history_move = move_uci;
    }
    else {
        // Fall back to the original format if we couldn't parse
        history_move = original_move_str;
    }

    // Update move history with the appropriate format
    move_history_.push_back(history_move);

    // Update recent moves queue with the appropriate format
    recent_moves_.push_back(history_move);
    if (static_cast<int>(recent_moves_.size()) > recent_moves_ply_) {
        recent_moves_.pop_front();
    }

    // Check if we should record this position
    if (clock_time >= static_cast<float>(min_clock_seconds_) && static_cast<int>(recent_moves_vec.size()) >= min_ply_) {
        // This position satisfies our criteria for recording

        // Create position key
        std::string position_key = generatePositionKey(recent_moves_vec, fen);

        // Get active player's ELO
        int player_elo = board_.sideToMove() == chess::Color::WHITE
                             ? std::stoi(headers_["WhiteElo"])
                             : std::stoi(headers_["BlackElo"]);

        // Create or update position record
        // ReSharper disable once CppUseStructuredBinding
        auto& record = position_data_[position_key];
        record.recent_moves = recent_moves_vec;
        record.fen = fen;

        // Add this move to the record - always use UCI format if available for consistency
        std::string recorded_move = use_uci ? move_uci : original_move_str;
        std::string clock_str = [&clock_time] {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1) << clock_time;
            return ss.str();
        }();
        std::string rating_str = std::to_string(player_elo);
        record.moves[recorded_move][clock_str][rating_str]++;
    }

    // Update the board state if we successfully parsed the move
    if (move != chess::Move::NO_MOVE) {
        try {
            board_.makeMove(move);
        }
        catch (const std::exception& e) {
            // If makeMove fails, log a warning but continue processing
            g_logger.warning("Error making move {}: {}", original_move_str, e.what());
        }
    }
    else {
        // If we couldn't parse the move, print a warning but continue
        // This is a best-effort approach - we'll collect what we can
        g_logger.warning("Could not parse move: {}", original_move_str);
    }
}

std::string ChessPgnVisitor::generatePositionKey(const std::vector<std::string>& recent, const std::string& fen) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < recent.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << recent[i] << "\"";
    }
    oss << ",\"" << fen << "\"]";
    return oss.str();
}
} // namespace chessmimic
