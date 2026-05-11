#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <unistd.h>
#include <simdjson.h>
#include "pgn_converter/external_sorter.hpp"
#include "pgn_converter/record_processor.hpp"
#include "../utils/thread_pool.hpp"

using namespace chessmimic;

// Test fixture for ExternalSorter tests
class ExternalSorterTest : public testing::Test {
protected:
    void SetUp() override {
        // Create a unique temporary directory for test files
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        auto pid = getpid();
        temp_dir_ = std::filesystem::temp_directory_path() / 
                    ("external_sorter_test_" + std::to_string(pid) + "_" + std::to_string(now));
        std::filesystem::create_directories(temp_dir_);

        // Setup thread pool and logger
        thread_pool_ = std::make_unique<ThreadPool>(4); // 4 threads
        logger_ = std::make_unique<Logger>();
        logger_->setLevel(Logger::Level::DEBUG); // Enable debug logging
        
        // Clear any common moves from previous tests
        RecordProcessor::clearCommonMoves();
    }

    // Helper to dump file contents for debugging
    static void dumpFileContents(const std::string& file_path) {
        std::ifstream in(file_path, std::ios::binary);
        if (!in.is_open()) {
            std::cerr << "Failed to open file for debugging: " << file_path << std::endl;
            return;
        }

        // Get file size
        in.seekg(0, std::ios::end);
        std::streamsize size = in.tellg();
        in.seekg(0, std::ios::beg);

        std::cerr << "File size: " << size << " bytes" << std::endl;

        // Read up to first 100 bytes as hex
        constexpr int max_display = 100;
        std::vector<char> buffer(std::min<std::streamsize>(size, max_display));
        if (in.read(buffer.data(), buffer.size())) {
            std::cerr << "File hex dump (first " << buffer.size() << " bytes):" << std::endl;
            for (size_t i = 0; i < buffer.size(); ++i) {
                std::cerr << std::hex << std::setw(2) << std::setfill('0')
                    << (static_cast<int>(buffer[i]) & 0xFF) << " ";
                if ((i + 1) % 16 == 0) std::cerr << std::endl;
            }
            std::cerr << std::dec << std::endl;
        }
    }

    void TearDown() override {
        // Clean up temporary files
        std::filesystem::remove_all(temp_dir_);

        // Clean up thread pool and logger
        thread_pool_.reset();
        logger_.reset();
    }

    // Helper to write a record to a binary file using the actual RecordProcessor
    static void writeRecord(std::ofstream& out, const std::string& key, const std::string& record) {
        // Use the actual RecordProcessor implementation to write the record
        // This ensures consistency with how records are read/written in the real code
        RecordProcessor::writeRawRecord(out, key, record);
    }

    // Helper to create a test file with records in a specific order
    [[nodiscard]] std::string createTestFile(const std::vector<std::pair<std::string, std::string>>& records,
                                         const std::string& suffix = "") const {
        std::string file_path = (temp_dir_ / ("test_records" + suffix + ".records")).string();
        std::ofstream out(file_path, std::ios::binary);
        EXPECT_TRUE(out.is_open()) << "Failed to open output file: " << file_path;

        for (const auto& [key, record] : records) {
            writeRecord(out, key, record);
        }

        return file_path;
    }

    // Helper to read records from a file into a vector for verification
    static std::vector<std::pair<std::string, std::string>> readRecordsFromFile(const std::string& file_path) {
        std::vector<std::pair<std::string, std::string>> records;
        std::ifstream in(file_path, std::ios::binary);
        EXPECT_TRUE(in.is_open()) << "Failed to open input file: " << file_path;

        while (in) {
            std::string record_json;
            if (std::string key; RecordProcessor::readRecord(in, key, record_json)) {
                try {
                    // Skip empty records (which may be padding)
                    if (key.empty() || record_json.empty()) {
                        std::cerr << "Skipping empty record" << std::endl;
                        continue;
                    }

                    // Debug output
                    std::cerr << "Read record with key: " << key << std::endl;
                    std::cerr << "JSON content: " << record_json << std::endl;

                    // Validate that it's valid JSON using simdjson
                    simdjson::dom::parser parser;
                    parser.parse(record_json);
                    
                    records.emplace_back(key, record_json);
                }
                catch (const simdjson::simdjson_error& e) {
                    ADD_FAILURE() << "Failed to parse JSON for key '" << key << "': " << e.what()
                        << "\nAttempting to parse: '" << record_json << "'";
                }
            }
            else {
                break;
            }
        }

        return records;
    }

