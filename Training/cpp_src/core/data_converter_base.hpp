#pragma once

#include <string>
#include <memory>
#include <filesystem>
#include "utils/thread_pool.hpp"
#include "utils/logger.hpp"

namespace chessmimic {

/**
 * Abstract base class for data converters
 * Provides common infrastructure for PGN to BAGZ conversion tools
 */
class DataConverterBase {
public:
    /**
     * Virtual destructor for proper cleanup
     */
    virtual ~DataConverterBase() = default;

protected:
    // Shared resources
    std::unique_ptr<ThreadPool> thread_pool_;
    std::unique_ptr<Logger> logger_;

    /**
     * Create logger with standardized log level mapping
     * @param log_level 0=ERROR, 1=INFO, 2=DEBUG
     * @return Unique pointer to configured Logger
     */
    static std::unique_ptr<Logger> createLogger(int log_level);

    /**
     * Create thread pool with specified number of threads
     * @param num_threads Number of worker threads
     * @return Unique pointer to ThreadPool
     */
    static std::unique_ptr<ThreadPool> createThreadPool(unsigned int num_threads);

    /**
     * Auto-detect system memory and calculate limit
     * @param memory_limit_gb Current memory limit (0 = auto-detect)
     * @param percentage Percentage of system RAM to use (e.g., 0.5 for 50%)
     * @return Memory limit in GB
     */
    static double autoDetectMemory(double memory_limit_gb, double percentage);

    /**
     * Auto-detect system memory and calculate limit in MB
     * @param memory_limit_mb Current memory limit (0 = auto-detect)
     * @param percentage Percentage of system RAM to use (e.g., 0.5 for 50%)
     * @return Memory limit in MB
     */
    static size_t autoDetectMemoryMB(size_t memory_limit_mb, double percentage);

    /**
     * Ensure temp directory exists, creating it if necessary
     * @param temp_dir Path to temporary directory
     */
    static void ensureTempDirectory(const std::string& temp_dir);

    /**
     * Clean up temporary files by removing directory recursively
     * @param temp_dir Path to temporary directory to remove
     */
    static void cleanupTempFiles(const std::filesystem::path& temp_dir);
};

} // namespace chessmimic
