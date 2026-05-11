# Common Position Extractor

A high-performance C++ tool for extracting frequently occurring chess positions from BAGZ files.

## Problem

The original `pgn_to_bagz` converter uses a composite key (recent moves + FEN) for position identification, which prevents proper aggregation of identical positions reached through different move sequences. This particularly affects endgame positions, which are rarely reached via identical move sequences.

## Solution

This tool:
1. Reads records from BAGZ files
2. Extracts and aggregates positions by FEN only (ignoring move history)
3. Filters positions exceeding a move count threshold
4. Outputs common positions to a JSONL file

## Architecture

### Directory Structure

```
cpp_src/
├── common_position/
│   ├── common_position_extractor.hpp/cpp  # Main extractor class
│   ├── position_data.hpp/cpp              # Position data structure
│   ├── interfaces.hpp                     # Abstract interfaces
│   ├── bagz_reader_impl.hpp/cpp          # BAGZ reader implementation
│   └── file_operations_impl.hpp/cpp      # File operations implementation
├── extract_common_positions_main.cpp      # Main executable
└── tests/
    ├── common_position_extractor_test.cpp # Unit tests
    └── common_position_integration_test.cpp # Integration tests
```

### Key Components

- **CommonPositionExtractor**: Main orchestrator class
- **PositionData**: Data structure for position information
- **IBagzReader**: Interface for reading BAGZ files (allows mocking for tests)
- **IFileOperations**: Interface for file I/O (allows mocking for tests)
- **BagzReaderImpl**: Production implementation of IBagzReader
- **FileOperationsImpl**: Production implementation of IFileOperations

### Processing Phases

1. **Chunk Processing**: Reads BAGZ file in parallel chunks, aggregating positions by FEN
2. **Sorting**: Sorts each chunk by FEN for efficient merging
3. **Merge & Filter**: Multi-way merges sorted chunks, filtering by threshold

### Memory Efficiency

- Uses external sorting to handle files larger than available RAM
- Processes data in configurable chunks
- Parallel processing with configurable thread count
- Temporary files are automatically cleaned up

## Building

```bash
# Build the executable
./build_extract_common_positions.sh

# Build and run tests
./run_common_position_tests.sh
```

## Usage

```bash
./build/extract_common_positions <bagz_file> <output_file> [options]

Options:
  --threshold <n>       Minimum move count (default: 25)
  --chunk-size <n>      Records per chunk (default: 1000000)
  --max-records <n>     Maximum records to process (default: all)
  --temp-dir <path>     Temporary directory
  --threads <n>         Number of threads (default: auto)
  --keep-temp-files     Keep temporary files
```

### Examples

```bash
# Extract positions with >25 moves from training data
./build/extract_common_positions \
    /path/to/train.bagz \
    /path/to/common_positions.jsonl \
    --threshold 25 \
    --temp-dir /tmp/extract \
    --threads 16

# Test with limited records
./build/extract_common_positions \
    /path/to/train.bagz \
    /path/to/test_output.jsonl \
    --max-records 100000 \
    --threshold 5
```

## Output Format

The output is a JSONL file where each line contains:
```json
{
  "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
  "moves": {
    "e2e4": 12567,
    "d2d4": 8934,
    "Nf3": 4521
  },
  "total": 26022
}
```

## Testing

### Unit Tests
- Test individual components in isolation
- Use mocks for external dependencies
- Located in `cpp_src/tests/common_position_extractor_test.cpp`

### Integration Tests
- Test full extraction pipeline
- Create temporary BAGZ files for testing
- Verify parallel processing correctness
- Located in `cpp_src/tests/common_position_integration_test.cpp`

### Running Tests
```bash
# Run all common position extractor tests
./build/run_tests --gtest_filter="CommonPosition*"

# Run with verbose output
./build/run_tests --gtest_filter="CommonPosition*" --gtest_print_time=1
```

## Performance

- Processes ~1M records/second on modern hardware
- Scales linearly with thread count for I/O-bound operations
- Memory usage is constant regardless of input size
- Temporary disk usage is proportional to unique positions

## Thread Safety

The BagFileReader class is not thread-safe due to its use of file seek operations. The CommonPositionExtractor protects access to the shared reader with a mutex. This ensures thread safety but may limit parallelism for I/O operations. Future optimizations could include:
- Creating a pool of readers (one per thread)
- Using memory-mapped files for lock-free parallel access
- Implementing a thread-safe caching layer

## Future Improvements

1. **Streaming Output**: Write results as they're computed rather than at the end
2. **Progress Reporting**: Add detailed progress reporting with ETA
3. **Resume Support**: Allow resuming interrupted extractions
4. **Filter Expressions**: Support complex filtering (e.g., by piece count, game phase)
5. **Direct S3/Cloud Support**: Read BAGZ files directly from cloud storage