    std::filesystem::path temp_dir_;
    std::unique_ptr<ThreadPool> thread_pool_;
    std::unique_ptr<Logger> logger_;
};

// Test sortFile with small sample input file
TEST_F(ExternalSorterTest, SortSmallFile) {
    // Create a test file with records in unsorted order
    std::vector<std::pair<std::string, std::string>> unsorted_records = {
        {"c", R"({"moves":{"c2c4":{"1000":{"1500":5}}},"recent_and_fen":[[],"fenC"]})"},
        {"a", R"({"moves":{"e2e4":{"1000":{"1500":5}}},"recent_and_fen":[[],"fenA"]})"},
        {"b", R"({"moves":{"d2d4":{"1000":{"1500":5}}},"recent_and_fen":[[],"fenB"]})"}
    };
    std::string input_file = createTestFile(unsorted_records);

    // Debug: dump input file contents
    std::cerr << "Input file: " << input_file << std::endl;
    dumpFileContents(input_file);

    // Create ExternalSorter
    ExternalSorter sorter(temp_dir_.string(), 1000, thread_pool_.get(), logger_.get(), false);

    // Sort the file
    std::string sorted_file = sorter.sortFiles({input_file});

    // Verify the sorted file exists
    EXPECT_TRUE(std::filesystem::exists(sorted_file)) << "Sorted file not created: " << sorted_file;

    // Debug: dump sorted file contents
    std::cerr << "Sorted file: " << sorted_file << std::endl;
    dumpFileContents(sorted_file);

    // Read the sorted records
    auto sorted_records = readRecordsFromFile(sorted_file);

    // Verify sorting
    ASSERT_EQ(3, sorted_records.size());
    EXPECT_EQ("a", sorted_records[0].first);
    EXPECT_EQ("b", sorted_records[1].first);
    EXPECT_EQ("c", sorted_records[2].first);
}

// Test sorting with empty file
TEST_F(ExternalSorterTest, SortEmptyFile) {
    // Create an empty test file
    std::string empty_file = (temp_dir_ / "empty.records").string();
    {
        std::ofstream out(empty_file, std::ios::binary);
        EXPECT_TRUE(out.is_open()) << "Failed to create empty file: " << empty_file;
    }

    // Create ExternalSorter
    ExternalSorter sorter(temp_dir_.string(), 1000, thread_pool_.get(), logger_.get(), false);

    // Sort the empty file (should still succeed)
    std::string sorted_file = sorter.sortFiles({empty_file});

    // Verify the sorted file exists and is empty
    EXPECT_TRUE(std::filesystem::exists(sorted_file)) << "Sorted file not created: " << sorted_file;

    // Read the sorted records (should be empty)
    auto sorted_records = readRecordsFromFile(sorted_file);
    EXPECT_TRUE(sorted_records.empty());
}

// Test sorting multiple files in chunks
TEST_F(ExternalSorterTest, SortMultipleChunks) {
    // Create test files with records in unsorted order
    std::vector<std::pair<std::string, std::string>> records1 = {
        {"m", R"({"moves":{"m2m4":{"1000":{"1500":1}}},"recent_and_fen":[[],"fenM"]})"},
        {"k", R"({"moves":{"k2k4":{"1000":{"1500":1}}},"recent_and_fen":[[],"fenK"]})"}
    };

    std::vector<std::pair<std::string, std::string>> records2 = {
        {"n", R"({"moves":{"n2n4":{"1000":{"1500":1}}},"recent_and_fen":[[],"fenN"]})"},
        {"j", R"({"moves":{"j2j4":{"1000":{"1500":1}}},"recent_and_fen":[[],"fenJ"]})"}
    };

    std::vector<std::pair<std::string, std::string>> records3 = {
        {"l", R"({"moves":{"l2l4":{"1000":{"1500":1}}},"recent_and_fen":[[],"fenL"]})"},
        {"i", R"({"moves":{"i2i4":{"1000":{"1500":1}}},"recent_and_fen":[[],"fenI"]})"}
    };

    // Create test files
    std::string file1 = createTestFile(records1, "_1");
    std::string file2 = createTestFile(records2, "_2");
    std::string file3 = createTestFile(records3, "_3");

    // Create ExternalSorter with small chunk size to force chunking
    ExternalSorter sorter(temp_dir_.string(), 1, thread_pool_.get(), logger_.get(), false);

    // Collect file paths and use sortFiles
    std::vector files = {file1, file2, file3};
    std::string sorted_file = sorter.sortFiles(files);

    // Verify the sorted file exists
    EXPECT_TRUE(std::filesystem::exists(sorted_file)) << "Sorted file not created: " << sorted_file;

    // Read the sorted records
    auto sorted_records = readRecordsFromFile(sorted_file);

    // Verify sorting
    ASSERT_EQ(6, sorted_records.size());
    EXPECT_EQ("i", sorted_records[0].first);
    EXPECT_EQ("j", sorted_records[1].first);
    EXPECT_EQ("k", sorted_records[2].first);
    EXPECT_EQ("l", sorted_records[3].first);
    EXPECT_EQ("m", sorted_records[4].first);
    EXPECT_EQ("n", sorted_records[5].first);
}

