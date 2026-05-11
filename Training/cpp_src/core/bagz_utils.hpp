#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace chessmimic::BagzUtils {

// Struct to hold parsed BAGZ header information
struct HeaderInfo {
    int64_t limits_start;        // Offset where the limits array begins
    int64_t num_records;         // Number of records in the file
    std::vector<int64_t> limits; // Array of record end offsets
};

// Read and parse BAGZ file header from memory-mapped data
// Returns HeaderInfo containing limits_start, num_records, and limits array
// Throws std::runtime_error if the header is invalid
HeaderInfo read_bagz_header(const char* data, size_t file_size);

} // namespace chessmimic::BagzUtils
