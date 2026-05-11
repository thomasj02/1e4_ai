#!/bin/bash
set -e

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

# Configure the project with CMake, ensuring it uses the virtual environment Python
if [ -n "$VIRTUAL_ENV" ]; then
    echo "Using Python from virtual environment: $(which python)"
    cmake .. -DPython_EXECUTABLE=$(which python)
else
    echo "Warning: No virtual environment active, using system Python"
    cmake ..
fi

# Build the project
cmake --build . -j$(nproc)

# Run the tests
./run_tests
