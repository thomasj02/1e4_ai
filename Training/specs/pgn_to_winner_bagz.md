# pgn_to_winner_bagz Specification

## Overview

`pgn_to_winner_bagz` is a C++ application for converting PGN chess games into a BAGZ format dataset where each position is labeled with the game's outcome. This tool is designed for training models to predict game outcomes from chess positions.

## Purpose

- Extract chess positions from PGN games
- Label each position with the game outcome (1 for white win, 0.5 for draw, 0 for black win)
- Support optional filtering of specific outcomes
- Create shuffled training datasets in BAGZ format
- Handle large-scale PGN collections efficiently
- Simple direct processing without position deduplication

## Key Differences from pgn_to_clock_bagz

| Feature | pgn_to_clock_bagz | pgn_to_winner_bagz |
|---------|-------------------|-------------------|
| Data extracted | Clock times, thinking time | Game result + clock states |
| Position label | Clock/time features | Winner (0, 0.5, 1) + clocks |
| Record complexity | 7 fields | 8 fields |
| Processing approach | Sort, merge, deduplicate | Direct write with shuffle |
| Common positions | Frequency-based filtering | No special handling |
| Draw handling | N/A | Included by default (0.5) |

## Data Structures

### WinnerPositionRecord

```cpp
class WinnerPositionRecord {
public:
    // Position data
    std::string fen;                        // Board position
    std::vector<std::string> recent_moves;  // Last 12 half-moves in UCI format (6 full moves)
    
    // Game outcome
    double winner;  // 1.0 = white wins, 0.5 = draw, 0.0 = black wins
    
    // Player ratings
    int white_rating;
    int black_rating;
    
    // Clock data (important for predicting outcomes)
    double white_clock;      // Seconds remaining for white
    double black_clock;      // Seconds remaining for black
    double increment;        // Seconds added per move
    
    // Methods
    std::string getPositionKey() const;  // Returns "FEN|move1,move2,move3"
    std::string toJson() const;
    static WinnerPositionRecord fromJson(const std::string& json_str, simdjson::dom::parser& parser);
};
```

### JSON Format

```json
{
    "fen": "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3",
    "recent_moves": [],
    "winner": 1.0,
    "white_rating": 1750,
    "black_rating": 1723,
    "white_clock": 178.0,
    "black_clock": 180.0,
    "increment": 2.0
}
```

## Component Architecture

### 1. Main Entry Point
- **File**: `pgn_to_winner_bagz_main.cpp`
- **Purpose**: Parse command line, create converter, run conversion

### 2. WinnerDataConverter
- **File**: `winner_converter/winner_data_converter.hpp/cpp`
- **Purpose**: Main coordinator class
- **Key Methods**:
  - `parseArguments()` - Parse CLI arguments
  - `convert()` - Run conversion
  - `processFiles()` - Parallel PGN processing
  - `writeOutput()` - Write to BAGZ with optional shuffle

### 3. WinnerGameParser
- **File**: `winner_converter/winner_game_parser.hpp/cpp`
- **Purpose**: Parse PGN games and extract positions with winner labels and clock states
- **Key Methods**:
  - `parseGame()` - Parse single PGN game
  - `parseHeaders()` - Extract Result, WhiteElo, BlackElo, TimeControl
  - `parseMoves()` - Generate position records with clock annotations
  - `parseResult()` - Convert Result header to winner value (1.0, 0.5, 0.0)
  - `parseClockTime()` - Extract clock from move comments (reused from clock converter)
  - `parseTimeControl()` - Extract initial time and increment

### 4. WinnerBagzWriter
- **File**: Reuses `clock_converter/clock_bagz_writer.hpp/cpp`
- **Purpose**: Write records directly to BAGZ format
- **Note**: Can reuse existing ClockBagzWriter as it just writes JSON records

### 5. Shared Components
- **BagzWriter**: Core BAGZ writing functionality
- **ShuffleManager**: In-memory shuffle for output randomization
- **ThreadPool**: Parallel PGN processing
- **Logger**: Logging and progress tracking

## Data Flow

### Simplified Processing Pipeline

1. **Parallel Extraction**:
   - Distribute PGN files across worker threads
   - Each thread processes assigned files independently
   - Parse games and generate WinnerPositionRecord for each position
   - Apply filters (outcome type, ratings, clock availability)
   - Collect records in memory buffers

2. **Direct Writing**:
   - Write records directly to temporary file or final output
   - No sorting or deduplication needed
   - Each position is treated independently

3. **Optional Shuffle**:
   - If shuffle is enabled, collect all records in memory
   - Perform random shuffle of all records
   - Write shuffled records to final BAGZ output
   - For very large outputs, could implement streaming shuffle

## Command Line Interface

