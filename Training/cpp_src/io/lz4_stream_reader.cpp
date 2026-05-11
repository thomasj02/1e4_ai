#include "io/lz4_stream_reader.hpp"
#include <fstream>
#include <stdexcept>
#include <cstring>

namespace chessmimic {

struct LZ4StreamReader::DecompressionState {
    LZ4F_decompressionContext_t ctx = nullptr;
    
    ~DecompressionState() {
        if (ctx) {
            LZ4F_freeDecompressionContext(ctx);
        }
    }
};

LZ4StreamReader::LZ4StreamReader(size_t input_buffer_size, size_t output_buffer_size)
    : input_buffer_size_(input_buffer_size),
      output_buffer_size_(output_buffer_size),
      bytes_read_(0),
      bytes_decompressed_(0),
      state_(std::make_unique<DecompressionState>()) {
    
    if (input_buffer_size_ == 0 || output_buffer_size_ == 0) {
        throw std::invalid_argument("Buffer sizes must be greater than zero");
    }
    
    input_buffer_.resize(input_buffer_size_);
    output_buffer_.resize(output_buffer_size_);
}

LZ4StreamReader::~LZ4StreamReader() = default;

LZ4StreamReader::LZ4StreamReader(LZ4StreamReader&& other) noexcept
    : input_buffer_size_(other.input_buffer_size_),
      output_buffer_size_(other.output_buffer_size_),
      bytes_read_(other.bytes_read_),
      bytes_decompressed_(other.bytes_decompressed_),
      input_buffer_(std::move(other.input_buffer_)),
      output_buffer_(std::move(other.output_buffer_)),
      state_(std::move(other.state_)) {
    
    // Reset moved-from object
    other.input_buffer_size_ = 0;
    other.output_buffer_size_ = 0;
    other.bytes_read_ = 0;
    other.bytes_decompressed_ = 0;
}

LZ4StreamReader& LZ4StreamReader::operator=(LZ4StreamReader&& other) noexcept {
    if (this != &other) {
        input_buffer_size_ = other.input_buffer_size_;
        output_buffer_size_ = other.output_buffer_size_;
        bytes_read_ = other.bytes_read_;
        bytes_decompressed_ = other.bytes_decompressed_;
        input_buffer_ = std::move(other.input_buffer_);
        output_buffer_ = std::move(other.output_buffer_);
        state_ = std::move(other.state_);
        
        // Reset moved-from object
        other.input_buffer_size_ = 0;
        other.output_buffer_size_ = 0;
        other.bytes_read_ = 0;
        other.bytes_decompressed_ = 0;
    }
    return *this;
}

void LZ4StreamReader::initDecompression() const {
    cleanupDecompression();

    if (size_t result = LZ4F_createDecompressionContext(&state_->ctx, LZ4F_VERSION); LZ4F_isError(result)) {
        throw std::runtime_error(
            "Failed to create LZ4 decompression context: " +
            std::string(LZ4F_getErrorName(result))
        );
    }
}

void LZ4StreamReader::cleanupDecompression() const {
    if (state_->ctx) {
        LZ4F_freeDecompressionContext(state_->ctx);
        state_->ctx = nullptr;
    }
}

void LZ4StreamReader::decompressFile(const std::string& filename, DataCallback callback) {
    // Open file
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + filename);
    }
    
    // Reset to beginning (in case the file was already open)
    file.seekg(0, std::ios::beg);
    
    // Initialize decompression
    initDecompression();
    bytes_read_ = 0;
    bytes_decompressed_ = 0;
    
    try {
        size_t hint = 1; // Start with any non-zero hint
        
        while (!file.eof() && hint > 0) {
            // Read compressed data
            file.read(input_buffer_.data(), input_buffer_size_);
            size_t bytes_read = file.gcount();
            
            if (bytes_read == 0) {
                break;
            }
            
            bytes_read_ += bytes_read;
            
            // Decompress the chunk
            size_t src_pos = 0;
            
            while (src_pos < bytes_read) {
                size_t src_size = bytes_read - src_pos;
                size_t dst_size = output_buffer_size_;
                
                size_t result = LZ4F_decompress(
                    state_->ctx,
                    output_buffer_.data(), &dst_size,
                    input_buffer_.data() + src_pos, &src_size,
                    nullptr
                );
                
                if (LZ4F_isError(result)) {
                    throw std::runtime_error(
                        "LZ4 decompression error: " +
                        std::string(LZ4F_getErrorName(result))
                    );
                }
                
                hint = result; // Hint about how much data is expected
                src_pos += src_size;
                
                // Call callback with decompressed data
                if (dst_size > 0) {
                    bytes_decompressed_ += dst_size;
                    if (!callback(output_buffer_.data(), dst_size)) {
                        // Early termination requested
                        cleanupDecompression();
                        return;
                    }
                }
                
                // If we've consumed all input and hint is 0, we're done
                if (src_pos >= bytes_read && hint == 0) {
                    break;
                }
            }
        }
        
        cleanupDecompression();
        
    } catch (...) {
        cleanupDecompression();
        throw;
    }
}

void LZ4StreamReader::decompressMemory(const char* compressed_data, 
                                      size_t compressed_size,
                                      const DataCallback& callback) {
    if (!compressed_data || compressed_size == 0) {
        return;
    }
    
    // Initialize decompression
    initDecompression();
    bytes_read_ = 0;
    bytes_decompressed_ = 0;
    
    try {
        size_t src_pos = 0;
        size_t hint = 1; // Start with any non-zero hint
        
        while (src_pos < compressed_size && hint > 0) {
            // Copy data to input buffer
            size_t chunk_size = std::min(input_buffer_size_, compressed_size - src_pos);
            std::memcpy(input_buffer_.data(), compressed_data + src_pos, chunk_size);
            bytes_read_ += chunk_size;
            
            // Decompress the chunk
            size_t chunk_pos = 0;
            
            while (chunk_pos < chunk_size) {
                size_t src_size = chunk_size - chunk_pos;
                size_t dst_size = output_buffer_size_;
                
                size_t result = LZ4F_decompress(
                    state_->ctx,
                    output_buffer_.data(), &dst_size,
                    input_buffer_.data() + chunk_pos, &src_size,
                    nullptr
                );
                
                if (LZ4F_isError(result)) {
                    throw std::runtime_error(
                        "LZ4 decompression error: " +
                        std::string(LZ4F_getErrorName(result))
                    );
                }
                
                hint = result;
                chunk_pos += src_size;
                src_pos += src_size;
                
                // Call callback with decompressed data
                if (dst_size > 0) {
                    bytes_decompressed_ += dst_size;
                    if (!callback(output_buffer_.data(), dst_size)) {
                        // Early termination requested
                        cleanupDecompression();
                        return;
                    }
                }
                
                // If we've consumed all input and hint is 0, we're done
                if (src_pos >= compressed_size && hint == 0) {
                    break;
                }
            }
        }
        
        cleanupDecompression();
        
    } catch (...) {
        cleanupDecompression();
        throw;
    }
}

} // namespace chessmimic