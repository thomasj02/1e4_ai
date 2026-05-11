#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "../common_position/common_position_extractor.hpp"
#include "../common_position/interfaces.hpp"
#include "../common_position/position_data.hpp"
#include <simdjson.h>
#include <memory>
#include <sstream>

using namespace chessmimic;
using testing::Return;
using testing::_;
using testing::Invoke;

// Mock implementations for testing
class MockBagzReader : public IBagzReader {
public:
    MOCK_METHOD(size_t, size, (), (const, override));
    MOCK_METHOD(std::vector<uint8_t>, get_record, (size_t), (const, override));
};

class MockBagzReaderFactory : public IBagzReaderFactory {
public:
    std::shared_ptr<MockBagzReader> mock_reader;
    
    explicit MockBagzReaderFactory(const std::shared_ptr<MockBagzReader>& reader)
        : mock_reader(reader) {}
    
    [[nodiscard]] std::unique_ptr<IBagzReader> createReader() const override {
        // Create a proxy reader that delegates to the shared mock
        class ProxyReader : public IBagzReader {
        public:
            explicit ProxyReader(const std::shared_ptr<MockBagzReader>& reader)
                : mock_reader_(reader) {}
            
            [[nodiscard]] size_t size() const override {
                return mock_reader_->size();
            }
            
            [[nodiscard]] std::vector<uint8_t> get_record(size_t index) const override {
                return mock_reader_->get_record(index);
            }
            
        private:
            std::shared_ptr<MockBagzReader> mock_reader_;
        };
        
        return std::make_unique<ProxyReader>(mock_reader);
    }
    
    [[nodiscard]] size_t size() const override {
        return mock_reader->size();
    }
};

class MockFileOperations : public IFileOperations {
public:
    // Store written data for verification as JSON strings
    std::unordered_map<std::string, std::vector<std::string>> written_data;
    std::unordered_map<std::string, std::vector<std::string>> file_contents;
    
    void writeJsonLine(const std::string& path, const std::string& json_line) override {
        written_data[path].push_back(json_line);
    }
    
    std::vector<std::string> readJsonLines(const std::string& path) override {
        std::vector<std::string> lines;
        if (file_contents.contains(path)) {
            return file_contents[path];
        }
        // Return data that was written
        if (written_data.contains(path)) {
            return written_data[path];
        }
        return lines;
    }
    
    void removeFile(const std::string& path) override {
        written_data.erase(path);
        file_contents.erase(path);
    }
    
    bool exists(const std::string& path) override {
        return written_data.contains(path) || file_contents.contains(path);
    }
};

// Test fixture
class CommonPositionExtractorTest : public testing::Test {
protected:
    void SetUp() override {
        thread_pool = std::make_shared<ThreadPool>(2);
        logger = std::make_shared<Logger>();
        logger->setLevel(Logger::Level::ERROR); // Only show errors during tests
        
        // Default config
        config.bagz_path = "test.bagz";
        config.output_path = "output.jsonl";
        config.temp_dir = "/tmp/test_extract";
        config.threshold = 5;
        config.chunk_size = 10;
        config.num_threads = 2;
    }
    
    // Helper to create a test record as JSON string
    static std::string createTestRecord(const std::string& fen,
                                        const std::vector<std::string>& recent_moves,
                                        const std::unordered_map<std::string, int>& moves) {
        std::ostringstream json;
        json << "{\"recent_and_fen\":[";
        
        // Add recent moves array
        json << "[";
        for (size_t i = 0; i < recent_moves.size(); ++i) {
            if (i > 0) json << ",";
            json << "\"" << recent_moves[i] << "\"";
        }
        json << "],\"" << fen << "\"],\"moves\":{";
        
        // Add moves
        bool first = true;
        for (const auto& [move, count] : moves) {
            if (!first) json << ",";
            json << "\"" << move << "\":{\"60.0\":{\"1500\":" << count << "}}";
            first = false;
        }
        json << "}}";
        
        return json.str();
    }

    static std::vector<uint8_t> jsonToBytes(const std::string& json_str) {
        return std::vector<uint8_t>(json_str.begin(), json_str.end());
    }

    std::shared_ptr<ThreadPool> thread_pool;
    std::shared_ptr<Logger> logger;
    CommonPositionExtractor::Config config;
};

// Test PositionData serialization
TEST_F(CommonPositionExtractorTest, PositionDataSerialization) {
    PositionData pos;
    pos.fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    pos.moves["e2e4"] = 10;
    pos.moves["d2d4"] = 8;
    pos.total = 18;
    
    auto json_str = pos.toJson();
    auto [fen, moves, total] = PositionData::fromJson(json_str);
    
    EXPECT_EQ(fen, pos.fen);
    EXPECT_EQ(moves.size(), pos.moves.size());
    EXPECT_EQ(moves["e2e4"], 10);
    EXPECT_EQ(moves["d2d4"], 8);
    EXPECT_EQ(total, pos.total);
}

