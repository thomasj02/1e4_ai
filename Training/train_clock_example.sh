#!/bin/bash
# Example training script for discrete clock prediction model

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$SCRIPT_DIR"

# Activate virtual environment
source "$REPO_ROOT/.venv/bin/activate"

DATA_ROOT="${CHESSMIMIC_DATA_ROOT:-data}"
CLOCK_TRAIN_BAGZ="${CHESSMIMIC_CLOCK_TRAIN_BAGZ:-$DATA_ROOT/clock_model/train.bagz}"
CLOCK_VAL_BAGZ="${CHESSMIMIC_CLOCK_VAL_BAGZ:-$DATA_ROOT/clock_model/val.bagz}"

# Step 1: Generate bucket boundaries (if not already done)
echo "Generating bucket boundaries..."
python generate_clock_buckets.py \
    --bagz_path "$CLOCK_TRAIN_BAGZ" \
    --output_path data/clock_buckets/blitz_buckets_28.json \
    --n-buckets 28 \
    --scheme linear_logarithmic \
    --max-samples 1000000

# Step 2: Train the model
echo "Training discrete clock prediction model..."
python ClockTrainer.py \
    --train_bagz_path "$CLOCK_TRAIN_BAGZ" \
    --val_bagz_path "$CLOCK_VAL_BAGZ" \
    --scalers clock_scalers_final.pkl \
    --bucket_boundaries data/clock_buckets/blitz_buckets_28.json \
    --tensorboard_name clock_discrete_blitz_28 \
    --batch_size 2048 \
    --val_check_interval 25000 \
    --max_epochs 30

# Optional: Fine-tune from pre-trained PatzerModel
# --pretrained_checkpoint /path/to/patzer_model.ckpt \
# --fine_tune_lr 1e-4

# Optional: Enable wandb logging
# --wandb_checkpoint
