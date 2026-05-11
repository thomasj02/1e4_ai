#!/usr/bin/env python3
"""
Generate clock bucket boundaries using auto-extending linear-quantile algorithm.

This implementation focuses on a single, optimized bucketing scheme:
- Linear buckets with width 1.0 for low thinking times
- Auto-extends linear threshold to capture all naturally occurring width-1 buckets
- Equal-probability quantile buckets for the tail distribution
"""

import argparse
import json
import numpy as np
from pathlib import Path
from typing import List, Dict, Tuple
import sys
from tqdm import tqdm

from bagz import BagFileReader


def load_thinking_times(bagz_path: str, max_samples: int = 1_000_000) -> np.ndarray:
    """Load thinking times from bagz file, with optional sampling."""
    reader = BagFileReader(bagz_path)
    total_records = len(reader)
    
    print(f"Loading thinking times from {bagz_path}")
    print(f"Total records: {total_records:,}")
    
    # Sample if needed
    if total_records > max_samples:
        import random
        print(f"Sampling {max_samples:,} random records...")
        indices = random.sample(range(total_records), max_samples)
    else:
        indices = range(total_records)
        
    thinking_times = []
    for idx in tqdm(indices, desc="Loading records", unit="rec"):
        record_bytes = reader[idx]
        record = json.loads(record_bytes.decode('utf-8'))
        thinking_times.append(record['thinking_time'])
        
    return np.array(thinking_times)


def detect_time_control(thinking_times: np.ndarray) -> str:
    """Auto-detect time control based on thinking time distribution."""
    p95_time = np.percentile(thinking_times, 95)
    
    if p95_time < 5:
        return "bullet"
    elif p95_time < 20:
        return "blitz"
    elif p95_time < 60:
        return "rapid"
    else:
        return "classical"


def get_time_control_params(time_control: str) -> Dict[str, float]:
    """Get initial parameters for the given time control."""
    params = {
        "bullet": {"linear_threshold": 3, "linear_width": 0.5},
        "blitz": {"linear_threshold": 10, "linear_width": 1.0},
        "rapid": {"linear_threshold": 20, "linear_width": 2.0},
        "classical": {"linear_threshold": 30, "linear_width": 3.0}
    }
    return params.get(time_control, params["blitz"])


def generate_linear_boundaries(threshold: float, width: float) -> List[float]:
    """Generate linear boundaries up to threshold with given width."""
    n_buckets = int(threshold / width)
    return [i * width for i in range(n_buckets + 1)]


def generate_tail_boundaries(
    tail_samples: np.ndarray,
    n_boundaries: int,
    last_linear_boundary: float
) -> List[float]:
    """
    Generate equal-probability boundaries for tail samples.
    
    Returns n_boundaries unique integer boundaries.
    """
    if len(tail_samples) == 0 or n_boundaries <= 0:
        return []
    
    # Generate percentiles that create equal-probability buckets
    percentiles = np.linspace(0, 100, n_boundaries + 2)[1:-1]
    percentile_values = np.percentile(tail_samples, percentiles)
    
    # Convert to unique integers
    boundaries = []
    last_boundary = last_linear_boundary
    
    for val in percentile_values:
        # Round to nearest integer
        int_val = int(np.round(val))
        
        # Ensure monotonic increase
        if int_val <= last_boundary:
            int_val = int(last_boundary) + 1
        
        # Only add if unique
        if not boundaries or int_val > boundaries[-1]:
            boundaries.append(float(int_val))
            last_boundary = int_val
    
    return boundaries


def find_extended_threshold(
    boundaries: List[float], 
    linear_width: float,
    initial_threshold: float
) -> float:
    """
    Find the maximum threshold where all buckets have width equal to linear_width.
    """
    extended_threshold = initial_threshold
    
    for i in range(1, len(boundaries) - 1):
        width = boundaries[i] - boundaries[i-1]
        
        # Allow small tolerance for floating point comparison
        if abs(width - linear_width) < 0.01:
            extended_threshold = boundaries[i]
        elif width > linear_width + 0.01:
            # Found first bucket with width > linear_width
            break
    
    return extended_threshold


