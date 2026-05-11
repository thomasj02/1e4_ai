#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "../clock_converter/clock_data_converter.hpp"

namespace chessmimic::test {

class ClockDataConverterTest : public testing::Test {
    protected:
        void SetUp() override {
            // Create temporary directory for tests
            temp_dir = std::filesystem::temp_directory_path() / "clock_converter_test";
            std::filesystem::create_directories(temp_dir);
        }

        void TearDown() override {
            // Clean up temporary directory
            std::filesystem::remove_all(temp_dir);
        }

        std::filesystem::path temp_dir;
};

TEST_F(ClockDataConverterTest, ParsesCommandLineArguments) {
    const char* argv[] = {
        "pgn_to_clock_bagz",
        "file1.pgn.lz4",
        "file2.pgn.lz4",
        "--output", "output.bagz",
        "--threads", "8",
        "--max-positions-per-key", "25",
        "--write-common-positions",
        "--common-positions-with-history", "common_with_history.jsonl",
        "--common-positions-fen-only", "common_fen_only.jsonl"
    };
    int argc = std::size(argv);

    ClockDataConverter::Config config = ClockDataConverter::parseArguments(argc, const_cast<char**>(argv));

    EXPECT_EQ(config.pgn_paths.size(), 2);
    EXPECT_EQ(config.pgn_paths[0], "file1.pgn.lz4");
    EXPECT_EQ(config.pgn_paths[1], "file2.pgn.lz4");
    EXPECT_EQ(config.output_bagz_path, "output.bagz");
    EXPECT_EQ(config.num_threads, 8);
    EXPECT_EQ(config.max_positions_per_key, 25);
    EXPECT_TRUE(config.write_common_positions);
    EXPECT_EQ(config.common_positions_with_history_path, "common_with_history.jsonl");
    EXPECT_EQ(config.common_positions_fen_only_path, "common_fen_only.jsonl");
}

TEST_F(ClockDataConverterTest, ParsesSkipCommonPositions) {
    const char* argv[] = {
        "pgn_to_clock_bagz",
        "file1.pgn.lz4",
        "--output", "output.bagz",
        "--skip-common-positions",
        "--common-positions-with-history", "common.jsonl"
    };
    int argc = std::size(argv);

    ClockDataConverter::Config config = ClockDataConverter::parseArguments(argc, const_cast<char**>(argv));

    EXPECT_FALSE(config.write_common_positions);
    EXPECT_TRUE(config.skip_common_positions);
    EXPECT_EQ(config.common_positions_with_history_path, "common.jsonl");
}

TEST_F(ClockDataConverterTest, ValidatesConfiguration) {
    // Create a dummy PGN file for validation
    std::filesystem::path dummy_pgn = temp_dir / "dummy.pgn.lz4";
    std::ofstream(dummy_pgn).close();

    ClockDataConverter::Config valid_config;
    valid_config.pgn_paths = {dummy_pgn.string()};
    valid_config.output_bagz_path = "output.bagz";
    valid_config.temp_dir = temp_dir.string();
    valid_config.num_threads = 4;
    valid_config.max_positions_per_key = 25;

    EXPECT_NO_THROW(ClockDataConverter::validateConfig(valid_config));
}

TEST_F(ClockDataConverterTest, ValidatesConfigurationMissingOutput) {
    ClockDataConverter::Config invalid_config;
    invalid_config.pgn_paths = {"file1.pgn.lz4"};
    // Missing output_bagz_path

    EXPECT_THROW(ClockDataConverter::validateConfig(invalid_config), std::invalid_argument);
}

TEST_F(ClockDataConverterTest, ValidatesConfigurationMissingPgnFiles) {
    ClockDataConverter::Config invalid_config;
    invalid_config.output_bagz_path = "output.bagz";
    // Missing pgn_paths

    EXPECT_THROW(ClockDataConverter::validateConfig(invalid_config), std::invalid_argument);
}

TEST_F(ClockDataConverterTest, ValidatesConfigurationInvalidThreads) {
    ClockDataConverter::Config invalid_config;
    invalid_config.pgn_paths = {"file1.pgn.lz4"};
    invalid_config.output_bagz_path = "output.bagz";
    invalid_config.num_threads = 0;  // Invalid

    EXPECT_THROW(ClockDataConverter::validateConfig(invalid_config), std::invalid_argument);
}

TEST_F(ClockDataConverterTest, CreatesTemporaryDirectory) {
    ClockDataConverter::Config config;
    config.pgn_paths = {"file1.pgn.lz4"};
    config.output_bagz_path = (temp_dir / "output.bagz").string();
    config.temp_dir = (temp_dir / "work").string();
    config.num_threads = 1;

    ClockDataConverter converter(config);

    // Check that temp directory was created
    EXPECT_TRUE(std::filesystem::exists(config.temp_dir));
}

TEST_F(ClockDataConverterTest, CalculatesMemoryLimits) {
    ClockDataConverter::Config config;
    config.pgn_paths = {"file1.pgn.lz4"};
    config.output_bagz_path = (temp_dir / "output.bagz").string();
    config.temp_dir = temp_dir.string();
    config.num_threads = 4;
    config.memory_limit_gb = 16.0;  // 16 GB total

    ClockDataConverter converter(config);

    // Should calculate chunk size as total_memory / num_threads
    // 16 / 4 = 4 GB per thread
    size_t expected_chunk_size = static_cast<size_t>(4.0 * 1024 * 1024 * 1024);
    EXPECT_EQ(converter.getMaxChunkSize(), expected_chunk_size);
}

TEST_F(ClockDataConverterTest, DistributesPgnFilesAcrossThreads) {
    std::vector<std::string> pgn_files = {
        "file1.pgn.lz4", "file2.pgn.lz4", "file3.pgn.lz4",
        "file4.pgn.lz4", "file5.pgn.lz4"
    };

    auto distributions = ClockDataConverter::distributeFiles(pgn_files, 2);

    EXPECT_EQ(distributions.size(), 2);
    // Thread 0 should get files 0, 2, 4
    EXPECT_EQ(distributions[0].size(), 3);
    EXPECT_EQ(distributions[0][0], "file1.pgn.lz4");
    EXPECT_EQ(distributions[0][1], "file3.pgn.lz4");
    EXPECT_EQ(distributions[0][2], "file5.pgn.lz4");

    // Thread 1 should get files 1, 3
    EXPECT_EQ(distributions[1].size(), 2);
    EXPECT_EQ(distributions[1][0], "file2.pgn.lz4");
    EXPECT_EQ(distributions[1][1], "file4.pgn.lz4");
}

}
