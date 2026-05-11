#include <gtest/gtest.h>
#include "../io/lz4_stream_reader.hpp"
#include <lz4frame.h>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <random>

namespace chessmimic {

class LZ4StreamReaderTest : public testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path() / 
                    ("lz4_stream_test_" + std::to_string(getpid()));
        std::filesystem::create_directories(temp_dir_);
    }
    
    void TearDown() override {
        std::filesystem::remove_all(temp_dir_);
    }
    
    // Helper to compress data to LZ4 format
    static std::vector<char> compressData(const std::string& data) {
        size_t max_dst_size = LZ4F_compressFrameBound(data.size(), nullptr);
        std::vector<char> compressed(max_dst_size);
        
        size_t compressed_size = LZ4F_compressFrame(
            compressed.data(), max_dst_size,
            data.data(), data.size(),
            nullptr
        );
        
        if (LZ4F_isError(compressed_size)) {
            throw std::runtime_error("Compression failed: " + 
                std::string(LZ4F_getErrorName(compressed_size)));
        }
        
        compressed.resize(compressed_size);
        return compressed;
    }
    
    // Helper to write compressed data to file
    void writeCompressedFile(const std::string& filename, const std::vector<char>& data) const {
        std::ofstream file(temp_dir_ / filename, std::ios::binary);
        file.write(data.data(), data.size());
        file.close();
    }
    
    // Helper to generate random data
    static std::string generateRandomData(size_t size) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution dis(32, 126); // Printable ASCII
        
        std::string result;
        result.reserve(size);
        for (size_t i = 0; i < size; ++i) {
            result.push_back(static_cast<char>(dis(gen)));
        }
        return result;
    }
    
    std::filesystem::path temp_dir_;
};

// Test basic decompression of small file
TEST_F(LZ4StreamReaderTest, DecompressSmallFile) {
    std::string original = "Hello, LZ4 streaming world!";
    auto compressed = compressData(original);
    writeCompressedFile("test.lz4", compressed);
    
    LZ4StreamReader reader;
    std::string decompressed;
    
    reader.decompressFile((temp_dir_ / "test.lz4").string(), 
        [&decompressed](const char* data, size_t size) {
            decompressed.append(data, size);
            return true;
        });
    
    EXPECT_EQ(original, decompressed);
    EXPECT_EQ(compressed.size(), reader.getBytesRead());
    EXPECT_EQ(original.size(), reader.getBytesDecompressed());
}

// Test decompression with early termination
TEST_F(LZ4StreamReaderTest, EarlyTermination) {
    std::string original = "This is a longer text that we will stop reading halfway through!";
    auto compressed = compressData(original);
    writeCompressedFile("test.lz4", compressed);
    
    LZ4StreamReader reader;
    std::string decompressed;
    size_t chunks_received = 0;
    
    reader.decompressFile((temp_dir_ / "test.lz4").string(), 
        [&](const char* data, size_t size) {
            decompressed.append(data, size);
            chunks_received++;
            return false; // Stop after first chunk
        });
    
    // Should have received only one chunk
    EXPECT_EQ(1, chunks_received);
    EXPECT_FALSE(decompressed.empty());
    EXPECT_LE(decompressed.size(), original.size());
}

// Test large file decompression (10MB)
TEST_F(LZ4StreamReaderTest, DecompressLargeFile) {
    constexpr size_t size = 10 * 1024 * 1024; // 10MB
    std::string original = generateRandomData(size);
    auto compressed = compressData(original);
    writeCompressedFile("large.lz4", compressed);
    
    // Use small buffers to ensure multiple chunks
    LZ4StreamReader reader(4096, 8192); // 4KB input, 8KB output buffers
    std::string decompressed;
    size_t chunks_received = 0;
    
    reader.decompressFile((temp_dir_ / "large.lz4").string(), 
        [&](const char* data, size_t _size) {
            decompressed.append(data, _size);
            chunks_received++;
            return true;
        });
    
    EXPECT_EQ(original, decompressed);
    EXPECT_GT(chunks_received, 100); // Should receive many chunks with small buffers
    EXPECT_EQ(compressed.size(), reader.getBytesRead());
    EXPECT_EQ(original.size(), reader.getBytesDecompressed());
}

// Test decompression from memory
TEST_F(LZ4StreamReaderTest, DecompressFromMemory) {
    std::string original = "Memory decompression test data!";
    auto compressed = compressData(original);
    
    LZ4StreamReader reader;
    std::string decompressed;
    
    reader.decompressMemory(compressed.data(), compressed.size(), 
        [&decompressed](const char* data, size_t size) {
            decompressed.append(data, size);
            return true;
        });
    
    EXPECT_EQ(original, decompressed);
    EXPECT_EQ(compressed.size(), reader.getBytesRead());
    EXPECT_EQ(original.size(), reader.getBytesDecompressed());
}

