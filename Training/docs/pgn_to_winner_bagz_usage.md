# pgn_to_winner_bagz Usage Guide

## Overview

`pgn_to_winner_bagz` is a high-performance tool for converting PGN chess games into winner-labeled BAGZ format datasets. Each chess position is labeled with the game's outcome (1.0 for white win, 0.5 for draw, 0.0 for black win), making it ideal for training models to predict game outcomes from positions.

## Key Features

- **Outcome Labeling**: Every position is labeled with the final game result
- **Clock Data**: Includes remaining clock times and increment for each position
- **Parallel Processing**: Utilizes multiple CPU cores for fast conversion
- **Flexible Filtering**: Filter by outcome type, rating range
- **Shuffle Support**: Built-in shuffling for creating randomized training sets
- **Memory Efficient**: Processes large datasets with configurable memory limits
- **BAGZ Compression**: Output uses zstd compression for efficient storage

## Installation

The tool is built as part of the ChessMimic training module:

```bash
cd Training
./cmake_configure.sh --ninja
cd build
ninja pgn_to_winner_bagz
```

## Basic Usage

### Convert a Single PGN File

```bash
./pgn_to_winner_bagz games.pgn -o training_data.bagz
```

### Convert Multiple PGN Files

```bash
./pgn_to_winner_bagz games1.pgn games2.pgn games3.pgn -o combined.bagz
```

### Convert a Directory of PGN Files

```bash
./pgn_to_winner_bagz /path/to/pgn_directory/ -o dataset.bagz
```

## Command-Line Options

```
pgn_to_winner_bagz [options] <pgn_files...> -o <output.bagz>

Required:
  -o, --output <path>          Output BAGZ file

Processing Options:
  -t, --threads <n>            Number of threads (default: auto-detect)
  -m, --memory <gb>            Memory limit in GB (default: 50% of system RAM)
  --temp-dir <path>            Temporary directory (default: temp_winner_converter)

Filtering Options:
  --filter-draws               Skip games that ended in draws
  --min-rating <n>             Minimum average rating (default: 0)
  --max-rating <n>             Maximum average rating (default: 9999)

Shuffle Options:
  --shuffle                    Enable output shuffling (recommended for training)
  --shuffle-seed <n>           Shuffle random seed (default: random)

Other Options:
  --keep-temp-files            Don't delete temporary files after processing
  --log-level <n>              0=ERROR, 1=INFO, 2=DEBUG (default: 1)
  -h, --help                   Show help message
```

## Output Format

The tool produces a BAGZ file containing newline-delimited JSON records:

```json
{
  "fen": "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq -",
  "recent_moves": ["e2e4", "e7e5", "g1f3", "b8c6"],
  "winner": 1.0,
  "white_rating": 1750,
  "black_rating": 1723,
  "white_clock": 175.0,
  "black_clock": 177.0,
  "increment": 2.0
}
```

### Field Descriptions

- **fen**: Board position in FEN notation
- **recent_moves**: Last 12 half-moves (6 full moves) in UCI format
- **winner**: Game outcome (1.0 = white wins, 0.5 = draw, 0.0 = black wins)
- **white_rating**: White player's rating (0 if not available)
- **black_rating**: Black player's rating (0 if not available)
- **white_clock**: Seconds remaining on white's clock
- **black_clock**: Seconds remaining on black's clock
- **increment**: Seconds added per move

## Usage Examples

### Create Training Dataset for Decisive Games Only

Filter out draws to train on decisive games:

```bash
./pgn_to_winner_bagz lichess_games.pgn -o decisive_games.bagz --filter-draws
```

### Process High-Quality Games

Filter by rating range to focus on stronger players:

```bash
./pgn_to_winner_bagz master_games.pgn -o master_dataset.bagz \
    --min-rating 2200 --max-rating 2800
```

### Create Shuffled Training Set

Shuffle is recommended for machine learning training:

```bash
./pgn_to_winner_bagz tournament_games.pgn -o shuffled_train.bagz \
    --shuffle --shuffle-seed 42
```

### Large-Scale Processing

Process large datasets with controlled resources:

