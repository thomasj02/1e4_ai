#!/bin/bash
set -e

# Navigate to the script directory
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

# Configure with CMake, ensuring it uses the virtual environment Python
echo "Configuring with CMake..."
if [ -n "$VIRTUAL_ENV" ]; then
    echo "Using Python from virtual environment: $(which python)"
    cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPython_EXECUTABLE=$(which python) ..
else
    echo "Warning: No virtual environment active, using system Python"
    cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
fi

# Build only the tests and clock converter
echo "Building clock converter components and tests..."
cmake --build . --target run_tests --target pgn_to_clock_bagz -j$(nproc)

# Run tests with optional parameters
echo "Running tests..."
if [ $# -eq 0 ]; then
    # Default to clock-related tests if no parameters provided
    ./run_tests --gtest_filter="ClockPositionRecordTest.*:ClockDataConverterTest.*"
else
    # Pass all parameters to run_tests
    ./run_tests "$@"
fi
