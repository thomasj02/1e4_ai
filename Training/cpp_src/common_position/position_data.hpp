#pragma once

#include <string>
#include <unordered_map>
#include <simdjson.h>

namespace chessmimic {

struct PositionData {
    std::string fen;
    std::unordered_map<std::string, int64_t> moves;
    int64_t total = 0;
    
    // Convert to JSON
    [[nodiscard]] std::string toJson() const;
    
    // Parse from JSON
    static PositionData fromJson(const std::string& json_str);
};

} // namespace chessmimic