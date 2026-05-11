#pragma once

#include <string>
#include <unordered_set>

namespace chessmimic::clock_converter {

/**
 * Loads common positions from FEN-only JSONL file for filtering during validation.
 * Only supports FEN-only common position files.
 */
class CommonPositionLoader {
public:
    /**
     * Load common FEN positions from file.
     * 
     * @param fen_only_jsonl_path Path to FEN-only common positions JSONL file
     */
    explicit CommonPositionLoader(const std::string& fen_only_jsonl_path);
    
    /**
     * Check if a position key's FEN is in the common positions set.
     * Extracts FEN from position key (format: FEN|recent_moves)
     * 
     * @param position_key The position key to check
     * @return true if position's FEN is common
     */
    bool isCommon(const std::string& position_key) const;
    
    /**
     * Get number of loaded FENs.
     */
    size_t size() const { return common_fens_.size(); }
    
private:
    std::unordered_set<std::string> common_fens_;
};

} // namespace chessmimic::clock_converter