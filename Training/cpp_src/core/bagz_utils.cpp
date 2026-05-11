#include "core/bagz_utils.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

namespace chessmimic::BagzUtils {

HeaderInfo read_bagz_header(const char* data, size_t file_size) {
    if (file_size < sizeof(int64_t)) {
        throw std::runtime_error("File too small to contain header");
    }

    HeaderInfo header;

    // Read the last 8 bytes to get index_start (where limits begin)
    std::memcpy(&header.limits_start, data + file_size - sizeof(int64_t), sizeof(int64_t));

    if (header.limits_start < 0 || header.limits_start >= static_cast<int64_t>(file_size)) {
        throw std::runtime_error("Invalid limits start offset: " + std::to_string(header.limits_start));
    }

    // Calculate number of records
    int64_t index_size = file_size - header.limits_start;
    header.num_records = index_size / sizeof(int64_t);

    if (header.num_records <= 0) {
        throw std::runtime_error("Invalid number of records: " + std::to_string(header.num_records));
    }

    // Read all limits into memory
    header.limits.resize(header.num_records);
    std::memcpy(header.limits.data(), data + header.limits_start, header.num_records * sizeof(int64_t));

    return header;
}

} // namespace chessmimic::BagzUtils
