#include "pgn/pgn_parsing_utils.hpp"
#include "external/chess.hpp"
#include <sstream>
#include <cctype>
#include <vector>
#include <regex>

namespace chessmimic::PgnParsingUtils {

float parseClockTime(const std::string& clock_str) {
    // Remove whitespace
    std::string trimmed;
    for (char c : clock_str) {
        if (!std::isspace(c)) {
            trimmed += c;
        }
    }

    // Try different formats

    // Format: pure seconds (e.g., "297.0" or "293")
    if (trimmed.find(':') == std::string::npos) {
        try {
            return std::stof(trimmed);
        } catch (...) {
            return -1.0f;
        }
    }

    // Format: H:MM:SS or M:SS or M:SS.s
    std::vector<std::string> parts;
    std::stringstream ss(trimmed);
    std::string part;

    while (std::getline(ss, part, ':')) {
        parts.push_back(part);
    }

    if (parts.empty() || parts.size() > 3) {
        return -1.0f;
    }

    try {
        float seconds = 0.0f;

        if (parts.size() == 3) {
            // H:MM:SS
            seconds += std::stof(parts[0]) * 3600;  // hours
            seconds += std::stof(parts[1]) * 60;    // minutes
            seconds += std::stof(parts[2]);          // seconds
        } else if (parts.size() == 2) {
            // M:SS or MM:SS
            seconds += std::stof(parts[0]) * 60;    // minutes
            seconds += std::stof(parts[1]);          // seconds
        } else {
            // Just seconds
            seconds = std::stof(parts[0]);
        }

        return seconds;
    } catch (...) {
        return -1.0f;
    }
}

float parseTimeControl(const std::string& time_control, float& increment) {
    increment = 0.0f;

    // Handle "seconds+increment" format
    if (size_t plus_pos = time_control.find('+'); plus_pos != std::string::npos) {
        try {
            float base_time = std::stof(time_control.substr(0, plus_pos));
            increment = std::stof(time_control.substr(plus_pos + 1));
            return base_time;
        } catch (...) {
            return -1.0f;
        }
    }

    // Handle single number (no increment)
    try {
        return std::stof(time_control);
    } catch (...) {
        return -1.0f;
    }
}

std::string extractClockFromComment(const std::string& comment) {
    // Fast path: check if [%clk is present
    size_t start = comment.find("[%clk");
    if (start == std::string::npos) {
        return "";
    }

    // Move past "[%clk"
    start += 5;

    // Skip whitespace
    while (start < comment.length() && std::isspace(comment[start])) {
        ++start;
    }

    // Find the closing ]
    size_t end = comment.find(']', start);
    if (end == std::string::npos || end <= start) {
        return "";
    }

    // Extract the clock value
    return comment.substr(start, end - start);
}

void updateRecentMoves(
    std::vector<std::string>& recent_moves,
    const chess::Move& move,
    size_t max_recent_moves
) {
    // Convert move to UCI format
    std::string uci_move = chess::uci::moveToUci(move);

    // Add to recent moves
    recent_moves.push_back(uci_move);

    // Keep only the last max_recent_moves
    if (recent_moves.size() > max_recent_moves) {
        recent_moves.erase(recent_moves.begin());
    }
}

PgnHeaders parsePgnHeaders(const std::string& pgn) {
    PgnHeaders headers;

    // Extract headers using regex
    std::regex header_regex(R"raw(\[(\w+)\s+"([^"]+)"\])raw");
    std::sregex_iterator it(pgn.begin(), pgn.end(), header_regex);
    std::sregex_iterator end;

    while (it != end) {
        std::string key = (*it)[1].str();
        std::string value = (*it)[2].str();

        if (key == "Result") {
            headers.result = value;
            headers.has_result = true;
        } else if (key == "TimeControl") {
            headers.initial_time = parseTimeControl(value, headers.increment);
            headers.has_time_control = headers.initial_time > 0;
        } else if (key == "WhiteElo") {
            try {
                headers.white_elo = std::stoi(value);
                headers.has_white_elo = true;
            } catch (...) {
                headers.has_white_elo = false;
            }
        } else if (key == "BlackElo") {
            try {
                headers.black_elo = std::stoi(value);
                headers.has_black_elo = true;
            } catch (...) {
                headers.has_black_elo = false;
            }
        }

        ++it;
    }

    return headers;
}

int parseResult(const std::string& result) {
    if (result == "1-0") {
        return 1;  // White wins
    }
    if (result == "0-1") {
        return -1;  // Black wins
    }
    if (result == "1/2-1/2") {
        return 0;  // Draw
    }
    return -999;  // Invalid result (e.g., "*")
}

} // namespace chessmimic::PgnParsingUtils
