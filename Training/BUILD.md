# ChessMimic Training Module Build Guide

## Prerequisites

1. **Virtual Environment**: The project requires a Python virtual environment with nanobind installed:
   ```bash
   source <repo-root>/.venv/bin/activate
   ```

2. **Build Tools**:
   - CMake 3.15+
   - C++20 compatible compiler (GCC 10+ or Clang 11+)
   - Ninja (optional but recommended)
   - Python 3.8+ with nanobind

## Quick Start

### Using the Convenience Scripts

The easiest way to build is using the provided build scripts which automatically handle the virtual environment:

```bash
# Configure CMake with virtual environment (creates build/ directory)
./cmake_configure.sh --ninja

# Build everything
cd build && ninja

# Or build specific targets
ninja pgn_to_clock_bagz      # Clock data converter
ninja chessmimic_core        # Python extension module
ninja run_tests              # Test executable
```

### Build Scripts

- **`cmake_configure.sh`**: Main configuration script with options:
  ```bash
  ./cmake_configure.sh [options]
    --debug              # Debug build
    --release            # Release build (default)
    --relwithdebinfo     # Release with debug info
    --ninja              # Use Ninja generator
    --build-dir <dir>    # Custom build directory
  ```

- **`build_and_run_tests.sh`**: Build and run all tests
- **`build_clock_tests.sh`**: Build clock converter and run specific tests
- **`build_cpp_ext.sh`**: Build the Python extension module
- **`build_extract_common_positions.sh`**: Build the common positions extractor

## Manual Building

If you prefer manual builds:

```bash
# Activate virtual environment
source <repo-root>/.venv/bin/activate

# Create build directory
mkdir -p build && cd build

# Configure with virtual environment Python
cmake .. -DPython_EXECUTABLE=$(which python) -DCMAKE_BUILD_TYPE=Release

# Build
make -j$(nproc)  # or ninja if using -GNinja
```

## Common Issues

### nanobind Not Found
If you see "Could not find nanobind", ensure:
1. Virtual environment is activated
2. nanobind is installed: `pip install nanobind`
3. CMake uses the correct Python: `-DPython_EXECUTABLE=$(which python)`

### Build Targets

- `pgn_to_clock_bagz`: Converts PGN files to clock training data
- `chessmimic_core`: Python extension module for fast chess operations
- `run_tests`: Unit test executable
- `extract_common_positions`: Extracts common positions from BAGZ files

## Python Tests

The Python tests live in `Training/tests/` and are configured by `pytest.ini`.
Build `chessmimic_core` first, then run:

```bash
source <repo-root>/.venv/bin/activate
cd Training
python -m pytest
```

Tests marked `cpp` require the C++ extension. Tests marked `data` use tracked
sample data or skip when an optional local build artifact is missing.

## Environment Setup

The `setup_build_env.sh` script is automatically sourced by other build scripts and:
- Activates the virtual environment
- Sets up Python paths
- Exports common CMake arguments

You can source it manually for custom builds:
```bash
source setup_build_env.sh
```
