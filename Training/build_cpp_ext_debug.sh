#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$SCRIPT_DIR"

# Activate virtual environment
source "$REPO_ROOT/.venv/bin/activate"

# Create build directory if it doesn't exist
mkdir -p build
cd build

# Get virtual environment Python path
VENV_PYTHON=$(which python)

# Configure CMake with Ninja generator in Release mode with aggressive optimization
cmake -GNinja -DCMAKE_BUILD_TYPE=Debug -DPython_EXECUTABLE="$VENV_PYTHON" ..

# Build the extension module
ninja

# Copy the built extension to the parent directory and the chessmimic_core package directory
for SO_FILE in $(find . -name "*.so"); do
    # Copy to parent directory
    cp "$SO_FILE" ..
    # Copy to chessmimic_core package directory
    mkdir -p ../chessmimic_core
    cp "$SO_FILE" ../chessmimic_core/
done

echo "Build completed successfully!"
