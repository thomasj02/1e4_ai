#pragma once

#include <string>
#include <vector>
#include <memory>
#include "utils/file_utils.hpp"

namespace chessmimic {

// Memory-mapped BAGZ reader for parallel access
class BagFileReaderMMap {
public:
    explicit BagFileReaderMMap(const std::string& filename);
    ~BagFileReaderMMap();
    
    [[nodiscard]] size_t size() const { return m_num_records; }
    [[nodiscard]] std::vector<uint8_t> get_record(int64_t index) const;
    
private:
    void read_header();
    
    std::string m_filename;
    FileUtils::MemoryMappedFile m_mmap;
    std::vector<int64_t> m_limits;
    int64_t m_num_records = 0;
    int64_t m_limits_start = 0;
};

} // namespace chessmimic