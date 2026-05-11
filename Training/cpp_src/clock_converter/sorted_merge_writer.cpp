#include "sorted_merge_writer.hpp"
#include "../core/bagz_record_writer.hpp"
#include "common_position_writer.hpp"
#include "common_position_loader.hpp"
#include <algorithm>
#include <queue>
#include <stdexcept>
#include <utility>
#include <cstring>
#include <cerrno>

namespace chessmimic::clock_converter {

bool SortedMergeWriter::ChunkReader::readNext() {
    if (!file || !file.is_open()) {
        has_next = false;
        return false;
    }

    if (std::string line; std::getline(file, line) && !line.empty()) {
        // Parse line: key\tjson
        size_t tab_pos = line.find('\t');
        if (tab_pos == std::string::npos) {
            throw std::runtime_error(
                "Malformed chunk line in temporary chunk file #" + std::to_string(chunk_id) + "\n"
                "Expected format: position_key<TAB>json_data\n"
                "Line content: " + line.substr(0, 100) + "...\n"
                "This indicates a corrupted intermediate file or processing error."
            );
        }
        
        current_key = line.substr(0, tab_pos);

        std::string json = line.substr(tab_pos + 1);
        try {
            // Use reusable parser for ~35% performance improvement
            current_record = ClockPositionRecord::fromJson(json, json_parser);
        } catch (const std::exception& e) {
            throw std::runtime_error(
                "Failed to parse JSON record in chunk file #" + std::to_string(chunk_id) + "\n"
                "Invalid JSON: " + json.substr(0, 200) + "...\n"
                "Error: " + e.what() + "\n"
                "This may indicate:\n"
                "  - Corrupted intermediate data\n"
                "  - Memory corruption during processing\n"
                "  - Incompatible data format\n"
                "Try reprocessing this batch of PGN files."
            );
        }
        
        has_next = true;
        return true;
    }
    
    has_next = false;
    return false;
}

SortedMergeWriter::SortedMergeWriter(Config  config) : config_(std::move(config)) {
    // Create output writers
    // Always create BAGZ writer unless output path is empty
    if (!config_.output_bagz_path.empty()) {
        bagz_writer_ = std::make_unique<BagzRecordWriter>(config_.output_bagz_path);
    }
    
    // Create records writer if specified
    if (!config_.output_records_path.empty()) {
        records_writer_.open(config_.output_records_path, std::ios::binary);
        if (!records_writer_.is_open()) {
            throw std::runtime_error("Failed to create records output file: " + config_.output_records_path);
        }
    }
    
    if (config_.write_common_positions) {
        common_writer_with_history_ = std::make_unique<CommonPositionWriter>(
            config_.common_positions_with_history_path, false);
        common_writer_fen_only_ = std::make_unique<CommonPositionWriter>(
            config_.common_positions_fen_only_path, true);
    }
    
    if (config_.skip_common_positions && !config_.common_positions_fen_only_path.empty()) {
        common_loader_ = std::make_unique<CommonPositionLoader>(
            config_.common_positions_fen_only_path);
    }
}

SortedMergeWriter::~SortedMergeWriter() {
    if (records_writer_.is_open()) {
        records_writer_.close();
    }
}

void SortedMergeWriter::mergeChunks(const std::vector<std::string>& chunk_files) {
    // Open all chunk files
    std::vector<std::unique_ptr<ChunkReader>> readers;
    for (size_t i = 0; i < chunk_files.size(); ++i) {
        auto reader = std::make_unique<ChunkReader>();
        reader->chunk_id = i;
        reader->file.open(chunk_files[i]);
        
        if (!reader->file) {
            throw std::runtime_error(
                "Failed to open sorted chunk file for merging: " + chunk_files[i] + "\n"
                "This file should have been created during the extraction phase.\n"
                "Possible causes:\n"
                "  - File was deleted or moved after creation\n"
                "  - Insufficient permissions to read temporary files\n"
                "  - Disk error or file system corruption\n"
                "  - Processing was interrupted before chunk was written\n"
                "Check that temp directory has sufficient space and is accessible."
            );
        }
        
        // Read first record
        if (reader->readNext()) {
            readers.push_back(std::move(reader));
        }
    }
    
    // Initialize min-heap
    std::priority_queue<ChunkReader*, std::vector<ChunkReader*>, ChunkComparator> heap;
    for (const auto& reader : readers) {
        if (reader->has_next) {
            heap.push(reader.get());
        }
    }
    
    // K-way merge with grouping
    std::string current_fen;
    std::vector<std::pair<std::string, std::vector<ClockPositionRecord>>> fen_groups;
    
    while (!heap.empty()) {
        // Get minimum record
        ChunkReader* min_reader = heap.top();
        heap.pop();
        
        std::string record_key = min_reader->current_key;
        std::string record_fen = extractFenFromKey(record_key);
        ClockPositionRecord record = min_reader->current_record;
        
        // Check if FEN changed
        if (!current_fen.empty() && record_fen != current_fen) {
            // Process all groups for the previous FEN
            processFenGroups(current_fen, fen_groups);
            fen_groups.clear();
            current_fen = record_fen;
        }
        
        // Find or create group for this key
        bool found = false;
        for (auto& [key, records] : fen_groups) {
            if (key == record_key) {
                records.push_back(record);
                found = true;
                break;
            }
        }
        if (!found) {
            fen_groups.emplace_back(record_key, std::vector{record});
        }
        
        if (current_fen.empty()) current_fen = record_fen;
        
        stats_.total_records_processed++;
        
        // Advance reader and update heap
        if (min_reader->readNext()) {
            heap.push(min_reader);
        }
    }
    
    // Process final FEN groups
    if (!fen_groups.empty()) {
        processFenGroups(current_fen, fen_groups);
    }
    
    // Close writers
    if (bagz_writer_) {
        bagz_writer_->close();
    }
    if (records_writer_.is_open()) {
        records_writer_.close();
    }
    if (common_writer_with_history_) {
        common_writer_with_history_->close();
    }
    if (common_writer_fen_only_) {
        common_writer_fen_only_->close();
    }
}

std::string SortedMergeWriter::extractFenFromKey(const std::string& key) {
    // Key format: FEN|recent_moves
    if (size_t pipe_pos = key.find('|'); pipe_pos != std::string::npos) {
        std::string fen = key.substr(0, pipe_pos);
        // Strip move clocks from FEN
        return ClockPositionRecord::stripMoveClockFromFen(fen);
    }
    return key;
}

void SortedMergeWriter::processPositionGroup(const std::string& key,
                                            const std::vector<ClockPositionRecord>& records,
                                            bool fen_is_common) {
    stats_.unique_positions++;
    
    // Check if position should be skipped
    if (config_.skip_common_positions && common_loader_ && common_loader_->isCommon(key)) {
        stats_.records_skipped += records.size();
        return;
    }
    
    if (fen_is_common) {
        stats_.common_positions_found++;
        
        // Write to common positions with history
        if (config_.write_common_positions && common_writer_with_history_) {
            common_writer_with_history_->writePositionGroup(key, records);
            stats_.records_written_to_common += records.size();
        }
    } else {
        // Write all records to output
        if (bagz_writer_) {
            // Write to BAGZ format
            for (const auto& record : records) {
                bagz_writer_->writeRecord(record);
                stats_.records_written_to_bagz++;
            }
        } else if (records_writer_.is_open()) {
            // Write to intermediate records format (compatible with RecordProcessor::readRecord)
            for (const auto& record : records) {
                std::string json = record.toJson();
                
                // Calculate total record size
                uint32_t key_len = static_cast<uint32_t>(key.size());
                uint32_t record_len = static_cast<uint32_t>(json.size());
                uint32_t total_size = sizeof(key_len) + key_len + sizeof(record_len) + record_len;
                
                // Write in the format expected by RecordProcessor::readRecord
                records_writer_.write(reinterpret_cast<const char*>(&total_size), sizeof(total_size));
                if (!records_writer_.good()) {
                    throw std::runtime_error(
                        "Failed to write record total size to intermediate file. "
                        "errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
                    );
                }
                
                records_writer_.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
                if (!records_writer_.good()) {
                    throw std::runtime_error(
                        "Failed to write key length to intermediate file. "
                        "errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
                    );
                }
                
                records_writer_.write(key.data(), static_cast<std::streamsize>(key.size()));
                if (!records_writer_.good()) {
                    throw std::runtime_error(
                        "Failed to write key data to intermediate file. "
                        "errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
                    );
                }
                
                records_writer_.write(reinterpret_cast<const char*>(&record_len), sizeof(record_len));
                if (!records_writer_.good()) {
                    throw std::runtime_error(
                        "Failed to write record length to intermediate file. "
                        "errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
                    );
                }
                
                records_writer_.write(json.data(), static_cast<std::streamsize>(json.size()));
                if (!records_writer_.good()) {
                    throw std::runtime_error(
                        "Failed to write record data to intermediate file. "
                        "errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
                    );
                }
                
                stats_.records_written_to_bagz++;  // Count as BAGZ even though it's intermediate
            }
        }
    }
}

void SortedMergeWriter::processFenGroups(const std::string& fen,
                                        const std::vector<std::pair<std::string, std::vector<ClockPositionRecord>>>& groups) {
    // Count total records for this FEN
    size_t total_fen_records = 0;
    for (const auto& records : groups | std::views::values) {
        total_fen_records += records.size();
    }
    
    // Determine if FEN is common
    bool fen_is_common = total_fen_records > config_.max_positions_per_key;
    
    // Write FEN-only common position if needed
    if (fen_is_common && config_.write_common_positions && common_writer_fen_only_) {
        std::vector<ClockPositionRecord> all_fen_records;
        for (const auto& records : groups | std::views::values) {
            all_fen_records.insert(all_fen_records.end(), records.begin(), records.end());
        }
        common_writer_fen_only_->writePositionGroup(fen, all_fen_records);
    }
    
    // Process each position key group
    for (const auto& [key, records] : groups) {
        processPositionGroup(key, records, fen_is_common);
    }
}

} // namespace chessmimic::clock_converter