#include "bagz_reader_impl.hpp"
#include "../core/bagz_mmap_shared.hpp"

namespace chessmimic {

BagzReaderImpl::BagzReaderImpl(std::shared_ptr<SharedMMapData> shared_data)
    : reader_(std::make_unique<BagFileReaderMMapShared>(shared_data)) {}

BagzReaderImpl::~BagzReaderImpl() = default; // Define destructor in cpp file where BagFileReader is complete

size_t BagzReaderImpl::size() const {
    return reader_->size();
}

std::vector<uint8_t> BagzReaderImpl::get_record(size_t index) const {
    return reader_->get_record(index);
}

// BagzReaderFactory implementation
BagzReaderFactory::BagzReaderFactory(const std::string& path)
    : shared_data_(std::make_shared<SharedMMapData>(path)) {}

std::unique_ptr<IBagzReader> BagzReaderFactory::createReader() const {
    return std::make_unique<BagzReaderImpl>(shared_data_);
}

size_t BagzReaderFactory::size() const {
    return shared_data_->num_records();
}

} // namespace chessmimic