#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include "utils/file_utils.hpp"

namespace chessmimic {

// Shared memory-mapped file data
class SharedMMapData {
public:
    explicit SharedMMapData(const std::string& filename);
    
    const void* data() const { return mmap_.data(); }
    size_t size() const { return mmap_.size(); }
    
    int64_t limits_start() const { return limits_start_; }
    int64_t num_records() const { return num_records_; }
    const std::vector<int64_t>& limits() const { return limits_; }
    
private:
    void read_header();
    
    FileUtils::MemoryMappedFile mmap_;
    std::vector<int64_t> limits_;
    int64_t num_records_ = 0;
    int64_t limits_start_ = 0;
};

// Thread-safe shared memory-mapped BAGZ reader
class BagFileReaderMMapShared {
public:
    explicit BagFileReaderMMapShared(std::shared_ptr<SharedMMapData> shared_data);
    
    [[nodiscard]] size_t size() const { return shared_data_->num_records(); }
    [[nodiscard]] std::vector<uint8_t> get_record(int64_t index) const;
    
private:
    std::shared_ptr<SharedMMapData> shared_data_;
};

// Factory for creating shared readers
class SharedBagzReaderFactory {
public:
    explicit SharedBagzReaderFactory(const std::string& path);
    
    std::unique_ptr<BagFileReaderMMapShared> createReader() const {
        return std::make_unique<BagFileReaderMMapShared>(shared_data_);
    }
    
    size_t size() const { return shared_data_->num_records(); }
    
private:
    std::shared_ptr<SharedMMapData> shared_data_;
};

} // namespace chessmimic