```bash
./pgn_to_winner_bagz /data/pgn_collection/ -o large_dataset.bagz \
    -t 16 -m 32 --temp-dir /fast_ssd/temp
```

### Combined Filtering

Apply multiple filters for specific training requirements:

```bash
./pgn_to_winner_bagz rated_games.pgn -o filtered_dataset.bagz \
    --filter-draws --min-rating 1600 --max-rating 2400 \
    --shuffle
```

## Performance Tips

1. **Use Multiple Threads**: The tool scales well with multiple CPU cores
   ```bash
   ./pgn_to_winner_bagz input.pgn -o output.bagz -t 8
   ```

2. **Fast Storage for Temp Files**: Use SSD for temporary directory
   ```bash
   ./pgn_to_winner_bagz input.pgn -o output.bagz --temp-dir /ssd/temp
   ```

3. **Memory Limits**: For very large files, set appropriate memory limits
   ```bash
   ./pgn_to_winner_bagz huge.pgn -o output.bagz -m 16
   ```

4. **Batch Processing**: Process multiple files in one run for efficiency
   ```bash
   ./pgn_to_winner_bagz *.pgn -o combined.bagz
   ```

## Working with Output

### Reading BAGZ Files in Python

```python
from bagz import BagFileReader

# Open the BAGZ file
reader = BagFileReader("training_data.bagz")

# Read records
for i in range(len(reader)):
    record = reader.get_record(i)
    data = json.loads(record.decode('utf-8'))
    
    # Access fields
    fen = data['fen']
    winner = data['winner']
    white_clock = data['white_clock']
    # ... etc
```

### Using with PyTorch

```python
import torch
from torch.utils.data import Dataset
from bagz import BagFileReader
import json

class WinnerPredictionDataset(Dataset):
    def __init__(self, bagz_path):
        self.reader = BagFileReader(bagz_path)
    
    def __len__(self):
        return len(self.reader)
    
    def __getitem__(self, idx):
        record = json.loads(self.reader.get_record(idx))
        
        # Convert to your model's input format
        features = self.extract_features(record)
        label = torch.tensor(record['winner'], dtype=torch.float32)
        
        return features, label
```

## Troubleshooting

### Empty Output File

- Check that PGN files contain valid games with moves
- Verify games have valid results (1-0, 0-1, 1/2-1/2)
- Games with result "*" (unfinished) are skipped

### Memory Issues

- Reduce memory limit: `-m 8` (use 8GB)
- Process fewer files at once
- Disable shuffle for very large datasets

### Slow Processing

- Increase thread count: `-t 16`
- Use faster storage for temp directory
- Check if input files are compressed (.pgn.lz4 is supported)

### Missing Clock Data

- Clock data is optional; positions without clocks will have 0.0 values
- TimeControl header determines increment value
- Clock comments like `{[%clk 0:05:00]}` are parsed when available

## Advanced Usage

### Processing Compressed Files

The tool automatically handles LZ4-compressed PGN files:

```bash
./pgn_to_winner_bagz games.pgn.lz4 -o output.bagz
```

### Monitoring Progress

Use INFO log level to see detailed progress:

```bash
./pgn_to_winner_bagz large_dataset.pgn -o output.bagz --log-level 1
```

### Debugging Issues

Enable DEBUG logging for troubleshooting:

```bash
./pgn_to_winner_bagz problem_file.pgn -o test.bagz --log-level 2
```

## Integration with Training Pipeline

The output is designed to work seamlessly with the ChessMimic training pipeline:

```bash
# 1. Convert PGN to winner-labeled BAGZ
./pgn_to_winner_bagz lichess_2023.pgn -o train_data.bagz --shuffle

# 2. Train winner prediction model
python WinnerTrainer.py \
    --train-bagz train_data.bagz \
    --model-type transformer \
    --epochs 10

# 3. Evaluate model
python WinnerInference.py \
    --model checkpoints/best_model.pth \
    --test-bagz test_data.bagz
```

## See Also

- [pgn_to_bagz](pgn_to_bagz_usage.md) - Convert PGN to move prediction format
- [pgn_to_clock_bagz](pgn_to_clock_bagz_usage.md) - Convert PGN to clock prediction format
- [ChessMimic Training Guide](../README.md) - Overall training documentation