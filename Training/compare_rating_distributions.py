#!/usr/bin/env python3
"""
Compare move probability distributions across rating bands (optimized with batching).

Finds positions with the largest differences between adjacent rating levels
by computing total variation distance between probability distributions.

Optimized with:
- PyTorch DataLoader for parallel data loading
- Batch inference for GPU efficiency
- torch.compile() for graph optimization
- Mixed precision (FP16) support
"""

import argparse
import csv
import heapq
import math
import os
import sys
from pathlib import Path
from typing import Optional

import numpy as np
import torch
import torch.nn.functional as F
from torch.utils.data import Dataset, DataLoader, ConcatDataset, Subset
from tqdm import tqdm

# Add backend to path to reuse model loading code
backend_path = Path(__file__).parent.parent / 'backend'
sys.path.insert(0, str(backend_path))

from model_inference import ModelMovePredictor
from chessmimic_core import MOVE_TO_ACTION, ACTION_TO_MOVE, NUM_ACTIONS, PAD_TOKEN, tokenize as tokenize_fen
from bagz import BagReader
import orjson
import chess
import gzip
import MoveDataset

# Create numpy array version of ACTION_TO_MOVE for faster lookups (~10x faster than dict.get())
ACTION_TO_MOVE_ARRAY = np.empty(NUM_ACTIONS, dtype=object)
for action_idx, move_uci in ACTION_TO_MOVE.items():
    ACTION_TO_MOVE_ARRAY[action_idx] = move_uci


def strip_fen_clocks(fen: str) -> str:
    """Strip move clocks from FEN for comparison with common_moves.

    Keeps only: position, active color, castling, en passant.
    Removes: halfmove clock, fullmove number.
    """
    parts = fen.split()
    if len(parts) >= 4:
        return ' '.join(parts[:4])
    return fen


def load_common_moves_fens(model_dir: Path) -> set[str]:
    """Load all FEN strings from common_moves file in model directory.

    Args:
        model_dir: Path to model directory (e.g., .../1000_1100_brier/)

    Returns:
        Set of FENs (stripped of move clocks) that are in common_moves
    """
    # Find common_moves file (could be .txt.gz or .jsonl.gz)
    common_moves_file = None
    for pattern in ["common_moves.jsonl.gz", "common_moves.txt.gz", "common_moves.txt", "common_moves.jsonl"]:
        candidate = model_dir / pattern
        if candidate.exists():
            common_moves_file = candidate
            break

    if not common_moves_file:
        return set()

    fens = set()
    open_func = gzip.open if common_moves_file.suffix == '.gz' else open
    mode = 'rt' if common_moves_file.suffix == '.gz' else 'r'

    try:
        with open_func(common_moves_file, mode) as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    data = orjson.loads(line)
                    fen_key = data['fen']

                    # Handle both formats (with/without move history)
                    try:
                        parsed_key = orjson.loads(fen_key)
                        if isinstance(parsed_key, list) and len(parsed_key) == 2:
                            fen = parsed_key[1]  # Extract FEN from [move_history, fen]
                        else:
                            fen = fen_key
                    except:
                        fen = fen_key

                    # Strip move clocks for consistency
                    fen = strip_fen_clocks(fen)
                    fens.add(fen)
                except Exception:
                    continue
    except Exception as e:
        print(f"  Warning: Could not load common_moves from {common_moves_file}: {e}")
        return set()

    return fens


