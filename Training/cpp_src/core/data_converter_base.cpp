#include "core/data_converter_base.hpp"
#include "utils/file_utils.hpp"
#include <filesystem>

namespace chessmimic {

std::unique_ptr<Logger> DataConverterBase::createLogger(int log_level) {
    auto level = Logger::Level::INFO;

    if (log_level == 0) {
        level = Logger::Level::ERROR;
    } else if (log_level == 2) {
        level = Logger::Level::DEBUG;
    }
    // Default is INFO for log_level == 1

    return std::make_unique<Logger>(level);
}

std::unique_ptr<ThreadPool> DataConverterBase::createThreadPool(unsigned int num_threads) {
    return std::make_unique<ThreadPool>(num_threads);
}

double DataConverterBase::autoDetectMemory(double memory_limit_gb, double percentage) {
    if (memory_limit_gb > 0.0) {
        // Memory limit already specified, return as-is
        return memory_limit_gb;
    }

    // Auto-detect system memory
    size_t system_memory = FileUtils::getSystemMemory();

    // Apply percentage and convert to GB
    return system_memory * percentage / (1024.0 * 1024.0 * 1024.0);
}

size_t DataConverterBase::autoDetectMemoryMB(size_t memory_limit_mb, double percentage) {
    if (memory_limit_mb > 0) {
        // Memory limit already specified, return as-is
        return memory_limit_mb;
    }

    // Auto-detect system memory
    size_t system_memory = FileUtils::getSystemMemory();

    // Apply percentage and convert to MB
    return static_cast<size_t>(system_memory * percentage / (1024.0 * 1024.0));
}

void DataConverterBase::ensureTempDirectory(const std::string& temp_dir) {
    std::filesystem::create_directories(temp_dir);
}

void DataConverterBase::cleanupTempFiles(const std::filesystem::path& temp_dir) {
    if (std::filesystem::exists(temp_dir)) {
        std::filesystem::remove_all(temp_dir);
    }
}

} // namespace chessmimic
