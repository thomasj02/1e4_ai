# C++ Clock Data Converter Specification

## Overview

The C++ Clock Data Converter is a specialized tool that processes compressed PGN files (`.pgn.lz4`) to generate BAGZ training data specifically for the clock prediction model. It creates a new BAGZ format optimized for clock training, separate from the existing move prediction BAGZ files.

## Functional Requirements

### 1. Input Processing

#### 1.1 PGN File Reading
- Accept a list of `.pgn.lz4` files as input (already filtered by rating/time control via split_pgn.py)
- Parse games with clock annotations in PGN comments (`[%clk H:MM:SS]`)
- Extract time control from game headers (e.g., "180+2")
- Support parallel processing of multiple PGN files

#### 1.2 Position and Clock Data Extraction
For each position in valid games:
- Extract FEN (includes move number)
- Track recent move history (last N moves in UCI format)
- Extract player ratings from headers
- Track clock times throughout the game:
  - Current player's clock time (from move comment)
  - Opponent's clock time (from opponent's last move)
  - Parse increment from time control header
- Calculate thinking time for each move

### 2. Data Structure Design

#### 2.1 Clock Training Record
Each BAGZ record should contain:
```json
{
  "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
  "recent_moves": ["e2e4", "e7e5", "g1f3"],
  "rating": 1750,
  "player_clock": 178.5,      // seconds remaining
  "opponent_clock": 175.0,    // seconds remaining  
  "increment": 2.0,           // seconds per move
  "thinking_time": 4.5        // seconds used for this move
}
```

#### 2.2 Multiple Records Per Position
Unlike the move prediction converter which aggregates all games into one record per position:
- Each game instance creates a separate record (even for duplicate positions)
- This captures the distribution of thinking times for each position
- Skip only positions that lack valid clock data

### 3. Common Position Management

#### 3.1 Training Phase
- Track positions that appear frequently across games
- Use configurable `--max_positions_per_key` threshold (e.g., 25)
- Position key format: `FEN|recent_move1,recent_move2,...`
  - FEN first enables grouping by position regardless of move history
- Two-phase processing with optimized I/O:
  - Phase 1: Extract and sort in memory before writing
    - Each thread builds chunks in memory
    - Sorts each chunk before writing to disk
    - Eliminates separate sorting pass
  - Phase 2: K-way merge with group counting
    - Merge pre-sorted chunks
    - Count at FEN level (aggregating all move sequences)
    - If FEN count > threshold: Write to common position files
    - If FEN count <= threshold: Write to BAGZ file
- Generate two common position files:
  
  **1. Common positions with move history** (`common_positions_with_history.jsonl`):
  - Groups by full key: `FEN|recent_moves`
  - Tracks positions that occur frequently with specific move sequences
  ```json
  {
    "position_key": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1|e2e4,e7e5,g1f3",
    "thinking_times": [2.5, 3.0, 1.5, 4.0, 2.0, ...],
    "clock_states": [
      {"player": 178.5, "opponent": 175.0, "increment": 2.0},
      {"player": 170.0, "opponent": 172.0, "increment": 2.0},
      ...
    ],
    "ratings": [1750, 1723, 1780, ...],
    "count": 150
  }
  ```
  
  **2. Common positions without move history** (`common_positions_fen_only.jsonl`):
  - Groups by FEN only (move clocks stripped)
  - Aggregates all occurrences of same position regardless of how it was reached
  - Similar to extract_common_positions output
  ```json
  {
    "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",
    "thinking_times": [2.5, 3.0, 1.5, 4.0, 2.0, 5.5, ...],
    "clock_states": [...],
    "ratings": [...],
    "count": 850
  }
  ```

#### 3.2 Validation Phase
- Load common positions JSONL file from training phase
- Skip ALL occurrences of positions that appear in common positions file
- This prevents validation on memorizable opening positions

### 4. Output Generation

#### 4.1 BAGZ File Format
- One record per position occurrence (same position can appear multiple times)
- No aggregation - each game instance is a separate record
- Compress records using ZStandard
- Create index for random access
- Compatible with Python BAGZ reader

#### 4.2 File Organization
- Separate BAGZ files for train/validation based on input PGN dates
- Common positions file shared between phases
- Support for incremental processing (append mode)

## Technical Architecture

### Processing Algorithm

The converter uses a two-phase parallel algorithm optimized for minimal I/O:

#### Phase 1: Parallel Extract & Sort
```
For each thread t in thread_pool:
  chunk_id = 0
  records_buffer = []
  
  For each assigned PGN.lz4 file:
    - Decompress and parse PGN
    - For each game:
      - Extract positions with clock data
      - Create ClockPositionRecord with key = "FEN|recent_moves"
      - Add to records_buffer
      
      - If memory_usage(records_buffer) > MAX_CHUNK_SIZE:
        - Sort records_buffer by position_key
        - Write to "sorted_t{t}_chunk{chunk_id}.tmp"
        - Clear records_buffer
        - chunk_id += 1
  
  - Sort and write final chunk if needed
```

