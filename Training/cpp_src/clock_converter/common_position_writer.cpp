#include "common_position_writer.hpp"
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <filesystem>

namespace chessmimic::clock_converter {

CommonPositionWriter::CommonPositionWriter(const std::string& output_path, 
                                         bool strip_move_history,
                                         bool append) 
    : strip_move_history_(strip_move_history), is_gzipped_(output_path.ends_with(".gz")) {
    
    // Create parent directory if needed
    if (std::filesystem::path path(output_path); path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    
    try {
        if (is_gzipped_) {
            // Note: GzipWriter doesn't support append mode
            if (append) {
                throw std::runtime_error(
                    "Append mode is not supported for gzip files: " + output_path + "\n"
                    "Gzip format does not support efficient appending."
                );
            }
            gzip_file_ = std::make_unique<GzipWriter>(output_path);
        } else {
            // Use regular file for non-gzip
            auto mode = append ? std::ios::out | std::ios::app : std::ios::out;
            plain_file_ = std::make_unique<std::ofstream>(output_path, mode);
            
            if (!plain_file_->is_open()) {
                throw std::runtime_error("Failed to open file for writing");
            }
        }
    } catch (const std::exception& e) {
        throw std::runtime_error(
            "Failed to open common position file: " + output_path + "\n"
            "Possible causes:\n"
            "  - Parent directory does not exist and could not be created\n"
            "  - Insufficient permissions to write to this location\n"
            "  - Invalid file path or filename\n"
            "  - Disk is full or quota exceeded\n"
            "Original error: " + std::string(e.what())
        );
    }
}

CommonPositionWriter::~CommonPositionWriter() {
    if (!closed_) {
        close();
    }
}

void CommonPositionWriter::writePositionGroup(const std::string& position_key,
                                            const std::vector<ClockPositionRecord>& records) {
    if (closed_) {
        throw std::runtime_error(
            "Attempted to write to a closed common position file.\n"
            "This is likely a programming error - the writer was closed prematurely.\n"
            "Check the processing pipeline for early close() calls."
        );
    }
    
    if (records.empty()) {
        // Optionally skip empty groups or write with count=0
        return;
    }
    
    // Build JSON string
    std::ostringstream oss;
    oss << "{";
    
    if (strip_move_history_) {
        // FEN-only format
        oss << R"("fen":")" << position_key << "\""; // Key should already be FEN-only
    } else {
        // Full position key format
        oss << R"("position_key":")" << position_key << "\"";
    }
    
    // Extract data from all records
    oss << ",\"thinking_times\":[";
    for (size_t i = 0; i < records.size(); ++i) {
        if (i > 0) oss << ",";
        oss << std::fixed << std::setprecision(2) << static_cast<float>(records[i].thinking_time);
    }
    
    oss << "],\"clock_states\":[";
    for (size_t i = 0; i < records.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{\"player\":" << std::fixed << std::setprecision(1) << records[i].player_clock
            << ",\"opponent\":" << records[i].opponent_clock
            << ",\"increment\":" << records[i].increment << "}";
    }
    
    oss << "],\"ratings\":[";
    for (size_t i = 0; i < records.size(); ++i) {
        if (i > 0) oss << ",";
        oss << records[i].rating;
    }
    
    oss << "],\"count\":" << records.size() << "}";
    
    // Write as single line
    std::string line = oss.str();
    
    if (is_gzipped_) {
        gzip_file_->writeline(line);
    } else {
        *plain_file_ << line << "\n";
    }
    
    // Update statistics
    stats_.total_position_groups++;
    stats_.total_records += records.size();
    stats_.total_bytes_written += line.size() + 1; // +1 for newline
}

void CommonPositionWriter::close() {
    if (!closed_) {
        if (is_gzipped_ && gzip_file_) {
            gzip_file_.reset();
        } else if (plain_file_ && plain_file_->is_open()) {
            plain_file_->close();
        }
        closed_ = true;
    }
}

} // namespace chessmimic::clock_converter