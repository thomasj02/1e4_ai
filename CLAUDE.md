# CLAUDE.md - ChessMimic Development Guide

## Important: Virtual Environment
⚠️ **ALWAYS activate the virtual environment before running any Python code:**
```bash
source <repo-root>/.venv/bin/activate
```
Most dependencies (PyTorch, nanobind, etc.) are installed in the virtual environment and code will not work without it.

This project uses the `uv` package manager. When installing new packages, use `uv pip install` instead of regular `pip`.

## Chess Validation
When you need to validate chess-specific data (FENs, moves, checks, checkmates, etc.), use Python's `chess` library:
```python
import chess

# Validate FEN
board = chess.Board("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")

# Check game state
is_checkmate = board.is_checkmate()
is_stalemate = board.is_stalemate()
is_check = board.is_check()

# Validate moves
is_legal = chess.Move.from_uci("e2e4") in board.legal_moves
```

## Commands
### Frontend
- `cd frontend && npm run dev` - Start frontend development server
- `cd frontend && npm run build` - Build frontend for production
- `cd frontend && npm run lint` - Run ESLint on frontend code
- `cd frontend && npm run lint -- src/App.jsx` - Lint a specific file

### Backend
- `source <repo-root>/.venv/bin/activate && cd backend && python -m uvicorn main:app --reload` - Start backend server
- `source <repo-root>/.venv/bin/activate && cd backend && python -m flake8` - Run Python linter (if installed)

#### Model Device Configuration
The backend model can run on CPU or GPU. By default, it's configured to run on CPU for cloud deployment evaluation.

To control device usage:
- **Force CPU**: `export CHESSMIMIC_FORCE_CPU=true` (default)
- **Allow GPU**: `export CHESSMIMIC_FORCE_CPU=false` (will use GPU if available)

Example:
```bash
# Force CPU mode
export CHESSMIMIC_FORCE_CPU=true
source <repo-root>/.venv/bin/activate && cd backend && python -m uvicorn main:app --reload

# Allow GPU if available
export CHESSMIMIC_FORCE_CPU=false
source <repo-root>/.venv/bin/activate && cd backend && python -m uvicorn main:app --reload
```

### Training & C++ Extensions

**Important**: The build scripts now automatically activate the virtual environment, so you don't need to activate it manually for building.

#### Quick Build Commands
- `cd Training && ./cmake_configure.sh --ninja` - Configure CMake with virtual environment
- `cd Training/build && ninja pgn_to_clock_bagz` - Build clock data converter
- `cd Training/build && ninja chessmimic_core` - Build Python extension
- `cd Training && ./build_and_run_tests.sh` - Build and run all tests
- `cd Training && ./build_cpp_ext.sh` - Build the C++ extension with optimizations

See `Training/BUILD.md` and `Training/Pipeline.txt` for detailed build and data-generation instructions.

## Development Workflow
- **Test-Driven Development**: Always write a failing test before making a fix
- **Incremental Development**: Make small, targeted changes and test frequently
- **Code Reviews**: Request reviews for significant changes
- If the task is unreasonable or infeasible, or if any of the tests are incorrect, please tell me. Do not hard code any test cases. Please tell me if the problem is unreasonable instead of hard coding test cases!

## Code Style Guidelines

### Frontend (React/JS)
- **Imports**: Group imports by type (React, third-party, local)
- **Formatting**: Use 2-space indentation
- **Components**: Function components with hooks preferred
- **Error Handling**: Use try/catch with informative console.error logs
- **State Management**: Use React hooks (useState, useEffect, useCallback)
- **Naming**: 
  - Components: PascalCase
  - Functions/variables: camelCase
  - Constants: UPPER_SNAKE_CASE

### Backend (Python/FastAPI)
- **Imports**: Follow PEP8 standards (stdlib, third-party, local)
- **Formatting**: Use 4-space indentation
- **Endpoints**: Use async functions with descriptive names
- **Error Handling**: Use proper exception handling with clear error messages
- **Type Hints**: Use modern Python 3.12+ type annotations:
  - Use `X | Y` instead of `Union[X, Y]` or `Optional[X]` (use `X | None`)
  - Use built-in generics: `list[str]`, `dict[str, int]` instead of `List[str]`, `Dict[str, int]`
  - Use `tuple[int, ...]` instead of `Tuple[int, ...]`
  - Use `type` aliases: `type UserDict = dict[str, str | int]`
  - Use `Self` for methods returning the class instance
  - No need for `from typing import List, Dict, Optional, Union` - use built-ins instead

### C++ Extensions (Training Module)
- **Class Style**: Follow C++17 standards and conventions
- **Naming**: 
  - Classes/Structs: PascalCase
  - Functions/Variables: camelCase or snake_case (be consistent within files)
  - Constants: UPPER_SNAKE_CASE
- **Documentation**: Add comments for complex logic and function documentation
- **Error Handling**: Use exceptions for error handling, with clear messages
- **Performance**: Minimize memory allocations in hot code paths
- **Python Binding**: Use nanobind for Python/C++ interoperability

## Documentation References

### Virtual Environment
- Located at: `<repo-root>/.venv/`
- Activate with: `source <repo-root>/.venv/bin/activate`
- Contains all Python dependencies including nanobind and pytorch
- **REQUIRED**: Always activate this environment before running any Python code in the project
- This project uses `uv` as the package manager. Always use `uv pip` instead of `pip` for installing packages
- If you get ImportError or ModuleNotFoundError, check that you've activated the virtual environment

### External Documentation
- Nanobind and chess-library reference docs are not vendored in this repository.
- Use the upstream project documentation when library-specific behavior needs confirmation.
