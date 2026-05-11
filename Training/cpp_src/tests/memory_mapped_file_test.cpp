#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <vector>
#include "utils/file_utils.hpp"
#include "pgn_converter/pgn_to_bagz_converter.hpp"

using namespace chessmimic;
using namespace chessmimic::FileUtils;

class MemoryMappedFileTest : public testing::Test {
protected:
    void SetUp() override {
        // Create a temporary directory for test files
        temp_dir_ = std::filesystem::temp_directory_path() / "mmap_test";
        std::filesystem::create_directories(temp_dir_);
        
        // Create a test file with known content
        test_file_path_ = temp_dir_ / "test_file.dat";
        createTestFile(test_file_path_, test_content_, 1024);
        
        // Create a large test file for size limit testing
        large_file_path_ = temp_dir_ / "large_file.dat";
        createTestFile(large_file_path_, std::vector(5 * 1024 * 1024, 'X'), 5 * 1024 * 1024);
    }

    void TearDown() override {
        // Clean up temporary directory
        std::filesystem::remove_all(temp_dir_);
    }
    
    // Helper to create a test file with specific content and size
    static void createTestFile(const std::filesystem::path& path, const std::vector<char>& content, size_t size) {
        std::ofstream file(path, std::ios::binary);
        ASSERT_TRUE(file.is_open()) << "Failed to create test file: " << path;
        
        // Write content
        file.write(content.data(), std::min(content.size(), size));
        
        // If content is smaller than size, pad with zeros
        if (content.size() < size) {
            std::vector<char> padding(size - content.size(), 0);
            file.write(padding.data(), padding.size());
        }
        
        file.close();
    }
    
    // Generate test content
    static std::vector<char> generateTestContent(size_t size) {
        std::vector<char> content(size);
        for (size_t i = 0; i < size; ++i) {
            content[i] = static_cast<char>(i % 256);
        }
        return content;
    }

    std::filesystem::path temp_dir_;
    std::filesystem::path test_file_path_;
    std::filesystem::path large_file_path_;
    std::vector<char> test_content_ = generateTestContent(1024);
};

// Test basic functionality: opening a file, reading data, and closing it
TEST_F(MemoryMappedFileTest, BasicFunctionality) {
    // Create a memory-mapped file
    MemoryMappedFile mmap_file(test_file_path_.string());
    
    // Check if file was opened successfully
    EXPECT_TRUE(mmap_file.is_open());
    
    // Verify file size
    EXPECT_EQ(mmap_file.size(), 1024);
    
    // Verify file content
    for (size_t i = 0; i < std::min(mmap_file.size(), test_content_.size()); ++i) {
        EXPECT_EQ(mmap_file.data()[i], test_content_[i])
            << "Content mismatch at position " << i;
    }
    
    // Test the at() method
    EXPECT_EQ(mmap_file.at(0), mmap_file.data());
    EXPECT_EQ(mmap_file.at(100)[0], test_content_[100]);
    
    // Memory-mapped file will be automatically closed in the destructor
}

// Test handling of empty files
TEST_F(MemoryMappedFileTest, EmptyFile) {
    // Create an empty file
    std::filesystem::path empty_file_path = temp_dir_ / "empty_file.dat";
    createTestFile(empty_file_path, std::vector<char>(), 0);
    
    // Try to memory map the empty file
    MemoryMappedFile mmap_file(empty_file_path.string());
    
    // The file descriptor should be open, but the memory mapping might not be successful
    // for empty files depending on the platform - let's account for both behaviors
    
    if (mmap_file.is_open()) {
        // Size should be 0
        EXPECT_EQ(mmap_file.size(), 0);
        
        // Data pointer should be null for empty file
        EXPECT_EQ(mmap_file.data(), nullptr);
    } else {
        // If the file isn't open, verify that the size and data are also consistent
        EXPECT_EQ(mmap_file.size(), 0);
        EXPECT_EQ(mmap_file.data(), nullptr);
    }
}

// Test error handling for non-existent files
TEST_F(MemoryMappedFileTest, NonExistentFile) {
    // Try to memory map a non-existent file
    std::filesystem::path non_existent_file = temp_dir_ / "non_existent_file.dat";
    
    // Should throw an exception
    EXPECT_THROW(MemoryMappedFile mmap_file(non_existent_file.string()), std::system_error);
}