class PositionDataset(Dataset):
    """Dataset for loading positions from bagz file for inference."""

    def __init__(self, bagz_path: Path, clock_time: float = 300.0):
        """Initialize the dataset.

        Args:
            bagz_path: Path to bagz file
            clock_time: Fixed clock time for all positions
        """
        self.bagz_path = str(bagz_path)
        self.clock_time = clock_time
        self.reader = BagReader(self.bagz_path)

    def __len__(self):
        return len(self.reader)

    def __getitem__(self, idx):
        """Get a single position with preprocessing.

        Returns:
            Tuple of (recent_moves_tokens, fen_tokens, move_mask, clock_time, fen_str, move_history)
            All heavy preprocessing (tokenization, chess.Board(), legal moves) done here
            in worker processes for parallelization.
        """
        entry = self.reader[idx]
        data = orjson.loads(entry)

        recent_and_fen = data.get('recent_and_fen', [])
        if len(recent_and_fen) != 2:
            # Return dummy data for malformed entries
            dummy_recent_moves = torch.zeros(12, dtype=torch.long)
            dummy_fen_tokens = torch.zeros(78, dtype=torch.long)
            dummy_move_mask = torch.zeros(NUM_ACTIONS, dtype=torch.float32)
            return (dummy_recent_moves, dummy_fen_tokens, dummy_move_mask, self.clock_time, "", [])

        recent_moves = recent_and_fen[0]
        fen = recent_and_fen[1]

        # Preprocess in worker process (parallelized!)
        # This does: tokenization, chess.Board() creation, legal move extraction
        recent_moves_tokens, fen_tokens, move_mask = \
            MoveDataset.BagzDataset.recent_moves_and_fen_to_inputs(recent_moves, fen)

        # Convert move_mask to tensor if it's a numpy array
        if isinstance(move_mask, np.ndarray):
            move_mask = torch.from_numpy(move_mask).float()

        # Keep FEN string and move history for output (needed for results)
        return (recent_moves_tokens, fen_tokens, move_mask, self.clock_time, fen, recent_moves)


def collate_fn(batch):
    """Custom collate function to stack pre-processed tensors."""
    recent_moves_batch = torch.stack([item[0] for item in batch])
    fen_tokens_batch = torch.stack([item[1] for item in batch])
    move_masks_batch = torch.stack([item[2] for item in batch])
    clock_times = torch.tensor([item[3] for item in batch], dtype=torch.float32)
    fens = [item[4] for item in batch]  # Keep FEN strings for output
    move_histories = [item[5] for item in batch]  # Keep move histories for output
    return recent_moves_batch, fen_tokens_batch, move_masks_batch, clock_times, fens, move_histories


class BatchModelLoader:
    """Batch-optimized model loader for inference."""

    def __init__(self, model_dir: Path, midpoint_rating: int, compile_model: bool = True, use_fp16: bool = False):
        """Initialize the batch model loader.

        Args:
            model_dir: Directory containing model.ckpt and scalers.pkl
            midpoint_rating: Midpoint rating for this model (e.g., 1050 for 1000-1100)
            compile_model: Whether to use torch.compile()
            use_fp16: Whether to use mixed precision (FP16)
        """
        model_path = model_dir / "model.ckpt"
        scalers_path = model_dir / "scalers.pkl"

        if not model_path.exists():
            raise FileNotFoundError(f"Model not found: {model_path}")
        if not scalers_path.exists():
            raise FileNotFoundError(f"Scalers not found: {scalers_path}")

        self.predictor = ModelMovePredictor(model_path, scalers_path)
        self.midpoint_rating = midpoint_rating
        self.model_dir = model_dir
        self.use_fp16 = use_fp16 and self.predictor.device.type == 'cuda'

        # Apply torch.compile() for optimization
        if compile_model:
            print(f"  Compiling model with torch.compile()...")
            self.predictor.model = torch.compile(self.predictor.model, mode="reduce-overhead")

        # Load common moves FENs
        print(f"  Loading common_moves...")
        self.common_moves_fens = load_common_moves_fens(model_dir)
        print(f"  Loaded {len(self.common_moves_fens):,} common FENs")

    def get_probabilities_batch(self, recent_moves_batch: torch.Tensor, fen_tokens_batch: torch.Tensor,
                                move_masks_batch: torch.Tensor, clock_times: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor]:
        """Get probability distributions for a batch of positions.

        Args:
            recent_moves_batch: Pre-tokenized recent moves [batch_size, 12]
            fen_tokens_batch: Pre-tokenized FENs [batch_size, 78]
            move_masks_batch: Pre-computed legal move masks [batch_size, NUM_ACTIONS]
            clock_times: Clock times [batch_size]

        Returns:
            Tuple of (probabilities, move_masks)
            Both tensors are [batch_size, NUM_ACTIONS] on GPU
        """
        batch_size = recent_moves_batch.size(0)
        device = self.predictor.device

        with torch.inference_mode():
            # Move tensors to device with non-blocking transfers (already pre-processed in workers!)
            recent_moves_batch = recent_moves_batch.to(device, non_blocking=True)
            fen_tokens_batch = fen_tokens_batch.to(device, non_blocking=True)
            move_masks_batch = move_masks_batch.to(device, non_blocking=True)

            # Vectorized rating scaling (all positions use same rating)
            scaled_rating = (self.midpoint_rating - self.predictor.rating_mean) / self.predictor.rating_std
            rating_batch = torch.full((batch_size,), scaled_rating, dtype=torch.float32, device=device)

            # Vectorized log time scaling
            log_times = torch.log1p(clock_times)  # log(1 + x)
            log_time_batch = ((log_times - self.predictor.log_time_mean) / self.predictor.log_time_std).to(device, non_blocking=True)

            # Single forward pass for entire batch
            if self.use_fp16:
                with torch.autocast(device_type='cuda', dtype=torch.float16):
                    logits = self.predictor.model(recent_moves_batch, fen_tokens_batch,
                                                  rating_batch, log_time_batch)
            else:
                logits = self.predictor.model(recent_moves_batch, fen_tokens_batch,
                                             rating_batch, log_time_batch)

            # Vectorized softmax and masking
            probabilities = F.softmax(logits, dim=1)
            probabilities = probabilities * move_masks_batch
            probabilities = probabilities / probabilities.sum(dim=1, keepdim=True)

            # Keep on GPU - no transfer yet!
            return probabilities, move_masks_batch