// Test aggregateSortedRecords with duplicate keys
TEST_F(ExternalSorterTest, AggregateSortedRecords) {
    // Create a sorted file with duplicate keys
    std::vector<std::pair<std::string, std::string>> sorted_records = {
        {"a", R"({"moves":{"e2e4":{"1000":{"1500":5}}},"recent_and_fen":[[],"fenA"]})"},
        {"a", R"({"moves":{"e2e4":{"1000":{"1600":3}}},"recent_and_fen":[[],"fenA"]})"},
        {"b", R"({"moves":{"d2d4":{"1000":{"1500":7}}},"recent_and_fen":[[],"fenB"]})"},
        {"c", R"({"moves":{"c2c4":{"1000":{"1500":2}}},"recent_and_fen":[[],"fenC"]})"},
        {"c", R"({"moves":{"c2c4":{"2000":{"1700":4}}},"recent_and_fen":[[],"fenC"]})"}
    };
    std::string sorted_file = createTestFile(sorted_records, "_sorted");

    // Create ExternalSorter
    ExternalSorter sorter(temp_dir_.string(), 1000, thread_pool_.get(), logger_.get(), false);

    // Aggregate the sorted records
    std::string aggregated_file = sorter.aggregateSortedRecords(sorted_file, 0);

    // Verify the aggregated file exists
    EXPECT_TRUE(std::filesystem::exists(aggregated_file)) << "Aggregated file not created: " << aggregated_file;

    // Read the aggregated records
    auto aggregated_records = readRecordsFromFile(aggregated_file);

    // Verify aggregation - should have 3 unique keys
    ASSERT_EQ(3, aggregated_records.size());

    // Check first key (a) - should have merged moves
    EXPECT_EQ("a", aggregated_records[0].first);
    {
        simdjson::dom::parser parser;
        simdjson::dom::element doc = parser.parse(aggregated_records[0].second);
        EXPECT_EQ(5, static_cast<int64_t>(doc["moves"]["e2e4"]["1000"]["1500"]));
        EXPECT_EQ(3, static_cast<int64_t>(doc["moves"]["e2e4"]["1000"]["1600"]));
    }

    // Check second key (b) - single record
    EXPECT_EQ("b", aggregated_records[1].first);
    {
        simdjson::dom::parser parser;
        simdjson::dom::element doc = parser.parse(aggregated_records[1].second);
        EXPECT_EQ(7, static_cast<int64_t>(doc["moves"]["d2d4"]["1000"]["1500"]));
    }

    // Check third key (c) - should have merged records with different time controls
    EXPECT_EQ("c", aggregated_records[2].first);
    {
        simdjson::dom::parser parser;
        simdjson::dom::element doc = parser.parse(aggregated_records[2].second);
        EXPECT_EQ(2, static_cast<int64_t>(doc["moves"]["c2c4"]["1000"]["1500"]));
        EXPECT_EQ(4, static_cast<int64_t>(doc["moves"]["c2c4"]["2000"]["1700"]));
    }
}

