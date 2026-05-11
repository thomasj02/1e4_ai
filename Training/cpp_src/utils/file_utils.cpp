#include "utils/file_utils.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace chessmimic::FileUtils {

// Helper function to check and throw detailed error message if file open failed
void throw_if_failed(std::ifstream& f, const std::string& path) {
    if (f.is_open()) return;

    // Capture errno **immediately**
    const int err = errno; // copy before it changes
    const std::error_code ec(err, std::generic_category());

    throw std::system_error(
        ec,
        "Failed to open file \"" + path + "\": " + std::strerror(err)
    );
}

// Similar helper for ofstream
void throw_if_failed(std::ofstream& f, const std::string& path) {
    if (f.is_open()) return;

    // Capture errno **immediately**
    const int err = errno;
    const std::error_code ec(err, std::generic_category());

    throw std::system_error(
        ec,
        "Failed to open output file \"" + path + "\": " + std::strerror(err)
    );
}

// MemoryMappedFile implementation

// Constructor
MemoryMappedFile::MemoryMappedFile(std::string file_path, bool read_only, size_t size_limit_mb)
    : path_(std::move(file_path)), fd_(-1), data_(nullptr), size_(0),
      read_only_(read_only), size_limit_mb_(size_limit_mb) {
    open();
}

// Destructor
MemoryMappedFile::~MemoryMappedFile() {
    close();
}

// Move constructor
MemoryMappedFile::MemoryMappedFile(MemoryMappedFile&& other) noexcept
    : path_(std::move(other.path_)), fd_(other.fd_), data_(other.data_),
      size_(other.size_), read_only_(other.read_only_), size_limit_mb_(other.size_limit_mb_) {
    other.fd_ = -1;
    other.data_ = nullptr;
    other.size_ = 0;
}

// Move assignment
MemoryMappedFile& MemoryMappedFile::operator=(MemoryMappedFile&& other) noexcept {
    if (this != &other) {
        close();
        path_ = std::move(other.path_);
        fd_ = other.fd_;
        data_ = other.data_;
        size_ = other.size_;
        read_only_ = other.read_only_;
        size_limit_mb_ = other.size_limit_mb_;
        other.fd_ = -1;
        other.data_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

// Get pointer to the memory mapped data
const char* MemoryMappedFile::data() const {
    return data_;
}

// Get size of the mapped file
size_t MemoryMappedFile::size() const {
    return size_;
}

// Check if the file is successfully mapped
bool MemoryMappedFile::is_open() const {
    return fd_ >= 0 && data_ != nullptr;
}

// Get the file path
const std::string& MemoryMappedFile::path() const {
    return path_;
}

// Get raw pointer at specific offset
const char* MemoryMappedFile::at(size_t offset) const {
    if (offset >= size_) {
        throw std::out_of_range("Offset " + std::to_string(offset) +
            " is out of range for file size " + std::to_string(size_));
    }
    return data_ + offset;
}

// Apply file advise hints with FileAdviseMode enum
bool MemoryMappedFile::advise(FileAdviseMode mode, size_t offset, size_t length) const {
    if (!is_open()) {
        return false;
    }

    // If length is 0, advise the entire file
    if (length == 0) {
        length = size_ - offset;
    }

    // Bounds check
    if (offset + length > size_) {
        return false;
    }

    // Convert FileAdviseMode to corresponding posix_madvise constant
    int advice;
    switch (mode) {
    case FileAdviseMode::SEQUENTIAL:
        advice = POSIX_MADV_SEQUENTIAL;
        break;
    case FileAdviseMode::RANDOM:
        advice = POSIX_MADV_RANDOM;
        break;
    case FileAdviseMode::WILLNEED:
        advice = POSIX_MADV_WILLNEED;
        break;
    case FileAdviseMode::DONTNEED:
        advice = POSIX_MADV_DONTNEED;
        break;
    case FileAdviseMode::NORMAL:
    default:
        advice = POSIX_MADV_NORMAL;
        break;
    }

    return posix_madvise(const_cast<char*>(data_) + offset, length, advice) == 0;
}


// Open and map the file
void MemoryMappedFile::open() {
    // Open the file
    fd_ = ::open(path_.c_str(), read_only_ ? O_RDONLY : O_RDWR);
    if (fd_ == -1) {
        const int err = errno;
        const std::error_code ec(err, std::generic_category());
        throw std::system_error(
            ec, "Failed to open file for memory mapping: " + path_ + ": " + std::strerror(err)
        );
    }

    // Get file size
    struct stat sb{};
    if (fstat(fd_, &sb) == -1) {
        const int err = errno;
        ::close(fd_);
        fd_ = -1;
        const std::error_code ec(err, std::generic_category());
        throw std::system_error(
            ec, "Failed to get file size for memory mapping: " + path_ + ": " + std::strerror(err)
        );
    }
    size_ = static_cast<size_t>(sb.st_size);

    // Check if file exceeds size limit (if a limit is set)
    if (size_limit_mb_ > 0) {
        if (size_t size_limit_bytes = size_limit_mb_ * 1024 * 1024; size_ > size_limit_bytes) {
            ::close(fd_);
            fd_ = -1;
            throw std::runtime_error(
                "File size (" + std::to_string(size_ / (1024 * 1024)) + " MB) exceeds limit (" +
                std::to_string(size_limit_mb_) + " MB) for memory mapping: " + path_
            );
        }
    }

    // Map the file into memory
    if (size_ > 0) {
        data_ = static_cast<char*>(mmap(
            nullptr, size_,
            read_only_ ? PROT_READ : PROT_READ | PROT_WRITE,
            MAP_SHARED, fd_, 0
        ));

        if (data_ == MAP_FAILED) {
            const int err = errno;
            ::close(fd_);
            fd_ = -1;
            data_ = nullptr;
            const std::error_code ec(err, std::generic_category());
            throw std::system_error(
                ec, "Failed to memory map file: " + path_ + ": " + std::strerror(err)
            );
        }

        // Apply sequential access pattern hint by default
        // This is typically a good default for most files
        [[maybe_unused]] bool advise_result = advise(FileAdviseMode::SEQUENTIAL);
    } else {
        // Empty file, nothing to map
        data_ = nullptr;
    }
}

// Unmap and close the file
void MemoryMappedFile::close() {
    if (data_ != nullptr && size_ > 0) {
        munmap(data_, size_);
        data_ = nullptr;
    }

    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }

    size_ = 0;
}

// Common file utility functions
bool endsWith(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() &&
        str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Ensure a directory exists, creating it if necessary
void ensureDirectoryExists(const std::string& path) {
    if (!path.empty() && !std::filesystem::exists(path)) {
        std::filesystem::create_directories(path);
    }
}

// Get total system memory in bytes
size_t getSystemMemory() {
    struct sysinfo info{};
    if (sysinfo(&info) != 0) {
        const int err = errno;
        throw std::runtime_error(fmt::format(
            "Failed to get system memory information: {}", std::strerror(err)
        ));
    }
    // Must multiply by mem_unit to get actual bytes
    return info.totalram * static_cast<size_t>(info.mem_unit);
}

}
