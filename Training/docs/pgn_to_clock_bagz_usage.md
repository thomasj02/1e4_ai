# pgn_to_clock_bagz - Chess Clock Data Converter

## Overview

`pgn_to_clock_bagz` is a high-performance C++ tool that processes compressed PGN files to generate BAGZ training data specifically for chess clock prediction models. It extracts clock times, thinking times, and position data from games with clock annotations.

## Features

- Parallel processing of compressed PGN files (`.pgn.lz4`)
- Extracts clock times and calculates thinking time for each move
- Generates BAGZ format optimized for clock training
- Common position filtering to avoid overfitting on openings
- Memory-efficient two-phase processing algorithm
- Compatible with Python BAGZ reader

## Building

### Prerequisites
- C++17 compatible compiler
- CMake 3.16+
- LZ4 library
- ZStandard library
- pthread support

### Build Instructions
```bash
cd Training
./build_cpp_ext.sh
```

The binary will be available at `build/pgn_to_clock_bagz`.

## Command Line Usage

```bash
./pgn_to_clock_bagz [OPTIONS] <input_files...>
```

### Required Arguments
- `<input_files...>`: One or more `.pgn.lz4` files to process

### Options

#### Basic Options
- `--output <path>`: Output BAGZ file path (required)
- `--threads <n>`: Number of worker threads (default: hardware concurrency)
- `--verbose`: Enable verbose output

#### Common Position Management
- `--max-positions-per-key <n>`: Threshold for common positions (default: 25)
- `--write-common-positions`: Generate common position files (training mode)
- `--skip-common-positions`: Skip positions that appear in FEN-only common position file (validation mode)
- `--common-positions-with-history <path>`: Path to common positions with move history (output only, .jsonl.gz format)
- `--common-positions-fen-only <path>`: Path to common positions by FEN only (used for both output and skipping, .jsonl.gz format)

#### Advanced Options
- `--min-game-length <n>`: Minimum moves per game (default: 10)
- `--max-recent-moves <n>`: Number of recent moves to track (default: 8)
- `--temp-dir <path>`: Directory for temporary files (default: system temp)

## Usage Examples

### Training Data Generation

Generate training data with common position tracking:

```bash
./pgn_to_clock_bagz \
  data/train/split_pgn_1700_1800_202409/*.pgn.lz4 \
  data/train/split_pgn_1700_1800_202410/*.pgn.lz4 \
  --output data/train/clock_1700_1800.bagz \
  --common-positions-with-history data/train/common_positions_with_history.jsonl.gz \
  --common-positions-fen-only data/train/common_positions_fen_only.jsonl.gz \
  --write-common-positions \
  --max-positions-per-key 25 \
  --threads 16 
```

### Validation Data Generation

Generate validation data excluding common positions:

```bash
./pgn_to_clock_bagz \
  data/val/split_pgn_1700_1800_202502/*.pgn.lz4 \
  --output data/val/clock_1700_1800.bagz \
  --common-positions-fen-only data/train/common_positions_fen_only.jsonl.gz \
  --skip-common-positions \
  --threads 16
```

Note: `--skip-common-positions` requires `--common-positions-fen-only` to be specified. Positions are skipped based on their FEN only, regardless of move history.

### Processing Large Datasets

For very large datasets, process in batches:

```bash
# Process monthly batches
for month in 202409 202410 202411; do
  ./pgn_to_clock_bagz \
    data/split_pgn_1700_1800_${month}/*.pgn.lz4 \
    --output data/clock_1700_1800_${month}.bagz \
    --threads 32 \
    --temp-dir /fast/ssd/temp
done
```

## Input Format

### PGN Requirements
- Files must be compressed with LZ4 (`.pgn.lz4` extension)
- Games must include clock annotations in PGN comments: `[%clk H:MM:SS]`
- Time control should be specified in headers (e.g., `TimeControl "180+2"`)
- Player ratings should be in headers (`WhiteElo`, `BlackElo`)

