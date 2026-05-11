#pragma once

#include <string>
#include <vector>
#include "../utils/thread_pool.hpp"
#include "../utils/logger.hpp"

namespace chessmimic {

// ExternalSorter class for sorting and aggregating records
class ExternalSorter {
public:
    // Constructor
    ExternalSorter(
        std::string temp_dir,
        int chunk_size,
        ThreadPool* thread_pool,
        Logger* logger,
        bool keep_temp_files
    );

    // Sort multiple files containing records
    [[nodiscard]] std::string sortFiles(const std::vector<std::string>& input_files) const;

    // Aggregate sorted records
    [[nodiscard]] std::string aggregateSortedRecords(
        const std::string& sorted_file,
        int max_moves_per_position
    ) const;
    
    // Combined sort and aggregate records in a single pipeline
    [[nodiscard]] std::string sortAndAggregateRecords(
        const std::vector<std::string>& input_files,
        int max_moves_per_position
    ) const;

private:
    std::string temp_dir_;
    int chunk_size_;
    ThreadPool* thread_pool_;
    Logger* logger_;
    bool keep_temp_files_{false};

    // Sort the file in chunks
    [[nodiscard]] std::vector<std::string> sortFileInChunks(const std::string& file) const;

    // Merge chunks into a single file
    [[nodiscard]] std::string mergeChunks(
        const std::vector<std::string>& chunks,
        const std::string& output_file
    ) const;
};

} // namespace chessmimic