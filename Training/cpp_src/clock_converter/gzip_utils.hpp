#pragma once

#include <string>
#include <fstream>
#include <zlib.h>

namespace chessmimic::clock_converter {

/**
 * Utility class for reading gzip-compressed JSONL files line by line
 */
class GzipReader {
public:
    explicit GzipReader(const std::string& file_path);
    ~GzipReader();
    
    // Non-copyable, non-movable for simplicity
    GzipReader(const GzipReader&) = delete;
    GzipReader& operator=(const GzipReader&) = delete;
    GzipReader(GzipReader&&) = delete;
    GzipReader& operator=(GzipReader&&) = delete;
    
    /**
     * Read next line from the gzip file
     * @param line Output string to store the line
     * @return true if line was read successfully, false if EOF or error
     */
    bool getline(std::string& line);
    
    /**
     * Check if the file is open and ready for reading
     */
    bool is_open() const { return file_ != nullptr; }
    
private:
    gzFile file_;
    static constexpr size_t BUFFER_SIZE = 8192;
    char buffer_[BUFFER_SIZE];
};

/**
 * Utility class for writing gzip-compressed JSONL files
 */
class GzipWriter {
public:
    explicit GzipWriter(const std::string& file_path, int compression_level = 6);
    ~GzipWriter();
    
    // Non-copyable, non-movable for simplicity
    GzipWriter(const GzipWriter&) = delete;
    GzipWriter& operator=(const GzipWriter&) = delete;
    GzipWriter(GzipWriter&&) = delete;
    GzipWriter& operator=(GzipWriter&&) = delete;
    
    /**
     * Write a line to the gzip file (automatically adds newline)
     * @param line The line to write
     * @return true if successful, false on error
     */
    bool writeline(const std::string& line) const;
    
    /**
     * Flush any buffered data
     */
    void flush() const;
    
    /**
     * Check if the file is open and ready for writing
     */
    bool is_open() const { return file_ != nullptr; }
    
private:
    gzFile file_;
};

} // namespace chessmimic::clock_converter