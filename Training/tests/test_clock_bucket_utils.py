"""Tests for clock bucket utilities with empirical distributions."""

import json
import numpy as np
import pytest
from pathlib import Path
from clock_bucket_utils import ClockBucketInfo, load_bucket_info


class TestClockBucketInfoWithEmpiricalDistributions:
    """Test ClockBucketInfo with empirical distribution support."""
    
    def test_validates_empirical_distributions_exist(self):
        """Test that ClockBucketInfo validates all buckets have empirical distributions."""
        # Create bucket data with missing empirical distributions
        bucket_data = {
            'boundaries': [0.0, 1.0, 2.0, float('inf')],
            'n_buckets': 2,
            'scheme': 'test',
            'time_control': 'test',
            'statistics': {'mean_time': 1.5},
            'bucket_probabilities': [0.5, 0.5],
            # Missing bucket_empirical_distributions
        }
        
        # Should raise ValueError when empirical distributions are missing
        with pytest.raises(ValueError, match="bucket_empirical_distributions"):
            ClockBucketInfo(bucket_data)
    
    def test_validates_all_buckets_have_distributions(self):
        """Test that every bucket must have an empirical distribution."""
        # Create bucket data with incomplete empirical distributions
        bucket_data = {
            'boundaries': [0.0, 1.0, 2.0, 3.0, float('inf')],
            'n_buckets': 3,
            'scheme': 'test',
            'time_control': 'test',
            'statistics': {'mean_time': 1.5},
            'bucket_probabilities': [0.33, 0.33, 0.34],
            'bucket_empirical_distributions': [
                {'type': 'seconds', 'distribution': {'0': 100.0}},
                {'type': 'seconds', 'distribution': {'1': 100.0}},
                # Missing distribution for bucket 2
            ]
        }
        
        # Should raise ValueError when not all buckets have distributions
        with pytest.raises(ValueError, match="Expected 3 empirical distributions"):
            ClockBucketInfo(bucket_data)
    
    def test_sample_from_seconds_distribution(self):
        """Test sampling from a seconds-type empirical distribution."""
        bucket_data = {
            'boundaries': [0.0, 5.0, float('inf')],
            'n_buckets': 2,
            'scheme': 'test',
            'time_control': 'test',
            'statistics': {'mean_time': 2.5},
            'bucket_probabilities': [0.8, 0.2],
            'bucket_empirical_distributions': [
                {
                    'type': 'seconds',
                    'distribution': {
                        '0': 10.0,
                        '1': 20.0,
                        '2': 30.0,
                        '3': 25.0,
                        '4': 15.0
                    }
                },
                {
                    'type': 'frequent_values',
                    'distribution': {'5.0': 50.0, '10.0': 50.0}
                }
            ]
        }
        
        bucket_info = ClockBucketInfo(bucket_data)
        
        # Sample many times to check distribution
        samples = []
        for _ in range(10000):
            sample = bucket_info.sample_from_bucket_empirical(0)
            samples.append(sample)
        
        # Check all samples are valid seconds
        assert all(s in [0, 1, 2, 3, 4] for s in samples)
        
        # Check distribution roughly matches (with some tolerance)
        counts = {i: samples.count(i) for i in range(5)}
        total = len(samples)
        
        assert abs(counts[0] / total * 100 - 10.0) < 2.0  # ~10%
        assert abs(counts[1] / total * 100 - 20.0) < 2.0  # ~20%
        assert abs(counts[2] / total * 100 - 30.0) < 2.0  # ~30%
        assert abs(counts[3] / total * 100 - 25.0) < 2.0  # ~25%
        assert abs(counts[4] / total * 100 - 15.0) < 2.0  # ~15%
    
    def test_sample_from_frequent_values_distribution(self):
        """Test sampling from a frequent_values-type empirical distribution."""
        bucket_data = {
            'boundaries': [40.0, float('inf')],
            'n_buckets': 1,
            'scheme': 'test',
            'time_control': 'test',
            'statistics': {'mean_time': 50.0},
            'bucket_probabilities': [1.0],
            'bucket_empirical_distributions': [
                {
                    'type': 'frequent_values',
                    'distribution': {
                        '40.0': 25.0,
                        '45.5': 35.0,
                        '50.0': 20.0,
                        '60.0': 15.0,
                        '75.0': 5.0
                    },
                    'coverage': 100.0
                }
            ]
        }
        
        bucket_info = ClockBucketInfo(bucket_data)
        
        # Sample many times to check distribution
        samples = []
        for _ in range(10000):
            sample = bucket_info.sample_from_bucket_empirical(0)
            samples.append(sample)
        
        # Check all samples are valid values
        valid_values = [40.0, 45.5, 50.0, 60.0, 75.0]
        assert all(s in valid_values for s in samples)
        
        # Check distribution roughly matches
        counts = {v: samples.count(v) for v in valid_values}
        total = len(samples)
        
        assert abs(counts[40.0] / total * 100 - 25.0) < 2.0
        assert abs(counts[45.5] / total * 100 - 35.0) < 2.0
        assert abs(counts[50.0] / total * 100 - 20.0) < 2.0
        assert abs(counts[60.0] / total * 100 - 15.0) < 2.0
        assert abs(counts[75.0] / total * 100 - 5.0) < 2.0
    
    def test_sample_from_bucket_empirical_invalid_index(self):
        """Test that sampling with invalid bucket index raises error."""
        bucket_data = {
            'boundaries': [0.0, 1.0, float('inf')],
            'n_buckets': 1,
            'scheme': 'test',
            'time_control': 'test',
            'statistics': {'mean_time': 0.5},
            'bucket_probabilities': [1.0],
            'bucket_empirical_distributions': [
                {'type': 'seconds', 'distribution': {'0': 100.0}}
            ]
        }
        
        bucket_info = ClockBucketInfo(bucket_data)
        
        # Test invalid bucket indices
        with pytest.raises(ValueError, match="Invalid bucket index"):
            bucket_info.sample_from_bucket_empirical(-1)
        
        with pytest.raises(ValueError, match="Invalid bucket index"):
            bucket_info.sample_from_bucket_empirical(1)  # Only have bucket 0
    
    def test_empty_distribution_error(self):
        """Test that empty distribution dict raises error."""
        bucket_data = {
            'boundaries': [0.0, 1.0, float('inf')],
            'n_buckets': 1,
            'scheme': 'test',
            'time_control': 'test',
            'statistics': {'mean_time': 0.5},
            'bucket_probabilities': [1.0],
            'bucket_empirical_distributions': [
                {'type': 'seconds', 'distribution': {}}  # Empty!
            ]
        }
        
        with pytest.raises(ValueError, match="empty distribution"):
            ClockBucketInfo(bucket_data)
    
    def test_load_real_bucket_file(self):
        """Test loading the actual bucket file with empirical distributions."""
        project_root = Path(__file__).resolve().parents[1]
        bucket_files = sorted((project_root / "backend" / "models" / "clock_model").glob("*/clock_buckets.json"))
        bucket_path = bucket_files[0] if bucket_files else None
        
        if bucket_path and bucket_path.exists():
            # Should load without errors
            bucket_info = load_bucket_info(str(bucket_path))
            
            # Check it has empirical distributions
            assert hasattr(bucket_info, 'empirical_distributions')
            assert len(bucket_info.empirical_distributions) == bucket_info.n_buckets
            
            # Test sampling from each bucket
            for i in range(bucket_info.n_buckets):
                sample = bucket_info.sample_from_bucket_empirical(i)
                assert isinstance(sample, (int, float))
                
                # For non-infinity buckets, check sample is in range
                low, high = bucket_info.get_bucket_range(i)
                if high != float('inf'):
                    # Allow small tolerance for float precision
                    assert low - 0.1 <= sample <= high + 0.1
    
    def test_deterministic_sampling_with_seed(self):
        """Test that sampling is deterministic with a fixed random seed."""
        bucket_data = {
            'boundaries': [0.0, 5.0, float('inf')],
            'n_buckets': 2,
            'scheme': 'test',
            'time_control': 'test',
            'statistics': {'mean_time': 2.5},
            'bucket_probabilities': [0.8, 0.2],
            'bucket_empirical_distributions': [
                {
                    'type': 'seconds',
                    'distribution': {'0': 20.0, '1': 30.0, '2': 50.0}
                },
                {
                    'type': 'frequent_values',
                    'distribution': {'5.0': 60.0, '10.0': 40.0}
                }
            ]
        }
        
        bucket_info = ClockBucketInfo(bucket_data)
        
        # Sample with fixed seed
        np.random.seed(42)
        samples1 = [bucket_info.sample_from_bucket_empirical(0) for _ in range(100)]
        
        # Reset seed and sample again
        np.random.seed(42)
        samples2 = [bucket_info.sample_from_bucket_empirical(0) for _ in range(100)]
        
        # Should be identical
        assert samples1 == samples2


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