// Test aggregation with move filtering
TEST_F(ExternalSorterTest, AggregateWithMoveFiltering) {
    // Create a test file with records containing many moves
    std::vector<std::pair<std::string, std::string>> many_moves_records = {
        {"a", R"({"moves":{"e2e4":{"1000":{"1500":15}}},"recent_and_fen":[[],"fenA"]})"},
        {"a", R"({"moves":{"d2d4":{"1000":{"1500":10}}},"recent_and_fen":[[],"fenA"]})"},
        {"b", R"({"moves":{"c2c4":{"1000":{"1500":3}}},"recent_and_fen":[[],"fenB"]})"}
    };
    std::string many_moves_file = createTestFile(many_moves_records, "_many_moves");

    // Create ExternalSorter
    ExternalSorter sorter(temp_dir_.string(), 1000, thread_pool_.get(), logger_.get(), false);

    // First sort the file
    std::string sorted_file = sorter.sortFiles({many_moves_file});

    // Set max_moves_per_position to 30 (a should pass, since total is 25)
    std::string aggregated_file = sorter.aggregateSortedRecords(sorted_file, 30);

    // Verify the aggregated file exists
    EXPECT_TRUE(std::filesystem::exists(aggregated_file)) << "Aggregated file not created: " << aggregated_file;

    // Read the aggregated records
    auto aggregated_records = readRecordsFromFile(aggregated_file);

    // Verify aggregation - should have 2 records (a and b)
    ASSERT_EQ(2, aggregated_records.size());
    EXPECT_EQ("a", aggregated_records[0].first);
    EXPECT_EQ("b", aggregated_records[1].first);

    // Create a new file since the previous one was probably deleted
    many_moves_file = createTestFile(many_moves_records, "_many_moves_2");
    sorted_file = sorter.sortFiles({many_moves_file});

    // Set max_moves_per_position to 20 (a should be filtered out)
    aggregated_file = sorter.aggregateSortedRecords(sorted_file, 20);

    // Read the aggregated records
    aggregated_records = readRecordsFromFile(aggregated_file);

    // Verify aggregation - should have only record b
    ASSERT_EQ(1, aggregated_records.size());
    EXPECT_EQ("b", aggregated_records[0].first);
}

// Test error handling for invalid inputs
TEST_F(ExternalSorterTest, HandleInvalidInputs) {
    // Create ExternalSorter
    ExternalSorter sorter(temp_dir_.string(), 1000, thread_pool_.get(), logger_.get(), false);

    // Test with non-existent file
    std::string non_existent_file = (temp_dir_ / "does_not_exist.records").string();
    EXPECT_THROW(std::ignore = sorter.sortFiles({non_existent_file}), std::runtime_error);

    // For the corrupt file test, we need to make sure our implementation
    // properly rejects the file, even if it doesn't throw.
    // Let's create a truly corrupt binary file that can't be parsed
    std::string corrupt_file = (temp_dir_ / "corrupt.records").string();
    {
        std::ofstream out(corrupt_file, std::ios::binary);
        EXPECT_TRUE(out.is_open()) << "Failed to create corrupt file: " << corrupt_file;

        // Write incomplete/malformed record header
        uint32_t invalid_size = 1000; // Much larger than the actual file
        out.write(reinterpret_cast<const char*>(&invalid_size), sizeof(invalid_size));
        // Write a few bytes but not the full record (will cause EOF during read)
        out.write("incomplete", 10);
    }

    // The implementation should handle this gracefully,
    // resulting in an empty chunk list (since no records can be read)
    // rather than throwing an exception. After our changes, we should test
    // that the operation completes but returns empty results.
    std::string result = sorter.sortFiles({corrupt_file});

    // The result should be a valid file, but have no records
    auto records = readRecordsFromFile(result);
    EXPECT_TRUE(records.empty()) << "Expected no records in the sorted file";
}

