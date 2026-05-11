# Winner Converter Shuffle Refactoring Plan

## Overview

This document outlines the plan to modify the winner converter to match the architecture of `pgn_to_bagz` and `pgn_to_clock_bagz` converters, specifically around their shuffling implementation.

## Current State Analysis

### Winner Converter (Current)
- Uses a simplified `WinnerShuffleManager` for in-memory BAGZ-to-BAGZ shuffling
- Single-phase processing: directly writes to BAGZ while processing PGN files
- No bucket-based shuffling support
- No intermediate format for complex shuffling operations

### Other Converters (pgn_to_bagz, pgn_to_clock_bagz)
- Use standard `ShuffleManager` with both in-memory and bucket-based shuffling
- Two-phase processing:
  1. Extract and write to intermediate format (BucketRecord format)
  2. Shuffle (if enabled) and write final BAGZ
- Support for datasets larger than RAM via bucket-based shuffling
- Consistent architecture across converters

## Goals

1. **Consistency**: Align winner converter with established architecture
2. **Scalability**: Enable processing of datasets larger than RAM
3. **Performance**: Leverage parallel bucket processing for large datasets
4. **Maintainability**: Reduce code duplication by using shared components

## Implementation Plan

### Phase 1: Configuration Updates

#### Update WinnerDataConverter::Config
```cpp
struct Config {
    // Existing fields...
    
    // New shuffle configuration
    size_t shuffle_buckets = 256;           // Number of shuffle buckets
    size_t shuffle_memory_threshold_mb = 1024; // Threshold for in-memory vs bucket shuffle
    size_t bucket_ram_limit_mb = 4096;      // Total RAM for bucket operations
};
```

#### Add Command-Line Options
```
--shuffle-buckets <n>        Number of buckets for shuffle (default: 256)
--shuffle-memory <MB>        Memory threshold for in-memory shuffle (default: 1024)
--bucket-memory <MB>         Total memory for bucket operations (default: 4096)
```

### Phase 2: Architecture Changes

#### 1. Remove WinnerShuffleManager
- Delete `winner_shuffle_manager.hpp` and `winner_shuffle_manager.cpp`
- Remove from CMakeLists.txt
- Update all references

#### 2. Integrate Standard ShuffleManager
- Include `pgn_converter/shuffle_manager.hpp`
- Create ShuffleManager instance in WinnerDataConverter
- Link with pgn_converter components in CMakeLists.txt

#### 3. Implement Two-Phase Processing

**Phase 1: Extract and Write Intermediate Format**
- When shuffle is enabled, write to intermediate file using BucketRecord format
- Format: `[record_size][key_len][key][record_len][record]`
- Key: FEN or combination of FEN + recent moves

**Phase 2: Shuffle and Write Final BAGZ**
- Determine shuffle method based on file size
- Use in-memory shuffle for small files
- Use bucket-based shuffle for large files

### Phase 3: Implementation Details

#### Modified processFiles() Method
```cpp
void processFiles(const std::vector<std::string>& pgn_files) {
    std::string output_path = config_.output_bagz_path;
    
    // If shuffling enabled, write to intermediate format first
    if (config_.shuffle_enabled) {
        output_path = fs::path(config_.temp_dir) / "unsorted_records.data";
        // Open binary file for intermediate format
        std::ofstream temp_file(output_path, std::ios::binary);
        // Process files and write in BucketRecord format
    } else {
        // Direct BAGZ writing (existing behavior)
    }
}
```

