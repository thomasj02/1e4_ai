#!/bin/bash
# Basic clock data conversion example
# Processes a single batch of PGN files into BAGZ format

set -e  # Exit on error

# Configuration
INPUT_DIR="data/pgn_files"
OUTPUT_FILE="data/clock_training.bagz"
THREADS=8

# Build the converter if needed
if [ ! -f "build/pgn_to_clock_bagz" ]; then
    echo "Building pgn_to_clock_bagz..."
    ./build_cpp_ext.sh
fi

# Run the converter
echo "Converting PGN files to clock BAGZ format..."
./build/pgn_to_clock_bagz \
    ${INPUT_DIR}/*.pgn.lz4 \
    --output ${OUTPUT_FILE} \
    --threads ${THREADS} \
    --verbose

echo "Conversion complete!"
echo "Output file: ${OUTPUT_FILE}"

# Show basic statistics
echo ""
echo "Verifying output..."
python3 -c "
from bagz import BagReader
reader = BagReader('${OUTPUT_FILE}')
count = sum(1 for _ in reader.read_records())
print(f'Total records: {count:,}')
"