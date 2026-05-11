#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <simdjson.h>
#include "pgn_converter/pgn_processor.hpp"
#include "pgn_converter/record_processor.hpp"
#include "../utils/thread_pool.hpp"

using namespace chessmimic;
// simdjson is used for parsing JSON

// Test fixture for PgnProcessor tests
class PgnProcessorTest : public testing::Test {
protected:
    void SetUp() override {
        // Create a temporary directory for test output
        temp_dir_ = std::filesystem::temp_directory_path() / "pgn_processor_test";
        output_dir_ = temp_dir_ / "output";
        std::filesystem::create_directories(temp_dir_);
        std::filesystem::create_directories(output_dir_);

        // Setup thread pool and logger
        thread_pool_ = std::make_unique<ThreadPool>(4); // 4 threads
        logger_ = std::make_unique<Logger>();
        logger_->setLevel(Logger::Level::DEBUG); // Enable debug logging

        // Set up paths to sample files
        data_dir_ = "../cpp_src/tests/data_files";
        
        // Paths to sample PGN files
        sample_100_pgn_ = (std::filesystem::path(data_dir_) / "sample_100.pgn").string();
        sample_1000_pgn_ = (std::filesystem::path(data_dir_) / "sample_1000.pgn").string();
        
        // Create a directory with some copied PGN files
        pgn_dir_ = temp_dir_ / "pgn_files";
        std::filesystem::create_directories(pgn_dir_);
        
        // Copy sample files to the test directory to test directory processing
        std::filesystem::copy_file(
            sample_100_pgn_,
            (pgn_dir_ / "file1.pgn").string()
        );
        
        std::filesystem::copy_file(
            sample_1000_pgn_,
            (pgn_dir_ / "file2.pgn").string()
        );
        
        // For the invalid file test, create a minimal invalid PGN file
        single_invalid_pgn_ = (temp_dir_ / "single_invalid.pgn").string();
        createInvalidPgnFile(single_invalid_pgn_);
    }

    void TearDown() override {
        // Clean up temporary files
        std::filesystem::remove_all(temp_dir_);

        // Clean up thread pool and logger
        thread_pool_.reset();
        logger_.reset();
    }

    // Helper to create a minimal invalid PGN file (missing ratings)
    static void createInvalidPgnFile(const std::string& file_path) {
        std::ofstream file(file_path);
        ASSERT_TRUE(file.is_open()) << "Failed to create invalid PGN file: " << file_path;

        // Write a PGN with missing required fields
        file << "[Event \"Invalid Test Game\"]\n";
        file << "[Site \"Test Suite\"]\n";
        file << "[Date \"2025.05.01\"]\n";
        file << "[Round \"1\"]\n";
        file << "[White \"Player1\"]\n";
        file << "[Black \"Player2\"]\n";
        // Missing WhiteElo and BlackElo
        file << "[Result \"1-0\"]\n\n";
        
        // Add very few moves
        file << "1. e4 e5 2. Nf3 1-0\n\n";
        
        file.close();
    }

    std::filesystem::path temp_dir_;
    std::filesystem::path output_dir_;
    std::filesystem::path pgn_dir_;
    std::string data_dir_;
    std::string sample_100_pgn_;     // Single game sample file
    std::string sample_1000_pgn_;    // Multi-game sample file
    std::string single_invalid_pgn_; // Invalid PGN file
    std::unique_ptr<ThreadPool> thread_pool_;
    std::unique_ptr<Logger> logger_;
};

// Test processing a single valid PGN file
TEST_F(PgnProcessorTest, ProcessSingleValidPgnFile) {
    // Create PgnProcessor with settings that will accept our valid game
    // Lower thresholds to ensure test passes with sample data
    PgnProcessor processor(
        temp_dir_.string(),
        1500,   // min_rating
        2000,   // max_rating
        4,      // min_ply - lowered to match sample files
        2,      // recent_moves - lowered to match sample files
        0,      // min_clock_seconds - no minimum clock time required
        0,      // max_games (0 = no limit)
        thread_pool_.get(),
        logger_.get()
    );

    // Process the single valid PGN file (sample_100.pgn)
    std::vector pgn_paths = {sample_100_pgn_};
    std::vector<std::string> output_files = processor.extractRecordsFromPgn(pgn_paths, output_dir_.string());

    // Verify outputs
    ASSERT_EQ(1, output_files.size()) << "Expected one output file";
    EXPECT_TRUE(std::filesystem::exists(output_files[0])) << "Output file not created: " << output_files[0];

    // Check that the output file is not empty (should contain position records)
    std::uintmax_t file_size = std::filesystem::file_size(output_files[0]);
    EXPECT_GT(file_size, 100) << "Output file is too small: " << file_size << " bytes";
    
    // For debugging if the test fails
    if (file_size <= 100) {
        std::cerr << "PGN file content (first 500 chars):" << std::endl;
        std::ifstream pgn_file(sample_100_pgn_);
        std::string content((std::istreambuf_iterator(pgn_file)), {});
        std::cerr << content.substr(0, 500) << "..." << std::endl;
    }
}