#### Phase 2: K-way Merge with Group Counting
```
- Open all sorted chunk files
- Initialize min-heap with first record from each chunk
- current_key = null
- current_fen = null
- key_group_buffer = []      // For FEN|recent_moves grouping
- fen_group_buffer = []      // For FEN-only grouping
- fen_is_common = false     // Track if current FEN exceeds threshold

While records remain:
  - record = extract_min(heap)
  - record_fen = extract_fen(record.key)
  
  // Check if FEN changed
  If record_fen != current_fen and current_fen != null:
    - fen_is_common = len(fen_group_buffer) > max_positions_per_key
    - If fen_is_common:
      - CommonPositionWriter.writeFenOnly(current_fen, fen_group_buffer)
    - fen_group_buffer = []
    - current_fen = record_fen
  
  // Check if full key changed
  If record.key != current_key and current_key != null:
    - If fen_is_common:
      - CommonPositionWriter.writeWithHistory(current_key, key_group_buffer)
      // Don't write to BAGZ - this FEN is too common
    - Else:
      - Write all records in key_group_buffer to BAGZ
    - key_group_buffer = []
    - current_key = record.key
  
  // Add record to both buffers
  - key_group_buffer.append(record)
  - fen_group_buffer.append(record)
  - If current_fen == null: current_fen = record_fen
  - If current_key == null: current_key = record.key
  
  - Advance chunk and update heap

// Process final groups
- fen_is_common = len(fen_group_buffer) > max_positions_per_key
- If fen_is_common:
  - Process fen_group_buffer to FEN-only file
  - Process key_group_buffer to with-history file
- Else:
  - Write key_group_buffer to BAGZ
```

#### Memory Management
- Available_Memory = System_RAM × 0.8 (safety margin)
- MAX_CHUNK_SIZE = Available_Memory / num_threads
- Ensures all threads can sort simultaneously without swapping

#### Performance Characteristics
- **I/O Passes**: Only 2 (read PGN → write sorted chunks → write final output)
- **Memory**: O(MAX_CHUNK_SIZE × num_threads) during extraction
- **Time**: O(N log(N/T)) where N = total records, T = threads
- **No Contention**: Each thread writes to independent files
- **Cache Friendly**: Sequential reads and writes throughout

### New Components

```
chessmimic::clock_converter/
├── ClockDataConverter       // Main coordinator
├── ClockGameParser         // PGN parser with clock tracking
├── ClockPositionRecord     // Data structure for clock training
├── ClockBagzWriter         // Specialized BAGZ writer
├── SortedMergeWriter       // Merge phase with group counting
├── CommonPositionWriter    // JSONL writer for common positions
└── CommonPositionLoader    // Load common positions for filtering
```

### Key Classes

#### ClockDataConverter
- Coordinates the conversion process
- Manages thread pool for parallel processing
- Handles command-line interface

#### ClockGameParser
- Extends PGN parsing to track clock progression
- Maintains clock state for both players
- Calculates thinking times
- Extracts time control and increment

#### ClockPositionRecord
- Lightweight structure for clock training data
- JSON serialization for BAGZ output
- Each game instance stored separately (no aggregation)

#### SortedMergeWriter
- Implements K-way merge of sorted chunks
- Uses min-heap for efficient merging
- Detects consecutive identical positions
- Accumulates groups until position key changes
- Routes complete groups based on size:
  - Size <= threshold: Write all records to BAGZ
  - Size > threshold: Send to CommonPositionWriter

#### CommonPositionWriter
- Manages two output files:
  1. **With History**: Writes groups as received (full FEN|recent_moves key)
  2. **FEN Only**: Strips move clocks and aggregates by base position
- Formats position groups into JSONL records:
  - Extracts thinking times from all instances
  - Collects clock states and ratings
  - Writes appropriate format to each file

#### CommonPositionLoader
- Loads JSONL file for validation phase
- Provides fast lookup for position filtering

### Design Decisions

1. **Separate Tool**: New binary `pgn_to_clock_bagz` rather than modifying existing converter
2. **No Aggregation**: Unlike move prediction, each game instance is a separate record
   - Move prediction: One record per unique position (aggregating all moves)
   - Clock prediction: Multiple records per position (one per game occurrence)
3. **Distribution Learning**: Multiple samples per position enable learning thinking time distributions
4. **Optimized I/O Algorithm**:
   - Sort in memory before first write (eliminates re-reading)
   - Two-phase processing instead of three
   - No file contention (each thread writes own chunks)
   - Streaming K-way merge for final output
5. **Dual Common Position Tracking**:
   - FEN|recent_moves key ordering enables both groupings
   - Common positions with move history (exact sequences)
   - Common positions by FEN only (all paths to position)
6. **Minimal Dependencies**: Reuse chess.hpp, thread pool, and BAGZ infrastructure
7. **Clean Interface**: Simple command-line usage for train/val splitting

## TODO List

### Phase 1: Core Infrastructure (3 days)
- [x] Create ClockDataConverter main class structure
- [x] Design ClockPositionRecord data structure
- [x] Set up command-line argument parsing
- [x] Create project structure and CMake configuration

