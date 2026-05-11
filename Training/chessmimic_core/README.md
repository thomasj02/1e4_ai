# ChessMimic Core Module

The `chessmimic_core` module provides optimized C++ implementations of chess-related functions
for the ChessMimic project, particularly focused on efficient tokenization and data processing
for neural network training.

## Module Structure

The module is implemented as a C++ extension using nanobind and directly exposed to Python. 
Type stubs are provided for better IDE integration and type checking.

## Key Components

### Constants

- `SEQUENCE_LENGTH` - Length of tokenized FEN sequences
- `INPUT_VOCAB_SIZE` - Size of the input vocabulary
- `CLASS_TOKEN` - Special token ID for the class token
- `PAD_TOKEN` - Special token ID for padding
- `NUM_ACTIONS` - Total number of possible chess moves/actions

### Mappings

- `MOVE_TO_ACTION` - Maps UCI move strings to action IDs
- `ACTION_TO_MOVE` - Maps action IDs back to UCI move strings
- `_CHARACTERS` - List of characters in the vocabulary
- `_CHARACTERS_INDEX` - Maps characters to their indices
- `_SPACES_CHARACTERS` - Set of characters representing spaces

### Functions

#### `tokenize(fen: str) -> numpy.ndarray`

Tokenizes a FEN string into a fixed-length array of token indices.

```python
# Example usage
import chessmimic_core
fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
tokens = chessmimic_core.tokenize(fen)
```

#### `prepare_recent_moves_tokens(recent_moves: List[str]) -> numpy.ndarray`

Prepares a fixed-length array of token indices from a list of recent moves.

```python
# Example usage
import chessmimic_core
moves = ["e2e4", "e7e5", "g1f3"]
tokens = chessmimic_core.prepare_recent_moves_tokens(moves)
```

#### `recent_moves_and_fen_to_inputs(recent_moves: List[str], fen: str) -> Tuple`

Converts recent moves and a FEN string into model inputs.

```python
# Example usage
import chessmimic_core
moves = ["e2e4", "e7e5", "g1f3"]
fen = "rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2"
recent_moves_tokens, fen_tokens, move_mask = chessmimic_core.recent_moves_and_fen_to_inputs(moves, fen)
```

## Implementation Details

The module is implemented in C++ using:
- nanobind for Python binding
- A specialized chess library for move generation and board representation
- Optimized algorithms for tokenization and data processing

See `cpp_src/chessmimic_core.cpp` for the implementation details.