#### New writeOutput() Method
```cpp
bool writeOutput(const std::string& temp_records_path) {
    if (!config_.shuffle_enabled) {
        return true; // Already at final location
    }
    
    // Determine shuffle method based on file size
    size_t file_size = fs::file_size(temp_records_path);
    size_t threshold = config_.shuffle_memory_threshold_mb * 1024 * 1024;
    
    if (file_size < threshold) {
        // In-memory shuffle
        CM_LOG_INFO("Using in-memory shuffle (file size: {:.2f} MB)", 
                    file_size / (1024.0 * 1024.0));
        shuffle_manager_->shuffleRecords(temp_records_path, config_.output_bagz_path);
    } else {
        // Bucket-based shuffle
        CM_LOG_INFO("Using bucket shuffle with {} buckets", config_.shuffle_buckets);
        BucketManager bucket_manager(
            config_.temp_dir,
            config_.shuffle_buckets,
            config_.bucket_ram_limit_mb * 1024 * 1024,
            thread_pool_.get(),
            logger_.get()
        );
        shuffle_manager_->bucketShuffleRecords(
            temp_records_path, 
            bucket_manager, 
            config_.output_bagz_path
        );
    }
    
    // Remove intermediate file
    if (!config_.keep_temp_files) {
        fs::remove(temp_records_path);
    }
    
    return true;
}
```

#### Writing Intermediate Format
```cpp
void writeIntermediateRecord(std::ofstream& out, 
                            const WinnerPositionRecord& record) {
    // Create key (FEN or FEN + recent moves)
    std::string key = record.fen;
    
    // Serialize record to JSON
    std::string json = record.toJson();
    
    // Calculate sizes
    uint32_t key_len = key.size();
    uint32_t record_len = json.size();
    uint32_t total_size = sizeof(key_len) + key_len + 
                         sizeof(record_len) + record_len;
    
    // Write in BucketRecord format
    out.write(reinterpret_cast<const char*>(&total_size), sizeof(total_size));
    out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
    out.write(key.data(), key_len);
    out.write(reinterpret_cast<const char*>(&record_len), sizeof(record_len));
    out.write(json.data(), record_len);
}
```

### Phase 4: Testing Strategy (TDD)

#### 1. Update Existing Tests
- `winner_data_converter_test.cpp`: Add shuffle configuration tests
- `winner_converter_integration_test.cpp`: Test two-phase processing

#### 2. Create New Tests
- Test intermediate format writing and reading
- Test integration with standard ShuffleManager
- Test threshold-based shuffle method selection
- Test bucket-based shuffle with large datasets
- Verify output format compatibility

#### 3. Performance Tests
- Compare performance with current implementation
- Measure memory usage with bucket-based shuffle
- Test with datasets of various sizes

### Phase 5: Build System Updates

#### CMakeLists.txt Changes
```cmake
# Remove WinnerShuffleManager
# Remove: winner_converter/winner_shuffle_manager.cpp

# Add dependency on pgn_converter components
target_link_libraries(pgn_to_winner_bagz 
    PRIVATE 
    winner_converter_lib
    pgn_converter_lib  # Add this
    # ... other libs
)
```

### Migration Path

1. **Backward Compatibility**: Default behavior remains unchanged (shuffle disabled)
2. **Gradual Rollout**: Test with small datasets first
3. **Performance Validation**: Benchmark before deploying to production

### Benefits

1. **Unified Architecture**: All converters use same shuffling approach
2. **Scalability**: Can handle arbitrarily large datasets
3. **Code Reuse**: Leverages battle-tested ShuffleManager
4. **Flexibility**: Choice of shuffle methods based on dataset size
5. **Maintainability**: Less code to maintain, shared improvements

### Risks and Mitigation

1. **Risk**: Performance regression for small datasets
   - **Mitigation**: Threshold-based method selection
   
2. **Risk**: Increased complexity
   - **Mitigation**: Comprehensive testing and documentation
   
3. **Risk**: Breaking changes to output format
   - **Mitigation**: Extensive compatibility testing

### Timeline

1. **Day 1**: Write failing tests (TDD approach)
2. **Day 2**: Remove WinnerShuffleManager, integrate ShuffleManager
3. **Day 3**: Implement two-phase processing
4. **Day 4**: Testing and debugging
5. **Day 5**: Performance validation and documentation

## Conclusion

This refactoring will bring the winner converter in line with the established architecture of other converters, providing better scalability and maintainability while preserving the specific functionality needed for winner prediction data extraction.