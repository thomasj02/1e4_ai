#pragma once

#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include "clock_position_record.hpp"

namespace chessmimic {
    class BagzRecordWriter;
}

namespace chessmimic::clock_converter {

// Forward declarations
class CommonPositionWriter;
class CommonPositionLoader;

/**
 * Performs K-way merge of sorted chunk files with group counting.
 * Routes records to either BAGZ output or common position files
 * based on position frequency.
 */
class SortedMergeWriter {
public:
    struct Config {
        size_t max_positions_per_key = 25;
        std::string output_bagz_path;
        std::string output_records_path;  // For intermediate format output
        std::string common_positions_with_history_path;
        std::string common_positions_fen_only_path;
        bool write_common_positions = false;
        bool skip_common_positions = false;
    };
    
    struct Statistics {
        size_t total_records_processed = 0;
        size_t unique_positions = 0;
        size_t common_positions_found = 0;
        size_t records_written_to_bagz = 0;
        size_t records_written_to_common = 0;
        size_t records_skipped = 0;
    };
    
    /**
     * Create a sorted merge writer with configuration.
     */
    explicit SortedMergeWriter(Config  config);
    
    ~SortedMergeWriter();
    
    /**
     * Merge sorted chunk files and write output.
     * 
     * @param chunk_files List of sorted chunk file paths
     */
    void mergeChunks(const std::vector<std::string>& chunk_files);
    
    /**
     * Get merge statistics.
     */
    [[nodiscard]] Statistics getStatistics() const { return stats_; }
    
private:
    // Chunk reader for K-way merge
    struct ChunkReader {
        std::ifstream file;
        std::string current_key;
        ClockPositionRecord current_record;
        size_t chunk_id;
        bool has_next = false;
        simdjson::dom::parser json_parser; // Reusable parser for performance
        
        bool readNext();
    };
    
    // Comparator for min-heap
    struct ChunkComparator {
        bool operator()(const ChunkReader* a, const ChunkReader* b) const {
            return a->current_key > b->current_key; // Min heap
        }
    };
    
    // Extract FEN from position key
    static std::string extractFenFromKey(const std::string& key);
    
    // Process a group of records with the same key
    void processPositionGroup(const std::string& key,
                            const std::vector<ClockPositionRecord>& records,
                            bool fen_is_common);
    
    // Process all groups for a FEN
    void processFenGroups(const std::string& fen,
                         const std::vector<std::pair<std::string, std::vector<ClockPositionRecord>>>& groups);
    
    Config config_;
    Statistics stats_;
    
    std::unique_ptr<BagzRecordWriter> bagz_writer_;
    std::unique_ptr<CommonPositionWriter> common_writer_with_history_;
    std::unique_ptr<CommonPositionWriter> common_writer_fen_only_;
    std::unique_ptr<CommonPositionLoader> common_loader_;
    std::ofstream records_writer_;  // For intermediate format output
};

} // namespace chessmimic::clock_converter