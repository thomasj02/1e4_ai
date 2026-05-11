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


class WinnerDataset(Dataset):
    def __init__(self, bagz_path: str, scalers: dict):
        """Dataset for loading chess winner prediction data from a bagz file.
        
        Args:
            bagz_path: Path to the winner bagz file
            scalers: Dictionary containing scaling parameters for ratings and clocks
        """
        self.bagz_path = bagz_path
        
        # Set up scalers for all features
        self.white_rating_mean = float(scalers['white_rating']['mean'])
        self.white_rating_std = float(scalers['white_rating']['std'])
        self.black_rating_mean = float(scalers['black_rating']['mean'])
        self.black_rating_std = float(scalers['black_rating']['std'])
        
        # Log-transformed clock scalers
        self.log_white_clock_mean = float(scalers['log_white_clock']['mean'])
        self.log_white_clock_std = float(scalers['log_white_clock']['std'])
        self.log_black_clock_mean = float(scalers['log_black_clock']['mean'])
        self.log_black_clock_std = float(scalers['log_black_clock']['std'])
        
        # Increment scaler (using log transform)
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
        winner = data['winner']  # -1 for black, 0 for draw, 1 for white
        white_rating = data['white_rating']
        black_rating = data['black_rating']
        white_clock = data['white_clock']
        black_clock = data['black_clock']
        increment = data['increment']
        
        # Parse recent moves and fen
        recent_moves_tokens, fen_tokens, move_mask = self.recent_moves_and_fen_to_inputs(recent_moves, fen)
        
        # Scale ratings
        scaled_white_rating = (white_rating - self.white_rating_mean) / self.white_rating_std
        scaled_black_rating = (black_rating - self.black_rating_mean) / self.black_rating_std
        
        # Prepare clock features with log transforms
        white_clock_scaled = (math.log(white_clock + 1) - self.log_white_clock_mean) / self.log_white_clock_std
        black_clock_scaled = (math.log(black_clock + 1) - self.log_black_clock_mean) / self.log_black_clock_std
        increment_scaled = (math.log(increment + 1) - self.log_increment_mean) / self.log_increment_std
        
        # Create features tensor: [white_rating, black_rating, white_clock, black_clock, increment]
        features = torch.FloatTensor([
            scaled_white_rating,
            scaled_black_rating,
            white_clock_scaled,
            black_clock_scaled,
            increment_scaled
        ])
        
        # Convert winner to categorical class index
        # -1 = black wins -> class 0
        # 0 = draw -> class 1
        # 1 = white wins -> class 2
        winner_raw = int(winner)
        if winner_raw == -1:
            winner_class = 0
        elif winner_raw == 0:
            winner_class = 1
        else:  # winner_raw == 1
            winner_class = 2
        
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
            'features': features,
            'move_mask': move_mask,
            'winner': winner_class,  # Categorical: 0=black, 1=draw, 2=white
            'winner_raw': winner_raw,  # Keep raw value for debugging: -1=black, 0=draw, 1=white
            'white_rating': white_rating,  # Keep raw values for analysis
            'black_rating': black_rating,
            'white_clock': white_clock,
            'black_clock': black_clock,
            'increment': increment
        }


if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description='Smoketest for winner dataset')
    parser.add_argument('--bagz_path', type=str, required=True, help='Path to the winner bagz file')
    parser.add_argument('--scalers', type=str, required=True, help='Path to the scalers pickle file')
    
    args = parser.parse_args()

    with open(args.scalers, 'rb') as f:
        _scalers = pickle.load(f)
    
    _dataset = WinnerDataset(args.bagz_path, _scalers)
    print(f"Dataset size: {len(_dataset)}")
    
    print("\nFirst item:")
    sample = _dataset[0]
    print(f"Input IDs shape: {sample['input_ids'].shape}")
    print(f"Attention mask shape: {sample['attention_mask'].shape}")
    print(f"Features shape: {sample['features'].shape}")
    print(f"Features values: {sample['features']}")
    print(f"Move mask: Shape {sample['move_mask'].shape}, Sum {sample['move_mask'].sum()}")
    print(f"Winner: class {sample['winner']} ({'black' if sample['winner'] == 0 else 'draw' if sample['winner'] == 1 else 'white'})")
    print(f"Raw ratings - White: {sample['white_rating']}, Black: {sample['black_rating']}")
    print(f"Raw clocks - White: {sample['white_clock']:.1f}s, Black: {sample['black_clock']:.1f}s")
    print(f"Increment: {sample['increment']}s")
    
    print("\nAnalyzing winner distribution (first 10000 samples)...")
    winner_counts = {'black': 0, 'draw': 0, 'white': 0}
    for i in tqdm(range(min(10000, len(_dataset)))):
        sample = _dataset[i]
        winner_class = sample['winner']
        if winner_class == 0:
            winner_counts['black'] += 1
        elif winner_class == 1:
            winner_counts['draw'] += 1
        else:  # winner_class == 2
            winner_counts['white'] += 1
    
    print("\nWinner distribution:")
    total_samples = sum(winner_counts.values())
    for outcome, count in winner_counts.items():
        pct = 100 * count / total_samples
        print(f"  {outcome.capitalize()}: {count:4d} ({pct:5.2f}%)")
    
    # For categorical classification with classes 0, 1, 2
    print(f"\nSample statistics for categorical classification:")
    print(f"  Total samples: {total_samples}")
    print(f"  Black win rate (class 0): {winner_counts['black'] / total_samples:.3f}")
    print(f"  Draw rate (class 1): {winner_counts['draw'] / total_samples:.3f}")
    print(f"  White win rate (class 2): {winner_counts['white'] / total_samples:.3f}")
    
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
            # Check winner values in the batch
            winner_values = batch['winner']
            print(f"\nWinner class statistics in batch:")
            print(f"  Min: {winner_values.min()}, Max: {winner_values.max()}")
            print(f"  Unique classes: {torch.unique(winner_values).tolist()}")
            # Count each class
            for cls in range(3):
                count = (winner_values == cls).sum().item()
                print(f"  Class {cls}: {count} samples ({100*count/len(winner_values):.1f}%)")
        if i >= num_batches:
            break
        _ = batch
    end_time = time.time()
    print(f"\nLoaded {num_batches} batches in {end_time - start_time:.2f} seconds")
    print(f"Average time per batch: {(end_time - start_time) / num_batches:.2f} seconds")
    print(f"Samples/second: {num_batches * batch_size / (end_time - start_time):.2f}")
    del _data_loader