### Phase 2: PGN Processing (3-4 days)
- [x] Implement ClockGameParser class
  - [x] Parse clock comments from PGN
  - [x] Track clock state for both players
  - [x] Calculate thinking times
  - [x] Extract time control and increment
- [x] Handle various clock annotation formats
- [x] Add error handling for missing/invalid clock data

### Phase 3: Data Generation (3-4 days)

#### 3.1 Parallel Processing Pipeline
- [x] Implement two-stage parallel processing:
  
  **Stage A: Parallel Extract & Sort**
  - [x] Each thread processes assigned PGN files
  - [x] Extract positions with clock data into memory buffer
  - [x] When buffer reaches MAX_CHUNK_SIZE:
    - [x] Sort buffer by (recent_moves, FEN) key
    - [x] Write sorted chunk to disk
    - [x] Clear buffer for next chunk
  - [x] MAX_CHUNK_SIZE = Available_Memory / num_threads
  
  **Stage B: K-way Merge with Group Counting**
  - [x] Open all sorted chunks with min-heap
  - [x] Stream through sorted records:
    - [x] Accumulate groups of identical positions
    - [x] Route based on group size vs threshold
    - [x] Write to BAGZ or CommonPositionWriter

#### 3.2 BAGZ Output Implementation
- [x] Implement ClockBagzWriter
  - [x] JSON serialization of clock records
  - [x] ZStandard compression
  - [x] Index generation

#### 3.3 Memory Management
- [x] Implement memory management
  - [x] Monitor memory usage per thread
  - [ ] Adaptive chunk sizing (future improvement)
  - [ ] Graceful handling of memory pressure (future improvement)

### Phase 4: Common Position Management (2 days)
- [x] Integrate SortedMergeWriter with main pipeline
  - [x] Group detection logic
  - [x] Threshold checking
  - [x] Routing to appropriate writer
- [x] Implement CommonPositionWriter
  - [x] Write to two output files (with/without history)
  - [x] Strip move clocks from FEN for FEN-only file
  - [x] Aggregate positions across different move histories
  - [x] Format as JSONL with thinking times, clock states, ratings
- [x] Implement CommonPositionLoader
  - [x] Load JSONL for validation phase
  - [x] Fast position lookup
- [x] Add command-line flags
  - [x] --max-positions-per-key
  - [x] --write-common-positions
  - [x] --skip-common-positions
  - [x] --common-positions-with-history (file path)
  - [x] --common-positions-fen-only (file path)

### Phase 5: Testing and Validation (2-3 days)
- [x] Unit tests for clock parsing
- [x] Integration tests with sample PGN files
- [x] Validate output format with Python reader
- [x] Performance benchmarking
- [x] Memory usage profiling

### Phase 6: Documentation and Polish (1-2 days)
- [x] Write usage documentation
- [x] Create example scripts
- [x] Add detailed error messages
- [x] Code cleanup and review

## Success Criteria

1. **Accuracy**: Correctly extracts clock times and calculates thinking time
2. **Performance**: Processes 1GB/minute of compressed PGN data
3. **Compatibility**: Output readable by Python BAGZ implementation
4. **Robustness**: Handles missing clock data gracefully
5. **Scalability**: Efficient memory usage for large datasets

## Example Usage

### Training Data Generation
```bash
./pgn_to_clock_bagz \
  data/train/split_pgn_1700_1800_202409/*.pgn.lz4 \
  data/train/split_pgn_1700_1800_202410/*.pgn.lz4 \
  --output data/train/clock_1700_1800.bagz \
  --common-positions-with-history data/train/common_positions_with_history.jsonl \
  --common-positions-fen-only data/train/common_positions_fen_only.jsonl \
  --write-common-positions \
  --max-positions-per-key 25 \
  --threads 16
```

### Validation Data Generation
```bash
./pgn_to_clock_bagz \
  data/val/split_pgn_1700_1800_202502/*.pgn.lz4 \
  --output data/val/clock_1700_1800.bagz \
  --common-positions-with-history data/train/common_positions_with_history.jsonl \
  --skip-common-positions \
  --threads 16
```

## Future Improvements

### Adaptive Memory Management
The current implementation uses fixed chunk sizes based on available memory at startup. Future enhancements could include:

1. **Dynamic Chunk Size Adjustment**
   - Monitor system memory availability during runtime
   - Adjust chunk sizes based on memory pressure
   - Implement per-thread memory quotas that adapt to system conditions

2. **Graceful Memory Pressure Handling**
   - Early chunk flushing when memory pressure detected
   - Fallback strategies for allocation failures
   - Thread pool size adjustment based on available memory
   - Platform-specific memory monitoring (e.g., `/proc/meminfo` on Linux)

3. **Benefits**
   - Better utilization of available memory on varying systems
   - Prevention of OOM conditions on memory-constrained systems  
   - Automatic adaptation to system load changes
   - Improved reliability for very large dataset processing

These improvements would make the system more robust for production deployments but are not critical for the initial implementation.

## Estimated Timeline

Total: 2-2.5 weeks for complete implementation with testing and documentation.