#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <memory>
#include <zstd.h>

namespace chessmimic {
// Python-compatible bagz file reader/writer
// This implementation aims to be compatible with the Python bagz.py module
// which uses zstandard compression

class BagFileReader {
public:
    explicit BagFileReader(const std::string& filename, bool separate_limits = false);
    ~BagFileReader();

    [[nodiscard]] size_t size() const;
    [[nodiscard]] std::vector<uint8_t> get_record(int64_t index) const;

private:
    std::string m_filename;
    std::unique_ptr<std::ifstream> m_records_file;
    std::unique_ptr<std::ifstream> m_limits_file;
    std::vector<int64_t> m_limits;
    bool m_separate_limits;
    int64_t m_num_records;
    int64_t m_limits_start;
};

class BagWriter {
public:
    explicit BagWriter(const std::string& filename, bool separate_limits = false);
    ~BagWriter();

    void write(const std::vector<uint8_t>& data) const;
    void close();

private:
    std::string m_filename;
    std::unique_ptr<std::ofstream> m_records_file;
    std::unique_ptr<std::ostream> m_limits_file; // can be either std::ofstream or std::stringstream
    bool m_separate_limits;
    mutable int64_t m_record_count = 0; // Track the number of actual records written
};

// Utility functions for Python compatibility testing
std::vector<uint8_t> decompress_zstd(const std::vector<uint8_t>& compressed_data);
std::vector<uint8_t> compress_zstd(const std::vector<uint8_t>& data, int compression_level = 0);
} // namespace chessmimic
