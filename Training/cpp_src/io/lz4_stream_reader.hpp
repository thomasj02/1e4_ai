#pragma once

#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <lz4frame.h>
#include "io/lz4_constants.hpp"

namespace chessmimic {

/**
 * A streaming reader for LZ4-compressed files that processes data in chunks
 * without loading the entire file into memory.
 */
class LZ4StreamReader {
public:
    /**
     * Callback function that receives decompressed data chunks.
     * Parameters: data pointer, data size
     * Return: true to continue reading, false to stop
     */
    using DataCallback = std::function<bool(const char* data, size_t size)>;
    
    /**
     * Creates a new LZ4 stream reader.
     * @param input_buffer_size Size of the buffer for reading compressed data (default: 64KB)
     * @param output_buffer_size Size of the buffer for decompressed data (default: 256KB)
     */
    explicit LZ4StreamReader(size_t input_buffer_size = LZ4_DEFAULT_INPUT_BUFFER_SIZE,
                           size_t output_buffer_size = LZ4_DEFAULT_OUTPUT_BUFFER_SIZE);
    
    ~LZ4StreamReader();
    
    // Disable copy operations
    LZ4StreamReader(const LZ4StreamReader&) = delete;
    LZ4StreamReader& operator=(const LZ4StreamReader&) = delete;
    
    // Enable move operations
    LZ4StreamReader(LZ4StreamReader&& other) noexcept;
    LZ4StreamReader& operator=(LZ4StreamReader&& other) noexcept;
    
    /**
     * Decompresses an LZ4 file in a streaming fashion.
     * @param filename Path to the LZ4-compressed file
     * @param callback Function called with each chunk of decompressed data
     * @throws std::runtime_error on decompression errors
     */
    void decompressFile(const std::string& filename, DataCallback callback);
    
    /**
     * Decompresses LZ4 data from memory in a streaming fashion.
     * @param compressed_data Pointer to compressed data
     * @param compressed_size Size of compressed data
     * @param callback Function called with each chunk of decompressed data
     * @throws std::runtime_error on decompression errors
     */
    void decompressMemory(const char* compressed_data, size_t compressed_size,
                         const DataCallback& callback);
    
    /**
     * Gets the total number of bytes read from the input.
     */
    size_t getBytesRead() const { return bytes_read_; }
    
    /**
     * Gets the total number of bytes decompressed.
     */
    size_t getBytesDecompressed() const { return bytes_decompressed_; }
    
private:
    size_t input_buffer_size_;
    size_t output_buffer_size_;
    size_t bytes_read_;
    size_t bytes_decompressed_;
    
    std::vector<char> input_buffer_;
    std::vector<char> output_buffer_;
    
    // Internal decompression state
    struct DecompressionState;
    std::unique_ptr<DecompressionState> state_;
    
    void initDecompression() const;
    void cleanupDecompression() const;
};

} // namespace chessmimic