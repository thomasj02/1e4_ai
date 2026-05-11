import argparse
import json
import pickle
import numpy as np
import math
import random
from tqdm.auto import tqdm
from bagz import BagReader


def collect_rating_statistics(bag_reader, num_entries, num_rating_samples=1000000):
    """
    Collects rating statistics from random samples across the bagz file.
    
    Args:
        bag_reader: BagReader object for the bagz file
        num_entries: Total number of entries in the bagz file
        num_rating_samples: Number of rating samples to collect
        
    Returns:
        dict: Rating statistics including mean, std, and count
    """
    print(f"\nSampling {num_rating_samples} ratings from across the database...")
    
    # Adjust sample size if needed
    num_rating_samples = min(num_rating_samples, num_entries * 10)  # Arbitrary upper limit based on entries
    
    # Collect ratings from random positions
    all_ratings = []
    samples_collected = 0
    stats = {}
    
    # Generate random indices for sampling
    if num_entries > 0:
        # Estimate number of indices needed (we may get multiple ratings per entry)
        estimated_indices_needed = min(num_entries, num_rating_samples // 2)
        indices = sorted(random.sample(range(num_entries), estimated_indices_needed))
        
        # Get the randomly sampled entries
        for idx in tqdm(indices, desc="Sampling entries for ratings"):
            try:
                # Get the record at the target index
                record = bag_reader[idx]
                
                # Parse the record
                record_data = json.loads(record)
                
                # Move data should be in the "moves" field
                if "moves" in record_data and isinstance(record_data["moves"], dict):
                    value_obj = record_data["moves"]
                    
                    # Extract ratings from this position
                    for move, clock_data in value_obj.items():
                        for clock_str, rating_data in clock_data.items():
                            try:
                                # For each rating in this clock time
                                for rating_str, count in rating_data.items():
                                    rating = int(rating_str)
                                    
                                    # Add the rating to its list count times
                                    all_ratings.extend([rating] * count)
                                    samples_collected += count
                                    
                                    # Break early if we've collected enough samples
                                    if samples_collected >= num_rating_samples:
                                        break
                            except (ValueError, TypeError):
                                continue
                        
                        # Break early if we've collected enough samples
                        if samples_collected >= num_rating_samples:
                            break
                    
                    # Break early if we've collected enough samples
                    if samples_collected >= num_rating_samples:
                        break
            except Exception as e:
                print(f"Error processing entry at index {idx}: {e}")
                continue
    
    # Calculate rating statistics
    if all_ratings:
        # Take a subset if we collected too many
        if len(all_ratings) > num_rating_samples:
            all_ratings = random.sample(all_ratings, num_rating_samples)
        
        mean = np.mean(all_ratings)
        std = np.std(all_ratings)
        count = len(all_ratings)
        
        stats['mean'] = float(mean)
        stats['std'] = float(std)
        stats['count'] = int(count)
        
        print(f"Rating statistics (from {count} samples):")
        print(f"  Mean: {mean:.2f}")
        print(f"  Standard Deviation: {std:.2f}")
        print(f"  Count: {count}")
    else:
        print("No ratings found in the samples.")
    
    return stats


def collect_clock_time_statistics(bag_reader, num_entries, num_clock_samples):
    """
    Collects clock time statistics from random samples across the bagz file.
    
    Args:
        bag_reader: BagReader object for the bagz file
        num_entries: Total number of entries in the bagz file
        num_clock_samples: Number of clock time samples to collect
        
    Returns:
        dict: Dictionary with log_time and raw_time statistics
    """
    print(f"\nSampling {num_clock_samples} clock times from across the database...")
    
    # Adjust sample size if needed
    num_clock_samples = min(num_clock_samples, num_entries * 10)  # Arbitrary upper limit based on entries
    
    # Collect log times from random positions
    all_log_times = []
    samples_collected = 0
    stats = {'log_time': {}, 'raw_time': {}}
    
    # Generate random indices for sampling
    if num_entries > 0:
        # Estimate number of indices needed (we may get multiple clock times per entry)
        estimated_indices_needed = min(num_entries, num_clock_samples // 2)
        indices = sorted(random.sample(range(num_entries), estimated_indices_needed))
        
        # Get the randomly sampled entries
        for idx in tqdm(indices, desc="Sampling entries for clock times"):
            try:
                # Get the record at the target index
                record = bag_reader[idx]
                
                # Parse the record
                record_data = json.loads(record)
                
                # Move data should be in the "moves" field
                if "moves" in record_data and isinstance(record_data["moves"], dict):
                    value_obj = record_data["moves"]
                    
                    # Extract clock times from this position
                    for move, clock_data in value_obj.items():
                        for clock_str, rating_data in clock_data.items():
                            try:
                                # Convert clock time from string to float and calculate log(1+time)
                                clock_time = float(clock_str)
                                log_time = math.log(1 + clock_time)
                                
                                # Get the total count for this clock time
                                total_count = sum(rating_data.values())
                                
                                # Add the log_time to its list count times
                                all_log_times.extend([log_time] * total_count)
                                samples_collected += total_count
                                
                                # Break early if we've collected enough samples
                                if samples_collected >= num_clock_samples:
                                    break
                            except (ValueError, TypeError):
                                continue
                        
                        # Break early if we've collected enough samples
                        if samples_collected >= num_clock_samples:
                            break
                    
                    # Break early if we've collected enough samples
                    if samples_collected >= num_clock_samples:
                        break
            except Exception as e:
                print(f"Error processing entry at index {idx}: {e}")
    
    # Calculate log time statistics
    if all_log_times:
        # Take a subset if we collected too many
        if len(all_log_times) > num_clock_samples:
            all_log_times = random.sample(all_log_times, num_clock_samples)
        
        mean = np.mean(all_log_times)
        std = np.std(all_log_times)
        count = len(all_log_times)
        
        stats['log_time']['mean'] = float(mean)
        stats['log_time']['std'] = float(std)
        stats['log_time']['count'] = int(count)
        
        print(f"\nLog(1+time) statistics (from {count} samples):")
        print(f"  Mean: {mean:.4f}")
        print(f"  Standard Deviation: {std:.4f}")
        print(f"  Count: {count}")
        
        # Also include the raw time stats for reference
        all_times = [math.exp(log_t) - 1 for log_t in all_log_times]
        stats['raw_time'] = {
            'mean': float(np.mean(all_times)),
            'std': float(np.std(all_times)),
            'min': float(np.min(all_times)),
            'max': float(np.max(all_times)),
            'median': float(np.median(all_times))
        }
        
        print(f"Raw time statistics (seconds):")
        print(f"  Mean: {stats['raw_time']['mean']:.2f}")
        print(f"  Standard Deviation: {stats['raw_time']['std']:.2f}")
        print(f"  Minimum: {stats['raw_time']['min']:.2f}")
        print(f"  Maximum: {stats['raw_time']['max']:.2f}")
        print(f"  Median: {stats['raw_time']['median']:.2f}")
    else:
        print("No clock times found in the samples.")
    
    return stats


def calculate_statistics(bagz_path, output_path, num_rating_samples=1000000, num_clock_samples=1000000):
    """
    Loads data from a bagz file and calculates the mean and standard deviation of:
    1. Ratings from random samples across the database
    2. log(1+time) for clock times from random samples across the database
    
    Saves the means, standard deviations, and counts in a dictionary to a pickle file.
    
    Args:
        bagz_path: Path to the bagz file
        output_path: Path to save the pickle file
        num_rating_samples: Number of rating samples to collect
        num_clock_samples: Number of clock time samples to collect
    """
    # Open the bagz file
    print(f"Opening bagz file: {bagz_path}")
    bag_reader = BagReader(bagz_path)
    num_entries = len(bag_reader)
    print(f"Bagz file contains {num_entries} entries")
    
    # Create dictionary to store the statistics
    stats = {
        'rating': {},
        'log_time': {}
    }
    
    # Collect rating statistics
    rating_stats = collect_rating_statistics(bag_reader, num_entries, num_rating_samples)
    if rating_stats:
        stats['rating'] = rating_stats
    
    # Collect clock time statistics
    time_stats = collect_clock_time_statistics(bag_reader, num_entries, num_clock_samples)
    if time_stats:
        stats.update(time_stats)
    
    # Save the statistics to a pickle file
    if stats['rating'] or stats.get('log_time'):
        with open(output_path, 'wb') as f:
            pickle.dump(stats, f)
        print(f"Statistics saved to {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Calculate rating and clock time statistics from bagz file")
    parser.add_argument("--bagz", type=str, help="Path to the bagz file", required=True)
    parser.add_argument("--output", type=str, help="Path to save the pickle file", required=True)
    parser.add_argument("--num_rating_samples", type=int, default=1000000, 
                        help="Number of rating samples to collect (default: 1,000,000)")
    parser.add_argument("--num_clock_samples", type=int, default=1000000, 
                        help="Number of clock time samples to collect (default: 1,000,000)")
    
    args = parser.parse_args()
    
    calculate_statistics(args.bagz, args.output, args.num_rating_samples, args.num_clock_samples)


if __name__ == "__main__":
    main()