#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <deque>
#include <functional>
#include <utility>

#include "external/chess.hpp"
#include "position_record.hpp"

namespace chessmimic {
// Custom PGN visitor implementing chess-library's visitor interface
class ChessPgnVisitor final : public chess::pgn::Visitor {
public:
    using EndPgnCallback = std::function<void(ChessPgnVisitor&)>;

    ChessPgnVisitor(int min_rating, int max_rating, int min_ply,
                    int recent_moves_ply, int min_clock_seconds);

    // Visitor interface implementation
    void startPgn() override;
    void header(std::string_view key, std::string_view value) override;
    void startMoves() override;
    void move(std::string_view move_sv, std::string_view comment_sv) override;
    void endPgn() override;


    // Results after parsing a game
    bool isValidGame() const;
    std::vector<std::pair<std::string, PositionRecord>> getPositionRecords() const;

    // Callback for endPgn to provide more control
    void setEndPgnCallback(const EndPgnCallback& callback) {
        end_pgn_callback_ = callback;
    }

private:
    // Game state
    chess::Board board_;
    std::unordered_map<std::string, std::string> headers_;
    std::vector<std::string> move_history_;
    std::deque<std::string> recent_moves_;

    // Position data
    std::unordered_map<std::string, PositionRecord> position_data_;

    // Configuration parameters
    int min_rating_;
    int max_rating_;
    int min_ply_;
    int recent_moves_ply_;
    int min_clock_seconds_;

    // Flags
    bool valid_game_ = false;

    // Callback for end of game processing
    EndPgnCallback end_pgn_callback_ = nullptr;

    // Utility methods
    void recordMove(const std::string& move_str, float clock_time);
    static std::string generatePositionKey(const std::vector<std::string>& recent, const std::string& fen);
};
} // namespace chessmimic
