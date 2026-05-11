#include "io/lz4_pgn_stream_reader.hpp"
#include "io/lz4_stream_reader.hpp"
#include <fstream>
#include <stdexcept>

namespace chessmimic {

LZ4PgnStreamReader::LZ4PgnStreamReader(size_t input_buffer_size, size_t output_buffer_size)
    : input_buffer_size_(input_buffer_size),
      output_buffer_size_(output_buffer_size) {
}

void LZ4PgnStreamReader::processFile(const std::string& file_path, const GameCallback& callback) {
    if (file_path.ends_with(".lz4")) {
        processLZ4File(file_path, callback);
    } else {
        processPlainTextFile(file_path, callback);
    }
}

void LZ4PgnStreamReader::processLZ4File(const std::string& file_path, const GameCallback& callback) const {
    LZ4StreamReader reader(input_buffer_size_, output_buffer_size_);

    std::string game_buffer;  // Buffer for incomplete games
    std::string line_buffer;  // Buffer for incomplete lines

    reader.decompressFile(file_path, [&](const char* data, size_t size) {
        // Append new data to line buffer
        line_buffer.append(data, size);

        // Process complete lines
        size_t line_start = 0;
        size_t line_end = 0;

        while ((line_end = line_buffer.find('\n', line_start)) != std::string::npos) {
            std::string line = line_buffer.substr(line_start, line_end - line_start);
            line_start = line_end + 1;

            // Check if this is the start of a new game
            if (line.starts_with("[Event ") && !game_buffer.empty()) {
                // Process the previous game
                callback(game_buffer);
                game_buffer.clear();
            }

            game_buffer += line + "\n";
        }

        // Keep remaining incomplete line for next chunk
        if (line_start < line_buffer.size()) {
            line_buffer = line_buffer.substr(line_start);
        } else {
            line_buffer.clear();
        }

        return true; // Continue processing
    });

    // Process final game if any
    if (!game_buffer.empty()) {
        callback(game_buffer);
    }
}

void LZ4PgnStreamReader::processPlainTextFile(const std::string& file_path, GameCallback callback) {
    std::ifstream file(file_path);
    if (!file) {
        throw std::runtime_error("Failed to open PGN file: " + file_path);
    }

    std::string game_buffer;
    std::string line;

    while (std::getline(file, line)) {
        // Check if this is the start of a new game
        if (line.starts_with("[Event ") && !game_buffer.empty()) {
            // Process the previous game
            callback(game_buffer);
            game_buffer.clear();
        }

        game_buffer += line + "\n";
    }

    // Process final game if any
    if (!game_buffer.empty()) {
        callback(game_buffer);
    }
}

} // namespace chessmimic