```bash
pgn_to_winner_bagz [options] <pgn_files...> -o <output.bagz>

Options:
  -o, --output <path>          Output BAGZ file (required)
  -t, --threads <n>            Number of threads (default: hardware concurrency)
  -m, --memory <gb>            Memory limit in GB (default: auto-detect)
  --temp-dir <path>            Temporary directory (default: temp_winner_converter)
  
  --filter-draws               Skip games that ended in draws (only include decisive games)
  --min-rating <n>             Minimum average rating (default: 0)
  --max-rating <n>             Maximum average rating (default: 9999)
  
  --shuffle                    Enable output shuffling (recommended for training)
  --shuffle-seed <n>           Shuffle random seed (default: random)
  
  --keep-temp-files            Don't delete temporary files
  --log-level <n>              0=ERROR, 1=INFO, 2=DEBUG (default: 1)
  -h, --help                   Show this help message
```

## Configuration Structure

```cpp
struct Config {
    // Input/Output
    std::vector<std::string> pgn_paths;
    std::string output_bagz_path;
    std::string temp_dir = "temp_winner_converter";
    
    // Filtering
    bool filter_draws = false;
    int min_rating = 0;
    int max_rating = 9999;
    
    // Processing
    unsigned int num_threads = std::thread::hardware_concurrency();
    double memory_limit_gb = 0.0;
    
    // Shuffle
    bool shuffle_enabled = false;
    unsigned int shuffle_seed = 0;
    
    // Other
    bool keep_temp_files = false;
    int log_level = 1;
};
```

## Implementation Plan

### Phase 1: Core Components (Completed)
1. ✅ Create directory structure: `cpp_src/winner_converter/`
2. ✅ Implement `WinnerPositionRecord` with tests
3. ✅ Implement `WinnerGameParser` with tests
4. ✅ Add Result parsing logic

### Phase 2: Main Converter
1. Implement `WinnerDataConverter` - simplified version
2. Add parallel file processing using ThreadPool
3. Integrate with BagzWriter for direct output
4. Add main entry point

### Phase 3: Shuffle Integration
1. Add in-memory shuffle option
2. Integrate with existing ShuffleManager
3. Add command-line parsing
4. Add filtering options (draws, ratings)

### Phase 4: Testing & Polish
1. Integration tests
2. Performance testing
3. Documentation
4. Example scripts

## Testing Strategy

### Unit Tests
- ✅ `winner_position_record_test.cpp` - Record serialization/deserialization
- ✅ `winner_game_parser_test.cpp` - PGN parsing with various result formats
- `winner_data_converter_test.cpp` - Main converter logic
- `winner_shuffle_test.cpp` - Shuffle functionality

### Integration Tests
- `winner_converter_integration_test.cpp` - Full pipeline test
- `winner_converter_performance_test.cpp` - Performance benchmarks
- `winner_converter_filter_test.cpp` - Draw and rating filters

### Test Cases
1. Games with all result types (1-0, 0-1, 1/2-1/2, *)
2. Games without ratings
3. Games without clock annotations
4. Games with partial clock data
5. Games without moves (just headers)
6. Corrupted PGN handling
7. Large file processing
8. Draw filtering
9. Rating filtering
10. Clock time validation

## Performance Considerations

1. **Simplified Architecture**: No sorting or merging overhead
2. **Direct Writing**: Records written immediately, reducing memory usage
3. **Parallel Processing**: Thread pool for concurrent PGN parsing
4. **I/O Optimization**: Buffered writing to BAGZ format
5. **Optional Shuffle**: In-memory shuffle for randomized training data
6. **Compression**: BAGZ format with zstd compression

## Output Format

BAGZ file containing newline-delimited JSON records:
```json
{"fen":"r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq -","recent_moves":["e2e4","e7e5","g1f3","b8c6"],"winner":1.0,"white_rating":1750,"black_rating":1723,"white_clock":175.0,"black_clock":177.0,"increment":2.0}
{"fen":"r1bqkb1r/pppp1ppp/2n2n2/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq -","recent_moves":["e7e5","g1f3","b8c6","g8f6"],"winner":0.0,"white_rating":1650,"black_rating":1700,"white_clock":172.0,"black_clock":174.0,"increment":2.0}
{"fen":"r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq -","recent_moves":["g1f3","b8c6","f1c4","f8c5"],"winner":0.5,"white_rating":1800,"black_rating":1850,"white_clock":298.0,"black_clock":297.0,"increment":0.0}
```

## Key Architectural Benefits

Unlike `pgn_to_clock_bagz`, this converter uses a much simpler architecture:

1. **No Sorting Phase**: Positions are written in the order they appear
2. **No Merging Phase**: No K-way merge or deduplication needed
3. **No Common Positions**: All positions are treated equally
4. **Direct Output**: Records go straight to BAGZ format
5. **Simple Shuffle**: Optional in-memory shuffle for training

This results in:
- Faster processing (no sort/merge overhead)
- Lower memory usage (no need to hold sorted chunks)
- Simpler codebase (fewer components)
- More suitable for training data where duplicate positions provide value

## Future Enhancements

1. **Multi-class Support**: Use categorical labels (WHITE_WIN, DRAW, BLACK_WIN) instead of continuous
2. **Feature Expansion**: Include game phase, material count
3. **Streaming Mode**: Process games without full file load
4. **Distributed Processing**: Support for cluster computing
5. **Model Integration**: Direct PyTorch dataset generation
6. **Streaming Shuffle**: For datasets too large for memory