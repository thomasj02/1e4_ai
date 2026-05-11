#!/bin/bash

# Build the extract_common_positions executable

set -e  # Exit on error

echo "Building extract_common_positions..."

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$SCRIPT_DIR"

# Activate virtual environment if it exists
if [ -f "$REPO_ROOT/.venv/bin/activate" ]; then
    echo "Activating virtual environment..."
    source "$REPO_ROOT/.venv/bin/activate"
fi

# Create build directory if it doesn't exist
mkdir -p build
cd build

# Configure with CMake using Ninja, ensuring it uses the virtual environment Python
if [ -n "$VIRTUAL_ENV" ]; then
    echo "Using Python from virtual environment: $(which python)"
    cmake .. -DCMAKE_BUILD_TYPE=Release -G Ninja -DPython_EXECUTABLE=$(which python)
else
    echo "Warning: No virtual environment active, using system Python"
    cmake .. -DCMAKE_BUILD_TYPE=Release -G Ninja
fi

# Build the specific target
ninja extract_common_positions

echo "Build complete! Executable is at: build/extract_common_positions"
echo ""
echo "Usage: ./build/extract_common_positions <bagz_file> <output_file> [options]"
echo "Options:"
echo "  --threshold <n>       Minimum move count (default: 25)"
echo "  --chunk-size <n>      Records per chunk (default: 1000000)"
echo "  --max-records <n>     Maximum records to process (default: all)"
echo "  --temp-dir <path>     Temporary directory"
echo "  --threads <n>         Number of threads (default: auto)"
echo "  --keep-temp-files     Keep temporary files"
