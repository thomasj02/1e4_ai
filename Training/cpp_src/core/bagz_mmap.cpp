#include "core/bagz_mmap.hpp"
#include "core/bagz.hpp"  // for decompress_zstd
#include "core/bagz_utils.hpp"

namespace chessmimic {
BagFileReaderMMap::BagFileReaderMMap(const std::string& filename)
  : m_filename(filename), m_mmap(filename, false) {
  // false = read-only
  read_header();
}

BagFileReaderMMap::~BagFileReaderMMap() = default;

void BagFileReaderMMap::read_header() {
  auto [limits_start, num_records, limits] = BagzUtils::read_bagz_header(
    m_mmap.data(),
    m_mmap.size()
  );
  m_limits_start = limits_start;
  m_num_records = num_records;
  m_limits = std::move(limits);
}

std::vector<uint8_t> BagFileReaderMMap::get_record(int64_t index) const {
  if (index < 0 || index >= m_num_records) {
    throw std::out_of_range("BagFileReaderMMap index out of range");
  }

  // Calculate the range for this record
  int64_t start = index == 0 ? 0 : m_limits[index - 1];
  int64_t end = m_limits[index];

  // If the range is empty, return empty vector
  if (start == end) {
    return {};
  }

  // Get pointer to compressed data - no seeking or locking needed!
  auto data = m_mmap.data();
  std::vector<uint8_t> compressed_data(data + start, data + end);

  // Decompress and return
  return decompress_zstd(compressed_data);
}
} // namespace chessmimic