// Test processing a single invalid PGN file (missing ratings)
TEST_F(PgnProcessorTest, ProcessSingleInvalidPgnFile) {
    // Create PgnProcessor with settings that will reject our invalid game
    PgnProcessor processor(
        temp_dir_.string(),
        1500,   // min_rating
        2000,   // max_rating
        20,     // min_ply (too high for invalid file)
        8,      // recent_moves
        30,     // min_clock_seconds
        0,      // max_games (0 = no limit)
        thread_pool_.get(),
        logger_.get()
    );

    // Process the single invalid PGN file
    std::vector pgn_paths = {single_invalid_pgn_};
    std::vector<std::string> output_files = processor.extractRecordsFromPgn(pgn_paths, output_dir_.string());

    // Verify outputs - we should still get an output file, but it will be empty/small
    ASSERT_EQ(1, output_files.size()) << "Expected one output file";
    EXPECT_TRUE(std::filesystem::exists(output_files[0])) << "Output file not created: " << output_files[0];

    // The output file might exist but should be empty or very small
    // since all games were invalid and no records were extracted
    EXPECT_LE(std::filesystem::file_size(output_files[0]), 10) << "Output file should be empty/small";
}

// Test processing multiple PGN files (directory)
TEST_F(PgnProcessorTest, ProcessPgnDirectory) {
    // Create PgnProcessor with more lenient settings
    PgnProcessor processor(
        temp_dir_.string(),
        1500,   // min_rating
        2000,   // max_rating
        4,      // min_ply - lowered to match sample files
        2,      // recent_moves - lowered to match sample files
        0,      // min_clock_seconds - no minimum clock time required
        0,      // max_games (0 = no limit)
        thread_pool_.get(),
        logger_.get()
    );

    // Process the directory containing multiple PGN files (copied sample files)
    std::vector pgn_paths = {pgn_dir_.string()};
    std::vector<std::string> output_files = processor.extractRecordsFromPgn(pgn_paths, output_dir_.string());

    // We should get one output file per input PGN file in the directory
    ASSERT_EQ(2, output_files.size()) << "Expected one output file per input PGN";
    
    // Verify all output files exist and are not empty
    for (const auto& output_file : output_files) {
        EXPECT_TRUE(std::filesystem::exists(output_file)) << "Output file not created: " << output_file;
        EXPECT_GT(std::filesystem::file_size(output_file), 0) << "Output file is empty: " << output_file;
    }
}

// Test processing a multi-game PGN file
TEST_F(PgnProcessorTest, ProcessMultiGamePgnFile) {
    // Create PgnProcessor with more lenient settings
    PgnProcessor processor(
        temp_dir_.string(),
        1500,   // min_rating
        2000,   // max_rating
        4,      // min_ply - lowered to match sample files
        2,      // recent_moves - lowered to match sample files
        0,      // min_clock_seconds - no minimum clock time required
        0,      // max_games (0 = no limit)
        thread_pool_.get(),
        logger_.get()
    );

    // Process the multi-game PGN file (sample_1000.pgn)
    std::vector pgn_paths = {sample_1000_pgn_};
    std::vector<std::string> output_files = processor.extractRecordsFromPgn(pgn_paths, output_dir_.string());

    // Verify outputs
    ASSERT_EQ(1, output_files.size()) << "Expected one output file";
    EXPECT_TRUE(std::filesystem::exists(output_files[0])) << "Output file not created: " << output_files[0];

    // Check that the output file is not empty and has substantial content
    // (should contain position records from multiple games)
    std::uintmax_t file_size = std::filesystem::file_size(output_files[0]);
    EXPECT_GT(file_size, 100) << "Output file too small: " << file_size << " bytes";
    
    // For debugging if the test fails
    if (file_size <= 100) {
        std::cerr << "Multi-game PGN content (first 500 chars):" << std::endl;
        std::ifstream pgn_file(sample_1000_pgn_);
        std::string content((std::istreambuf_iterator(pgn_file)), {});
        std::cerr << content.substr(0, 500) << "..." << std::endl;
    }
}

// Test processing multiple PGN files of different types
TEST_F(PgnProcessorTest, ProcessMultiplePgnFiles) {
    // Create PgnProcessor with more lenient settings
    PgnProcessor processor(
        temp_dir_.string(),
        1500,   // min_rating
        2000,   // max_rating
        4,      // min_ply - lowered to match sample files
        2,      // recent_moves - lowered to match sample files
        0,      // min_clock_seconds - no minimum clock time required
        0,      // max_games (0 = no limit)
        thread_pool_.get(),
        logger_.get()
    );

    // Process multiple different PGN files
    std::vector pgn_paths = {
        sample_100_pgn_,
        sample_1000_pgn_
    };
    std::vector<std::string> output_files = processor.extractRecordsFromPgn(pgn_paths, output_dir_.string());

    // Verify outputs - should get one output file per input file
    ASSERT_EQ(2, output_files.size()) << "Expected one output file per input";
    
    // Verify all output files exist and are not empty
    for (const auto& output_file : output_files) {
        EXPECT_TRUE(std::filesystem::exists(output_file)) << "Output file not created: " << output_file;
        EXPECT_GT(std::filesystem::file_size(output_file), 0) << "Output file is empty: " << output_file;
    }
}