// Test merging pre-sorted chunks
TEST_F(ExternalSorterTest, MergePresortedChunks) {
    // Create three files with unsorted records
    // The ExternalSorter will sort each file into chunks, then merge all chunks
    std::vector<std::pair<std::string, std::string>> chunk1 = {
        {"d", R"({"moves":{"d1":{"1000":{"1500":1}}},"recent_and_fen":[[],"fenD"]})"},
        {"a", R"({"moves":{"a1":{"1000":{"1500":1}}},"recent_and_fen":[[],"fenA"]})"}
    };

    std::vector<std::pair<std::string, std::string>> chunk2 = {
        {"e", R"({"moves":{"e1":{"1000":{"1500":1}}},"recent_and_fen":[[],"fenE"]})"},
        {"b", R"({"moves":{"b1":{"1000":{"1500":1}}},"recent_and_fen":[[],"fenB"]})"}
    };

    std::vector<std::pair<std::string, std::string>> chunk3 = {
        {"f", R"({"moves":{"f1":{"1000":{"1500":1}}},"recent_and_fen":[[],"fenF"]})"},
        {"c", R"({"moves":{"c1":{"1000":{"1500":1}}},"recent_and_fen":[[],"fenC"]})"}
    };

    // Create test files for the chunks
    std::string chunk1_file = createTestFile(chunk1, "_chunk1");
    std::string chunk2_file = createTestFile(chunk2, "_chunk2");
    std::string chunk3_file = createTestFile(chunk3, "_chunk3");

    // Create ExternalSorter
    ExternalSorter sorter(temp_dir_.string(), 1000, thread_pool_.get(), logger_.get(), false);

    // Create a method to access the private mergeChunks method
    // In a real implementation, we would test this through a public interface
    // But for testing purposes, we're assuming access to this method
    // We could also restructure the tests to test the whole pipeline

    // Use sortFiles to merge the chunks
    std::vector chunk_files = {chunk1_file, chunk2_file, chunk3_file};
    std::string merged_file = sorter.sortFiles(chunk_files);

    // Read the merged records
    auto merged_records = readRecordsFromFile(merged_file);

    // Verify merge - should be sorted a-f
    ASSERT_EQ(6, merged_records.size());
    EXPECT_EQ("a", merged_records[0].first);
    EXPECT_EQ("b", merged_records[1].first);
    EXPECT_EQ("c", merged_records[2].first);
    EXPECT_EQ("d", merged_records[3].first);
    EXPECT_EQ("e", merged_records[4].first);
    EXPECT_EQ("f", merged_records[5].first);
}

// Test complete sorting and aggregation pipeline
TEST_F(ExternalSorterTest, CompletePipeline) {
    // Create a test file with unsorted records and duplicates
    std::vector<std::pair<std::string, std::string>> unsorted_records = {
        {"c", R"({"moves":{"c2c4":{"1000":{"1500":5}}},"recent_and_fen":[[],"fenC"]})"},
        {"a", R"({"moves":{"e2e4":{"1000":{"1500":5}}},"recent_and_fen":[[],"fenA"]})"},
        {"b", R"({"moves":{"d2d4":{"1000":{"1500":5}}},"recent_and_fen":[[],"fenB"]})"},
        {"a", R"({"moves":{"d2d4":{"1000":{"1500":3}}},"recent_and_fen":[[],"fenA"]})"},
        {"c", R"({"moves":{"e2e4":{"1000":{"1500":2}}},"recent_and_fen":[[],"fenC"]})"}
    };
    std::string input_file = createTestFile(unsorted_records, "_pipeline");

    // Create ExternalSorter
    ExternalSorter sorter(temp_dir_.string(), 1000, thread_pool_.get(), logger_.get(), false);

    // Process the complete pipeline
    std::string sorted_file = sorter.sortFiles({input_file});
    std::string aggregated_file = sorter.aggregateSortedRecords(sorted_file, 0);

    // Read the aggregated records
    auto aggregated_records = readRecordsFromFile(aggregated_file);

    // Verify pipeline result
    ASSERT_EQ(3, aggregated_records.size());

    // Check 'a' record - should have both moves
    EXPECT_EQ("a", aggregated_records[0].first);
    {
        simdjson::dom::parser parser;
        simdjson::dom::element doc = parser.parse(aggregated_records[0].second);
        EXPECT_EQ(5, static_cast<int64_t>(doc["moves"]["e2e4"]["1000"]["1500"]));
        EXPECT_EQ(3, static_cast<int64_t>(doc["moves"]["d2d4"]["1000"]["1500"]));
    }

    // Check 'b' record
    EXPECT_EQ("b", aggregated_records[1].first);
    {
        simdjson::dom::parser parser;
        simdjson::dom::element doc = parser.parse(aggregated_records[1].second);
        EXPECT_EQ(5, static_cast<int64_t>(doc["moves"]["d2d4"]["1000"]["1500"]));
    }

    // Check 'c' record - should have both moves
    EXPECT_EQ("c", aggregated_records[2].first);
    {
        simdjson::dom::parser parser;
        simdjson::dom::element doc = parser.parse(aggregated_records[2].second);
        EXPECT_EQ(5, static_cast<int64_t>(doc["moves"]["c2c4"]["1000"]["1500"]));
        EXPECT_EQ(2, static_cast<int64_t>(doc["moves"]["e2e4"]["1000"]["1500"]));
    }
}

