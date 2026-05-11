#pragma once

#include <string>
#include <memory>
#include <vector>
#include "core/bagz.hpp"

namespace chessmimic {

/**
 * Generic writer for records to BAGZ format.
 * 
 * This class handles serialization of any record type with a toJson() method
 * to JSON and writing them to a compressed BAGZ file.
 */
class BagzRecordWriter {
public:
    struct Statistics {
        size_t total_records = 0;
        size_t total_bytes_uncompressed = 0;
    };
    
    /**
     * Create a new BAGZ writer.
     * 
     * @param output_path Path to the output BAGZ file
     * @throws std::runtime_error if file cannot be created
     */
    explicit BagzRecordWriter(const std::string& output_path);
    
    ~BagzRecordWriter();
    
    /**
     * Write any record that has a toJson() method to the BAGZ file.
     * 
     * @param record The record to write (must have toJson() method)
     */
    template<typename RecordType>
    void writeRecord(const RecordType& record) {
        // Serialize record to JSON
        std::string json = record.toJson();
        
        // Convert to vector<uint8_t>
        std::vector<uint8_t> data(json.begin(), json.end());
        
        // Write to BAGZ file
        writer_->write(data);
        
        // Update statistics
        stats_.total_records++;
        stats_.total_bytes_uncompressed += json.size();
    }
    
    /**
     * Close the writer and finalize the BAGZ file.
     * This is called automatically in the destructor if not called explicitly.
     */
    void close();
    
    /**
     * Get writing statistics.
     * 
     * @return Current statistics
     */
    [[nodiscard]] Statistics getStatistics() const { return stats_; }
    
    /**
     * Get total bytes written (before compression).
     * 
     * @return Total uncompressed bytes
     */
    [[nodiscard]] size_t getTotalBytesWritten() const { return stats_.total_bytes_uncompressed; }
    
private:
    std::unique_ptr<BagWriter> writer_;
    Statistics stats_;
    bool closed_ = false;
};

} // namespace chessmimic