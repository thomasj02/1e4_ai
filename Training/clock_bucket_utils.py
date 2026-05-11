"""Utilities for working with clock bucket boundaries."""

import json
import numpy as np
from pathlib import Path
from typing import List, Dict, Tuple, Optional


class ClockBucketInfo:
    """Container for clock bucket information."""
    
    def __init__(self, bucket_data: Dict):
        """Initialize from loaded bucket data."""
        # Convert None to float('inf') in boundaries
        self.boundaries = []
        for b in bucket_data['boundaries']:
            if b is None:
                self.boundaries.append(float('inf'))
            else:
                self.boundaries.append(float(b))
                
        self.n_buckets = bucket_data['n_buckets']
        self.scheme = bucket_data['scheme']
        self.time_control = bucket_data['time_control']
        self.statistics = bucket_data['statistics']
        self.bucket_probabilities = np.array(bucket_data['bucket_probabilities'])
        
        # Load and validate empirical distributions
        if 'bucket_empirical_distributions' not in bucket_data:
            raise ValueError("bucket_empirical_distributions missing from bucket data")
        
        self.empirical_distributions = bucket_data['bucket_empirical_distributions']
        
        # Validate we have distributions for all buckets
        if len(self.empirical_distributions) != self.n_buckets:
            raise ValueError(f"Expected {self.n_buckets} empirical distributions, "
                           f"but got {len(self.empirical_distributions)}")
        
        # Validate each distribution
        for i, dist in enumerate(self.empirical_distributions):
            if 'type' not in dist:
                raise ValueError(f"bucket {i} missing 'type' field")
            if 'distribution' not in dist:
                raise ValueError(f"bucket {i} missing 'distribution' field")
            if not dist['distribution']:
                raise ValueError(f"bucket {i} has empty distribution")
        
        # Precompute bucket widths
        self.bucket_widths = []
        for i in range(self.n_buckets):
            if self.boundaries[i+1] == float('inf'):
                self.bucket_widths.append(float('inf'))
            else:
                self.bucket_widths.append(self.boundaries[i+1] - self.boundaries[i])
                
    def get_bucket_index(self, thinking_time: float) -> int:
        """Get bucket index for a thinking time."""
        idx = np.searchsorted(self.boundaries[:-1], thinking_time, side='right') - 1
        return np.clip(idx, 0, self.n_buckets - 1)
        
    def get_bucket_center(self, bucket_idx: int) -> float:
        """Get center value for a bucket."""
        if bucket_idx < 0 or bucket_idx >= self.n_buckets:
            raise ValueError(f"Invalid bucket index: {bucket_idx}")
            
        low = self.boundaries[bucket_idx]
        high = self.boundaries[bucket_idx + 1]
        
        if high == float('inf'):
            # For infinite bucket, use low + expected tail value
            tail_means = {
                'bullet': 10,
                'blitz': 30,
                'rapid': 60,
                'classical': 120
            }
            return low + tail_means.get(self.time_control, 30)
        else:
            return (low + high) / 2
            
    def get_bucket_range(self, bucket_idx: int) -> Tuple[float, float]:
        """Get range [low, high) for a bucket."""
        if bucket_idx < 0 or bucket_idx >= self.n_buckets:
            raise ValueError(f"Invalid bucket index: {bucket_idx}")
            
        return self.boundaries[bucket_idx], self.boundaries[bucket_idx + 1]
        
    def get_class_weights(self, epsilon: float = 1e-6) -> np.ndarray:
        """
        Compute inverse frequency class weights.
        
        Args:
            epsilon: Small value to avoid division by zero
            
        Returns:
            Array of class weights normalized to mean 1.0
        """
        weights = 1.0 / (self.bucket_probabilities + epsilon)
        return weights / weights.mean()
        
    def sample_from_bucket(self, bucket_idx: int) -> float:
        """
        Sample a time value from within a bucket.
        
        For finite buckets, samples uniformly.
        For infinite bucket, uses exponential distribution.
        """
        low, high = self.get_bucket_range(bucket_idx)
        
        if high == float('inf'):
            # Exponential distribution for tail
            tail_means = {
                'bullet': 10,
                'blitz': 30,
                'rapid': 60, 
                'classical': 120
            }
            mean_time = tail_means.get(self.time_control, 30)
            return low + np.random.exponential(mean_time)
        else:
            # Uniform within bucket
            return np.random.uniform(low, high)
    
    def sample_from_bucket_empirical(self, bucket_idx: int) -> float:
        """
        Sample a time value from within a bucket using empirical distribution.
        
        Args:
            bucket_idx: Index of the bucket to sample from
            
        Returns:
            Sampled thinking time value
            
        Raises:
            ValueError: If bucket index is invalid
        """
        if bucket_idx < 0 or bucket_idx >= self.n_buckets:
            raise ValueError(f"Invalid bucket index: {bucket_idx}")
        
        dist_info = self.empirical_distributions[bucket_idx]
        dist_type = dist_info['type']
        distribution = dist_info['distribution']
        
        if dist_type == 'seconds':
            # For seconds distribution, keys are integer seconds as strings
            # Convert to proper format for sampling
            seconds = []
            probabilities = []
            
            for sec_str, prob in distribution.items():
                seconds.append(int(sec_str))
                probabilities.append(prob)
            
            # Normalize probabilities to sum to 1
            probabilities = np.array(probabilities) / sum(probabilities)
            
            # Sample from the distribution
            return float(np.random.choice(seconds, p=probabilities))
            
        elif dist_type == 'frequent_values':
            # For frequent values, keys are float values as strings
            values = []
            probabilities = []
            
            for val_str, prob in distribution.items():
                values.append(float(val_str))
                probabilities.append(prob)
            
            # Normalize probabilities to sum to 1
            probabilities = np.array(probabilities) / sum(probabilities)
            
            # Sample from the distribution
            return float(np.random.choice(values, p=probabilities))
            
        else:
            raise ValueError(f"Unknown distribution type: {dist_type}")
            
    def __repr__(self) -> str:
        return (f"ClockBucketInfo(n_buckets={self.n_buckets}, "
                f"time_control={self.time_control}, "
                f"scheme={self.scheme})")


