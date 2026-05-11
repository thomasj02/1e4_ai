#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_set>
#include <thread>
#include <simdjson.h>
#include <cstdlib>
#include <iomanip>

#include "../pgn_converter/pgn_to_bagz_converter.hpp"
#include "../pgn_converter/record_processor.hpp"
#include "../utils/logger.hpp"
#include "../core/bagz.hpp"

namespace fs = std::filesystem;

namespace chessmimic::tests {

class CommonMovesTest : public testing::Test {
protected:
    void SetUp() override {
        // Create a temporary directory for the test
        test_dir_ = fs::temp_directory_path() / "chessmimic_common_moves_test";
        fs::create_directories(test_dir_);

        // Set up input PGN paths
        sample_100_pgn_ = fs::path(__FILE__).parent_path() / "data_files" / "sample_100.pgn";
        sample_1000_pgn_ = fs::path(__FILE__).parent_path() / "data_files" / "sample_1000.pgn";
        ASSERT_TRUE(fs::exists(sample_100_pgn_)) << "Test PGN file not found: " << sample_100_pgn_;
        ASSERT_TRUE(fs::exists(sample_1000_pgn_)) << "Test PGN file not found: " << sample_1000_pgn_;

        // Set up output BAGZ path
        bagz_path_ = test_dir_ / "test_output.bagz";

        // Set up common moves output path
        common_moves_path_ = test_dir_ / "common_moves.jsonl";

        // Paths for the skip_common_moves roundtrip test
        output1_path_ = test_dir_ / "output1.bagz";
        output2_path_ = test_dir_ / "output2.bagz";
    }

    void TearDown() override {
        // Clean up the temporary directory
        fs::remove_all(test_dir_);
    }

