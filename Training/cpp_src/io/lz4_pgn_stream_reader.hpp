#pragma once

#include <string>
#include <functional>
#include "io/lz4_constants.hpp"

namespace chessmimic {

/**
 * Utility class for reading PGN games from LZ4-compressed or plain text files.
 *
 * This class handles:
 * - Streaming decompression of LZ4 files
 * - Line buffering for incomplete lines
 * - Game buffering to detect complete PGN games
 * - Game boundary detection using [Event ] markers
 *
 * Usage:
 *   LZ4PgnStreamReader reader;
 *   reader.processFile("games.pgn.lz4", [](const std::string& pgn_game) {
 *       // Process complete PGN game string
 *   });
 */
class LZ4PgnStreamReader {
public:
    /**
     * Callback function that receives complete PGN game strings.
     * Parameter: const std::string& pgn_game - Complete PGN game text
     */
    using GameCallback = std::function<void(const std::string& pgn_game)>;

    /**
     * Creates a new LZ4 PGN stream reader.
     * @param input_buffer_size Size of the buffer for reading compressed data (default: 64KB)
     * @param output_buffer_size Size of the buffer for decompressed data (default: 256KB)
     */
    explicit LZ4PgnStreamReader(size_t input_buffer_size = LZ4_DEFAULT_INPUT_BUFFER_SIZE,
                                 size_t output_buffer_size = LZ4_DEFAULT_OUTPUT_BUFFER_SIZE);

    ~LZ4PgnStreamReader() = default;

    // Disable copy operations
    LZ4PgnStreamReader(const LZ4PgnStreamReader&) = delete;
    LZ4PgnStreamReader& operator=(const LZ4PgnStreamReader&) = delete;

    // Enable move operations
    LZ4PgnStreamReader(LZ4PgnStreamReader&&) noexcept = default;
    LZ4PgnStreamReader& operator=(LZ4PgnStreamReader&&) noexcept = default;

    /**
     * Process a PGN file (LZ4-compressed or plain text) and call the callback
     * for each complete game found.
     *
     * @param file_path Path to the PGN file (.lz4 or plain text)
     * @param callback Function called with each complete PGN game
     * @throws std::runtime_error on file read or decompression errors
     */
    void processFile(const std::string& file_path, const GameCallback& callback);

private:
    size_t input_buffer_size_;
    size_t output_buffer_size_;

    /**
     * Process LZ4-compressed PGN file
     */
    void processLZ4File(const std::string& file_path, const GameCallback& callback) const;

    /**
     * Process plain text PGN file
     */
    static void processPlainTextFile(const std::string& file_path, GameCallback callback);
};

} // namespace chessmimic
