#pragma once

#include <string>
#include <vector>
#include <mutex>
#include "absl/container/flat_hash_map.h"
#include <simdjson.h>

namespace chessmimic {

// Struct to represent a compound key for move data
struct MoveKey {
    std::string move;
    std::string clock;
    std::string rating;
    
    // Constructor
    MoveKey(std::string m, std::string c, std::string r) 
        : move(std::move(m)), clock(std::move(c)), rating(std::move(r)) {}
    
    // Equality operator for hash map
    bool operator==(const MoveKey& other) const {
        return move == other.move && clock == other.clock && rating == other.rating;
    }
    
    // Hash function
    template <typename H>
    friend H AbslHashValue(H h, const MoveKey& key) {
        return H::combine(std::move(h), key.move, key.clock, key.rating);
    }
};

// Struct to represent an aggregated record in memory
struct AggregatedRecord {
    // Recent moves leading to the position
    std::vector<std::string> recent_moves;
    
    // FEN string representing the position
    std::string fen;
    
    // Flat map of move data: MoveKey -> count
    absl::flat_hash_map<MoveKey, int64_t> moves;
    
    // Default constructor
    AggregatedRecord() = default;
    
    // Check if the record is empty
    bool empty() const {
        return moves.empty();
    }
    
    // Clear the record
    void clear() {
        recent_moves.clear();
        fen.clear();
        moves.clear();
    }
};

class RecordProcessor {
public:
    /**
     * Reads a record from an input stream
     * 
     * @param in The input stream to read from
     * @param key The string to store the record key in
     * @param record The string to store the record content in
     * @return True if a record was successfully read, false otherwise
     */
    static bool readRecord(std::ifstream& in, std::string& key, std::string& record);

    /**
     * Merges a source record JSON string into a target AggregatedRecord
     * 
     * @param target The target AggregatedRecord that will be updated
     * @param source The source record JSON string containing data to merge in
     */
    static void mergeRecord(AggregatedRecord& target, const std::string& source);
    
    /**
     * Converts an AggregatedRecord to a JSON string
     * 
     * @param record The AggregatedRecord to convert
     * @return The JSON string representation
     */
    static std::string aggregatedRecordToJson(const AggregatedRecord& record);

    /**
     * Counts the total number of moves in a record
     * 
     * @param record The AggregatedRecord to count moves in
     * @return The total number of moves
     */
    static int countTotalMoves(const AggregatedRecord& record);

    /**
     * Writes a raw record to an output stream (no filtering)
     * 
     * @param out The output stream to write to
     * @param key The record key
     * @param record The record JSON string to write
     */
    static void writeRawRecord(std::ofstream& out, const std::string& key, 
                              const std::string& record);
    
    /**
     * Writes a record to an output stream, applying max_moves_per_position filtering
     * If enabled, it will track FENs with more than max_moves_per_position for later output
     * 
     * @param out The output stream to write to
     * @param key The record key
     * @param record The AggregatedRecord to write
     * @param max_moves_per_position Maximum number of moves (0 for no limit)
     * @param track_common_positions Whether to track positions that have too many moves
     */
    static void writeRecord(std::ofstream& out, const std::string& key, 
                           const AggregatedRecord& record, int max_moves_per_position,
                           bool track_common_positions = false);

    /**
     * Extracts the pure FEN string from a position key
     * 
     * @param key The position key containing the FEN
     * @return The extracted pure FEN string
     */
    static std::string extractFenFromKey(const std::string& key);

    /**
     * Writes the set of common move positions to a file
     * 
     * @param filepath Path to the output file
     * @return Number of positions written
     */
    static int writeCommonMovesFile(const std::string& filepath);

    /**
     * Clears the set of common move positions
     */
    static void clearCommonMoves();

    /**
     * Reads a file containing common moves and loads them into memory
     * 
     * @param filepath Path to the input file
     * @return Number of positions read, -1 if file could not be opened
     */
    static int readCommonMovesFile(const std::string& filepath);

    /**
     * Checks if a position key is in the common moves list
     * 
     * @param key The position key to check
     * @return True if the position is in the common moves list, false otherwise
     */
    static bool isCommonMove(const std::string& key);

private:
    // Map to track unique FENs that have more than max_moves_per_position and their move breakdowns
    static absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, int>> common_move_positions_;
    
    // Mutex to protect concurrent access to common_move_positions_
    static std::mutex common_moves_mutex_;
};

} // namespace chessmimic