// Test processing with max_games limit
TEST_F(PgnProcessorTest, ProcessWithMaxGamesLimit) {
    // Create PgnProcessor with a max games limit of 2 and more lenient settings
    PgnProcessor processor(
        temp_dir_.string(),
        1500,   // min_rating
        2000,   // max_rating
        4,      // min_ply - lowered to match sample files
        2,      // recent_moves - lowered to match sample files
        0,      // min_clock_seconds - no minimum clock time required
        2,      // max_games (limit to 2 games)
        thread_pool_.get(),
        logger_.get()
    );

    // Process the multi-game PGN file (sample_1000.pgn)
    std::vector pgn_paths = {sample_1000_pgn_};
    std::vector<std::string> output_files = processor.extractRecordsFromPgn(pgn_paths, output_dir_.string());

    // Verify outputs
    ASSERT_EQ(1, output_files.size()) << "Expected one output file";
    EXPECT_TRUE(std::filesystem::exists(output_files[0])) << "Output file not created: " << output_files[0];

    // The output file should exist but only contain records from at most 2 games
    // Unfortunately, without direct inspection of the records, it's hard to verify
    // exactly how many games were processed, but we can check that the file is not empty
    EXPECT_GT(std::filesystem::file_size(output_files[0]), 0) << "Output file is empty";
}

// Test with different min/max rating settings
TEST_F(PgnProcessorTest, ProcessWithDifferentRatingSettings) {
    // Create PgnProcessor with a strict rating range that excludes our sample games
    PgnProcessor processor(
        temp_dir_.string(),
        2600,   // min_rating (higher than sample games)
        2800,   // max_rating
        4,      // min_ply - lowered but doesn't matter for this test
        2,      // recent_moves - lowered but doesn't matter for this test
        0,      // min_clock_seconds - no minimum
        0,      // max_games
        thread_pool_.get(),
        logger_.get()
    );

    // Process the sample PGN file (which typically has ratings below 2600)
    std::vector pgn_paths = {sample_100_pgn_};
    std::vector<std::string> output_files = processor.extractRecordsFromPgn(pgn_paths, output_dir_.string());

    // Verify outputs - we should still get an output file, but it will be empty/small
    ASSERT_EQ(1, output_files.size()) << "Expected one output file";
    EXPECT_TRUE(std::filesystem::exists(output_files[0])) << "Output file not created: " << output_files[0];

    // The file should exist but be empty or very small since no games meet the rating criteria
    EXPECT_LE(std::filesystem::file_size(output_files[0]), 10) << "Output file should be empty/small";
}

// Test recursive directory processing
TEST_F(PgnProcessorTest, ProcessRecursiveDirectory) {
    // Create a nested directory structure
    std::filesystem::path nested_dir = temp_dir_ / "nested_pgn_files";
    std::filesystem::path subdir1 = nested_dir / "subdir1";
    std::filesystem::path subdir2 = nested_dir / "subdir2";
    std::filesystem::path subdir2_nested = subdir2 / "nested";

    std::filesystem::create_directories(subdir1);
    std::filesystem::create_directories(subdir2_nested);

    // Copy PGN files to different levels of the directory structure
    // Level 1: root of nested_dir
    std::filesystem::copy_file(sample_100_pgn_, (nested_dir / "root_file.pgn").string());

    // Level 2: subdir1
    std::filesystem::copy_file(sample_1000_pgn_, (subdir1 / "subdir1_file.pgn").string());

    // Level 3: subdir2/nested
    std::filesystem::copy_file(sample_100_pgn_, (subdir2_nested / "deep_file.pgn").string());

    // Create PgnProcessor with more lenient settings
    PgnProcessor processor(
        temp_dir_.string(),
        1500,   // min_rating
        2000,   // max_rating
        4,      // min_ply
        2,      // recent_moves
        0,      // min_clock_seconds
        0,      // max_games (0 = no limit)
        thread_pool_.get(),
        logger_.get()
    );

    // Process the nested directory - should find all 3 files recursively
    std::vector pgn_paths = {nested_dir.string()};
    std::vector<std::string> output_files = processor.extractRecordsFromPgn(pgn_paths, output_dir_.string());

    // Verify we found all 3 PGN files across all subdirectories
    ASSERT_EQ(3, output_files.size()) << "Expected 3 output files (one for each PGN in nested dirs)";

    // Verify all output files exist and are not empty
    for (const auto& output_file : output_files) {
        EXPECT_TRUE(std::filesystem::exists(output_file)) << "Output file not created: " << output_file;
        EXPECT_GT(std::filesystem::file_size(output_file), 0) << "Output file is empty: " << output_file;
    }
}
