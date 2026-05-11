# C++ Optimizations for ChessMimic Training Pipeline

This document describes the C++ optimizations implemented for the ChessMimic training pipeline to improve performance during training.

## Implemented Optimizations

We've implemented C++ versions of key functions in the ChessMimic training pipeline:

1. **FEN String Tokenization** - Direct port from Python to C++
   - Implemented as part of the `chessmimic_core` module
   - Exact compatibility with Python implementation
   - ~1.5x faster than Python implementation

2. **Recent Moves Tokenization** (`_prepare_recent_moves_tokens`)
   - C++ implementation processes move lists to prepare token arrays
   - Implementation now in `chessmimic_core.cpp`

3. **FEN and Moves to Model Inputs** (`recent_moves_and_fen_to_inputs`)
   - Processes chess positions and moves for model training
   - Complete implementation in C++ using chess.hpp library
   - ~6x faster than Python implementation

## Performance Improvements

The benchmarks show significant improvements in processing times:

| Function                        | Python Time (ms) | C++ Time (ms) | Speedup |
|---------------------------------|------------------|---------------|---------|
| `_prepare_recent_moves_tokens`  | 0.002031        | 0.002201      | 0.92x   |
| `recent_moves_and_fen_to_inputs`| 0.144631        | 0.024131      | 5.99x   |
| Complete dataset iteration      | 0.179003        | 0.030404      | 5.89x   |

For a dataset with 1 million records, the estimated time savings is approximately 149 seconds per iteration through the dataset. When using multiple epochs during training, this can lead to significant time savings.

## Usage

The C++ implementations are used by default in `MoveDataset.py`. If you need to fall back to the Python implementations for debugging or comparison, you can set an environment variable:

```bash
export CHESSMIMIC_USE_PYTHON_TOKENIZER=true
```

## Implementation Details

1. The C++ code is implemented in `cpp_src/chessmimic_core.cpp`
2. Python bindings are created using nanobind
3. The implementation contains:
   - ChessTokenizer class for FEN tokenization
   - ChessMoveGenerator class for move generation
   - ChessMoveDatasetHelpers class for dataset-related operations

## Next Steps

Potential further optimizations:

1. Implement a full C++ version of the BagzDataset class
2. Optimize data loading and batching for PyTorch
3. Create custom CUDA kernels for tokenization operations
4. Implement multithreaded batch processing

## Building

The C++ extension can be built using:

```bash
./build_cpp_ext.sh  # For release build
./build_cpp_ext_debug.sh  # For debug build
```