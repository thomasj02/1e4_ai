#include "external_sorter.hpp"
#include "../utils/file_utils.hpp"
#include "record_processor.hpp"
#include "../utils/stopwatch.hpp"
#include <fstream>
#include <filesystem>
#include <system_error>
#include <stdexcept>
#include <queue>
#include <future>
#include <simdjson.h>
#include <cstring>
#include "../utils/progress_tracker.hpp"
#include <memory>

namespace chessmimic {
using namespace FileUtils;

ExternalSorter::ExternalSorter(
    std::string temp_dir,
    int chunk_size,
    ThreadPool* thread_pool,
    Logger* logger,
    bool keep_temp_files
)
    : temp_dir_(std::move(temp_dir)),
      chunk_size_(chunk_size),
      thread_pool_(thread_pool),
      logger_(logger),
      keep_temp_files_(keep_temp_files) {
}

std::string ExternalSorter::sortFiles(const std::vector<std::string>& input_files) const {
    // Direct use of provided files
    std::vector<std::string> record_files = input_files;

    if (record_files.empty()) {
        throw std::runtime_error("No record files provided for sorting");
    }

    CM_LOG_INFO("Sorting {} record files using all available threads", record_files.size());

    // Create progress tracker
    ProgressTracker progress(record_files.size(), "File Sorting");

    // Process files in parallel
    std::mutex chunks_mutex;
    std::vector<std::string> all_sorted_chunks;
    std::vector<std::future<std::vector<std::string>>> futures;

    // Process each file in parallel
    for (const auto& file : record_files) {
        futures.push_back(thread_pool_->enqueue([this, file, &chunks_mutex, &all_sorted_chunks, &progress] {
            // Sort file in chunks
            auto chunks = sortFileInChunks(file);

            // Thread-safe update of all chunks
            {
                std::lock_guard lock(chunks_mutex);
                all_sorted_chunks.insert(all_sorted_chunks.end(), chunks.begin(), chunks.end());
            }

            // Remove original file after sorting
            if (!keep_temp_files_) {
                std::filesystem::remove(file);
            }

            // Update progress
            progress.update();

            return chunks;
        }));
    }

    // Wait for all sorting tasks to complete
    for (auto& future : futures) {
        future.wait();
    }

    CM_LOG_INFO("Created {} sorted chunks", all_sorted_chunks.size());

    // Merge sorted chunks (sequential operation)
    std::string merged_file = temp_dir_ + "/all_sorted.records";
    merged_file = mergeChunks(all_sorted_chunks, merged_file);

    return merged_file;
}


std::vector<std::string> ExternalSorter::sortFileInChunks(const std::string& file) const {
    std::vector<std::string> chunks;
    std::mutex chunks_mutex;

    CM_LOG_INFO("Sorting file in chunks: {}", file);

    std::ifstream in(file, std::ios::binary);
    throw_if_failed(in, file);

    // Log the file info 
    CM_LOG_INFO("File open successful: {}", file);

    // First, check the file size to determine if we need to split it into chunks
    std::filesystem::path file_path(file);
    std::uintmax_t file_size = std::filesystem::file_size(file_path);

    // If the file is small enough (< 10 MB), just sort it in memory
    if (constexpr std::uintmax_t max_single_file_size = 10 * 1024 * 1024; file_size < max_single_file_size) {
        CM_LOG_INFO("File {} is small ({}KB), sorting in one chunk",
                     file, file_size / 1024);

        try {
            // Read all records at once
            std::vector<std::pair<std::string, std::string>> records;
            records.reserve(1000); // Initial capacity

            int records_read = 0;
            while (in) {
                std::string key;
                if (std::string record; RecordProcessor::readRecord(in, key, record)) {
                    records_read++;
                    records.emplace_back(key, record);
                }
                else {
                    break;
                }
            }

            CM_LOG_INFO("Total records read: {}", records_read);

            // Sort records by key
            std::ranges::sort(records,
                              [](const auto& a, const auto& b) {
                                  return a.first < b.first;
                              });

            // Write sorted file
            std::string full_chunk_file_path = std::filesystem::path(file).string();
            std::ranges::replace(full_chunk_file_path, '/', '_');
            std::string chunk_file = temp_dir_ + "/" + full_chunk_file_path + "_sorted.chunk";

            std::ofstream out(chunk_file, std::ios::binary);
            if (!out.is_open()) {
                const int err = errno;
                const std::error_code ec(err, std::generic_category());
                throw std::system_error(
                    ec, "Failed to create sorted chunk file \"" + chunk_file + "\": " + std::strerror(err));
            }

            for (const auto& [key, record] : records) {
                // Use RecordProcessor to write the record consistently
                RecordProcessor::writeRawRecord(out, key, record);
            }

            chunks.push_back(chunk_file);
        }
        catch (...) {
            throw;
        }

        return chunks;
    }

    // For larger files, read records in chunks and process them in parallel
    std::mutex in_mutex; // Mutex to protect the input stream
    std::atomic chunk_idx{0};
    std::vector<std::future<void>> futures;
    bool more_records = true;

    // Function to read a chunk of records from the file
    auto read_chunk = [&]() -> std::vector<std::pair<std::string, std::string>> {
        std::vector<std::pair<std::string, std::string>> records;
        records.reserve(chunk_size_);

        std::lock_guard lock(in_mutex);

        int records_read = 0;
        for (int i = 0; i < chunk_size_ && in; ++i) {
            std::string key;
            if (std::string record; RecordProcessor::readRecord(in, key, record)) {
                records_read++;
                records.emplace_back(key, record);
            }
            else {
                break;
            }
        }

        if (records.empty()) {
            more_records = false;
        }

        return records;
    };

    // Continue processing chunks until no more records
    while (more_records) {
        // Read a chunk of records from the input file
        auto records = read_chunk();

        if (records.empty()) {
            break;
        }

        // Process this chunk in a thread pool task
        int current_chunk_idx = chunk_idx++;
        futures.push_back(thread_pool_->enqueue(
            [this, records, current_chunk_idx, file, &chunks_mutex, &chunks]() mutable {
                CM_LOG_INFO("Processing chunk {} with {} records", current_chunk_idx, records.size());

                // Sort records by key
                std::ranges::sort(records,
                                  [](const auto& a, const auto& b) {
                                      return a.first < b.first;
                                  });

                // Write sorted chunk to file
                std::string full_chunk_file_path = std::filesystem::path(file).string();
                std::ranges::replace(full_chunk_file_path, '/', '_');
                std::string chunk_file = temp_dir_ + "/" + full_chunk_file_path + "_chunk_" + std::to_string(current_chunk_idx) + ".sorted";

                std::ofstream out(chunk_file, std::ios::binary);
                if (!out.is_open()) {
                    const int err = errno;
                    const std::error_code ec(err, std::generic_category());
                    throw std::system_error(
                        ec, "Failed to create sorted chunk file \"" + chunk_file + "\": " + std::strerror(err));
                }

                for (const auto& [key, record] : records) {
                    // Use RecordProcessor to write the record consistently
                    RecordProcessor::writeRawRecord(out, key, record);
                }

                // Add this chunk to the results in a thread-safe way
                {
                    std::lock_guard lock(chunks_mutex);
                    chunks.push_back(chunk_file);
                }
            }));
    }

    // Wait for all chunk processing to complete
    for (auto& future : futures) {
        future.wait();
    }

    return chunks;
}

std::string ExternalSorter::mergeChunks(const std::vector<std::string>& chunks,
                                        const std::string& output_file) const {
    if (chunks.empty()) {
        throw std::runtime_error("No chunks to merge");
    }

    if (chunks.size() == 1) {
        // If there's only one chunk, just rename it
        std::filesystem::rename(chunks[0], output_file);
        return output_file;
    }

    CM_LOG_INFO("Merging {} chunks...", chunks.size());

    // For a large number of chunks, use a divide-and-conquer approach
    if (chunks.size() > 16) {
        // Arbitrary threshold, can be adjusted
        CM_LOG_INFO("Using parallel divide-and-conquer merge for {} chunks", chunks.size());

        // Divide chunks into groups and merge them in parallel
        const size_t num_groups = std::min(chunks.size() / 4, thread_pool_->thread_count());
        std::vector<std::vector<std::string>> chunk_groups(num_groups);

        // Distribute chunks evenly across groups
        for (size_t i = 0; i < chunks.size(); ++i) {
            chunk_groups[i % num_groups].push_back(chunks[i]);
        }

        // Merge each group in parallel
        std::vector<std::future<std::string>> futures;
        std::vector<std::string> intermediate_results;

        for (size_t i = 0; i < num_groups; ++i) {
            std::string intermediate_file = temp_dir_ + "/intermediate_merge_" + std::to_string(i) + ".records";
            futures.push_back(thread_pool_->enqueue([this, chunk_group = chunk_groups[i], intermediate_file] {
                return mergeChunks(chunk_group, intermediate_file);
            }));
        }

        // Collect intermediate results
        for (auto& future : futures) {
            intermediate_results.push_back(future.get());
        }

        // Final merge of the intermediate results
        return mergeChunks(intermediate_results, output_file);
    }

    // Standard merge for a smaller number of chunks
    // Open all chunk files
    std::vector<std::ifstream> inputs;

    for (const auto& chunk : chunks) {
        inputs.emplace_back(chunk, std::ios::binary);
        if (!inputs.back().is_open()) {
            const int err = errno;
            const std::error_code ec(err, std::generic_category());
            throw std::system_error(ec, "Failed to open chunk file \"" + chunk + "\": " + std::strerror(err));
        }
    }

    // Create output file
    std::ofstream out(output_file, std::ios::binary);
    if (!out.is_open()) {
        const int err = errno;
        const std::error_code ec(err, std::generic_category());
        throw std::system_error(ec, "Failed to create merged file \"" + output_file + "\": " + std::strerror(err));
    }
    // Priority queue for k-way merge
    using RecordInfo = std::tuple<std::string, std::string, size_t>; // key, record, chunk_index

    // Merge chunks - sequential approach for correctness
    size_t record_count = 0;
    
    // Current record buffer for each chunk
    std::vector<std::optional<RecordInfo>> current_records(inputs.size());
    
    // Read initial record from each chunk
    for (size_t i = 0; i < inputs.size(); ++i) {
        std::string key;
        if (std::string record; RecordProcessor::readRecord(inputs[i], key, record)) {
            current_records[i] = RecordInfo{key, record, i};
        }
    }
    
    // Merge loop
    while (true) {
        // Find the minimum key among current records
        size_t min_idx = inputs.size();
        std::string min_key;
        
        for (size_t i = 0; i < current_records.size(); ++i) {
            if (current_records[i] && (min_idx == inputs.size() || std::get<0>(*current_records[i]) < min_key)) {
                min_idx = i;
                min_key = std::get<0>(*current_records[i]);
            }
        }
        
        // If no valid record found, we're done
        if (min_idx == inputs.size()) {
            break;
        }
        
        // Write the minimum record
        auto [key, record, chunk_idx] = *current_records[min_idx];
        RecordProcessor::writeRawRecord(out, key, record);
        record_count++;
        
        // Read next record from the same chunk
        std::string next_key;
        if (std::string next_record; RecordProcessor::readRecord(inputs[chunk_idx], next_key, next_record)) {
            current_records[chunk_idx] = RecordInfo{next_key, next_record, chunk_idx};
        } else {
            current_records[chunk_idx] = std::nullopt;
        }
    }


    CM_LOG_INFO("Merged {} records", record_count);

    // Remove chunk files after merging
    if (!keep_temp_files_) {
        std::vector<std::future<void>> delete_futures;

        for (const auto& chunk : chunks) {
            delete_futures.push_back(thread_pool_->enqueue([chunk] {
                std::filesystem::remove(chunk);
            }));
        }

        for (auto& future : delete_futures) {
            future.wait();
        }
    }

    return output_file;
}

std::string ExternalSorter::aggregateSortedRecords(const std::string& sorted_file, int max_moves_per_position) const {
    std::string output_file = temp_dir_ + "/aggregated.records";

    // For a large file, we'll use a multi-threaded approach by chunking the file
    // First, let's determine the file size
    std::filesystem::path file_path(sorted_file);

    // If the file is large enough, we'll process it in multiple threads
    if (std::uintmax_t file_size = std::filesystem::file_size(file_path); file_size > 50 * 1024 * 1024) {
        // Threshold of 50MB
        CM_LOG_INFO("Large file detected ({}MB), using parallel aggregation...", file_size / (1024 * 1024));

        // Phase 1: Scan the file to determine key boundaries for parallel processing
        struct KeyPosition {
            std::string key;
            std::streampos position;
        };

        std::vector<KeyPosition> key_positions;
        {
            std::ifstream scanner(sorted_file, std::ios::binary);
            throw_if_failed(scanner, sorted_file);

            // Add the start position
            key_positions.push_back({"", 0});

            std::string prev_key;
            std::streampos position = scanner.tellg();

            while (scanner) {
                std::string key;
                if (std::string record_json_str; !RecordProcessor::readRecord(scanner, key, record_json_str)) {
                    break;
                }

                // If key changes, record the position
                if (!prev_key.empty() && key != prev_key) {
                    key_positions.push_back({key, position});
                }

                // Save for next iteration
                prev_key = key;
                position = scanner.tellg();
            }
        }

        // Phase 2: Process each section in parallel
        struct AggregationResult {
            std::string temp_file;
            size_t record_count;
            size_t unique_keys;
        };

        size_t num_sections = std::min(key_positions.size(), thread_pool_->thread_count() * 2);
        std::vector<std::pair<KeyPosition, KeyPosition>> sections;

        // Create evenly distributed sections based on key positions
        if (num_sections > 1 && key_positions.size() > 1) {
            size_t step = key_positions.size() / num_sections;
            for (size_t i = 0; i < num_sections; ++i) {
                size_t start_idx = i * step;

                // Calculate end index - use the last position for the final section
                size_t end_idx;
                if (i == num_sections - 1) {
                    end_idx = key_positions.size() - 1; // Last section goes to the end
                }
                else {
                    end_idx = (i + 1) * step; // Normal sections use regular step size
                }

                // Only add the section if the end index is valid
                if (end_idx < key_positions.size()) {
                    sections.emplace_back(key_positions[start_idx], key_positions[end_idx]);
                }
            }
        }
        else {
            // Fall back to single section
            if (key_positions.size() >= 2) {
                sections.emplace_back(key_positions.front(), key_positions.back());
            }
        }

        CM_LOG_INFO("Divided file into {} processing sections", sections.size());

        // Process each section in parallel
        std::vector<std::future<AggregationResult>> futures;

        for (size_t i = 0; i < sections.size(); ++i) {
            const auto& [start_pos, end_pos] = sections[i];
            std::string temp_output = temp_dir_ + "/aggregated_part_" + std::to_string(i) + ".records";

            futures.push_back(thread_pool_->enqueue(
                [this, sorted_file, start_pos, end_pos, temp_output, max_moves_per_position]() -> AggregationResult {
                    std::ifstream in(sorted_file, std::ios::binary);
                    throw_if_failed(in, sorted_file);

                    // Seek to starting position
                    in.seekg(start_pos.position);

                    std::ofstream out(temp_output, std::ios::binary);
                    if (!out.is_open()) {
                        const int err = errno;
                        const std::error_code ec(err, std::generic_category());
                        throw std::system_error(
                            ec, "Failed to create partial aggregated file \"" + temp_output + "\": " + std::strerror(
                                err));
                    }

                    std::string current_key;
                    AggregatedRecord aggregated_record;  // Now a struct
                    size_t record_count = 0;
                    size_t unique_keys = 0;

                    // Process until we hit the end position's key or EOF
                    while (in) {
                        std::string key, record_json_str;
                        if (!RecordProcessor::readRecord(in, key, record_json_str)) {
                            break;
                        }

                        // If we've reached the end key and it has changed, we're done with this section
                        if (!end_pos.key.empty() && key == end_pos.key && key != current_key) {
                            // Push the key back (unget not possible with binary streams)
                            // We'll signal this position by rewinding the stream
                            in.seekg(-static_cast<std::streamoff>(key.size() + record_json_str.size() + 8),
                                     std::ios::cur);
                            break;
                        }

                        record_count++;

                        // Validate JSON with simdjson
                        try {
                            thread_local simdjson::dom::parser parser;
                            parser.parse(record_json_str);
                        }
                        catch (const simdjson::simdjson_error& e) {
                            CM_LOG_INFO("Error parsing JSON record: {} File: {} JSON: {}",
                                         simdjson::error_message(e.error()), sorted_file, record_json_str);
                            throw;
                        }

                        // If key changes, write the previous aggregated record
                        if (key != current_key && !current_key.empty()) {
                            RecordProcessor::writeRecord(out, current_key, aggregated_record, max_moves_per_position,
                                                         true);
                            aggregated_record.clear();
                            unique_keys++;
                        }

                        // Update current key
                        current_key = key;

                        // Aggregate this record with existing data
                        RecordProcessor::mergeRecord(aggregated_record, record_json_str);
                    }

                    // Write the last aggregated record
                    if (!current_key.empty()) {
                        RecordProcessor::writeRecord(out, current_key, aggregated_record, max_moves_per_position, true);
                        unique_keys++;
                    }

                    return {temp_output, record_count, unique_keys};
                }));
        }

        // Collect results
        std::vector<std::string> temp_files;
        size_t total_records = 0;
        size_t total_unique_keys = 0;

        for (auto& future : futures) {
            auto [temp_file, record_count, unique_keys] = future.get();
            temp_files.push_back(temp_file);
            total_records += record_count;
            total_unique_keys += unique_keys;
        }

        // Phase 3: Merge the temporary files
        std::ofstream out(output_file, std::ios::binary);
        if (!out.is_open()) {
            const int err = errno;
            const std::error_code ec(err, std::generic_category());
            throw std::system_error(
                ec, "Failed to create final aggregated file \"" + output_file + "\": " + std::strerror(err));
        }

        for (const auto& temp_file : temp_files) {
            std::ifstream in(temp_file, std::ios::binary);
            throw_if_failed(in, temp_file);

            // Copy the entire content
            out << in.rdbuf();

            // Remove temporary file
            if (!keep_temp_files_) {
                std::filesystem::remove(temp_file);
            }
        }

        CM_LOG_INFO("Aggregated {} records into {} unique positions (parallel processing, max_moves filter: {})",
                     total_records, total_unique_keys, max_moves_per_position);
    }
    else {
        // For smaller files, use the original single-threaded approach
        std::ifstream in(sorted_file, std::ios::binary);
        throw_if_failed(in, sorted_file);

        std::ofstream out(output_file, std::ios::binary);
        if (!out.is_open()) {
            const int err = errno;
            const std::error_code ec(err, std::generic_category());
            throw std::system_error(
                ec, "Failed to create aggregated file \"" + output_file + "\": " + std::strerror(err));
        }

        std::string current_key;
        AggregatedRecord aggregated_record;  // Now a struct
        size_t record_count = 0;
        size_t unique_keys = 0;

        CM_LOG_INFO("Aggregating records...");

        // Read records one by one
        while (in) {
            std::string key, record_json_str;
            if (!RecordProcessor::readRecord(in, key, record_json_str)) {
                break;
            }

            record_count++;

            // Validate JSON with simdjson
            try {
                thread_local simdjson::dom::parser parser;
                parser.parse(record_json_str);
            }
            catch (const simdjson::simdjson_error& e) {
                CM_LOG_INFO("Error parsing JSON record: {} File: {} JSON: {}",
                             simdjson::error_message(e.error()), sorted_file, record_json_str);
                throw;
            }

            // If key changes, write the previous aggregated record
            if (key != current_key && !current_key.empty()) {
                RecordProcessor::writeRecord(out, current_key, aggregated_record, max_moves_per_position, true);
                aggregated_record.clear();
                unique_keys++;
            }

            // Update current key
            current_key = key;

            // Aggregate this record with existing data
            RecordProcessor::mergeRecord(aggregated_record, record_json_str);
        }

        // Write the last aggregated record
        if (!current_key.empty()) {
            RecordProcessor::writeRecord(out, current_key, aggregated_record, max_moves_per_position, true);
            unique_keys++;
        }

        CM_LOG_INFO("Aggregated {} records into {} unique positions (max_moves filter: {})",
                     record_count, unique_keys, max_moves_per_position);
    }

    // Remove sorted file after aggregation
    if (!keep_temp_files_) {
        std::filesystem::remove(sorted_file);
    }

    return output_file;
}

std::string ExternalSorter::sortAndAggregateRecords(const std::vector<std::string>& input_files,
                                                    int max_moves_per_position) const {
    SCOPED_TIMER("ExternalSorter::sortAndAggregateRecords");

    // Create output file for the aggregated records
    std::string output_file = temp_dir_ + "/sorted_aggregated.records";

    if (input_files.empty()) {
        throw std::runtime_error("No record files provided for sorting and aggregation");
    }

    CM_LOG_INFO("Sorting and aggregating {} record files using all available threads", input_files.size());

    // Create progress tracker
    ProgressTracker progress(input_files.size(), "File Sorting");

    // Process files in parallel
    std::vector<std::string> all_sorted_chunks;

    // Process each file in parallel
    {
        std::vector<std::future<std::vector<std::string>>> futures;
        std::mutex chunks_mutex;
        SCOPED_TIMER("ExternalSorter::FileParallelSortPhase");
        for (const auto& file : input_files) {
            futures.push_back(thread_pool_->enqueue([this, file, &chunks_mutex, &all_sorted_chunks, &progress] {
                // Sort file in chunks
                SCOPED_TIMER("ExternalSorter::sortFileInChunks");
                auto chunks = sortFileInChunks(file);

                // Thread-safe update of all chunks
                {
                    std::lock_guard lock(chunks_mutex);
                    all_sorted_chunks.insert(all_sorted_chunks.end(), chunks.begin(), chunks.end());
                }

                // Remove original file after sorting
                if (!keep_temp_files_) {
                    std::filesystem::remove(file);
                }

                // Update progress
                progress.update();

                return chunks;
            }));
        }

        // Wait for all sorting tasks to complete
        for (auto& future : futures) {
            future.wait();
        }
    }

    CM_LOG_INFO("Created {} sorted chunks, now merging and aggregating", all_sorted_chunks.size());

    // Merge chunks and aggregate in a single pass
    if (all_sorted_chunks.empty()) {
        throw std::runtime_error("No chunks to merge");
    }

    // If there's only one chunk, just read and aggregate it
    if (all_sorted_chunks.size() == 1) {
        SCOPED_TIMER("ExternalSorter::SingleChunkAggregate");
        return aggregateSortedRecords(all_sorted_chunks[0], max_moves_per_position);
    }

    // Perform multi-chunk merge and aggregate
    SCOPED_TIMER("ExternalSorter::MultiChunkMergeAggregate");

    // Create output file
    std::ofstream out(output_file, std::ios::binary);
    if (!out.is_open()) {
        const int err = errno;
        const std::error_code ec(err, std::generic_category());
        throw std::system_error(ec, "Failed to create aggregated file \"" + output_file + "\": " + std::strerror(err));
    }

    // Open all chunk files
    std::vector<std::ifstream> inputs;
    {
        SCOPED_TIMER("ExternalSorter::OpenChunkFiles");
        for (const auto& chunk : all_sorted_chunks) {
            inputs.emplace_back(chunk, std::ios::binary);
            if (!inputs.back().is_open()) {
                const int err = errno;
                const std::error_code ec(err, std::generic_category());
                throw std::system_error(ec, "Failed to open chunk file \"" + chunk + "\": " + std::strerror(err));
            }
        }
    }

    // Priority queue for k-way merge
    using RecordInfo = std::tuple<std::string, std::string, size_t>; // key, record, chunk_index
    auto compare = [](const RecordInfo& a, const RecordInfo& b) {
        return std::get<0>(a) > std::get<0>(b); // Min-heap by key
    };
    std::priority_queue<RecordInfo, std::vector<RecordInfo>, decltype(compare)> pq(compare);

    // Initialize with first record from each chunk
    {
        SCOPED_TIMER("ExternalSorter::InitMergeQueue");
        for (size_t i = 0; i < inputs.size(); ++i) {
            if (auto& in = inputs[i]) {
                std::string key;
                if (std::string record; RecordProcessor::readRecord(in, key, record)) {
                    pq.emplace(key, record, i);
                }
            }
        }
    }

    // Merge chunks with on-the-fly aggregation
    std::string current_key;
    AggregatedRecord aggregated_record;
    size_t record_count = 0;
    size_t unique_keys = 0;

    {
        SCOPED_TIMER("ExternalSorter::MergeAggregateProcess");
        while (!pq.empty()) {
            auto [key, record_json_str, chunk_idx] = pq.top();
            pq.pop();

            record_count++;

            // Validate JSON with simdjson
            try {
                thread_local simdjson::dom::parser parser;
                parser.parse(record_json_str);
            }
            catch (const simdjson::simdjson_error& e) {
                CM_LOG_INFO("Error parsing JSON record: {} JSON: {}", simdjson::error_message(e.error()), record_json_str);
                throw;
            }

            // If key changes, write the previous aggregated record
            if (key != current_key && !current_key.empty()) {
                RecordProcessor::writeRecord(out, current_key, aggregated_record, max_moves_per_position, true);
                aggregated_record.clear();
                unique_keys++;
            }

            // Update current key
            current_key = key;

            // Aggregate this record with existing data
            RecordProcessor::mergeRecord(aggregated_record, record_json_str);

            // Read next record from this chunk
            if (auto& in = inputs[chunk_idx]) {
                std::string next_key;
                if (std::string next_record; RecordProcessor::readRecord(in, next_key, next_record)) {
                    pq.emplace(next_key, next_record, chunk_idx);
                }
            }
        }
    }

    // Write the last aggregated record
    {
        SCOPED_TIMER("ExternalSorter::WriteFinalRecord");
        if (!current_key.empty()) {
            RecordProcessor::writeRecord(out, current_key, aggregated_record, max_moves_per_position, true);
            unique_keys++;
        }
    }

    CM_LOG_INFO("Aggregated {} records into {} unique positions (max_moves filter: {})",
                 record_count, unique_keys, max_moves_per_position);

    // Remove chunk files after merging
    if (!keep_temp_files_) {
        SCOPED_TIMER("ExternalSorter::CleanupChunkFiles");
        for (const auto& chunk : all_sorted_chunks) {
            std::filesystem::remove(chunk);
        }
    }

    return output_file;
}
} // namespace chessmimic