// Test size limit enforcement
TEST_F(MemoryMappedFileTest, SizeLimit) {
    // Try to memory map a file with a size limit that's smaller than the file
    constexpr size_t size_limit_mb = 2; // 2MB limit
    
    // Should throw an exception because the file is 5MB
    EXPECT_THROW(
        MemoryMappedFile mmap_file(large_file_path_.string(), true, size_limit_mb),
        std::runtime_error
    );
    
    // Now try with a larger limit
    constexpr size_t larger_limit_mb = 10; // 10MB limit
    
    // Should work fine
    EXPECT_NO_THROW({
        MemoryMappedFile mmap_file(large_file_path_.string(), true, larger_limit_mb);
        EXPECT_TRUE(mmap_file.is_open());
        EXPECT_EQ(mmap_file.size(), 5 * 1024 * 1024);
    });
}

// Test out-of-bounds access
TEST_F(MemoryMappedFileTest, OutOfBoundsAccess) {
    MemoryMappedFile mmap_file(test_file_path_.string());
    
    // Legal access
    EXPECT_NO_THROW({
        [[maybe_unused]] const char* data = mmap_file.at(0);
    });
    EXPECT_NO_THROW({
        [[maybe_unused]] const char* data = mmap_file.at(mmap_file.size() - 1);
    });
    
    // Out-of-bounds access should throw
    EXPECT_THROW({
        [[maybe_unused]] const char* data = mmap_file.at(mmap_file.size());
    }, std::out_of_range);
    EXPECT_THROW({
        [[maybe_unused]] const char* data = mmap_file.at(mmap_file.size() + 100);
    }, std::out_of_range);
}

// Test advise hints
TEST_F(MemoryMappedFileTest, AdviseHints) {
    MemoryMappedFile mmap_file(test_file_path_.string());
    
    // Apply various advise hints
    EXPECT_TRUE(mmap_file.advise(FileAdviseMode::SEQUENTIAL));
    EXPECT_TRUE(mmap_file.advise(FileAdviseMode::RANDOM));
    EXPECT_TRUE(mmap_file.advise(FileAdviseMode::WILLNEED));
    
    // Test with offset and length
    EXPECT_TRUE(mmap_file.advise(FileAdviseMode::SEQUENTIAL, 0, 512));
    
    // Invalid offset/length should return false
    EXPECT_FALSE(mmap_file.advise(FileAdviseMode::SEQUENTIAL, mmap_file.size(), 100));
    EXPECT_FALSE(mmap_file.advise(FileAdviseMode::SEQUENTIAL, 100, mmap_file.size()));
}

// Test move semantics
TEST_F(MemoryMappedFileTest, MoveSemantics) {
    // Create initial memory-mapped file
    MemoryMappedFile mmap_file1(test_file_path_.string());
    const char* original_data = mmap_file1.data();
    size_t original_size = mmap_file1.size();
    
    // Move construct a new memory-mapped file
    MemoryMappedFile mmap_file2(std::move(mmap_file1));
    
    // Check that data was moved correctly
    EXPECT_EQ(mmap_file2.data(), original_data);
    EXPECT_EQ(mmap_file2.size(), original_size);
    
    // Original file should now be in an empty state
    EXPECT_FALSE(mmap_file1.is_open());
    EXPECT_EQ(mmap_file1.data(), nullptr);
    EXPECT_EQ(mmap_file1.size(), 0);
    
    // Move assign to another memory-mapped file
    MemoryMappedFile mmap_file3(large_file_path_.string());
    mmap_file3 = std::move(mmap_file2);
    
    // Check that data was moved correctly
    EXPECT_EQ(mmap_file3.data(), original_data);
    EXPECT_EQ(mmap_file3.size(), original_size);
    
    // Second file should now be in an empty state
    EXPECT_FALSE(mmap_file2.is_open());
    EXPECT_EQ(mmap_file2.data(), nullptr);
    EXPECT_EQ(mmap_file2.size(), 0);
}

// Test read-only vs. read-write modes
TEST_F(MemoryMappedFileTest, ReadWriteModes) {
    // Create a read-only memory-mapped file (default)
    MemoryMappedFile readonly_mmap(test_file_path_.string());
    
    // Create a read-write memory-mapped file
    MemoryMappedFile readwrite_mmap(test_file_path_.string(), false);
    
    // Both should be open
    EXPECT_TRUE(readonly_mmap.is_open());
    EXPECT_TRUE(readwrite_mmap.is_open());
    
    // Both should have the same size
    EXPECT_EQ(readonly_mmap.size(), readwrite_mmap.size());
    
    // Note: We don't actually test writing to the read-write mapping because
    // that would modify the data and potentially affect other tests
}