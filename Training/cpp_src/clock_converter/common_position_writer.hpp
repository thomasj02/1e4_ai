#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <memory>
#include "clock_position_record.hpp"
#include "gzip_utils.hpp"

namespace chessmimic::clock_converter {

/**
 * Writes common positions to JSONL or JSONL.GZ format files.
 * Can write either with full position keys (FEN|recent_moves) or FEN-only.
 * Automatically detects gzip format based on .gz file extension.
 */
class CommonPositionWriter {
public:
    struct Statistics {
        size_t total_position_groups = 0;
        size_t total_records = 0;
        size_t total_bytes_written = 0;
    };
    
    /**
     * Create a common position writer.
     * 
     * @param output_path Path to output JSONL file
     * @param strip_move_history If true, writes FEN-only format
     * @param append If true, append to existing file
     */
    explicit CommonPositionWriter(const std::string& output_path, 
                                 bool strip_move_history = false,
                                 bool append = false);
    
    ~CommonPositionWriter();
    
    /**
     * Write a group of positions with the same key.
     * 
     * @param position_key The position key (may be modified if strip_move_history)
     * @param records All records for this position
     */
    void writePositionGroup(const std::string& position_key,
                           const std::vector<ClockPositionRecord>& records);
    
    /**
     * Close the writer.
     */
    void close();
    
    /**
     * Get writing statistics.
     */
    Statistics getStatistics() const { return stats_; }
    
private:
    std::unique_ptr<std::ofstream> plain_file_;
    std::unique_ptr<GzipWriter> gzip_file_;
    bool strip_move_history_;
    bool is_gzipped_;
    Statistics stats_;
    bool closed_ = false;
};

} // namespace chessmimic::clock_converter