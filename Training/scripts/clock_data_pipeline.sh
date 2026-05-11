#!/bin/bash
# Full pipeline for generating clock training and validation data
# This script handles common position detection and filtering

set -e  # Exit on error

# Configuration
RATING_BAND="1700_1800"
TRAIN_MONTHS="202409 202410 202411 202412"
VAL_MONTHS="202501 202502"
DATA_ROOT="/data/chess/split_pgn"
OUTPUT_ROOT="/data/chess/clock_bagz"
THREADS=32
MAX_POSITIONS=25

# Ensure output directories exist
mkdir -p ${OUTPUT_ROOT}/train
mkdir -p ${OUTPUT_ROOT}/val
mkdir -p ${OUTPUT_ROOT}/common_positions

# Build the converter if needed
if [ ! -f "build/pgn_to_clock_bagz" ]; then
    echo "Building pgn_to_clock_bagz..."
    ./build_cpp_ext.sh
fi

# Step 1: Generate training data with common position detection
echo "=== Generating training data ==="
TRAIN_FILES=""
for month in ${TRAIN_MONTHS}; do
    TRAIN_FILES="${TRAIN_FILES} ${DATA_ROOT}/${RATING_BAND}/${month}/*.pgn.lz4"
done

echo "Processing training files from months: ${TRAIN_MONTHS}"
./build/pgn_to_clock_bagz \
    ${TRAIN_FILES} \
    --output ${OUTPUT_ROOT}/train/clock_${RATING_BAND}.bagz \
    --common-positions-with-history ${OUTPUT_ROOT}/common_positions/with_history_${RATING_BAND}.jsonl \
    --common-positions-fen-only ${OUTPUT_ROOT}/common_positions/fen_only_${RATING_BAND}.jsonl \
    --write-common-positions \
    --max-positions-per-key ${MAX_POSITIONS} \
    --threads ${THREADS} \
    --verbose

# Step 2: Generate validation data filtering common positions
echo ""
echo "=== Generating validation data ==="
VAL_FILES=""
for month in ${VAL_MONTHS}; do
    VAL_FILES="${VAL_FILES} ${DATA_ROOT}/${RATING_BAND}/${month}/*.pgn.lz4"
done

echo "Processing validation files from months: ${VAL_MONTHS}"
./build/pgn_to_clock_bagz \
    ${VAL_FILES} \
    --output ${OUTPUT_ROOT}/val/clock_${RATING_BAND}.bagz \
    --common-positions-with-history ${OUTPUT_ROOT}/common_positions/with_history_${RATING_BAND}.jsonl \
    --skip-common-positions \
    --threads ${THREADS} \
    --verbose

# Step 3: Generate statistics
echo ""
echo "=== Dataset Statistics ==="
python3 << EOF
from bagz import BagReader
import json

# Training statistics
train_reader = BagReader('${OUTPUT_ROOT}/train/clock_${RATING_BAND}.bagz')
train_count = sum(1 for _ in train_reader.read_records())
print(f"Training records: {train_count:,}")

# Validation statistics
val_reader = BagReader('${OUTPUT_ROOT}/val/clock_${RATING_BAND}.bagz')
val_count = sum(1 for _ in val_reader.read_records())
print(f"Validation records: {val_count:,}")

# Common position statistics
with open('${OUTPUT_ROOT}/common_positions/fen_only_${RATING_BAND}.jsonl', 'r') as f:
    common_count = sum(1 for line in f)
    f.seek(0)
    total_instances = sum(json.loads(line)['count'] for line in f)
    
print(f"\\nCommon positions (FEN only): {common_count:,}")
print(f"Total instances filtered: {total_instances:,}")

with open('${OUTPUT_ROOT}/common_positions/with_history_${RATING_BAND}.jsonl', 'r') as f:
    history_count = sum(1 for line in f)
    
print(f"Common positions (with history): {history_count:,}")
EOF

echo ""
echo "Pipeline complete!"
echo "Training data: ${OUTPUT_ROOT}/train/clock_${RATING_BAND}.bagz"
echo "Validation data: ${OUTPUT_ROOT}/val/clock_${RATING_BAND}.bagz"