class BatchDistributionComparer:
    """Compare probability distributions between two rating levels (batch version)."""

    def __init__(self, lower_model: BatchModelLoader, higher_model: BatchModelLoader, batch_local_topk: Optional[int] = None):
        """Initialize the comparer.

        Args:
            lower_model: Model for lower rating band
            higher_model: Model for higher rating band
            batch_local_topk: Number of top positions to keep per batch (GPU filtering).
                            If None, keeps all positions (no filtering).
        """
        self.lower_model = lower_model
        self.higher_model = higher_model
        self.batch_local_topk = batch_local_topk

    def compare_positions_batch(self, batch) -> list[dict]:
        """Compare distributions for a batch of positions (GPU-optimized with minimal transfers).

        Args:
            batch: Tuple of (recent_moves_batch, fen_tokens_batch, move_masks_batch,
                   clock_times, fens, move_histories) from DataLoader

        Returns:
            List of dictionaries with comparison results
        """
        recent_moves_batch, fen_tokens_batch, move_masks_batch, clock_times, fens, move_histories = batch

        # Get probabilities as GPU tensors (kept on device!)
        lower_probs, lower_masks = self.lower_model.get_probabilities_batch(
            recent_moves_batch, fen_tokens_batch, move_masks_batch, clock_times)
        higher_probs, higher_masks = self.higher_model.get_probabilities_batch(
            recent_moves_batch, fen_tokens_batch, move_masks_batch, clock_times)

        # Vectorized total variation distance on GPU (BLAZING FAST!)
        # Shape: [batch_size]
        tvd_gpu = torch.sum(torch.abs(lower_probs - higher_probs), dim=1)

        # GPU-side filtering: keep only top-K positions per batch
        original_batch_size = len(fens)
        if self.batch_local_topk is not None and original_batch_size > self.batch_local_topk:
            # Find top-K positions in this batch on GPU
            batch_topk_tvd, batch_topk_indices = torch.topk(tvd_gpu, k=min(self.batch_local_topk, original_batch_size))

            # Filter GPU tensors using the indices
            tvd_gpu = batch_topk_tvd
            lower_probs = lower_probs[batch_topk_indices]
            higher_probs = higher_probs[batch_topk_indices]

            # Filter CPU lists using the indices
            batch_topk_indices_cpu = batch_topk_indices.cpu().numpy()
            fens = [fens[i] for i in batch_topk_indices_cpu]
            move_histories = [move_histories[i] for i in batch_topk_indices_cpu]

        # Extract top-5 moves on GPU using torch.topk
        # Returns: values [filtered_batch_size, 5], indices [filtered_batch_size, 5]
        lower_top5_values, lower_top5_indices = torch.topk(lower_probs, k=5, dim=1)
        higher_top5_values, higher_top5_indices = torch.topk(higher_probs, k=5, dim=1)

        # NOW transfer minimal data to CPU (only what we need for output!)
        # After GPU filtering, we transfer much less data!
        tvd_array = tvd_gpu.cpu().numpy()
        lower_top5_indices_cpu = lower_top5_indices.cpu().numpy()
        lower_top5_values_cpu = lower_top5_values.cpu().numpy()
        higher_top5_indices_cpu = higher_top5_indices.cpu().numpy()
        higher_top5_values_cpu = higher_top5_values.cpu().numpy()

        batch_size = len(fens)
        results = []

        # Convert top-5 indices to UCI moves (minimal lookups!)
        for i in range(batch_size):
            # Convert ONLY top-5 moves to UCI strings
            lower_top_moves = []
            for j in range(5):
                action_idx = lower_top5_indices_cpu[i, j]
                move_uci = ACTION_TO_MOVE_ARRAY[action_idx]
                if move_uci:
                    lower_top_moves.append((move_uci, float(lower_top5_values_cpu[i, j])))

            higher_top_moves = []
            for j in range(5):
                action_idx = higher_top5_indices_cpu[i, j]
                move_uci = ACTION_TO_MOVE_ARRAY[action_idx]
                if move_uci:
                    higher_top_moves.append((move_uci, float(higher_top5_values_cpu[i, j])))

            # Store FEN for later common_moves check (deferred until we know top-K)
            results.append({
                'fen': fens[i],
                'move_history': move_histories[i],
                'total_variation_distance': float(tvd_array[i]),
                'lower_top_moves': lower_top_moves,
                'higher_top_moves': higher_top_moves,
                'lower_rating': self.lower_model.midpoint_rating,
                'higher_rating': self.higher_model.midpoint_rating,
                # Common moves flags will be added later for top-K only
            })

        return results