// Test with empty file
TEST_F(LZ4StreamReaderTest, EmptyFile) {
    std::string original = "";
    auto compressed = compressData(original);
    writeCompressedFile("empty.lz4", compressed);
    
    LZ4StreamReader reader;
    std::string decompressed;

    reader.decompressFile((temp_dir_ / "empty.lz4").string(), 
        [&](const char* data, size_t size) {
            decompressed.append(data, size);
            return true;
        });
    
    EXPECT_EQ(original, decompressed);
    // Callback might not be called for empty data
    EXPECT_EQ(0, reader.getBytesDecompressed());
}

// Test error handling - corrupted file
TEST_F(LZ4StreamReaderTest, CorruptedFile) {
    // Create a file with invalid LZ4 data
    std::vector corrupted = {'B', 'A', 'D', ' ', 'D', 'A', 'T', 'A'};
    writeCompressedFile("corrupted.lz4", corrupted);
    
    LZ4StreamReader reader;
    
    EXPECT_THROW({
        reader.decompressFile((temp_dir_ / "corrupted.lz4").string(), 
            [](const char*, size_t) { return true; });
    }, std::runtime_error);
}

// Test error handling - non-existent file
TEST_F(LZ4StreamReaderTest, NonExistentFile) {
    LZ4StreamReader reader;
    
    EXPECT_THROW({
        reader.decompressFile((temp_dir_ / "nonexistent.lz4").string(), 
            [](const char*, size_t) { return true; });
    }, std::runtime_error);
}

// Test move operations
TEST_F(LZ4StreamReaderTest, MoveOperations) {
    std::string original = "Move test data";
    auto compressed = compressData(original);
    
    // Test move constructor
    LZ4StreamReader reader1(1024, 2048);
    LZ4StreamReader reader2(std::move(reader1));
    
    std::string decompressed;
    reader2.decompressMemory(compressed.data(), compressed.size(),
        [&decompressed](const char* data, size_t size) {
            decompressed.append(data, size);
            return true;
        });
    
    EXPECT_EQ(original, decompressed);
    
    // Test move assignment
    LZ4StreamReader reader3 = std::move(reader2);
    
    decompressed.clear();
    reader3.decompressMemory(compressed.data(), compressed.size(),
        [&decompressed](const char* data, size_t size) {
            decompressed.append(data, size);
            return true;
        });
    
    EXPECT_EQ(original, decompressed);
}

// Test streaming behavior with PGN-like data
TEST_F(LZ4StreamReaderTest, StreamingPGNData) {
    // Create PGN-like data with multiple games
    std::stringstream pgn;
    constexpr size_t num_games = 100;
    for (size_t i = 0; i < num_games; ++i) {
        pgn << "[Event \"Test Game " << i << "\"]\n"
            << "[Date \"2023.01.01\"]\n"
            << "[White \"Player1\"]\n"
            << "[Black \"Player2\"]\n"
            << "[Result \"1-0\"]\n"
            << "[TimeControl \"180+2\"]\n\n"
            << "1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} "
            << "2. Nf3 {[%clk 0:02:55]} Nc6 {[%clk 0:02:54]} 1-0\n\n";
    }
    
    std::string original = pgn.str();
    auto compressed = compressData(original);
    writeCompressedFile("games.pgn.lz4", compressed);
    
    // Use small buffers to simulate streaming
    LZ4StreamReader reader(1024, 2048);
    
    // Verify we can decompress the entire content correctly
    std::string decompressed;
    size_t chunks_received = 0;
    
    reader.decompressFile((temp_dir_ / "games.pgn.lz4").string(), 
        [&](const char* data, size_t size) {
            decompressed.append(data, size);
            chunks_received++;
            return true;
        });
    
    // Verify content matches
    EXPECT_EQ(original, decompressed);
    
    // Verify we received multiple chunks (streaming behavior)
    EXPECT_GT(chunks_received, 10); // With small buffers, should get many chunks
    
    // Verify the decompressed content contains all games
    size_t event_count = 0;
    size_t pos = 0;
    while ((pos = decompressed.find("[Event \"Test Game", pos)) != std::string::npos) {
        event_count++;
        pos += 17;
    }
    EXPECT_EQ(num_games, event_count);
}

// Benchmark test (disabled by default)
TEST_F(LZ4StreamReaderTest, DISABLED_BenchmarkLargeFile) {
    constexpr size_t size = 100 * 1024 * 1024; // 100MB
    std::string original = generateRandomData(size);
    auto compressed = compressData(original);
    writeCompressedFile("benchmark.lz4", compressed);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    LZ4StreamReader reader;
    size_t total_size = 0;
    
    reader.decompressFile((temp_dir_ / "benchmark.lz4").string(), 
        [&total_size](const char* /*data*/, size_t _size) {
            total_size += _size;
            return true;
        });
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    double throughput = size / (1024.0 * 1024.0) / (duration.count() / 1000.0);
    
    std::cout << "Decompressed " << size / (1024 * 1024) << " MB in " 
              << duration.count() << " ms"
              << " (throughput: " << throughput << " MB/s)" << std::endl;
    
    EXPECT_EQ(size, total_size);
}

} // namespace chessmimic