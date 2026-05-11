#include <fstream>
#include <regex>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <array>
#include <cstring>
#include <cerrno>
#include <simdjson.h>
#include <fmt/format.h>
#include "absl/container/flat_hash_map.h"
#include "../utils/logger.hpp"
#include "../shuffle/bucket_record.hpp"
#include "record_processor.hpp"

namespace chessmimic {
// Initialize the static member for tracking common move positions and their move breakdowns
absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, int>> RecordProcessor::common_move_positions_;

// Initialize the static mutex for thread-safe access to common_move_positions_
std::mutex RecordProcessor::common_moves_mutex_;

// Lookup tables for fast JSON escaping
static constexpr std::array<bool, 256> kNeedsEscape = []{
    std::array<bool, 256> tbl{};
    for (unsigned i = 0; i < 0x20; ++i) tbl[i] = true;   // control chars
    tbl['"']  = tbl['\\'] = true;
    return tbl;
}();

static constexpr std::array<std::array<char, 4>, 0x20> kControlEsc = []{
    std::array<std::array<char, 4>, 0x20> tbl{};
    for (unsigned v = 0; v < 0x20; ++v) {
        constexpr char hex[] = "0123456789abcdef";
        tbl[v] = { {'0','0', hex[v >> 4], hex[v & 0xF]} };   // 00xx
    }
    return tbl;
}();

// Helper function to escape JSON strings
static std::string escapeJsonString(const std::string& input) {
    // Worst-case every byte needs a six-byte escape (\,u,0,0,hex,hex)
    std::string out;
    out.reserve(input.size() * 6);

    for (unsigned char c : input) {
        if (!kNeedsEscape[c]) {          // fast path: nothing to do
            out.push_back(static_cast<char>(c));
            continue;
        }

        switch (c) {                     // common single-char escapes
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;

            default:                    // any other control char < 0x20
                out += "\\u00";
                const auto& tail = kControlEsc[c];
                out.append(tail.data(), 4);
                break;
        }
    }
    return out;
}

bool RecordProcessor::readRecord(std::ifstream& in, std::string& key, std::string& record) {
    // First read the record size (for skipping)
    uint32_t record_size;
    in.read(reinterpret_cast<char*>(&record_size), sizeof(record_size));
    if (!in || in.eof()) return false;

    // Read key length
    uint32_t key_len;
    in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
    if (!in || in.eof()) return false;

    // Read key
    key.resize(key_len);
    in.read(&key[0], key_len);
    if (!in || in.eof()) return false;

    // Read record length
    uint32_t record_len;
    in.read(reinterpret_cast<char*>(&record_len), sizeof(record_len));
    if (!in || in.eof()) return false;

    // Read record
    record.resize(record_len);
    in.read(&record[0], record_len);
    if (!in || in.eof()) return false;

    return true;
}

void RecordProcessor::mergeRecord(AggregatedRecord& target, const std::string& source) {
    thread_local simdjson::dom::parser source_parser;
    
    // Parse source JSON
    simdjson::dom::element source_doc = source_parser.parse(source);
    
    // Get recent_and_fen - use from source if available, otherwise keep target
    if (source_doc["recent_and_fen"].error() == simdjson::SUCCESS) {
        // Handle array format: [[moves...], fen]
        auto recent_and_fen_array = source_doc["recent_and_fen"].get_array();
        
        // First element is the array of recent moves
        target.recent_moves.clear();
        for (auto move : recent_and_fen_array.at(0).get_array()) {
            target.recent_moves.emplace_back(move.get_string().value());
        }
        
        // Second element is the FEN string
        target.fen = std::string(recent_and_fen_array.at(1).get_string().value());
    }
    
    // Process source moves and merge if they exist
    if (source_doc["moves"].error() == simdjson::SUCCESS) {
        for (auto move : source_doc["moves"].get_object()) {
            std::string move_str(move.key);
            for (auto clock : move.value.get_object()) {
                std::string clock_str(clock.key);
                for (auto rating : clock.value.get_object()) {
                    std::string rating_str(rating.key);
                    auto count = rating.value.get_int64().value();
                    target.moves[MoveKey(move_str, clock_str, rating_str)] += count;
                }
            }
        }
    }
}

std::string RecordProcessor::aggregatedRecordToJson(const AggregatedRecord& record) {
    std::ostringstream result;
    result << "{";
    
    // Write recent_and_fen in array format: [[moves...], fen]
    result << "\"recent_and_fen\":[[";
    
    // Write recent moves array
    bool first_move = true;
    for (const auto& move : record.recent_moves) {
        if (!first_move) result << ",";
        result << "\"" << escapeJsonString(move) << "\"";
        first_move = false;
    }
    
    result << "],\"" << escapeJsonString(record.fen) << "\"],";
    
    // Start moves object
    result << "\"moves\":{";
    
    // We need to reconstruct the nested structure for JSON output
    // Group by move -> clock -> rating
    absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, int64_t>>> output_structure;
    
    // Reorganize flat map into nested structure for output
    for (const auto& [key, count] : record.moves) {
        output_structure[key.move][key.clock][key.rating] = count;
    }

    // Write the nested structure to JSON
    bool first_move_key = true;
    for (const auto& [move, clock_data] : output_structure) {
        if (!first_move_key) result << ",";
        first_move_key = false;
        
        result << "\"" << escapeJsonString(move) << "\":{";
        
        bool first_clock = true;
        for (const auto& [clock, rating_data] : clock_data) {
            if (!first_clock) result << ",";
            first_clock = false;
            
            result << "\"" << escapeJsonString(clock) << "\":{";
            
            bool first_rating = true;
            for (const auto& [rating, count] : rating_data) {
                if (!first_rating) result << ",";
                first_rating = false;
                
                result << "\"" << escapeJsonString(rating) << "\":" << count;
            }
            
            result << "}";
        }
        
        result << "}";
    }
    
    result << "}}";

    return result.str();
}

int RecordProcessor::countTotalMoves(const AggregatedRecord& record) {
    int total = 0;
    
    for (const auto& count : record.moves | std::views::values) {
        total += count;
    }
    
    return total;
}

void RecordProcessor::writeRawRecord(std::ofstream& out, const std::string& key, 
                                    const std::string& record) {
    // First write the record size (so we can read it during shuffling)
    uint32_t record_size = sizeof(uint32_t) + key.size() + sizeof(uint32_t) + record.size();
    out.write(reinterpret_cast<const char*>(&record_size), sizeof(record_size));
    if (!out.good()) {
        throw std::runtime_error(
            "Failed to write record size. "
            "errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
        );
    }

    // Write key
    uint32_t key_len = key.size();
    out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
    if (!out.good()) {
        throw std::runtime_error(
            "Failed to write key length. "
            "errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
        );
    }
    
    out.write(key.data(), key_len);
    if (!out.good()) {
        throw std::runtime_error(
            "Failed to write key data. "
            "errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
        );
    }

    // Write record
    uint32_t record_len = record.size();
    out.write(reinterpret_cast<const char*>(&record_len), sizeof(record_len));
    if (!out.good()) {
        throw std::runtime_error(
            "Failed to write record length. "
            "errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
        );
    }
    
    out.write(record.data(), record_len);
    if (!out.good()) {
        throw std::runtime_error(
            "Failed to write record data. "
            "errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
        );
    }
}

std::string RecordProcessor::extractFenFromKey(const std::string& key) {
    // The key is a JSON array: [recent_moves..., fen]
    // We need to extract the FEN which is the last element
    try {
        thread_local simdjson::dom::parser parser;

        if (simdjson::dom::element doc = parser.parse(key); doc.is_array()) {
            auto arr = doc.get_array();
            // Get the last element which should be the FEN
            std::string fen;
            for (auto elem : arr) {
                fen = std::string(elem);
            }
            return fen;
        }
    } catch (const std::exception&) {
        // If parsing fails, assume the key is already a FEN
    }
    
    // Fallback: assume the key itself is the FEN
    return key;
}

void RecordProcessor::writeRecord(std::ofstream& out, const std::string& key,
                                  const AggregatedRecord& record, int max_moves_per_position,
                                  bool track_common_positions) {
    // Check if this is a common move we should skip
    if (isCommonMove(key)) {
        return; // Skip this record
    }

    // Check if this position has too many moves
    if (max_moves_per_position > 0) {
        if (int total_moves = countTotalMoves(record); total_moves > max_moves_per_position) {
            // Track this position if tracking is enabled
            if (track_common_positions) {
                std::string fen = extractFenFromKey(key);

                // Extract move breakdown from the struct
                absl::flat_hash_map<std::string, int> move_counts;

                // Sum moves by move name across all clock times and ratings
                for (const auto& [move_key, count] : record.moves) {
                    move_counts[move_key.move] += count;
                }

                // Store the FEN with its move breakdown (thread-safe)
                {
                    std::lock_guard lock(common_moves_mutex_);
                    common_move_positions_[fen] = move_counts;
                }
            }

            // Skip writing this record if it has too many moves
            return;
        }
    }

    // Convert AggregatedRecord to JSON
    std::string record_json = aggregatedRecordToJson(record);

    // First write the record size (so we can read it during shuffling)
    uint32_t record_size = sizeof(uint32_t) + key.size() + sizeof(uint32_t) + record_json.size();
    out.write(reinterpret_cast<const char*>(&record_size), sizeof(record_size));
    if (!out.good()) {
        throw std::runtime_error(
            "Failed to write record size in writeRecord. "
            "errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
        );
    }

    // Write key
    uint32_t key_len = key.size();
    out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
    if (!out.good()) {
        throw std::runtime_error(
            "Failed to write key length in writeRecord. "
            "errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
        );
    }
    
    out.write(key.data(), key_len);
    if (!out.good()) {
        throw std::runtime_error(
            "Failed to write key data in writeRecord. "
            "errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
        );
    }

    // Write record
    uint32_t record_len = record_json.size();
    out.write(reinterpret_cast<const char*>(&record_len), sizeof(record_len));
    if (!out.good()) {
        throw std::runtime_error(
            "Failed to write record length in writeRecord. "
            "errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
        );
    }
    
    out.write(record_json.data(), record_len);
    if (!out.good()) {
        throw std::runtime_error(
            "Failed to write record data in writeRecord. "
            "errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
        );
    }
}

int RecordProcessor::writeCommonMovesFile(const std::string& filepath) {
    // Open output file
    std::ofstream out(filepath);
    if (!out.is_open()) {
        return -1; // Failed to open file
    }

    int count = 0;
    // Write each position as a JSONL record (thread-safe)
    std::lock_guard lock(common_moves_mutex_);
    for (const auto& [fen, moves] : common_move_positions_) {
        std::ostringstream record;
        record << "{\"fen\":\"" << escapeJsonString(fen) << "\",\"moves\":{";
        
        bool first_move = true;
        for (const auto& [move, move_count] : moves) {
            if (!first_move) record << ",";
            first_move = false;
            record << "\"" << escapeJsonString(move) << "\":" << move_count;
        }
        
        record << "}}";
        out << record.str() << "\n";
        
        // Check for write errors
        if (!out.good()) {
            throw std::runtime_error(
                "Failed to write common moves record to file: " + filepath +
                ", errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
            );
        }
        
        count++;
    }
    
    // Flush and check for final errors
    out.flush();
    if (!out.good()) {
        throw std::runtime_error(
            "Failed to flush common moves file: " + filepath +
            ", errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
        );
    }

    return count;
}

void RecordProcessor::clearCommonMoves() {
    std::lock_guard lock(common_moves_mutex_);
    common_move_positions_.clear();
}

int RecordProcessor::readCommonMovesFile(const std::string& filepath) {
    // Open input file
    std::ifstream in(filepath);
    if (!in.is_open()) {
        return -1; // Failed to open file
    }

    // Clear existing common moves
    clearCommonMoves();

    // Read and process the file
    std::string line;
    int count = 0;
    thread_local simdjson::dom::parser parser;

    while (std::getline(in, line)) {
        try {
            simdjson::dom::element json = parser.parse(line);
            auto fen = std::string(json["fen"]);
            absl::flat_hash_map<std::string, int> moves;
            
            for (auto move : json["moves"].get_object()) {
                std::string move_str(move.key);
                auto move_count = move.value.get_int64().value();
                moves[move_str] = move_count;
            }
            
            {
                std::lock_guard lock(common_moves_mutex_);
                common_move_positions_[fen] = moves;
            }
            count++;
        }
        catch (const std::exception& e) {
            throw std::runtime_error("Error parsing common moves file at line " +
                std::to_string(count + 1) + ": " + e.what());
        }
    }

    return count;
}

bool RecordProcessor::isCommonMove(const std::string& key) {
    // Extract FEN from the key first
    std::string fen = extractFenFromKey(key);
    std::lock_guard lock(common_moves_mutex_);
    return common_move_positions_.contains(fen);
}
} // namespace chessmimic
