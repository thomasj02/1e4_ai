# Training Module Build System

## Important: Always use the virtual environment
The build system requires the virtual environment to find nanobind and other dependencies:
```bash
source <repo-root>/.venv/bin/activate
```

## Quick Build Commands

### Configure and Build Everything
```bash
# Configure with Ninja (recommended)
./cmake_configure.sh --ninja

# Build all targets
cd build && ninja
```

### Build Specific Targets
```bash
# Clock data converter
cd build && ninja pgn_to_clock_bagz

# Python extension
cd build && ninja chessmimic_core

# Tests
cd build && ninja run_tests
```

## Build Scripts

- **`cmake_configure.sh`**: Main configuration script
  - `--ninja`: Use Ninja instead of Make (faster)
  - `--debug`: Debug build
  - `--release`: Release build (default)
  - `--relwithdebinfo`: Release with debug symbols

- **`build_and_run_tests.sh`**: Build and run all unit tests
- **`build_clock_tests.sh`**: Build clock converter and run specific tests
- **`build_cpp_ext.sh`**: Build Python extension with optimizations
- **`build_extract_common_positions.sh`**: Build common positions extractor

## Common Issues

### "Could not find nanobind"
This means CMake is using system Python instead of virtual environment Python.
Solution: Ensure the virtual environment is activated before running any build script.

### Manual CMake Configuration
If build scripts fail, configure manually:
```bash
source <repo-root>/.venv/bin/activate
mkdir -p build && cd build
cmake .. -DPython_EXECUTABLE=$(which python) -DCMAKE_BUILD_TYPE=Release -GNinja
ninja
```

## Testing
After building, run tests:
```bash
./build_and_run_tests.sh
# or for specific tests:
./build/run_tests --gtest_filter="ClockDataConverterTest.*"
```

## Terminal Tips
- If you're ever unsure about what directory you're in, use `pwd` to confirm the directory