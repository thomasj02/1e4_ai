#pragma once

#include <string>
#include <fmt/format.h>

namespace chessmimic::FileUtils {

// Helper function to check and throw detailed error message if file open failed
void throw_if_failed(std::ifstream& f, const std::string& path);

// Similar helper for ofstream
void throw_if_failed(std::ofstream& f, const std::string& path);

// File advise constants for defining access patterns
enum class FileAdviseMode {
    NORMAL,     // No special treatment
    SEQUENTIAL, // File will be accessed sequentially (from lower to higher offsets)
    RANDOM,     // File accesses will be random
    WILLNEED,   // Will need specified pages soon
    DONTNEED    // Don't need these pages in cache
};

// Memory mapped file wrapper class for efficient reading of files
class MemoryMappedFile {
public:
    // Constructor - opens and maps the file
    explicit MemoryMappedFile(std::string file_path, bool read_only = true, size_t size_limit_mb = 0);

    // Destructor - unmaps and closes the file
    ~MemoryMappedFile();

    // Delete copy constructor and assignment
    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;

    // Move constructor
    MemoryMappedFile(MemoryMappedFile&& other) noexcept;

    // Move assignment
    MemoryMappedFile& operator=(MemoryMappedFile&& other) noexcept;

    // Get pointer to the memory mapped data
    [[nodiscard]] const char* data() const;

    // Get size of the mapped file
    [[nodiscard]] size_t size() const;

    // Check if the file is successfully mapped
    [[nodiscard]] bool is_open() const;

    // Get the file path
    [[nodiscard]] const std::string& path() const;

    // Get raw pointer at specific offset
    [[nodiscard]] const char* at(size_t offset) const;

    // Apply file advise hints (wrapper for posix_madvise)
    [[nodiscard]] bool advise(FileAdviseMode mode, size_t offset = 0, size_t length = 0) const;

private:
    std::string path_;        // File path
    int fd_;                  // File descriptor
    char* data_;              // Pointer to memory mapped region
    size_t size_;             // Size of the mapped file
    bool read_only_;          // Whether the file is mapped read-only
    size_t size_limit_mb_;    // Maximum file size to map (in MB, 0 = no limit)

    // Open and map the file
    void open();

    // Unmap and close the file
    void close();
};

// Common file utility functions
bool endsWith(const std::string& str, const std::string& suffix);

// Ensure a directory exists, creating it if necessary
void ensureDirectoryExists(const std::string& path);

// Get total system memory in bytes
size_t getSystemMemory();

}