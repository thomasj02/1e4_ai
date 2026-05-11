#!/bin/bash
# Convenience script to configure CMake with proper Python environment

set -e

# Source the build environment setup
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/setup_build_env.sh"

# Parse command line arguments
BUILD_TYPE="Release"
BUILD_DIR="build"
GENERATOR=""
EXTRA_ARGS=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --relwithdebinfo)
            BUILD_TYPE="RelWithDebInfo"
            shift
            ;;
        --ninja)
            GENERATOR="-G Ninja"
            shift
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        *)
            EXTRA_ARGS="$EXTRA_ARGS $1"
            shift
            ;;
    esac
done

# Create and enter build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
echo "Configuring CMake build..."
echo "  Build type: $BUILD_TYPE"
echo "  Build directory: $BUILD_DIR"
if [ -n "$GENERATOR" ]; then
    echo "  Generator: Ninja"
fi

if [ -n "$VIRTUAL_ENV" ]; then
    echo "  Python: $VENV_PYTHON (from virtual environment)"
    cmake .. -DCMAKE_BUILD_TYPE=$BUILD_TYPE $CMAKE_COMMON_ARGS $GENERATOR $EXTRA_ARGS
else
    echo "  Python: System default"
    echo "  Warning: Virtual environment not active, some features may not work"
    cmake .. -DCMAKE_BUILD_TYPE=$BUILD_TYPE $GENERATOR $EXTRA_ARGS
fi

echo ""
echo "Configuration complete! You can now build with:"
if [ -n "$GENERATOR" ]; then
    echo "  cd $BUILD_DIR && ninja"
else
    echo "  cd $BUILD_DIR && make -j\$(nproc)"
fi