#include "core/bagz.hpp"
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cerrno>
#include <fmt/format.h>

namespace chessmimic {
// Utility function to decompress data using zstd
std::vector<uint8_t> decompress_zstd(const std::vector<uint8_t>& compressed_data) {
    // Get the decompressed size
    size_t decompressed_size = ZSTD_getFrameContentSize(compressed_data.data(), compressed_data.size());
    if (decompressed_size == ZSTD_CONTENTSIZE_ERROR) {
        throw std::runtime_error("Not a valid zstd compressed buffer");
    }
    if (decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        throw std::runtime_error("Unknown content size in zstd compressed buffer");
    }

    // Allocate buffer for decompressed data
    std::vector<uint8_t> decompressed_data(decompressed_size);

    // Decompress
    const size_t result = ZSTD_decompress(
        decompressed_data.data(), decompressed_size,
        compressed_data.data(), compressed_data.size()
    );

    if (ZSTD_isError(result)) {
        throw std::runtime_error(std::string("ZSTD decompression error: ") + ZSTD_getErrorName(result));
    }

    return decompressed_data;
}

// Utility function to compress data using zstd
std::vector<uint8_t> compress_zstd(const std::vector<uint8_t>& data, int compression_level) {
    // Get the max size needed for the compressed buffer
    size_t max_compressed_size = ZSTD_compressBound(data.size());

    // Allocate buffer for compressed data
    std::vector<uint8_t> compressed_data(max_compressed_size);

    // Compress
    size_t result = ZSTD_compress(
        compressed_data.data(), max_compressed_size,
        data.data(), data.size(),
        compression_level
    );

    if (ZSTD_isError(result)) {
        throw std::runtime_error(std::string("ZSTD compression error: ") + ZSTD_getErrorName(result));
    }

    // Resize to actual compressed size
    compressed_data.resize(result);

    return compressed_data;
}

// BagFileReader implementation
BagFileReader::BagFileReader(const std::string& filename, bool separate_limits)
    : m_filename(filename), m_separate_limits(separate_limits), m_num_records(0), m_limits_start(0) {
    // Open the records file
    m_records_file = std::make_unique<std::ifstream>(filename, std::ios::binary);
    if (!m_records_file->is_open()) {
        throw std::runtime_error("Could not open file for reading: " + filename);
    }

    // Get the file size
    m_records_file->seekg(0, std::ios::end);
    std::streamsize file_size = m_records_file->tellg();
    m_records_file->seekg(0, std::ios::beg);

    if (separate_limits) {
        // Extract the filename component without the directory path
        std::string limits_filename;
        if (size_t last_slash = filename.find_last_of("/\\"); last_slash != std::string::npos) {
            // If path contains slashes, use the directory + "limits." + filename
            std::string dir = filename.substr(0, last_slash + 1);
            std::string base_name = filename.substr(last_slash + 1);
            limits_filename = dir + "limits." + base_name;
        }
        else {
            // Otherwise just prepend "limits."
            limits_filename = "limits." + filename;
        }

        m_limits_file = std::make_unique<std::ifstream>(limits_filename, std::ios::binary);
        if (!m_limits_file->is_open()) {
            throw std::runtime_error("Could not open limits file for reading: " + limits_filename);
        }

        // Get limits file size
        m_limits_file->seekg(0, std::ios::end);
        std::streamsize limits_size = m_limits_file->tellg();
        m_limits_file->seekg(0, std::ios::beg);

        // Read all limits
        m_num_records = static_cast<int64_t>(limits_size / sizeof(int64_t));
        m_limits.resize(m_num_records);

        for (int64_t i = 0; i < m_num_records; ++i) {
            int64_t limit;
            m_limits_file->read(reinterpret_cast<char*>(&limit), sizeof(int64_t));
            m_limits[i] = limit;
        }
    }
    else {
        // Read the last 8 bytes to get index_start
        if (file_size >= 8) {
            m_records_file->seekg(file_size - 8);
            m_records_file->read(reinterpret_cast<char*>(&m_limits_start), sizeof(int64_t));

            // Calculate number of records
            int64_t index_size = file_size - m_limits_start;
            m_num_records = static_cast<int64_t>(index_size / sizeof(int64_t));

            // Validate the calculated number of records
            if (m_num_records > 0) {
                // Read all limits
                m_records_file->seekg(m_limits_start);
                m_limits.resize(m_num_records);

                for (int64_t i = 0; i < m_num_records; ++i) {
                    int64_t limit;
                    m_records_file->read(reinterpret_cast<char*>(&limit), sizeof(int64_t));
                    m_limits[i] = limit;
                }
            }
        }
    }
}

BagFileReader::~BagFileReader() {
    if (m_records_file) {
        m_records_file->close();
    }
    if (m_limits_file) {
        m_limits_file->close();
    }
}

size_t BagFileReader::size() const {
    return m_num_records;
}

std::vector<uint8_t> BagFileReader::get_record(int64_t index) const {
    if (index < 0 || index >= m_num_records) {
        throw std::out_of_range("BagFileReader index out of range");
    }

    // Calculate the range for this record
    int64_t start;
    if (index == 0) {
        start = 0;
    }
    else {
        start = m_limits[index - 1];
    }
    int64_t end = m_limits[index];

    // If the range is empty (start == end), return empty vector
    if (start == end) {
        return {};
    }

    // Read the record data
    std::vector<uint8_t> compressed_data(end - start);
    m_records_file->seekg(start);
    m_records_file->read(reinterpret_cast<char*>(compressed_data.data()), end - start);

    // Return data if it's empty (no decompression needed)
    if (compressed_data.empty()) {
        return compressed_data;
    }

    // Decompress the data
    return decompress_zstd(compressed_data);
}

// BagWriter implementation
BagWriter::BagWriter(const std::string& filename, bool separate_limits)
    : m_filename(filename), m_separate_limits(separate_limits) {
    // Open the records file
    m_records_file = std::make_unique<std::ofstream>(filename, std::ios::binary);
    if (!m_records_file->is_open()) {
        throw std::runtime_error(
            "Could not open file for writing: " + filename +
            ", errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
        );
    }

    // Open the limits file if needed
    if (separate_limits) {
        // Extract the filename component without the directory path
        std::string limits_filename;
        if (size_t last_slash = filename.find_last_of("/\\"); last_slash != std::string::npos) {
            // If path contains slashes, use the directory + "limits." + filename
            std::string dir = filename.substr(0, last_slash + 1);
            std::string base_name = filename.substr(last_slash + 1);
            limits_filename = dir + "limits." + base_name;
        }
        else {
            // Otherwise just prepend "limits."
            limits_filename = "limits." + filename;
        }

        auto real_file = std::make_unique<std::ofstream>(limits_filename, std::ios::binary);
        if (!real_file->is_open()) {
            throw std::runtime_error(
                "Could not open limits file for writing: " + limits_filename +
                ", errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
            );
        }
        m_limits_file = std::move(real_file);
    }
    else {
        // Use a stringstream to collect the limits data internally
        m_limits_file = std::make_unique<std::stringstream>();
    }
}

BagWriter::~BagWriter() {
    close();
}

void BagWriter::write(const std::vector<uint8_t>& data) const {
    // Check if writer has been closed
    if (!m_records_file || !m_limits_file) {
        throw std::runtime_error("Cannot write to closed BAGZ file");
    }
    
    // Compress data (empty or not) - Python implementation always calls compress
    // even on empty data (which returns empty data but still processes it)
    std::vector<uint8_t> compressed_data;
    if (!data.empty()) {
        compressed_data = compress_zstd(data, 0);
        m_records_file->write(reinterpret_cast<const char*>(compressed_data.data()),
                              static_cast<std::streamsize>(compressed_data.size()));
        
        // Check for write errors
        if (!m_records_file->good()) {
            throw std::runtime_error(
                "Failed to write compressed data to BAGZ file. "
                "Stream state: fail=" + std::to_string(m_records_file->fail()) +
                ", bad=" + std::to_string(m_records_file->bad()) +
                ", errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
            );
        }
    }
    
    // Flush to ensure accurate position and detect write errors
    m_records_file->flush();
    if (!m_records_file->good()) {
        throw std::runtime_error(
            "Failed to flush BAGZ records file. "
            "Stream state: fail=" + std::to_string(m_records_file->fail()) +
            ", bad=" + std::to_string(m_records_file->bad()) +
            ", errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
        );
    }

    // Get file position - must be done after flush
    int64_t end_pos = m_records_file->tellp();
    if (end_pos == -1) {
        throw std::runtime_error(
            "Failed to get file position in BAGZ file (tellp returned -1). "
            "Stream state: fail=" + std::to_string(m_records_file->fail()) +
            ", bad=" + std::to_string(m_records_file->bad()) +
            ", errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
        );
    }

    // Write to the limits file or stream
    m_limits_file->write(reinterpret_cast<const char*>(&end_pos), sizeof(int64_t));
    
    // Check limits write succeeded
    if (!m_limits_file->good()) {
        throw std::runtime_error(
            "Failed to write limit position to BAGZ file. "
            "Position was: " + std::to_string(end_pos) +
            ", errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
        );
    }

    // Increment the record count
    m_record_count++;
}


void BagWriter::close() {
    if (!m_separate_limits && m_records_file && m_limits_file) {
        // --- Non-Separate Limits Mode ---

        // Check if any records were actually written. If not, just close.
        if (m_record_count == 0) {
            m_records_file->close();
            m_records_file.reset();
            m_limits_file.reset(); // Stringstream doesn't need closing, just reset ptr
            return;
        }

        // Get the limits data collected in the stringstream
        auto* ss = dynamic_cast<std::stringstream*>(m_limits_file.get());
        if (!ss) {
            // Should not happen given the constructor logic, but handle defensively
            if (m_records_file) m_records_file->close();
            m_records_file.reset();
            m_limits_file.reset();
            throw std::runtime_error("Internal error: limits_file is not a stringstream in non-separate mode.");
        }
        std::string limits_str = ss->str();

        // If limits_str is somehow empty despite records being written, just close.
        if (limits_str.empty()) {
            m_records_file->close();
            m_records_file.reset();
            m_limits_file.reset();
            return;
        }

        // Append the collected limits data directly to the end of the records file.
        m_records_file->write(limits_str.data(), static_cast<std::streamsize>(limits_str.size()));
        
        // Check write succeeded before closing
        if (!m_records_file->good()) {
            throw std::runtime_error(
                "Failed to write limits data to BAGZ file during close. "
                "Stream state: fail=" + std::to_string(m_records_file->fail()) +
                ", bad=" + std::to_string(m_records_file->bad()) +
                ", errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
            );
        }

        // Close and check for errors
        m_records_file->close();
        if (m_records_file->fail()) {
            throw std::runtime_error(
                "Failed to close BAGZ records file. "
                "errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
            );
        }
        m_records_file.reset();
        m_limits_file.reset(); // Reset the unique_ptr for the stringstream
    }
    else if (m_separate_limits) {
        // --- Separate Limits Mode ---
        // Close the files for separate limits mode
        if (m_records_file) {
            m_records_file->close();
            if (m_records_file->fail()) {
                throw std::runtime_error(
                    "Failed to close BAGZ records file in separate limits mode. "
                    "errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
                );
            }
            m_records_file.reset();
        }

        if (m_limits_file) {
            // Only close if it's a real file (std::ofstream)
            if (auto* real_file = dynamic_cast<std::ofstream*>(m_limits_file.get())) {
                real_file->close();
                if (real_file->fail()) {
                    throw std::runtime_error(
                        "Failed to close BAGZ limits file in separate limits mode. "
                        "errno=" + std::to_string(errno) + " (" + std::strerror(errno) + ")"
                    );
                }
            }
            // Else it might be the stringstream from the non-separate case which is already handled,
            // or potentially an error state, but resetting the ptr is safe.
            m_limits_file.reset();
        }
    }
}
} // namespace chessmimic
