#pragma once

#include <vector>
#include <string>
#include <memory>

namespace chessmimic {

// Interface for reading bagz files (for dependency injection)
class IBagzReader {
public:
    virtual ~IBagzReader() = default;
    [[nodiscard]] virtual size_t size() const = 0;
    [[nodiscard]] virtual std::vector<uint8_t> get_record(size_t index) const = 0;
};

// Factory for creating bagz readers (allows thread-safe reader creation)
class IBagzReaderFactory {
public:
    virtual ~IBagzReaderFactory() = default;
    [[nodiscard]] virtual std::unique_ptr<IBagzReader> createReader() const = 0;
    [[nodiscard]] virtual size_t size() const = 0;
};

// Interface for file operations (for testing)
class IFileOperations {
public:
    virtual ~IFileOperations() = default;
    virtual void writeJsonLine(const std::string& path, const std::string& json_line) = 0;
    virtual std::vector<std::string> readJsonLines(const std::string& path) = 0;
    virtual void removeFile(const std::string& path) = 0;
    virtual bool exists(const std::string& path) = 0;
};

} // namespace chessmimic