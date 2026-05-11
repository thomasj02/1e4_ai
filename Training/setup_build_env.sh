#!/bin/bash
# Common setup script for build environment
# Source this script from other build scripts to ensure consistent environment

# Activate virtual environment if it exists
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VENV_PATH="$REPO_ROOT/.venv"
if [ -f "$VENV_PATH/bin/activate" ]; then
    echo "Activating virtual environment..."
    source "$VENV_PATH/bin/activate"
    export VENV_PYTHON=$(which python)
    echo "Using Python: $VENV_PYTHON"
    echo "nanobind location: $(python -m nanobind --cmake_dir 2>/dev/null || echo 'Not found')"
else
    echo "Warning: Virtual environment not found at $VENV_PATH"
    echo "Some builds may fail without the required Python packages."
fi

# Export common CMake arguments
export CMAKE_COMMON_ARGS=""
if [ -n "$VIRTUAL_ENV" ]; then
    CMAKE_COMMON_ARGS="-DPython_EXECUTABLE=$VENV_PYTHON"
fi

# Function to configure CMake with virtual environment
configure_cmake() {
    local BUILD_TYPE=${1:-Release}
    local EXTRA_ARGS=${2:-}
    
    if [ -n "$VIRTUAL_ENV" ]; then
        echo "Configuring CMake with virtual environment Python..."
        cmake .. -DCMAKE_BUILD_TYPE=$BUILD_TYPE $CMAKE_COMMON_ARGS $EXTRA_ARGS
    else
        echo "Configuring CMake with system Python..."
        cmake .. -DCMAKE_BUILD_TYPE=$BUILD_TYPE $EXTRA_ARGS
    fi
}

echo "Build environment setup complete."