def load_bucket_info(path: str) -> ClockBucketInfo:
    """
    Load bucket information from JSON file.
    
    Args:
        path: Path to bucket boundaries JSON file
        
    Returns:
        ClockBucketInfo object
    """
    with open(path, 'r') as f:
        bucket_data = json.load(f)
    return ClockBucketInfo(bucket_data)


def print_bucket_summary(bucket_info: ClockBucketInfo) -> None:
    """Print a summary of the bucket scheme."""
    print(f"Clock Bucket Summary: {bucket_info}")
    print("="*70)
    print(f"{'Bucket':>6} {'Range':>20} {'Center':>8} {'Width':>8} {'Prob':>8}")
    print("-"*70)
    
    for i in range(bucket_info.n_buckets):
        low, high = bucket_info.get_bucket_range(i)
        center = bucket_info.get_bucket_center(i)
        width = bucket_info.bucket_widths[i]
        prob = bucket_info.bucket_probabilities[i]
        
        if high == float('inf'):
            range_str = f"[{low:.1f}, ∞)"
            width_str = "∞"
        else:
            range_str = f"[{low:.1f}, {high:.1f})"
            width_str = f"{width:.1f}"
            
        print(f"{i:6d} {range_str:>20} {center:8.1f} {width_str:>8} {prob:8.3f}")
        
    print("-"*70)
    print(f"Statistics: mean={bucket_info.statistics['mean_time']:.1f}s, "
          f"median={bucket_info.statistics['median_time']:.1f}s, "
          f"p95={bucket_info.statistics['p95_time']:.1f}s")


if __name__ == "__main__":
    # Test with generated bucket file
    import sys
    
    if len(sys.argv) > 1:
        bucket_path = sys.argv[1]
    else:
        bucket_path = "data/clock_buckets/blitz_buckets.json"
        
    if Path(bucket_path).exists():
        bucket_info = load_bucket_info(bucket_path)
        print_bucket_summary(bucket_info)
        
        # Test bucket assignment
        print("\nTest bucket assignments:")
        test_times = [0, 0.5, 1, 2.5, 5, 10, 15, 25, 45, 75, 150, 300]
        for t in test_times:
            idx = bucket_info.get_bucket_index(t)
            center = bucket_info.get_bucket_center(idx)
            low, high = bucket_info.get_bucket_range(idx)
            print(f"  {t:6.1f}s -> bucket {idx:2d} center={center:5.1f}")
    else:
        print(f"Bucket file not found: {bucket_path}")
        print("Generate it with: python generate_clock_buckets.py")