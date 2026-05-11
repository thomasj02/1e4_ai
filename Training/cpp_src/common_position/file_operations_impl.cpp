#include "file_operations_impl.hpp"
#include <fstream>
#include <filesystem>
#include <stdexcept>

namespace chessmimic {

void FileOperationsImpl::writeJsonLine(const std::string& path, const std::string& json_line) {
    std::ofstream out(path, std::ios::app);
    if (!out) {
        throw std::runtime_error("Failed to open file for writing: " + path);
    }
    out << json_line << "\n";
}

std::vector<std::string> FileOperationsImpl::readJsonLines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Failed to open file for reading: " + path);
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

void FileOperationsImpl::removeFile(const std::string& path) {
    std::filesystem::remove(path);
}

bool FileOperationsImpl::exists(const std::string& path) {
    return std::filesystem::exists(path);
}

} // namespace chessmimic