def find_max_batch_size(model_loader: BatchModelLoader, start_size: int = 128):
    """Find the maximum batch size (power of 2) that fits in GPU memory.

    Args:
        model_loader: Model to test
        start_size: Starting batch size (should be a power of 2)

    Returns:
        Maximum batch size (power of 2) that fits
    """
    if model_loader.predictor.device.type == 'cpu':
        print("  Running on CPU, using default batch size")
        return 64

    print(f"  Finding maximum batch size (testing powers of 2)...")

    # Ensure start_size is a power of 2
    if start_size & (start_size - 1) != 0:
        # Round down to nearest power of 2
        start_size = 2 ** (start_size.bit_length() - 1)

    # Test with powers of 2
    current_size = start_size
    last_successful = start_size

    while True:
        try:
            torch.cuda.empty_cache()

            # Create dummy tensors (pre-processed format)
            dummy_recent_moves = torch.zeros((current_size, 12), dtype=torch.long)
            dummy_fen_tokens = torch.zeros((current_size, 78), dtype=torch.long)
            dummy_move_mask = torch.zeros((current_size, NUM_ACTIONS), dtype=torch.float32)
            dummy_move_mask[:, 0] = 1.0  # At least one legal move
            dummy_clock_times = torch.full((current_size,), 300.0, dtype=torch.float32)

            # Try inference
            _ = model_loader.get_probabilities_batch(
                dummy_recent_moves, dummy_fen_tokens, dummy_move_mask, dummy_clock_times)

            last_successful = current_size
            print(f"    Batch size {current_size}: OK")
            current_size *= 2  # Next power of 2

        except RuntimeError as e:
            if "out of memory" in str(e).lower():
                print(f"    Batch size {current_size}: OOM")
                break
            else:
                raise

    # Return the largest power of 2 that didn't OOM
    print(f"  Maximum batch size: {last_successful}")

    return last_successful