def generate_boundaries(
    thinking_times: np.ndarray,
    n_buckets: int,
    time_control: str | None = None,
    linear_threshold_override: float | None = None
) -> Tuple[List[float], Dict[str, float]]:
    """
    Generate bucket boundaries using auto-extending linear-quantile algorithm.
    
    Returns:
        boundaries: List of bucket boundaries
        params: Dictionary of parameters used
    """
    # Detect time control if not specified
    if time_control is None:
        time_control = detect_time_control(thinking_times)
    
    # Get initial parameters
    params = get_time_control_params(time_control)
    
    # Override linear threshold if specified
    if linear_threshold_override is not None:
        params["linear_threshold"] = linear_threshold_override
    
    print(f"\nGenerating buckets for {time_control} time control")
    print(f"Initial linear threshold: {params['linear_threshold']}s")
    
    # First pass: generate boundaries with initial threshold
    initial_linear_boundaries = generate_linear_boundaries(
        params["linear_threshold"], 
        params["linear_width"]
    )
    
    # Get tail samples for initial threshold
    tail_mask = thinking_times > params["linear_threshold"]
    tail_samples = thinking_times[tail_mask]
    
    # Calculate remaining boundaries needed
    n_linear = len(initial_linear_boundaries) - 1  # Number of linear buckets
    n_tail = n_buckets - n_linear - 1  # -1 for infinity bucket
    
    # Generate initial tail boundaries
    initial_tail_boundaries = generate_tail_boundaries(
        tail_samples, n_tail, initial_linear_boundaries[-1]
    )
    
    # Combine for initial pass
    all_boundaries = initial_linear_boundaries + initial_tail_boundaries
    
    # Find extended threshold
    extended_threshold = find_extended_threshold(
        all_boundaries, 
        params["linear_width"], 
        params["linear_threshold"]
    )
    
    # Second pass if we can extend
    if extended_threshold > params["linear_threshold"]:
        print(f"Extending linear threshold from {params['linear_threshold']} to {extended_threshold}")
        params["linear_threshold"] = extended_threshold
        
        # Regenerate with extended threshold
        linear_boundaries = generate_linear_boundaries(
            extended_threshold, 
            params["linear_width"]
        )
        
        # Get new tail samples
        tail_mask = thinking_times > extended_threshold
        tail_samples = thinking_times[tail_mask]
        
        # Recalculate boundaries needed
        n_linear = len(linear_boundaries) - 1
        n_tail = n_buckets - n_linear - 1  # -1 for infinity
        
        # Generate new tail boundaries
        tail_boundaries = generate_tail_boundaries(
            tail_samples, n_tail, linear_boundaries[-1]
        )
        
        boundaries = linear_boundaries + tail_boundaries
    else:
        boundaries = all_boundaries
    
    # Add infinity as final boundary
    boundaries.append(float('inf'))
    
    # Ensure we have exactly n_buckets + 1 boundaries
    if len(boundaries) > n_buckets + 1:
        boundaries = boundaries[:n_buckets + 1]
    
    # Print summary
    print(f"Final linear threshold: {params['linear_threshold']}s")
    print(f"Linear buckets: {n_linear}")
    print(f"Tail buckets: {n_buckets - n_linear} (including infinity)")
    print(f"Tail samples: {len(tail_samples):,} ({len(tail_samples)/len(thinking_times)*100:.1f}%)")
    
    return boundaries, params


def calculate_bucket_empirical_distributions(
    thinking_times: np.ndarray,
    boundaries: List[float],
    max_frequent_values: int = 20
) -> List[Dict]:
    """
    Calculate empirical distributions within each bucket.
    
    For buckets < 10 seconds wide: percentage of each second
    For buckets >= 10 seconds wide: percentage of most frequent values
    """
    n_buckets = len(boundaries) - 1
    bucket_distributions = []
    
    for i in range(n_buckets):
        low, high = boundaries[i], boundaries[i+1]
        
        # Get samples in this bucket
        if high == float('inf'):
            bucket_mask = thinking_times >= low
        else:
            bucket_mask = (thinking_times >= low) & (thinking_times < high)
        
        bucket_samples = thinking_times[bucket_mask]
        
        if len(bucket_samples) == 0:
            bucket_distributions.append({
                "type": "empty",
                "distribution": {}
            })
            continue
        
        # Calculate bucket width
        if high == float('inf'):
            # For infinity bucket, use most frequent values approach
            width = float('inf')
        else:
            width = high - low
        
        if width < 10:
            # For narrow buckets, calculate percentage for each second
            distribution = {}
            
            # Generate all integer seconds in the range
            start_second = int(np.floor(low))
            end_second = int(np.ceil(high)) if high != float('inf') else int(np.ceil(low + 10))
            
            for second in range(start_second, end_second):
                # Count samples that round to this second
                count = np.sum(np.round(bucket_samples) == second)
                if count > 0:
                    percentage = (count / len(bucket_samples)) * 100
                    distribution[str(second)] = round(percentage, 2)
            
            bucket_distributions.append({
                "type": "seconds",
                "distribution": distribution
            })
        else:
            # For wide buckets, find most frequent values
            # Round to nearest 0.1 second for granularity
            rounded_samples = np.round(bucket_samples, 1)
            unique_values, counts = np.unique(rounded_samples, return_counts=True)
            
            # Sort by frequency
            sorted_indices = np.argsort(counts)[::-1]
            
            # Take top values up to max_frequent_values
            distribution = {}
            total_count = len(bucket_samples)
            cumulative_percentage = 0
            
            for idx in sorted_indices[:max_frequent_values]:
                value = unique_values[idx]
                count = counts[idx]
                percentage = (count / total_count) * 100
                cumulative_percentage += percentage
                
                # Store as string key with percentage value
                distribution[f"{value:.1f}"] = round(percentage, 2)
                
                # Stop if we've covered 95% of samples
                if cumulative_percentage > 95:
                    break
            
            bucket_distributions.append({
                "type": "frequent_values",
                "distribution": distribution,
                "coverage": round(cumulative_percentage, 2)
            })
    
    return bucket_distributions


