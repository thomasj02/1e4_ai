import torch
from torch.utils.data import Dataset
import orjson
from bagz import BagFileReader
from tqdm.auto import tqdm
import pickle
import math
import os
import chessmimic_core
from typing import Dict, Optional
from clock_bucket_utils import ClockBucketInfo, load_bucket_info


class ClockDataset(Dataset):
    def __init__(self, bagz_path: str, scalers: dict, bucket_boundaries_path: str):
        """Dataset for loading chess clock prediction data from a bagz file.
        
        Args:
            bagz_path: Path to the clock bagz file
            scalers: Dictionary containing scaling parameters from FitClockScalersFromBagz.py
            bucket_boundaries_path: Path to JSON file with bucket boundaries
        """
        self.bagz_path = bagz_path
        
        # Load bucket boundaries
        self.bucket_info = load_bucket_info(bucket_boundaries_path)
        print(f"Loaded bucket info: {self.bucket_info}")
        
        # Set up scalers for all features
        self.rating_mean = float(scalers['rating']['mean'])
        self.rating_std = float(scalers['rating']['std'])
        
        # Log-transformed clock scalers
        self.log_player_clock_mean = float(scalers['log_player_clock']['mean'])
        self.log_player_clock_std = float(scalers['log_player_clock']['std'])
        
        self.log_opponent_clock_mean = float(scalers['log_opponent_clock']['mean'])
        self.log_opponent_clock_std = float(scalers['log_opponent_clock']['std'])
        
        self.log_increment_mean = float(scalers['log_increment']['mean'])
        self.log_increment_std = float(scalers['log_increment']['std'])
        
        self.reader = BagFileReader(bagz_path, separate_limits=False)
        self.num_entries = len(self.reader)

    def __len__(self):
        return self.num_entries

    # Helper method for recent moves preparation (reused from MoveDataset)
    @staticmethod
    def _prepare_recent_moves_tokens(recent_moves):
        """Efficiently prepare the recent moves tokens array using C++ implementation."""
        return chessmimic_core.prepare_recent_moves_tokens(recent_moves)
    
    @staticmethod
    def recent_moves_and_fen_to_inputs(recent_moves, fen):
        """Convert recent moves and FEN to model inputs using C++ implementation."""
        cpp_result = chessmimic_core.recent_moves_and_fen_to_inputs(recent_moves, fen)
        move_tokens_array, fen_tokens_uint8, move_mask = cpp_result

        # Convert to PyTorch tensors
        recent_moves_tokens = torch.LongTensor(move_tokens_array)
        fen_tokens = torch.LongTensor(fen_tokens_uint8)

        return recent_moves_tokens, fen_tokens, move_mask

    def __getitem__(self, idx):
        # Read entry from bagz file
        entry = self.reader[idx]
        data = orjson.loads(entry)
        
        # Extract fields from the record
        fen = data['fen']
        recent_moves = data['recent_moves']
        rating = data['rating']
        player_clock = data['player_clock']
        opponent_clock = data['opponent_clock']
        increment = data['increment']
        thinking_time = data['thinking_time']
        
        # Parse recent moves and fen
        recent_moves_tokens, fen_tokens, move_mask = self.recent_moves_and_fen_to_inputs(recent_moves, fen)
        
        # Scale rating (no log transform)
        scaled_rating = (rating - self.rating_mean) / self.rating_std
        
        # Prepare 3D clock features vector with log transforms
        player_clock_scaled = (math.log(player_clock + 1) - self.log_player_clock_mean) / self.log_player_clock_std
        opponent_clock_scaled = (math.log(opponent_clock + 1) - self.log_opponent_clock_mean) / self.log_opponent_clock_std
        increment_scaled = (math.log(increment + 1) - self.log_increment_mean) / self.log_increment_std
        
        clock_features = torch.FloatTensor([player_clock_scaled, opponent_clock_scaled, increment_scaled])
        
        # Assign to bucket
        target_bucket = self.bucket_info.get_bucket_index(thinking_time)
        
        # Create bucket validity mask based on remaining clock time
        # With increment, a player can think for up to player_clock + increment time
        # (they can let their clock run down to nearly 0, then the increment saves them)
        max_thinking_time = player_clock + increment
        max_valid_bucket = self.bucket_info.get_bucket_index(max_thinking_time)
        
        # Create mask: buckets up to and including max_valid_bucket are valid
        bucket_mask = torch.zeros(self.bucket_info.n_buckets, dtype=torch.bool)
        bucket_mask[:max_valid_bucket + 1] = True
        
        # Ensure at least the first bucket is always valid (for edge cases)
        bucket_mask[0] = True
        
        # Convert move_mask to tensor if it isn't already
        if not isinstance(move_mask, torch.Tensor):
            move_mask = torch.FloatTensor(move_mask)
        
        # Combine tokens for input_ids
        input_ids = torch.cat([recent_moves_tokens, fen_tokens])
        
        # Create attention mask (all 1s for now)
        attention_mask = torch.ones_like(input_ids)
        
        # Return dictionary format for cleaner interface
        return {
            'input_ids': input_ids,
            'attention_mask': attention_mask,
            'rating': scaled_rating,
            'clock_features': clock_features,
            'move_mask': move_mask,
            'bucket_mask': bucket_mask,  # New: bucket validity mask
            'target_bucket': target_bucket,
            'raw_thinking_time': thinking_time  # Keep for analysis/debugging
        }
    
    def get_n_buckets(self) -> int:
        """Return number of buckets for model construction."""
        return self.bucket_info.n_buckets
    
    def get_class_weights(self) -> Optional[torch.Tensor]:
        """Return class weights for balanced training."""
        weights = self.bucket_info.get_class_weights()
        return torch.tensor(weights, dtype=torch.float32)
    
    def get_bucket_info(self) -> ClockBucketInfo:
        """Return bucket information object for evaluation/generation."""
        return self.bucket_info