// Test extracting FEN from record
TEST_F(CommonPositionExtractorTest, ExtractFenFromRecord) {
    auto record_str = createTestRecord(
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        {"e2e4", "e7e5"},
        {{"Nf3", 5}}
    );
    
    std::string fen = CommonPositionExtractor::extractFenFromRecord(record_str);
    // Expect the FEN with move clocks stripped
    EXPECT_EQ(fen, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -");
}

// Test extracting position data from record
TEST_F(CommonPositionExtractorTest, ExtractPositionFromRecord) {
    auto record_str = createTestRecord(
        "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
        {"e2e4"},
        {{"Nf3", 5}, {"Nc3", 3}}
    );
    
    auto [fen, moves, total] = CommonPositionExtractor::extractPositionFromRecord(record_str);
    
    // Expect stripped FEN
    EXPECT_EQ(fen, "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3");
    EXPECT_EQ(moves.size(), 2);
    EXPECT_EQ(moves["Nf3"], 5);
    EXPECT_EQ(moves["Nc3"], 3);
    EXPECT_EQ(total, 8);
}

// Test aggregating positions
TEST_F(CommonPositionExtractorTest, AggregatePosition) {
    PositionData target;
    target.fen = "test_fen";
    target.moves["e4"] = 10;
    target.total = 10;
    
    PositionData source;
    source.fen = "test_fen";
    source.moves["e4"] = 5;
    source.moves["d4"] = 3;
    source.total = 8;
    
    CommonPositionExtractor::aggregatePosition(target, source);
    
    EXPECT_EQ(target.moves["e4"], 15);
    EXPECT_EQ(target.moves["d4"], 3);
    EXPECT_EQ(target.total, 18);
}

// Test processing a single chunk
TEST_F(CommonPositionExtractorTest, ProcessSingleChunk) {
    auto mock_reader = std::make_shared<MockBagzReader>();
    auto mock_file_ops = new MockFileOperations();
    
    EXPECT_CALL(*mock_reader, size()).WillRepeatedly(Return(3));
    
    // Use valid FEN strings
    const std::string fen1 = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    const std::string fen2 = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";
    
    auto record1 = createTestRecord(fen1, {"e2e4"}, {{"Nf3", 5}});
    auto record2 = createTestRecord(fen1, {"d2d4"}, {{"Nf3", 3}, {"Nc3", 2}});
    auto record3 = createTestRecord(fen2, {"e2e4"}, {{"d5", 4}});
    
    EXPECT_CALL(*mock_reader, get_record(0)).WillRepeatedly(Return(jsonToBytes(record1)));
    EXPECT_CALL(*mock_reader, get_record(1)).WillRepeatedly(Return(jsonToBytes(record2)));
    EXPECT_CALL(*mock_reader, get_record(2)).WillRepeatedly(Return(jsonToBytes(record3)));
    
    auto factory = std::make_unique<MockBagzReaderFactory>(mock_reader);
    
    CommonPositionExtractor extractor(
        config,
        std::move(factory),
        std::unique_ptr<IFileOperations>(mock_file_ops),
        thread_pool,
        logger
    );
    
    auto chunk_files = extractor.processChunks();
    
    ASSERT_EQ(chunk_files.size(), 1);
    
    auto& written = mock_file_ops->written_data[chunk_files[0]];
    ASSERT_EQ(written.size(), 2);
    
    bool found_fen1 = false;
    bool found_fen2 = false;
    
    // Expected stripped FENs
    const std::string stripped_fen1 = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
    const std::string stripped_fen2 = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3";
    
    simdjson::dom::parser parser;
    for (const auto& json_str : written) {
        simdjson::dom::element json = parser.parse(json_str);

        if (std::string_view fen = json["fen"]; fen == stripped_fen1) {
            found_fen1 = true;
            EXPECT_EQ(static_cast<int64_t>(json["total"]), 10);
            EXPECT_EQ(static_cast<int64_t>(json["moves"]["Nf3"]), 8);
            EXPECT_EQ(static_cast<int64_t>(json["moves"]["Nc3"]), 2);
        } else if (fen == stripped_fen2) {
            found_fen2 = true;
            EXPECT_EQ(static_cast<int64_t>(json["total"]), 4);
            EXPECT_EQ(static_cast<int64_t>(json["moves"]["d5"]), 4);
        }
    }
    
    EXPECT_TRUE(found_fen1);
    EXPECT_TRUE(found_fen2);
}

// Test sorting functionality
TEST_F(CommonPositionExtractorTest, SortChunks) {
    auto mock_file_ops = new MockFileOperations();
    
    // Use valid FEN strings
    const std::string fen1 = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
    const std::string fen2 = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3";
    const std::string fen3 = "rnbqkb1r/pppppppp/5n2/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
    
    std::vector<std::string> unsorted_data = {
        PositionData{fen3, {{"e4", 1}}, 1}.toJson(),
        PositionData{fen1, {{"d4", 2}}, 2}.toJson(),
        PositionData{fen2, {{"Nf3", 3}}, 3}.toJson()
    };
    
    mock_file_ops->file_contents["chunk_0.jsonl"] = unsorted_data;
    
    auto mock_reader = std::make_shared<MockBagzReader>();
    auto factory = std::make_unique<MockBagzReaderFactory>(mock_reader);
    
    CommonPositionExtractor extractor(
        config,
        std::move(factory),
        std::unique_ptr<IFileOperations>(mock_file_ops),
        thread_pool,
        logger
    );
    
    auto sorted_files = extractor.sortChunks({"chunk_0.jsonl"});
    
    ASSERT_EQ(sorted_files.size(), 1);
    
    auto sorted_lines = mock_file_ops->readJsonLines(sorted_files[0]);
    ASSERT_EQ(sorted_lines.size(), 3);
    
    auto pos1 = PositionData::fromJson(sorted_lines[0]);
    auto pos2 = PositionData::fromJson(sorted_lines[1]);
    auto pos3 = PositionData::fromJson(sorted_lines[2]);
    
    // Verify they are sorted lexicographically
    // fen3 (rnbqkb1r...) comes before fen1 (rnbqkbnr...)
    EXPECT_EQ(pos1.fen, fen3);
    EXPECT_EQ(pos2.fen, fen2);
    EXPECT_EQ(pos3.fen, fen1);
}

// Test merge and filter with threshold
TEST_F(CommonPositionExtractorTest, MergeAndFilter) {
    auto mock_file_ops = new MockFileOperations();
    
    // Use valid FEN strings
    const std::string fen1 = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
    const std::string fen2 = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3";
    const std::string fen3 = "rnbqkb1r/pppppppp/5n2/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
    
    std::vector<std::string> file1_data = {
        PositionData{fen1, {{"e4", 10}}, 10}.toJson(),
        PositionData{fen3, {{"d4", 3}}, 3}.toJson()
    };
    
    std::vector<std::string> file2_data = {
        PositionData{fen1, {{"d4", 5}}, 5}.toJson(),
        PositionData{fen2, {{"Nf3", 8}}, 8}.toJson()
    };
    
    mock_file_ops->file_contents["sorted_0.jsonl"] = file1_data;
    mock_file_ops->file_contents["sorted_1.jsonl"] = file2_data;
    
    config.threshold = 5;
    auto mock_reader = std::make_shared<MockBagzReader>();
    auto factory = std::make_unique<MockBagzReaderFactory>(mock_reader);
    
    CommonPositionExtractor extractor(
        config,
        std::move(factory),
        std::unique_ptr<IFileOperations>(mock_file_ops),
        thread_pool,
        logger
    );
    
    extractor.mergeAndFilter({"sorted_0.jsonl", "sorted_1.jsonl"});
    
    auto& output = mock_file_ops->written_data[config.output_path];
    ASSERT_EQ(output.size(), 2);
    
    bool found_fen1 = false;
    bool found_fen2 = false;
    
    simdjson::dom::parser parser;
    for (const auto& json_str : output) {
        simdjson::dom::element json = parser.parse(json_str);
        std::string_view fen = json["fen"];
        
        if (fen == fen1) {
            found_fen1 = true;
            EXPECT_EQ(static_cast<int64_t>(json["total"]), 15);
            EXPECT_EQ(static_cast<int64_t>(json["moves"]["e4"]), 10);
            EXPECT_EQ(static_cast<int64_t>(json["moves"]["d4"]), 5);
        } else if (fen == fen2) {
            found_fen2 = true;
            EXPECT_EQ(static_cast<int64_t>(json["total"]), 8);
            EXPECT_EQ(static_cast<int64_t>(json["moves"]["Nf3"]), 8);
        }
    }
    
    EXPECT_TRUE(found_fen1);
    EXPECT_TRUE(found_fen2);
}

// Test end-to-end extraction with small dataset
TEST_F(CommonPositionExtractorTest, EndToEndExtraction) {
    auto mock_reader = std::make_shared<MockBagzReader>();
    auto mock_file_ops = new MockFileOperations();
    
    EXPECT_CALL(*mock_reader, size()).WillRepeatedly(Return(5));
    
    // Use valid FEN strings
    const std::string fen1 = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    const std::string fen2 = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";
    const std::string fen3 = "rnbqkb1r/pppppppp/5n2/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 1 2";
    
    auto records = std::vector<std::string>{
        createTestRecord(fen1, {"e2e4"}, {{"Nf3", 3}}),
        createTestRecord(fen2, {"d2d4"}, {{"e5", 2}}),
        createTestRecord(fen1, {"e2e4", "e7e5"}, {{"Nf3", 2}, {"Bc4", 1}}),
        createTestRecord(fen3, {"Nf3"}, {{"d5", 7}}),
        createTestRecord(fen1, {"d2d4"}, {{"Bc4", 2}})
    };
    
    for (size_t i = 0; i < records.size(); ++i) {
        EXPECT_CALL(*mock_reader, get_record(i)).WillRepeatedly(Return(jsonToBytes(records[i])));
    }
    
    config.threshold = 5;
    config.chunk_size = 3;
    
    auto factory = std::make_unique<MockBagzReaderFactory>(mock_reader);
    
    CommonPositionExtractor extractor(
        config,
        std::move(factory),
        std::unique_ptr<IFileOperations>(mock_file_ops),
        thread_pool,
        logger
    );
    
    extractor.extract();
    
    auto& output = mock_file_ops->written_data[config.output_path];
    ASSERT_EQ(output.size(), 2);
    
    // Expected stripped FENs
    const std::string stripped_fen1 = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
    const std::string stripped_fen3 = "rnbqkb1r/pppppppp/5n2/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
    
    bool found_fen1 = false;
    bool found_fen3 = false;
    
    simdjson::dom::parser parser;
    for (const auto& json_str : output) {
        simdjson::dom::element json = parser.parse(json_str);
        std::string_view fen = json["fen"];
        
        if (fen == stripped_fen1) {
            found_fen1 = true;
            EXPECT_EQ(static_cast<int64_t>(json["total"]), 8);
            EXPECT_EQ(static_cast<int64_t>(json["moves"]["Nf3"]), 5);
            EXPECT_EQ(static_cast<int64_t>(json["moves"]["Bc4"]), 3);
        } else if (fen == stripped_fen3) {
            found_fen3 = true;
            EXPECT_EQ(static_cast<int64_t>(json["total"]), 7);
            EXPECT_EQ(static_cast<int64_t>(json["moves"]["d5"]), 7);
        }
    }
    
    EXPECT_TRUE(found_fen1);
    EXPECT_TRUE(found_fen3);
}

// Test with max_records limit
TEST_F(CommonPositionExtractorTest, MaxRecordsLimit) {
    auto mock_reader = std::make_shared<MockBagzReader>();
    auto mock_file_ops = new MockFileOperations();
    
    EXPECT_CALL(*mock_reader, size()).WillRepeatedly(Return(10));
    
    config.max_records = 3;
    
    // Use valid FEN strings with different move numbers
    for (int i = 0; i < 3; ++i) {
        std::string fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 " + std::to_string(i + 1);
        auto record = createTestRecord(fen, {}, {{"e4", 1}});
        EXPECT_CALL(*mock_reader, get_record(i)).WillRepeatedly(Return(jsonToBytes(record)));
    }
    
    auto factory = std::make_unique<MockBagzReaderFactory>(mock_reader);
    
    CommonPositionExtractor extractor(
        config,
        std::move(factory),
        std::unique_ptr<IFileOperations>(mock_file_ops),
        thread_pool,
        logger
    );
    
    auto chunk_files = extractor.processChunks();
    
    // All 3 records will be aggregated into 1 position since they have the same stripped FEN
    size_t total_records = 0;
    for (const auto& file : chunk_files) {
        total_records += mock_file_ops->written_data[file].size();
    }
    EXPECT_EQ(total_records, 1);  // All 3 records aggregate to same position
    
    // Verify the aggregated position
    ASSERT_EQ(chunk_files.size(), 1);
    auto& written = mock_file_ops->written_data[chunk_files[0]];
    ASSERT_EQ(written.size(), 1);
    
    simdjson::dom::parser parser;
    simdjson::dom::element json = parser.parse(written[0]);
    EXPECT_EQ(std::string(json["fen"].get_string().value()), "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -");
    EXPECT_EQ(static_cast<int64_t>(json["total"]), 3);  // 3 records with count 1 each
    EXPECT_EQ(static_cast<int64_t>(json["moves"]["e4"]), 3);
}