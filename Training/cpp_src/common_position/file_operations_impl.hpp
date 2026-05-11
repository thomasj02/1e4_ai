#pragma once

#include "interfaces.hpp"

namespace chessmimic {

// Production implementation of IFileOperations
class FileOperationsImpl : public IFileOperations {
public:
    void writeJsonLine(const std::string& path, const std::string& json_line) override;
    std::vector<std::string> readJsonLines(const std::string& path) override;
    void removeFile(const std::string& path) override;
    bool exists(const std::string& path) override;
};

} // namespace chessmimic