if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description='Smoketest for clock dataset')
    parser.add_argument('--bagz_path', type=str, required=True, help='Path to the clock bagz file')
    parser.add_argument('--scalers', type=str, required=True, help='Path to the scalers pickle file')
    parser.add_argument('--bucket_boundaries', type=str, required=True, help='Path to bucket boundaries JSON')
    
    args = parser.parse_args()

    with open(args.scalers, 'rb') as f:
        _scalers = pickle.load(f)
    
    _dataset = ClockDataset(args.bagz_path, _scalers, args.bucket_boundaries)
    print(f"Dataset size: {len(_dataset)}")
    print(f"Number of buckets: {_dataset.get_n_buckets()}")
    print(f"Bucket info: {_dataset.get_bucket_info()}")
    
    print("\nFirst item:")
    sample = _dataset[0]
    print(f"Input IDs shape: {sample['input_ids'].shape}")
    print(f"Attention mask shape: {sample['attention_mask'].shape}")
    print(f"Scaled rating: {sample['rating']:.4f}")
    print(f"Clock features: {sample['clock_features']}")
    print(f"Move mask: Shape {sample['move_mask'].shape}, Sum {sample['move_mask'].sum()}")
    print(f"Bucket mask: Shape {sample['bucket_mask'].shape}, Valid buckets: {sample['bucket_mask'].sum()}")
    print(f"Target bucket: {sample['target_bucket']}")
    print(f"Raw thinking time: {sample['raw_thinking_time']:.2f}s")
    
    # Check bucket assignment
    bucket_center = _dataset.bucket_info.get_bucket_center(sample['target_bucket'])
    bucket_range = _dataset.bucket_info.get_bucket_range(sample['target_bucket'])
    print(f"Bucket center: {bucket_center:.1f}s, Range: [{bucket_range[0]:.1f}, {bucket_range[1]:.1f})")
    
    # Show which buckets are valid
    print(f"Valid bucket indices: {torch.where(sample['bucket_mask'])[0].tolist()}")
    
    print("\nAnalyzing bucket distribution (first 10000 samples)...")
    bucket_counts = torch.zeros(_dataset.get_n_buckets())
    for i in tqdm(range(min(10000, len(_dataset)))):
        sample = _dataset[i]
        bucket_counts[sample['target_bucket']] += 1
    
    print("\nBucket distribution:")
    for i in range(_dataset.get_n_buckets()):
        count = bucket_counts[i].item()
        pct = 100 * count / bucket_counts.sum()
        bucket_range = _dataset.bucket_info.get_bucket_range(i)
        if bucket_range[1] == float('inf'):
            print(f"  Bucket {i:2d} [{bucket_range[0]:5.1f}, ∞): {count:4.0f} ({pct:5.2f}%)")
        else:
            print(f"  Bucket {i:2d} [{bucket_range[0]:5.1f}, {bucket_range[1]:5.1f}): {count:4.0f} ({pct:5.2f}%)")
    
    # Get class weights
    class_weights = _dataset.get_class_weights()
    print(f"\nClass weights shape: {class_weights.shape}")
    print(f"Class weights min: {class_weights.min():.3f}, max: {class_weights.max():.3f}, mean: {class_weights.mean():.3f}")
    
    # Benchmark dataset loading
    from torch.utils.data import DataLoader
    batch_size = 2048
    _data_loader = DataLoader(
        _dataset,
        batch_size=batch_size,
        shuffle=False,
        pin_memory=True,
        persistent_workers=True,
        num_workers=os.cpu_count(),
    )
    num_batches = 1000
    import time
    start_time = time.time()
    first_batch_time = None
    for i, batch in tqdm(enumerate(_data_loader), total=num_batches):
        if i == 0:
            first_batch_time = time.time()
            print(f"\nFirst batch loaded in {first_batch_time - start_time:.2f} seconds")
            print(f"Batch keys: {batch.keys()}")
            for k, v in batch.items():
                if isinstance(v, torch.Tensor):
                    print(f"  {k}: shape {v.shape}, dtype {v.dtype}")
        if i >= num_batches:
            break
        _ = batch
    end_time = time.time()
    print(f"\nLoaded {num_batches} batches in {end_time - start_time:.2f} seconds")
    print(f"Average time per batch: {(end_time - start_time) / num_batches:.2f} seconds")
    print(f"Samples/second: {num_batches * batch_size / (end_time - start_time):.2f}")
    del _data_loader