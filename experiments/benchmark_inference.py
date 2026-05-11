#!/usr/bin/env python3
"""Benchmark script to compare CPU vs CUDA performance for model inference."""

import time
import os
import torch
import numpy as np
from pathlib import Path
import chess
from collections import defaultdict

import sys

PROJECT_ROOT = Path(__file__).resolve().parents[1]
BACKEND_DIR = PROJECT_ROOT / "backend"
sys.path.insert(0, str(BACKEND_DIR))

from model_inference import ModelMovePredictor
from common_moves_db import load_common_moves_db


def generate_test_positions(num_positions: int = 100) -> list[tuple[str, list[str], int, float]]:
    """Generate diverse test positions for benchmarking.
    
    Returns list of (fen, move_history, rating, clock_time) tuples.
    """
    positions = []
    
    # Start from initial position and play random games
    for i in range(num_positions):
        board = chess.Board()
        move_history = []
        
        # Play random number of moves (5-40)
        num_moves = np.random.randint(5, 40)
        for _ in range(num_moves):
            legal_moves = list(board.legal_moves)
            if not legal_moves:
                break
            
            move = np.random.choice(legal_moves)
            move_history.append(move.uci())
            board.push(move)
        
        # Random rating between 1000-2500
        rating = np.random.randint(1000, 2500)
        
        # Random clock time between 10-600 seconds
        clock_time = np.random.uniform(10, 600)
        
        positions.append((board.fen(), move_history, rating, clock_time))
    
    return positions


def benchmark_inference(model_predictor: ModelMovePredictor, 
                       positions: list[tuple[str, list[str], int, float]],
                       num_runs: int = 3) -> dict:
    """Benchmark model inference on test positions.
    
    Returns dict with timing statistics.
    """
    times = []
    
    print(f"Running {num_runs} benchmark iterations on {len(positions)} positions...")
    
    for run in range(num_runs):
        start_time = time.perf_counter()
        
        for fen, move_history, rating, clock_time in positions:
            # Only time the actual prediction
            move, info = model_predictor.predict_move(
                fen, move_history, rating, clock_time, min_probability=0.01
            )
        
        end_time = time.perf_counter()
        elapsed = end_time - start_time
        times.append(elapsed)
        
        print(f"  Run {run + 1}: {elapsed:.3f}s ({elapsed/len(positions)*1000:.2f}ms per position)")
    
    times = np.array(times)
    
    return {
        "total_positions": len(positions),
        "num_runs": num_runs,
        "times": times,
        "mean_time": np.mean(times),
        "std_time": np.std(times),
        "min_time": np.min(times),
        "max_time": np.max(times),
        "mean_per_position_ms": np.mean(times) / len(positions) * 1000,
        "std_per_position_ms": np.std(times) / len(positions) * 1000,
    }


def benchmark_with_device(model_path: Path, scalers_path: Path, positions: list, 
                         force_cpu: bool = False) -> tuple[str, dict]:
    """Run benchmark with specified device settings."""
    if force_cpu:
        # Create a modified ModelMovePredictor that forces CPU
        class CPUModelMovePredictor(ModelMovePredictor):
            def __init__(self, model_path, scalers_path):
                # Override device selection
                original_cuda_available = torch.cuda.is_available
                torch.cuda.is_available = lambda: False
                super().__init__(model_path, scalers_path)
                torch.cuda.is_available = original_cuda_available
        
        model_predictor = CPUModelMovePredictor(model_path, scalers_path)
        device_name = "CPU"
    else:
        model_predictor = ModelMovePredictor(model_path, scalers_path)
        device_name = "CUDA" if model_predictor.device.type == "cuda" else "CPU"
    
    print(f"Using device: {model_predictor.device}")
    stats = benchmark_inference(model_predictor, positions, num_runs=3)
    
    # Clean up model to free memory
    del model_predictor
    if torch.cuda.is_available():
        torch.cuda.empty_cache()
    
    return device_name, stats


def main():
    """Run benchmarks comparing CPU vs CUDA performance."""
    
    # Check current device
    has_cuda = torch.cuda.is_available()
    print(f"CUDA available: {has_cuda}")
    if has_cuda:
        print(f"CUDA device: {torch.cuda.get_device_name(0)}")
    
    # Load common moves database first (not included in benchmark timing)
    print("\nLoading common moves database...")
    common_moves_db = load_common_moves_db()
    
    # Generate test positions
    print("\nGenerating test positions...")
    positions = generate_test_positions(num_positions=100)
    print(f"Generated {len(positions)} test positions")
    
    # Paths
    model_dir = Path(os.getenv(
        "CHESSMIMIC_EXPERIMENT_MOVE_MODEL_DIR",
        PROJECT_ROOT / "backend" / "models" / "move_model" / "1800_1900_brier",
    ))
    model_path = model_dir / "model.ckpt"
    scalers_path = model_dir / "scalers.pkl"
    
    results = {}
    
    # Benchmark with current settings (CUDA if available)
    print("\n" + "="*60)
    print("BENCHMARK WITH CURRENT SETTINGS")
    print("="*60)
    
    device_name, stats = benchmark_with_device(model_path, scalers_path, positions, force_cpu=False)
    results[device_name] = stats
    
    # Force CPU and benchmark
    if has_cuda:
        print("\n" + "="*60)
        print("BENCHMARK WITH CPU FORCED")
        print("="*60)
        
        device_name, stats = benchmark_with_device(model_path, scalers_path, positions, force_cpu=True)
        results["CPU"] = stats
    
    # Analysis
    print("\n" + "="*60)
    print("PERFORMANCE COMPARISON")
    print("="*60)
    
    for device, stats in results.items():
        print(f"\n{device}:")
        print(f"  Mean time per run: {stats['mean_time']:.3f}s ± {stats['std_time']:.3f}s")
        print(f"  Mean time per position: {stats['mean_per_position_ms']:.2f}ms ± {stats['std_per_position_ms']:.2f}ms")
        print(f"  Min/Max time per run: {stats['min_time']:.3f}s / {stats['max_time']:.3f}s")
    
    if "CUDA" in results and "CPU" in results:
        cuda_mean = results["CUDA"]["mean_per_position_ms"]
        cpu_mean = results["CPU"]["mean_per_position_ms"]
        speedup = cpu_mean / cuda_mean
        
        print(f"\nSpeedup Analysis:")
        print(f"  CUDA is {speedup:.2f}x {'faster' if speedup > 1 else 'slower'} than CPU")
        print(f"  Time difference per position: {abs(cuda_mean - cpu_mean):.2f}ms")
        
        # For 1000 moves in a game
        print(f"\nProjected time for 1000 moves:")
        print(f"  CUDA: {cuda_mean * 1000 / 1000:.1f} seconds")
        print(f"  CPU:  {cpu_mean * 1000 / 1000:.1f} seconds")
        print(f"  Difference: {abs(cuda_mean - cpu_mean) * 1000 / 1000:.1f} seconds")


if __name__ == "__main__":
    main()
