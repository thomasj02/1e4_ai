#pragma once

namespace chessmimic {

/**
 * Default buffer sizes for LZ4 decompression streaming.
 *
 * These values are tuned for optimal performance when processing
 * compressed PGN files and other LZ4-compressed data.
 */

/**
 * Default size for the input buffer when reading compressed data.
 * 64KB provides a good balance between memory usage and I/O efficiency.
 */
constexpr size_t LZ4_DEFAULT_INPUT_BUFFER_SIZE = 65536;  // 64KB

/**
 * Default size for the output buffer when decompressing data.
 * 256KB allows efficient decompression with minimal reallocation.
 */
constexpr size_t LZ4_DEFAULT_OUTPUT_BUFFER_SIZE = 262144;  // 256KB

} // namespace chessmimic
