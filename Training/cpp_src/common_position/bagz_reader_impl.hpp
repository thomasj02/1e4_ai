#pragma once

#include "interfaces.hpp"
#include <memory>
#include <string>

namespace chessmimic {

// Forward declarations
class BagFileReaderMMapShared;
class SharedMMapData;

// Production implementation of IBagzReader
class BagzReaderImpl : public IBagzReader {
public:
    explicit BagzReaderImpl(std::shared_ptr<SharedMMapData> shared_data);
    ~BagzReaderImpl() override; // Need explicit destructor for pimpl with unique_ptr
    [[nodiscard]] size_t size() const override;
    [[nodiscard]] std::vector<uint8_t> get_record(size_t index) const override;
    
private:
    std::unique_ptr<BagFileReaderMMapShared> reader_;
};

// Factory implementation for creating BagzReaderImpl instances with shared memory mapping
class BagzReaderFactory : public IBagzReaderFactory {
public:
    explicit BagzReaderFactory(const std::string& path);
    [[nodiscard]] std::unique_ptr<IBagzReader> createReader() const override;
    [[nodiscard]] size_t size() const override;
    
private:
    std::shared_ptr<SharedMMapData> shared_data_;
};

} // namespace chessmimic