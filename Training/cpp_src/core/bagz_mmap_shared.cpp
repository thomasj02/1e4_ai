#include "core/bagz_mmap_shared.hpp"
#include "core/bagz.hpp"  // for decompress_zstd
#include "core/bagz_utils.hpp"
#include <cstring>
#include <stdexcept>

namespace chessmimic {

SharedMMapData::SharedMMapData(const std::string& filename)
    : mmap_(filename, true) {  // true = read-only
    read_header();
}

void SharedMMapData::read_header() {
    auto [limits_start, num_records, limits] = BagzUtils::read_bagz_header(
        mmap_.data(),
        mmap_.size()
    );
    limits_start_ = limits_start;
    num_records_ = num_records;
    limits_ = std::move(limits);
}

BagFileReaderMMapShared::BagFileReaderMMapShared(std::shared_ptr<SharedMMapData> shared_data)
    : shared_data_(std::move(shared_data)) {}

std::vector<uint8_t> BagFileReaderMMapShared::get_record(int64_t index) const {
    if (index < 0 || index >= shared_data_->num_records()) {
        throw std::out_of_range("BagFileReaderMMapShared index out of range");
    }
    
    const auto& limits = shared_data_->limits();
    
    // Calculate the range for this record
    int64_t start = index == 0 ? 0 : limits[index - 1];
    int64_t end = limits[index];
    
    // If the range is empty, return empty vector
    if (start == end) {
        return {};
    }
    
    // Get pointer to compressed data - no locking needed, just reading
    auto data = static_cast<const char*>(shared_data_->data());
    std::vector<uint8_t> compressed_data(data + start, data + end);
    
    // Decompress and return
    return decompress_zstd(compressed_data);
}

SharedBagzReaderFactory::SharedBagzReaderFactory(const std::string& path)
    : shared_data_(std::make_shared<SharedMMapData>(path)) {}

} // namespace chessmimic