#!/bin/bash
# Generate bucket boundaries for different time controls

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$SCRIPT_DIR"

# Activate virtual environment
source "$REPO_ROOT/.venv/bin/activate"

# Output directory
OUTPUT_DIR="data/clock_buckets"
DATA_ROOT="${CHESSMIMIC_DATA_ROOT:-data}"
CLOCK_TRAIN_BAGZ="${CHESSMIMIC_CLOCK_TRAIN_BAGZ:-$DATA_ROOT/clock_model/train.bagz}"
mkdir -p "$OUTPUT_DIR"

# Blitz (default for current data)
echo "Generating blitz buckets..."
python generate_clock_buckets.py \
    --bagz_path "$CLOCK_TRAIN_BAGZ" \
    --output_path $OUTPUT_DIR/blitz_buckets.json \
    --n-buckets 28 \
    --scheme linear_logarithmic \
    --time-control blitz

# Example commands for other time controls (when data is available)
echo ""
echo "Example commands for other time controls:"
echo ""

# Bullet
echo "# Bullet games (1-3 minute):"
echo "python generate_clock_buckets.py \\"
echo "    --bagz_path /path/to/bullet/train.bagz \\"
echo "    --output_path $OUTPUT_DIR/bullet_buckets.json \\"
echo "    --n-buckets 20 \\"
echo "    --scheme linear_logarithmic \\"
echo "    --time-control bullet"
echo ""

# Rapid  
echo "# Rapid games (10-30 minute):"
echo "python generate_clock_buckets.py \\"
echo "    --bagz_path /path/to/rapid/train.bagz \\"
echo "    --output_path $OUTPUT_DIR/rapid_buckets.json \\"
echo "    --n-buckets 32 \\"
echo "    --scheme linear_logarithmic \\"
echo "    --time-control rapid"
echo ""

# Classical
echo "# Classical games (30+ minute):"
echo "python generate_clock_buckets.py \\"
echo "    --bagz_path /path/to/classical/train.bagz \\"
echo "    --output_path $OUTPUT_DIR/classical_buckets.json \\"
echo "    --n-buckets 36 \\"
echo "    --scheme linear_logarithmic \\"
echo "    --time-control classical"