def filter_dataset_by_move_number(dataset: Dataset, min_move_number: int) -> Dataset:
    """Filter dataset to only include positions with fullmove number >= min_move_number.

    Uses fast C++ implementation for scanning bagz files with progress tracking.

    Args:
        dataset: PositionDataset or ConcatDataset to filter
        min_move_number: Minimum fullmove number (positions before this are excluded)

    Returns:
        Subset of dataset containing only valid positions
    """
    if min_move_number <= 1:
        return dataset  # No filtering needed

    print(f"\nFiltering positions with fullmove number >= {min_move_number}...")

    valid_indices = []

    # Import C++ extension for fast filtering
    from chessmimic_core import filter_bagz_by_fullmove

    # Handle both single dataset and ConcatDataset
    if hasattr(dataset, 'datasets'):  # ConcatDataset
        print(f"Filtering {len(dataset.datasets)} bagz files...\n")
        cumulative = 0
        for i, sub_dataset in enumerate(dataset.datasets):
            bagz_path = sub_dataset.bagz_path
            total = len(sub_dataset)

            # Create progress bar for this bagz file
            with tqdm(total=total, desc=f"  [{i+1}/{len(dataset.datasets)}] {Path(bagz_path).name}",
                     unit="pos", unit_scale=True) as pbar:

                # Progress callback for tqdm
                def update_progress(current, total_records):
                    pbar.n = current
                    pbar.refresh()

                # Call C++ function with progress callback
                local_valid_indices = filter_bagz_by_fullmove(bagz_path, min_move_number, update_progress)

            # Adjust indices for concatenated dataset
            global_indices = [cumulative + idx for idx in local_valid_indices]
            valid_indices.extend(global_indices)

            print(f"    Kept {len(local_valid_indices):,} / {total:,} positions\n")
            cumulative += total
    else:  # Regular PositionDataset
        bagz_path = dataset.bagz_path
        total = len(dataset)

        # Create progress bar
        with tqdm(total=total, desc=f"  Filtering {Path(bagz_path).name}",
                 unit="pos", unit_scale=True) as pbar:

            # Progress callback for tqdm
            def update_progress(current, total_records):
                pbar.n = current
                pbar.refresh()

            # Call C++ function with progress callback
            valid_indices = filter_bagz_by_fullmove(bagz_path, min_move_number, update_progress)

    total_positions = len(dataset)
    filtered_positions = len(valid_indices)
    discarded = total_positions - filtered_positions

    print(f"\nFiltering complete:")
    print(f"  Total positions: {total_positions:,}")
    print(f"  Kept (move >= {min_move_number}): {filtered_positions:,}")
    print(f"  Discarded: {discarded:,} ({discarded/total_positions*100:.1f}%)")

    return Subset(dataset, valid_indices)


def parse_rating_pair(pair_str: str) -> tuple[str, str, int, int]:
    """Parse rating pair string like '1000-1100,1100-1200'.

    Returns:
        Tuple of (lower_band, higher_band, lower_midpoint, higher_midpoint)
    """
    parts = pair_str.split(',')
    if len(parts) != 2:
        raise ValueError(f"Invalid rating pair format: {pair_str}. Expected 'X-Y,Y-Z'")

    lower_band = parts[0].strip()
    higher_band = parts[1].strip()

    # Parse midpoints
    lower_parts = lower_band.split('-')
    higher_parts = higher_band.split('-')

    lower_min = int(lower_parts[0])
    lower_max = int(lower_parts[1])
    higher_min = int(higher_parts[0])
    higher_max = int(higher_parts[1])

    lower_midpoint = (lower_min + lower_max) // 2
    higher_midpoint = (higher_min + higher_max) // 2

    return lower_band, higher_band, lower_midpoint, higher_midpoint


def format_moves_for_csv(moves_list: list[tuple[str, float]]) -> str:
    """Format top moves for CSV output."""
    return '; '.join([f"{move}:{prob:.4f}" for move, prob in moves_list])