### Sample PGN with Clock Data
```
[Event "Rated Blitz game"]
[WhiteElo "1750"]
[BlackElo "1723"]
[TimeControl "180+2"]

1. e4 {[%clk 0:02:58]} e5 {[%clk 0:02:57]} 
2. Nf3 {[%clk 0:02:55]} Nc6 {[%clk 0:02:54]}
```

## Output Format

### BAGZ Record Structure
Each record in the BAGZ file contains:
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

### Common Position Files

Both files are compressed with gzip to reduce storage requirements (typically 3-5x compression ratio).

**common_positions_with_history.jsonl.gz**:
```json
{
  "position_key": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1|e2e4,e7e5,g1f3",
  "thinking_times": [2.5, 3.0, 1.5, 4.0, 2.0],
  "clock_states": [
    {"player": 178.5, "opponent": 175.0, "increment": 2.0},
    {"player": 170.0, "opponent": 172.0, "increment": 2.0}
  ],
  "ratings": [1750, 1723, 1780],
  "count": 150
}
```

**common_positions_fen_only.jsonl.gz**:
```json
{
  "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",
  "thinking_times": [2.5, 3.0, 1.5, 4.0, 2.0, 5.5],
  "clock_states": [...],
  "ratings": [...],
  "count": 850
}
```

## Performance Considerations

### Memory Usage
- Memory usage scales with thread count and chunk size
- Default: 80% of system RAM divided among threads
- Monitor with `--verbose` flag

### Processing Speed
- Typical: 1-2 GB/minute of compressed PGN data
- Factors affecting speed:
  - Number of threads
  - Disk I/O speed (SSD recommended)
  - Game density (games per MB)
  - Clock annotation coverage

### File Sizes
- Common position files are automatically compressed with gzip (3-5x reduction)
- Use `.jsonl.gz` extension for automatic gzip handling
- Legacy `.jsonl` files are still supported for reading

### Optimization Tips
1. Use SSD for temporary files: `--temp-dir /path/to/ssd`
2. Adjust thread count based on CPU cores and memory
3. Process files in chronological order for better cache usage
4. Pre-filter PGN files by rating/time control with `split_pgn.py`

## Troubleshooting

### Common Issues

**"Failed to parse clock annotation"**
- Ensure PGN files have proper clock format: `[%clk H:MM:SS]`
- Check that clock times decrease monotonically

**"Out of memory" errors**
- Reduce thread count
- Process smaller batches of files
- Check available system memory

**"Failed to decompress LZ4 file"**
- Verify files are properly compressed with LZ4
- Check file integrity: `lz4 -t file.pgn.lz4`

**"No games with valid clock data"**
- Verify PGN files contain clock annotations
- Check minimum game length setting

### Validation

Verify output with Python:
```python
from bagz import BagReader

reader = BagReader("output.bagz")
for record in reader.read_records():
    assert 'player_clock' in record
    assert 'thinking_time' in record
    print(f"Position: {record['fen'][:20]}... Time: {record['thinking_time']:.1f}s")
```

## Integration with Training Pipeline

The output BAGZ files can be used directly with the clock prediction training code:

```python
from MoveDataset import ClockDataset

dataset = ClockDataset(
    bagz_file="data/train/clock_1700_1800.bagz",
    common_positions="data/train/common_positions_fen_only.jsonl.gz"
)
```

### Common Position Filtering Logic

When `--skip-common-positions` is used:
1. The tool loads the FEN-only common positions file
2. For each position, it extracts the FEN from the position key (format: `FEN|recent_moves`)
3. If the FEN matches any FEN in the common positions file, the position is skipped
4. This means all positions with the same FEN are skipped, regardless of move history

## Algorithm Details

The converter uses a two-phase parallel processing algorithm:

### Phase 1: Extract & Sort
- Each thread processes assigned PGN files
- Extracts positions with clock data
- Sorts records in memory before writing to disk
- Creates sorted temporary chunks

### Phase 2: K-way Merge
- Merges all sorted chunks efficiently
- Groups positions by FEN for common position detection
- Routes to BAGZ or common position files based on frequency

This design minimizes I/O operations and maximizes parallelism while maintaining memory efficiency.