def calculate_statistics(
    thinking_times: np.ndarray,
    boundaries: List[float],
    max_frequent_values: int = 20
) -> Dict:
    """Calculate bucket statistics and probabilities."""
    n_buckets = len(boundaries) - 1
    
    # Calculate bucket widths
    widths = []
    for i in range(n_buckets):
        if boundaries[i+1] == float('inf'):
            widths.append(None)
        else:
            widths.append(boundaries[i+1] - boundaries[i])
    
    # Calculate sample distribution
    bucket_counts = np.histogram(thinking_times, bins=boundaries)[0]
    bucket_probs = bucket_counts / bucket_counts.sum()
    
    # Calculate empirical distributions for each bucket
    bucket_distributions = calculate_bucket_empirical_distributions(
        thinking_times, boundaries, max_frequent_values
    )
    
    # Summary statistics
    stats = {
        "total_samples": len(thinking_times),
        "mean_time": float(np.mean(thinking_times)),
        "median_time": float(np.median(thinking_times)),
        "p95_time": float(np.percentile(thinking_times, 95)),
        "p99_time": float(np.percentile(thinking_times, 99)),
    }
    
    return {
        "bucket_probabilities": bucket_probs.tolist(),
        "bucket_widths": widths,
        "bucket_empirical_distributions": bucket_distributions,
        "statistics": stats
    }


def main():
    parser = argparse.ArgumentParser(
        description="Generate clock bucket boundaries using auto-extending linear-quantile algorithm"
    )
    parser.add_argument(
        "--bagz_path",
        required=True,
        help="Path to clock bagz file"
    )
    parser.add_argument(
        "--output_path",
        required=True,
        help="Output path for boundaries JSON file"
    )
    parser.add_argument(
        "--n-buckets",
        type=int,
        default=30,
        help="Target number of buckets (default: 30)"
    )
    parser.add_argument(
        "--time-control",
        choices=["bullet", "blitz", "rapid", "classical"],
        help="Override auto-detected time control"
    )
    parser.add_argument(
        "--max-samples",
        type=int,
        default=1_000_000,
        help="Maximum samples to analyze (default: 1M)"
    )
    parser.add_argument(
        "--linear-threshold",
        type=float,
        help="Override initial linear threshold"
    )
    parser.add_argument(
        "--max-frequent-values",
        type=int,
        default=20,
        help="Maximum frequent values to track for wide buckets (default: 20)"
    )
    
    args = parser.parse_args()
    
    # Check input file
    if not Path(args.bagz_path).exists():
        print(f"Error: Input file not found: {args.bagz_path}")
        sys.exit(1)
    
    # Load thinking times
    thinking_times = load_thinking_times(args.bagz_path, args.max_samples)
    
    # Generate boundaries
    boundaries, params = generate_boundaries(
        thinking_times,
        args.n_buckets,
        args.time_control,
        args.linear_threshold
    )
    
    # Calculate statistics
    stats_data = calculate_statistics(thinking_times, boundaries, args.max_frequent_values)
    
    # Build result
    result = {
        "boundaries": boundaries,
        "n_buckets": len(boundaries) - 1,
        "scheme": "linear_quantile",
        "time_control": args.time_control or detect_time_control(thinking_times),
        **stats_data
    }
    
    # Save results
    output_path = Path(args.output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    
    with open(output_path, 'w') as f:
        json.dump(result, f, indent=2)
    print(f"\nBoundaries saved to {output_path}")
    
    # Print summary
    print("\nBucket boundaries and distributions:")
    for i in range(len(boundaries) - 1):
        low, high = boundaries[i], boundaries[i+1]
        prob = result["bucket_probabilities"][i]
        width = result["bucket_widths"][i]
        dist_info = result["bucket_empirical_distributions"][i]
        
        if high == float('inf'):
            print(f"  Bucket {i:2d}: [{low:6.1f}, ∞) - {prob*100:5.2f}%")
        else:
            print(f"  Bucket {i:2d}: [{low:6.1f}, {high:6.1f}) width={width:5.1f} - {prob*100:5.2f}%")
        
        # Print empirical distribution
        if dist_info["type"] == "empty":
            print(f"    No samples in bucket")
        elif dist_info["type"] == "seconds":
            print(f"    Second-by-second distribution:")
            sorted_seconds = sorted(dist_info["distribution"].items(), key=lambda x: int(x[0]))
            for second, percent in sorted_seconds:
                print(f"      {second}s: {percent}%")
        elif dist_info["type"] == "frequent_values":
            print(f"    Most frequent values (covering {dist_info.get('coverage', 0)}% of samples):")
            sorted_values = sorted(dist_info["distribution"].items(), key=lambda x: x[1], reverse=True)
            for value, percent in sorted_values[:10]:  # Show top 10
                print(f"      {value}s: {percent}%")


if __name__ == "__main__":
    main()