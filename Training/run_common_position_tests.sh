#!/bin/bash

# Build and run tests for common position extractor

set -e  # Exit on error

echo "Building tests..."

# Create build directory if it doesn't exist
mkdir -p build
cd build

# Configure with CMake using Ninja
cmake .. -DCMAKE_BUILD_TYPE=Debug -G Ninja

# Build the test executable
ninja run_tests

echo "Running common position extractor tests..."

# Run only the common position extractor tests
./run_tests --gtest_filter="CommonPositionExtractorTest.*"

echo ""
echo "To run all tests: ./build/run_tests"
echo "To run with verbose output: ./build/run_tests --gtest_filter='CommonPositionExtractorTest.*' --gtest_print_time=1"