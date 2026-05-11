#include "core/bagz_record_writer.hpp"
#include <stdexcept>

namespace chessmimic {

BagzRecordWriter::BagzRecordWriter(const std::string& output_path) {
    try {
        writer_ = std::make_unique<BagWriter>(output_path);
    } catch (const std::exception& e) {
        throw std::runtime_error(
            "Failed to create BAGZ writer for file: " + output_path + "\n"
            "Error: " + std::string(e.what()) + "\n"
            "Possible causes:\n"
            "  - Directory does not exist (create parent directories first)\n"
            "  - Insufficient permissions to write to this location\n"
            "  - Disk is full or quota exceeded"
        );
    }
}

BagzRecordWriter::~BagzRecordWriter() {
    if (!closed_) {
        close();
    }
}

void BagzRecordWriter::close() {
    if (!closed_) {
        writer_->close();
        closed_ = true;
    }
}

} // namespace chessmimic