#include "gzip_utils.hpp"
#include <stdexcept>
#include <filesystem>
#include <cstring>

namespace chessmimic::clock_converter {
GzipReader::GzipReader(const std::string &file_path) : buffer_{} {
  file_ = gzopen(file_path.c_str(), "rb");
  if (file_ == nullptr) {
    throw std::runtime_error(
      "Failed to open gzip file for reading: " + file_path + "\n"
      "Possible causes:\n"
      "  - File does not exist\n"
      "  - Insufficient permissions to read the file\n"
      "  - File is not a valid gzip file\n"
      "  - Out of memory"
    );
  }
}

GzipReader::~GzipReader() {
  if (file_ != nullptr) {
    gzclose(file_);
  }
}

bool GzipReader::getline(std::string &line) {
  if (file_ == nullptr) {
    return false;
  }

  line.clear();

  while (true) {
    // Read a chunk from the file
    if (char *result = gzgets(file_, buffer_, BUFFER_SIZE); result == nullptr) {
      // Check if it's EOF or error
      int error;
      const char *error_msg = gzerror(file_, &error);
      if (error == Z_OK) {
        // EOF reached
        return !line.empty(); // Return true if we have partial content
      }
      // Error occurred
      throw std::runtime_error(
        "Error reading from gzip file: " + std::string(error_msg)
      );
    }

    // Append to line
    line += buffer_;

    // Check if we have a complete line (ends with newline)
    if (!line.empty() && line.back() == '\n') {
      // Remove the newline character
      line.pop_back();
      return true;
    }

    // If buffer is not full, we've reached EOF
    if (strlen(buffer_) < BUFFER_SIZE - 1) {
      return !line.empty();
    }
  }
}

GzipWriter::GzipWriter(const std::string &file_path, int compression_level) {
  // Create parent directory if needed
  if (std::filesystem::path path(file_path); path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path());
  }

  // Open file for writing with specified compression level
  std::string mode = "wb" + std::to_string(compression_level);
  file_ = gzopen(file_path.c_str(), mode.c_str());

  if (file_ == nullptr) {
    throw std::runtime_error(
      "Failed to open gzip file for writing: " + file_path + "\n"
      "Possible causes:\n"
      "  - Parent directory does not exist and could not be created\n"
      "  - Insufficient permissions to write to this location\n"
      "  - Invalid file path or filename\n"
      "  - Disk is full or quota exceeded\n"
      "  - Out of memory"
    );
  }
}

GzipWriter::~GzipWriter() {
  if (file_ != nullptr) {
    gzclose(file_);
  }
}

bool GzipWriter::writeline(const std::string &line) const {
  if (file_ == nullptr) {
    return false;
  }

  // Write the line with a newline
  std::string line_with_newline = line + "\n";

  if (int bytes_written = gzwrite(file_, line_with_newline.data(), line_with_newline.size());
    bytes_written != static_cast<int>(line_with_newline.size())) {
    int error;
    const char *error_msg = gzerror(file_, &error);
    throw std::runtime_error(
      "Error writing to gzip file: " + std::string(error_msg)
    );
  }

  return true;
}

void GzipWriter::flush() const {
  if (file_ != nullptr) {
    gzflush(file_, Z_SYNC_FLUSH);
  }
}
} // namespace chessmimic::clock_converter