def write_csv_output(results: list[dict], output_path: Path):
    """Write results to CSV file."""
    with open(output_path, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow([
            'fen', 'recent_moves', 'rating_pair', 'total_variation_distance',
            'top5_lower_rating', 'top5_higher_rating',
            'in_lower_common_moves', 'in_higher_common_moves'
        ])

        for result in results:
            rating_pair = f"{result['lower_rating']}-{result['higher_rating']}"
            recent_moves_str = ','.join(result['move_history'] or [])

            writer.writerow([
                result['fen'],
                recent_moves_str,
                rating_pair,
                f"{result['total_variation_distance']:.6f}",
                format_moves_for_csv(result['lower_top_moves']),
                format_moves_for_csv(result['higher_top_moves']),
                result.get('in_lower_common_moves', False),
                result.get('in_higher_common_moves', False)
            ])


def format_single_position_text(result: dict, rank: int) -> str:
    """Format a single position for text output.

    Args:
        result: Dictionary with position comparison results
        rank: Position rank/number (e.g., 1 for best)

    Returns:
        Formatted string for this position
    """
    lines = []
    lines.append("=" * 80)
    lines.append(f"Position #{rank}")
    lines.append(f"FEN: {result['fen']}")
    if result['move_history']:
        lines.append(f"Recent moves: {' '.join(result['move_history'])}")
    lines.append(f"Total Variation Distance: {result['total_variation_distance']:.6f}")
    lines.append(f"Rating comparison: {result['lower_rating']} vs {result['higher_rating']}")

    # Add common_moves information
    lower_common = "Yes" if result.get('in_lower_common_moves', False) else "No"
    higher_common = "Yes" if result.get('in_higher_common_moves', False) else "No"
    lines.append(f"In lower model common_moves: {lower_common}")
    lines.append(f"In higher model common_moves: {higher_common}")
    lines.append("")

    # Side-by-side top moves
    lines.append(f"{'Lower Rating (' + str(result['lower_rating']) + ')':^38} | {'Higher Rating (' + str(result['higher_rating']) + ')':^38}")
    lines.append("-" * 40 + "|" + "-" * 40)

    for j in range(5):
        lower_move = result['lower_top_moves'][j] if j < len(result['lower_top_moves']) else ('', 0.0)
        higher_move = result['higher_top_moves'][j] if j < len(result['higher_top_moves']) else ('', 0.0)

        lower_str = f"{lower_move[0]:6} {lower_move[1]:6.2%}" if lower_move[0] else ""
        higher_str = f"{higher_move[0]:6} {higher_move[1]:6.2%}" if higher_move[0] else ""

        lines.append(f"{lower_str:^38} | {higher_str:^38}")

    lines.append("=" * 80)
    return "\n".join(lines)


def write_text_output(results: list[dict], output_path: Path):
    """Write results to human-readable text file."""
    with open(output_path, 'w') as f:
        f.write("=" * 80 + "\n")
        f.write("POSITIONS WITH LARGEST DISTRIBUTION DIFFERENCES\n")
        f.write("=" * 80 + "\n\n")

        for i, result in enumerate(results, 1):
            f.write(format_single_position_text(result, i) + "\n\n")


def main():
    parser = argparse.ArgumentParser(
        description='Compare move probability distributions across rating bands (optimized)')
    parser.add_argument('--bagz', required=True, type=Path, nargs='+',
                       help='One or more bagz files containing positions')
    parser.add_argument('--rating-pairs', nargs='+', required=True,
                       help='Rating pairs to compare (e.g., "1000-1100,1100-1200")')
    parser.add_argument('--clock-time', type=float, default=300.0,
                       help='Clock time in seconds (default: 300)')
    parser.add_argument('--top-k', type=int, default=100,
                       help='Number of top positions to output (default: 100)')
    parser.add_argument('--output-dir', type=Path, default=Path('./rating_comparison_results'),
                       help='Output directory for results')
    parser.add_argument('--models-dir', type=Path,
                       default=Path(__file__).parent.parent / 'backend' / 'models' / 'move_model',
                       help='Directory containing model folders')
    parser.add_argument('--limit', type=int, default=None,
                       help='Limit number of positions to process (for testing)')
    parser.add_argument('--batch-size', type=int, default=8192,
                       help='Batch size for inference (default: 8192)')
    parser.add_argument('--auto-batch-size', action='store_true',
                       help='Automatically find maximum batch size')
    parser.add_argument('--num-workers', type=int, default=None,
                       help='Number of data loading workers (default: cpu_count)')
    parser.add_argument('--no-compile', action='store_true',
                       help='Disable torch.compile() optimization')
    parser.add_argument('--fp16', action='store_true',
                       help='Use mixed precision (FP16) for inference')
    parser.add_argument('--prefetch-factor', type=int, default=4,
                       help='Number of batches to prefetch per worker (default: 4)')
    parser.add_argument('--batch-local-topk', type=int, default=None,
                       help='Per-batch GPU filtering: keep only top-N positions per batch before CPU transfer. '
                            'Reduces transfer overhead. Default: top_k * 10 (auto-calculated)')
    parser.add_argument('--min-move-number', type=int, default=1,
                       help='Minimum fullmove number to process (filters out early-game positions, default: 1)')

    args = parser.parse_args()

    # Auto-calculate batch_local_topk if not specified
    if args.batch_local_topk is None:
        args.batch_local_topk = max(args.top_k * 10, 1000)

    # Set PyTorch optimizations
    torch.set_float32_matmul_precision('high')

    # Set num_workers (cap at 12 to avoid memory pressure with prefetching)
    if args.num_workers is None:
        args.num_workers = min(os.cpu_count(), 12)

    # Create output directory
    args.output_dir.mkdir(parents=True, exist_ok=True)

    # Process each rating pair
    for pair_str in args.rating_pairs:
        print(f"\n{'='*80}")
        print(f"Processing rating pair: {pair_str}")
        print(f"{'='*80}\n")

        # Parse rating pair
        lower_band, higher_band, lower_midpoint, higher_midpoint = parse_rating_pair(pair_str)

        # Construct model directory names
        lower_model_dir = args.models_dir / f"{lower_band.replace('-', '_')}_brier"
        higher_model_dir = args.models_dir / f"{higher_band.replace('-', '_')}_brier"

        # Load models
        print(f"Loading lower rating model ({lower_band}) from {lower_model_dir}...")
        lower_model = BatchModelLoader(lower_model_dir, lower_midpoint,
                                       compile_model=not args.no_compile,
                                       use_fp16=args.fp16)

        print(f"Loading higher rating model ({higher_band}) from {higher_model_dir}...")
        higher_model = BatchModelLoader(higher_model_dir, higher_midpoint,
                                        compile_model=not args.no_compile,
                                        use_fp16=args.fp16)

        # Auto-detect batch size if requested
        if args.auto_batch_size:
            args.batch_size = find_max_batch_size(lower_model, start_size=args.batch_size)

        print(f"\nConfiguration:")
        print(f"  Batch size: {args.batch_size}")
        print(f"  Batch-local top-K (GPU filtering): {args.batch_local_topk}")
        print(f"  Num workers: {args.num_workers}")
        print(f"  torch.compile: {not args.no_compile}")
        print(f"  Mixed precision (FP16): {args.fp16}")
        print(f"  Device: {lower_model.predictor.device}")

        # Create dataset and dataloader
        bagz_paths = list(args.bagz)
        if len(bagz_paths) == 1:
            print(f"\nCreating dataset from: {bagz_paths[0]}")
        else:
            print(f"\nCreating dataset from {len(bagz_paths)} bagz files:")
            for path in bagz_paths:
                print(f"  - {path}")

        datasets = [PositionDataset(path, clock_time=args.clock_time) for path in bagz_paths]
        dataset: Dataset
        if len(datasets) == 1:
            dataset = datasets[0]
        else:
            dataset = ConcatDataset(datasets)

        total_available = len(dataset)
        print(f"Dataset size: {total_available} positions")

        # Limit dataset size if requested
        if args.limit is not None:
            limit = min(args.limit, total_available)
            if limit < total_available:
                print(f"Limiting dataset to first {limit} positions")
                dataset = Subset(dataset, range(limit))
                print(f"Limited dataset size: {len(dataset)} positions")
            else:
                print(f"Limit {args.limit} covers all available positions; using full dataset")

        # Filter by minimum move number if requested
        if args.min_move_number > 1:
            dataset = filter_dataset_by_move_number(dataset, args.min_move_number)

        dataloader = DataLoader(
            dataset,
            batch_size=args.batch_size,
            shuffle=False,
            pin_memory=True,
            persistent_workers=True if args.num_workers > 0 else False,
            num_workers=args.num_workers,
            prefetch_factor=args.prefetch_factor if args.num_workers > 0 else None,
            collate_fn=collate_fn,
            drop_last=False
        )

        # Create comparer
        comparer = BatchDistributionComparer(lower_model, higher_model, batch_local_topk=args.batch_local_topk)

        # Process all batches using a min-heap to keep only top K results
        # This keeps memory usage constant at O(K) instead of O(N)
        print(f"\nProcessing positions in batches of {args.batch_size}...")

        # Min-heap: stores tuples of (tvd, index, result_dict)
        # Python heapq is a min-heap, so we keep the K largest TVD values
        top_k_heap = []
        result_index = 0
        total_positions = 0
        total_positions_after_filtering = 0
        current_max_tvd = 0.0

        for batch in tqdm(dataloader, desc="Analyzing positions"):
            original_batch_size = len(batch[4])  # batch[4] is fens list
            batch_results = comparer.compare_positions_batch(batch)
            total_positions += original_batch_size
            total_positions_after_filtering += len(batch_results)

            for result in batch_results:
                tvd = result['total_variation_distance']

                # Check if this is a new maximum TVD
                if tvd > current_max_tvd:
                    # Immediately check common_moves membership for this new maximum
                    fen_stripped = strip_fen_clocks(result['fen'])
                    result['in_lower_common_moves'] = fen_stripped in lower_model.common_moves_fens
                    result['in_higher_common_moves'] = fen_stripped in higher_model.common_moves_fens

                    # Print the new maximum using tqdm.write (doesn't disrupt progress bar)
                    tqdm.write("\n" + "!" * 80)
                    tqdm.write(f"NEW MAXIMUM TVD: {tvd:.6f}")
                    tqdm.write("!" * 80)
                    tqdm.write(format_single_position_text(result, 1))
                    tqdm.write("\n")

                    current_max_tvd = tvd

                if len(top_k_heap) < args.top_k:
                    # Heap not full yet, add this result
                    heapq.heappush(top_k_heap, (tvd, result_index, result))
                    result_index += 1
                elif tvd > top_k_heap[0][0]:
                    # This result has higher TVD than the minimum in our top-K
                    # Replace the minimum with this result
                    heapq.heapreplace(top_k_heap, (tvd, result_index, result))
                    result_index += 1

        # Extract results from heap and sort descending by TVD
        # The heap is a min-heap, so we need to sort to get descending order
        top_results = sorted([item[2] for item in top_k_heap],
                           key=lambda x: x['total_variation_distance'],
                           reverse=True)

        print(f"\nFound {total_positions} valid positions")
        if args.batch_local_topk:
            positions_filtered = total_positions - total_positions_after_filtering
            filter_percentage = (positions_filtered / total_positions * 100) if total_positions > 0 else 0
            transfer_reduction = (total_positions / total_positions_after_filtering) if total_positions_after_filtering > 0 else 1
            print(f"GPU filtering: {total_positions_after_filtering:,} positions kept ({filter_percentage:.1f}% filtered)")
            print(f"Transfer reduction: {transfer_reduction:.1f}x")
        print(f"Outputting top {len(top_results)} positions")

        # NOW check common_moves for only the top-K results (not all N positions!)
        print(f"Checking common_moves membership for top {len(top_results)} positions...")
        for result in top_results:
            fen_stripped = strip_fen_clocks(result['fen'])
            result['in_lower_common_moves'] = fen_stripped in lower_model.common_moves_fens
            result['in_higher_common_moves'] = fen_stripped in higher_model.common_moves_fens

        # Generate output filenames
        pair_name = f"{lower_band.replace('-', '_')}_vs_{higher_band.replace('-', '_')}"
        csv_output = args.output_dir / f"{pair_name}_top{args.top_k}.csv"
        text_output = args.output_dir / f"{pair_name}_top{args.top_k}.txt"

        # Write outputs
        print(f"\nWriting CSV output to: {csv_output}")
        write_csv_output(top_results, csv_output)

        print(f"Writing text output to: {text_output}")
        write_text_output(top_results, text_output)

        # Print summary statistics
        if top_results:
            print(f"\nSummary Statistics:")
            print(f"  Max TVD: {top_results[0]['total_variation_distance']:.6f}")
            print(f"  Min TVD (in top {args.top_k}): {top_results[-1]['total_variation_distance']:.6f}")
            print(f"  Mean TVD (in top {args.top_k}): {np.mean([r['total_variation_distance'] for r in top_results]):.6f}")

    print(f"\n{'='*80}")
    print(f"All comparisons complete! Results saved to: {args.output_dir}")
    print(f"{'='*80}\n")


if __name__ == '__main__':
    main()
