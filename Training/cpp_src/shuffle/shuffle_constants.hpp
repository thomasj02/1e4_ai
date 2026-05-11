#pragma once

#include <cstddef>

namespace chessmimic {

/**
 * Default configuration constants for shuffle operations.
 *
 * These values control the behavior of bucket-based shuffling,
 * which is used when datasets are too large to shuffle in memory.
 */

/**
 * Default maximum size for a single shuffle bucket file in bytes.
 * When a bucket exceeds this size, an overflow bucket is created.
 * 128MB provides a good balance between memory usage and I/O efficiency.
 */
constexpr size_t DEFAULT_MAX_BUCKET_SIZE_BYTES = 128 * 1024 * 1024;  // 128MB

/**
 * Default maximum size for a single shuffle bucket file in megabytes.
 * This is the same as DEFAULT_MAX_BUCKET_SIZE_BYTES but expressed in MB
 * for use in configuration structures.
 */
constexpr size_t DEFAULT_MAX_BUCKET_SIZE_MB = 128;  // 128MB

} // namespace chessmimic