// Test combined sort and aggregate method
TEST_F(ExternalSorterTest, CombinedSortAndAggregate) {
    // Create multiple test files with unsorted records and duplicates
    std::vector<std::pair<std::string, std::string>> file1_records = {
        {"c", R"({"moves":{"c2c4":{"1000":{"1500":5}}},"recent_and_fen":[[],"fenC"]})"},
        {"a", R"({"moves":{"e2e4":{"1000":{"1500":5}}},"recent_and_fen":[[],"fenA"]})"}
    };
    
    std::vector<std::pair<std::string, std::string>> file2_records = {
        {"b", R"({"moves":{"d2d4":{"1000":{"1500":5}}},"recent_and_fen":[[],"fenB"]})"},
        {"a", R"({"moves":{"d2d4":{"1000":{"1500":3}}},"recent_and_fen":[[],"fenA"]})"}
    };
    
    std::vector<std::pair<std::string, std::string>> file3_records = {
        {"c", R"({"moves":{"e2e4":{"1000":{"1500":2}}},"recent_and_fen":[[],"fenC"]})"},
        {"b", R"({"moves":{"e2e4":{"1000":{"1500":7}}},"recent_and_fen":[[],"fenB"]})"}
    };
    
    std::string file1 = createTestFile(file1_records, "_combined_1");
    std::string file2 = createTestFile(file2_records, "_combined_2");
    std::string file3 = createTestFile(file3_records, "_combined_3");
    
    std::vector input_files = {file1, file2, file3};

    // Create ExternalSorter
    ExternalSorter sorter(temp_dir_.string(), 1000, thread_pool_.get(), logger_.get(), false);

    // Use the combined method
    std::string aggregated_file = sorter.sortAndAggregateRecords(input_files, 0);

    // Verify the output file exists
    EXPECT_TRUE(std::filesystem::exists(aggregated_file)) 
        << "Combined aggregated file not created: " << aggregated_file;

    // Read the aggregated records
    auto aggregated_records = readRecordsFromFile(aggregated_file);

    // Verify pipeline result - should have 3 unique keys
    ASSERT_EQ(3, aggregated_records.size());

    // Check 'a' record - should have both moves
    EXPECT_EQ("a", aggregated_records[0].first);
    {
        simdjson::dom::parser parser;
        simdjson::dom::element doc = parser.parse(aggregated_records[0].second);
        EXPECT_EQ(5, static_cast<int64_t>(doc["moves"]["e2e4"]["1000"]["1500"]));
        EXPECT_EQ(3, static_cast<int64_t>(doc["moves"]["d2d4"]["1000"]["1500"]));
    }

    // Check 'b' record - should have both moves
    EXPECT_EQ("b", aggregated_records[1].first);
    {
        simdjson::dom::parser parser;
        simdjson::dom::element doc = parser.parse(aggregated_records[1].second);
        EXPECT_EQ(5, static_cast<int64_t>(doc["moves"]["d2d4"]["1000"]["1500"]));
        EXPECT_EQ(7, static_cast<int64_t>(doc["moves"]["e2e4"]["1000"]["1500"]));
    }

    // Check 'c' record - should have both moves
    EXPECT_EQ("c", aggregated_records[2].first);
    {
        simdjson::dom::parser parser;
        simdjson::dom::element doc = parser.parse(aggregated_records[2].second);
        EXPECT_EQ(5, static_cast<int64_t>(doc["moves"]["c2c4"]["1000"]["1500"]));
        EXPECT_EQ(2, static_cast<int64_t>(doc["moves"]["e2e4"]["1000"]["1500"]));
    }
    
    // Now test with max_moves filtering
    std::vector<std::pair<std::string, std::string>> many_moves_records = {
        {"a", R"({"moves":{"e2e4":{"1000":{"1500":15}}},"recent_and_fen":[[],"fenA"]})"},
        {"a", R"({"moves":{"d2d4":{"1000":{"1500":10}}},"recent_and_fen":[[],"fenA"]})"},
        {"b", R"({"moves":{"c2c4":{"1000":{"1500":3}}},"recent_and_fen":[[],"fenB"]})"}
    };
    
    std::string many_moves_file = createTestFile(many_moves_records, "_combined_many_moves");
    
    // Set max_moves_per_position to 20 (a should be filtered out)
    aggregated_file = sorter.sortAndAggregateRecords({many_moves_file}, 20);
    
    // Read the aggregated records
    aggregated_records = readRecordsFromFile(aggregated_file);
    
    // Verify filtering - should only have record b
    ASSERT_EQ(1, aggregated_records.size());
    EXPECT_EQ("b", aggregated_records[0].first);
}