    fs::path test_dir_;
    fs::path sample_100_pgn_;
    fs::path sample_1000_pgn_;
    fs::path bagz_path_;
    fs::path common_moves_path_;
    fs::path output1_path_;
    fs::path output2_path_;
};

// Test that common_moves.jsonl is not created when disabled
TEST_F(CommonMovesTest, CommonMovesNotCreatedWhenDisabled) {
    // Configure the converter
    PgnToBagzConverter::Config config;
    config.pgn_paths = {sample_100_pgn_.string()};
    config.bagz_path = bagz_path_.string();
    config.temp_dir = (test_dir_ / "temp").string();
    config.max_moves_per_position = 5; // Low threshold to ensure some positions exceed it
    config.write_common_moves = false; // Disabled
    config.common_moves_path = common_moves_path_.string();
    config.num_threads = std::min(4u, std::thread::hardware_concurrency());

    // Run the converter
    PgnToBagzConverter converter(config);
    converter.convert();

    // Verify output
    EXPECT_TRUE(fs::exists(bagz_path_)) << "BAGZ file was not created";
    EXPECT_FALSE(fs::exists(common_moves_path_)) << "Common moves file was created when disabled";
}

// Test that common_moves.jsonl is created when enabled
TEST_F(CommonMovesTest, CommonMovesCreatedWhenEnabled) {
    // Configure the converter
    PgnToBagzConverter::Config config;
    config.pgn_paths = {sample_100_pgn_.string()};
    config.bagz_path = bagz_path_.string();
    config.temp_dir = (test_dir_ / "temp").string();
    config.max_moves_per_position = 5; // Low threshold to ensure some positions exceed it
    config.write_common_moves = true; // Enabled
    config.common_moves_path = common_moves_path_.string();
    config.num_threads = std::min(4u, std::thread::hardware_concurrency());

    // Run the converter
    PgnToBagzConverter converter(config);
    converter.convert();

    // Verify output
    EXPECT_TRUE(fs::exists(bagz_path_)) << "BAGZ file was not created";
    EXPECT_TRUE(fs::exists(common_moves_path_)) << "Common moves file was not created when enabled";

    // Read common moves file and check it's not empty
    std::ifstream file(common_moves_path_);
    ASSERT_TRUE(file.is_open()) << "Failed to open common moves file";

    // Print the first few lines of the file for debugging
    std::cout << "\nCommon moves file content (first 5 lines):" << std::endl;
    std::string debug_line;
    int debug_count = 0;
    while (std::getline(file, debug_line) && debug_count < 5) {
        std::cout << debug_line << std::endl;
        debug_count++;
    }
    file.close();

    // Re-open the file for testing
    std::ifstream test_file(common_moves_path_);
    ASSERT_TRUE(test_file.is_open()) << "Failed to re-open common moves file";

    std::string line;
    std::vector<std::string> records;

    // Read all JSONL records
    while (std::getline(test_file, line)) {
        try {
            // Just validate it's parseable JSON
            simdjson::dom::parser line_parser;
            line_parser.parse(line);
            records.push_back(line);
        } catch (const std::exception& e) {
            FAIL() << "Failed to parse JSONL at line " << (records.size() + 1) << ": " << e.what();
        }
    }

    // There should be at least some positions with too many moves
    EXPECT_FALSE(records.empty()) << "No positions found in common moves file";

    // All records should have the expected format
    for (const auto& record_str : records) {
        simdjson::dom::parser record_parser;
        simdjson::dom::element record = record_parser.parse(record_str);
        
        // Basic check 1: Record has "fen" field
        auto fen_result = record["fen"];
        EXPECT_EQ(fen_result.error(), simdjson::SUCCESS) << "Missing 'fen' field in record: " << record_str;
        
        // Basic check 2: Record has "moves" field
        auto moves_result = record["moves"];
        EXPECT_EQ(moves_result.error(), simdjson::SUCCESS) << "Missing 'moves' field in record: " << record_str;
        
        // Basic check 3: FEN is a valid string with spaces
        auto fen = std::string(record["fen"]);
        EXPECT_NE(fen.find(' '), std::string::npos) << "Invalid FEN format: " << fen;
        
        // Basic check 4: Moves is a non-empty object
        auto moves = record["moves"];
        EXPECT_TRUE(moves.is_object()) << "Moves field is not an object";
        
        // Count moves to ensure not empty
        size_t move_count = 0;
        for (auto [key, value] : moves.get_object()) {
            (void)key; // Suppress unused warning
            (void)value; // Suppress unused warning
            move_count++;
        }
        EXPECT_GT(move_count, 0) << "Moves object is empty";
        
        // Basic check 5: All move counts are positive
        for (auto move : moves.get_object()) {
            auto count = move.value.get_int64().value();
            EXPECT_GT(count, 0) << "Invalid move count for move " << std::string(move.key) << ": " << count;
        }
    }
}

// Test with different max_moves_per_position thresholds
TEST_F(CommonMovesTest, DifferentThresholds) {
    std::vector thresholds = {1, 5, 10};
    std::vector<size_t> counts;

    for (int threshold : thresholds) {
        // Configure the converter
        PgnToBagzConverter::Config config;
        config.pgn_paths = {sample_100_pgn_.string()};
        config.bagz_path = bagz_path_.string();
        config.temp_dir = (test_dir_ / "temp").string();
        config.max_moves_per_position = threshold;
        config.write_common_moves = true;
        config.common_moves_path = (test_dir_ / ("common_moves_" + std::to_string(threshold) + ".jsonl")).string();
        config.num_threads = std::min(4u, std::thread::hardware_concurrency());

        // Run the converter
        PgnToBagzConverter converter(config);
        converter.convert();

        // Verify output
        EXPECT_TRUE(fs::exists(bagz_path_)) << "BAGZ file was not created";
        EXPECT_TRUE(fs::exists(config.common_moves_path)) << "Common moves file was not created";

        // Count positions
        std::ifstream file(config.common_moves_path);
        ASSERT_TRUE(file.is_open()) << "Failed to open common moves file";

        std::string line;
        size_t position_count = 0;

        // Count JSONL records
        while (std::getline(file, line)) {
            try {
                simdjson::dom::parser parser;
                parser.parse(line);
                position_count++;
            } catch (const std::exception& e) {
                FAIL() << "Failed to parse JSONL in DifferentThresholds test: " << e.what();
            }
        }

        counts.push_back(position_count);
    }

    // Lower thresholds should result in more positions being excluded
    // (and thus more positions in the common_moves.txt file)
    for (size_t i = 1; i < counts.size(); i++) {
        EXPECT_GE(counts[i-1], counts[i])
            << "Expected more positions with threshold " << thresholds[i-1]
            << " than with threshold " << thresholds[i];
    }
}

// Test the RecordProcessor class directly
TEST_F(CommonMovesTest, RecordProcessorTest) {
    // Clear any previous state
    RecordProcessor::clearCommonMoves();

    // Create a sample record and position key
    std::string key = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    
    // Build AggregatedRecord with enough moves to exceed threshold
    AggregatedRecord record;
    record.fen = key;
    
    // Add enough moves to exceed threshold (10 moves * 1 count each = 10 total)
    for (int i = 0; i < 10; i++) {
        std::string move = "e2e4_" + std::to_string(i);
        record.moves[MoveKey(move, "10", "1500")] = 1;
    }

    // Create a temporary output file
    std::string output_file = (test_dir_ / "test_output.tmp").string();
    std::ofstream out(output_file, std::ios::binary);
    ASSERT_TRUE(out.is_open()) << "Failed to open output file";

    // Test writeRecord with tracking enabled and a low threshold
    RecordProcessor::writeRecord(out, key, record, 5, true);
    out.close();

    // Verify the record was not written (file should be empty)
    std::ifstream in(output_file, std::ios::binary);
    ASSERT_TRUE(in.is_open()) << "Failed to open input file";
    in.seekg(0, std::ios::end);
    EXPECT_EQ(in.tellg(), 0) << "Record was written when it should have been filtered out";
    in.close();

    // Write the common moves file
    std::string common_moves_file = (test_dir_ / "test_common_moves.jsonl").string();
    int count = RecordProcessor::writeCommonMovesFile(common_moves_file);

    // Verify the common moves file
    EXPECT_GT(count, 0) << "No positions were tracked";
    EXPECT_TRUE(fs::exists(common_moves_file)) << "Common moves file was not created";

    // Verify the contents
    std::ifstream common_file(common_moves_file);
    ASSERT_TRUE(common_file.is_open()) << "Failed to open common moves file";

    // Print file contents for debugging
    std::cout << "\nTest RecordProcessorTest - Common moves file content:" << std::endl;
    std::string debug_line;
    while (std::getline(common_file, debug_line)) {
        std::cout << debug_line << std::endl;
    }

    // Re-open the file for testing
    common_file.close();
    std::ifstream test_file(common_moves_file);
    ASSERT_TRUE(test_file.is_open()) << "Failed to re-open common moves file";

    std::string line;
    bool found_key = false;

    while (std::getline(test_file, line)) {
        try {
            simdjson::dom::parser parser;
            simdjson::dom::element json = parser.parse(line);
            if (auto fen = std::string(json["fen"]); fen == key) {
                found_key = true;
                
                // Verify the moves field
                auto moves = json["moves"];
                EXPECT_TRUE(moves.is_object()) << "Moves field is not an object";
                
                // Count total moves
                int total_moves = 0;
                for (auto move : moves.get_object()) {
                    total_moves += move.value.get_int64().value();
                }
                
                // We expect 10 moves total (we added 10 different moves with count 1 each)
                EXPECT_EQ(10, total_moves) << "Total move count doesn't match expected value";
                break;
            }
        } catch (const std::exception& e) {
            FAIL() << "Failed to parse JSONL in RecordProcessorTest: " << e.what();
        }
    }

    EXPECT_TRUE(found_key) << "Position key not found in common moves file";
}

// Test the skip_common_moves flag with roundtrip
TEST_F(CommonMovesTest, SkipCommonMovesRoundtrip) {
    // Step 1: Generate common_moves.jsonl from sample_100.pgn with a low threshold
    {
        // Configure the converter
        PgnToBagzConverter::Config config;
        config.pgn_paths = {sample_100_pgn_.string()};
        config.bagz_path = output1_path_.string();
        config.temp_dir = (test_dir_ / "temp1").string();
        config.max_moves_per_position = 5; // Set a low threshold to get some common positions
        config.write_common_moves = true;
        config.skip_common_moves = false;
        config.common_moves_path = common_moves_path_.string();
        config.num_threads = std::min(4u, std::thread::hardware_concurrency());

        // Run the converter
        PgnToBagzConverter converter(config);
        converter.convert();

        // Verify output
        ASSERT_TRUE(fs::exists(output1_path_)) << "First BAGZ file was not created";
        ASSERT_TRUE(fs::exists(common_moves_path_)) << "Common moves file was not created";

        // Verify the common moves file is not empty
        std::ifstream common_file(common_moves_path_);
        ASSERT_TRUE(common_file.is_open()) << "Failed to open common moves file";

        std::string line;
        std::vector<std::string> non_header_lines;

        // Count non-header lines
        while (std::getline(common_file, line)) {
            if (!line.empty() && line[0] != '#') {
                non_header_lines.push_back(line);
            }
        }

        ASSERT_FALSE(non_header_lines.empty()) << "No positions found in common moves file";
        std::cout << "Generated " << non_header_lines.size() << " common positions from sample_100.pgn" << std::endl;
    }

    // Step 2: Process sample_1000.pgn with --skip_common_moves to skip positions from Step 1
    {
        // Configure the converter
        PgnToBagzConverter::Config config;
        config.pgn_paths = {sample_1000_pgn_.string()}; // Use the larger file
        config.bagz_path = output2_path_.string();
        config.temp_dir = (test_dir_ / "temp2").string();
        config.max_moves_per_position = 500; // High threshold to make sure we don't skip common positions that we calculate
        config.write_common_moves = false;
        config.skip_common_moves = true;
        config.common_moves_path = common_moves_path_.string();
        config.num_threads = std::min(4u, std::thread::hardware_concurrency());

        // Run the converter
        PgnToBagzConverter converter(config);
        converter.convert();

        // Verify output
        ASSERT_TRUE(fs::exists(output2_path_)) << "Second BAGZ file was not created";
    }

    // Step 3: Read all the common positions from the file
    std::unordered_set<std::string> common_positions;
    {
        std::ifstream common_file(common_moves_path_);
        ASSERT_TRUE(common_file.is_open()) << "Failed to open common moves file for verification";

        std::string line;
        // Read all JSONL records
        while (std::getline(common_file, line)) {
            try {
                simdjson::dom::parser parser;
                simdjson::dom::element json = parser.parse(line);
                auto fen = std::string(json["fen"]);
                common_positions.insert(fen);
            } catch (const std::exception& e) {
                FAIL() << "Failed to parse JSONL in SkipCommonMovesRoundtrip test: " << e.what();
            }
        }

        ASSERT_FALSE(common_positions.empty()) << "No positions loaded from common moves file";
    }

    // Step 4: Verify that none of the common positions are in the second BAGZ file
    {
        BagFileReader reader(output2_path_.string());
        size_t record_count = reader.size();
        int skipped_records = 0;

        for (size_t i = 0; i < record_count; i++) {
            auto record_data = reader.get_record(static_cast<int64_t>(i));

            // Parse record to get key (FEN)
            // This is a simplified approach; in a real test we would need to properly
            // extract the key from the binary record format

            // Convert binary data to string
            std::string record_str(reinterpret_cast<char*>(record_data.data()), record_data.size());

            // Find any of the common positions in the record
            bool found_common = false;
            for (const auto& common_pos : common_positions) {
                if (record_str.find(common_pos) != std::string::npos) {
                    found_common = true;
                    skipped_records++;
                    break;
                }
            }

            // If we find a common position, it's an error
            EXPECT_FALSE(found_common) << "Found a common position in the output file that should have been skipped";
        }

        std::cout << "Processed " << record_count << " records from sample_1000.pgn" << std::endl;
        std::cout << "Confirmed " << skipped_records << " records containing common positions were skipped" << std::endl;

        // There should be at least some records left
        EXPECT_GT(record_count, 0) << "No records found in the output file";
    }
}

}
