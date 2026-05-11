import numpy as np
import torch
import chess
from torch.utils.data import Dataset
import orjson
from bagz import BagReader
from tqdm.auto import tqdm
import pickle
import math

# Default to using C++ module
USING_CPP = True

# Import C++ module
import chessmimic_core
from chessmimic_core import (
    tokenize,
    MOVE_TO_ACTION,
    ACTION_TO_MOVE,
    SEQUENCE_LENGTH,
    NUM_ACTIONS,
    PAD_TOKEN
)

# Import Python tokenizer if requested for compatibility/testing
# Note: This can be controlled by setting the CHESSMIMIC_USE_PYTHON_TOKENIZER environment variable
import os
if os.environ.get('CHESSMIMIC_USE_PYTHON_TOKENIZER', '').lower() in ('1', 'true', 'yes'):
    try:
        print("Using Python tokenizer for compatibility/testing.")
        import tokenizer
        # Override C++ imports with Python tokenizer values
        tokenize = tokenizer.tokenize
        MOVE_TO_ACTION = tokenizer.MOVE_TO_ACTION
        ACTION_TO_MOVE = tokenizer.ACTION_TO_MOVE
        SEQUENCE_LENGTH = tokenizer.SEQUENCE_LENGTH
        NUM_ACTIONS = tokenizer.NUM_ACTIONS
        PAD_TOKEN = tokenizer.PAD_TOKEN
        USING_CPP = False
    except ImportError:
        # If Python tokenizer can't be imported, stay with C++
        print("Python tokenizer not found, using C++ tokenizer.")
        pass
else:
    print("Using C++ tokenizer.")


class BagzDataset(Dataset):
    def __init__(self, bagz_path, scalers):
        """Dataset for loading chess positions and moves from a bagz file.
        
        Args:
            bagz_path (str): Path to the bagz file
            scalers (dict): Dictionary containing scaling parameters for ratings and log_time
        """
        self.bagz_path = bagz_path
        
        # Set up rating and log time scalers
        self.rating_mean = float(scalers['rating']['mean'])
        self.rating_std = float(scalers['rating']['std'])
        self.log_time_mean = float(scalers['log_time']['mean'])
        self.log_time_std = float(scalers['log_time']['std'])
        
        self.reader = BagReader(bagz_path, separate_limits=False)
        self.num_entries = len(self.reader)
        self.generator = np.random.default_rng(42)

    def __len__(self):
        return self.num_entries

    # Helper method for recent moves preparation
    @staticmethod
    def _prepare_recent_moves_tokens(recent_moves):
        """Efficiently prepare the recent moves tokens array.

        This is now a wrapper for the C++ implementation.
        """
        if USING_CPP:
            # Use C++ implementation
            return chessmimic_core.prepare_recent_moves_tokens(recent_moves)
        else:
            # Use original Python implementation
            num_moves = len(recent_moves)
            if num_moves < 12:
                # Create array for padding + moves
                tokens = np.full(12, PAD_TOKEN, dtype=np.int64)
                # Fill in only the necessary values, starting at the right position
                for i, move in enumerate(recent_moves):
                    tokens[12-num_moves+i] = MOVE_TO_ACTION[move]
            else:
                # Only use the last 12 moves
                tokens = np.zeros(12, dtype=np.int64)
                for i, move in enumerate(recent_moves[-12:]):
                    tokens[i] = MOVE_TO_ACTION[move]
            return tokens
    
    @staticmethod
    def recent_moves_and_fen_to_inputs(recent_moves, fen):
        """Convert recent moves and FEN to model inputs.

        Now uses C++ implementation for better performance.
        """
        if USING_CPP:
            # Use C++ implementation
            cpp_result = chessmimic_core.recent_moves_and_fen_to_inputs(recent_moves, fen)
            move_tokens_array, fen_tokens_uint8, move_mask = cpp_result

            # Convert to PyTorch tensors
            recent_moves_tokens = torch.LongTensor(move_tokens_array)
            fen_tokens = torch.LongTensor(fen_tokens_uint8)

            return recent_moves_tokens, fen_tokens, move_mask
        else:
            # Use original Python implementation
            # 1. Process recent moves efficiently using a numpy array
            move_tokens_array = BagzDataset._prepare_recent_moves_tokens(recent_moves)

            # 2. Parse board and get legal moves directly
            board = chess.Board(fen)

            # 3. Create move mask efficiently by pre-allocating and using indexed assignment
            move_mask = np.zeros(NUM_ACTIONS, dtype=np.float32)
            # Process legal moves directly
            for move in board.legal_moves:
                move_str = str(move)
                move_mask[MOVE_TO_ACTION[move_str]] = 1.0

            # 4. Create tensors directly from numpy arrays
            recent_moves_tokens = torch.LongTensor(move_tokens_array)
            fen_tokens = torch.LongTensor(tokenize(fen))

            return recent_moves_tokens, fen_tokens, move_mask

    def __getitem__(self, idx):
        # Read entry from bagz file
        entry = self.reader[idx]
        data = orjson.loads(entry)
        
        recent_moves, fen = data['recent_and_fen']
        moves = data['moves']
        
        # Calculate total number of occurrences across all moves, clock times, and ratings
        total_count = 0
        move_rating_time_tuples = []
        move_probs = []
        
        # Process the new format where each move has clock times and ratings
        for move, clock_times in moves.items():
            for clock_str, ratings_dict in clock_times.items():
                clock_time = float(clock_str)
                move_count = sum(ratings_dict.values())
                total_count += move_count
                
                # Store each (move, rating, clock_time) tuple with its count
                for rating_str, count in ratings_dict.items():
                    move_rating_time_tuples.append((move, int(rating_str), clock_time))
                    move_probs.append(int(count))
        
        # Normalize probabilities
        move_probs = [p / total_count for p in move_probs]
        
        # Probabilistically pick a move-rating-time tuple
        idx = self.generator.choice(len(move_rating_time_tuples), p=move_probs)
        selected_move, selected_rating, selected_time = move_rating_time_tuples[idx]
        
        # Convert move to action token
        output_move = MOVE_TO_ACTION[selected_move]
        
        # Parse recent moves and fen
        recent_moves_tokens, fen_tokens, move_mask = self.recent_moves_and_fen_to_inputs(recent_moves, fen)

        # Scale rating and log time
        scaled_rating = (selected_rating - self.rating_mean) / self.rating_std
        log_time = math.log(1 + selected_time)
        scaled_log_time = (log_time - self.log_time_mean) / self.log_time_std
        
        # Return the move, rating, and clock time
        return recent_moves_tokens, fen_tokens, scaled_rating, scaled_log_time, move_mask, output_move


if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description='Smoketest for bagz dataset')
    parser.add_argument('--bagz_path', type=str, required=True, help='Path to the bagz file')
    parser.add_argument('--scalers', type=str, required=True, help='Path to the scalers pickle file')
    
    args = parser.parse_args()

    with open(args.scalers, 'rb') as f:
        _scalers = pickle.load(f)
    
    _dataset = BagzDataset(args.bagz_path, _scalers)
    print(f"Dataset size: {len(_dataset)}")
    print("First item:")
    sample = _dataset[0]
    print(f"Recent moves tokens: {sample[0]}")
    print(f"FEN tokens: {sample[1]}")
    print(f"Scaled rating: {sample[2]}")
    print(f"Scaled log time: {sample[3]}")
    print(f"Move mask: Shape {sample[4].shape}, Sum {sample[4].sum()}")
    print(f"Output move: {sample[5]}")
    
    print("Second item:")
    sample = _dataset[1]
    print(f"Recent moves tokens: {sample[0]}")
    print(f"FEN tokens: {sample[1]}")
    print(f"Scaled rating: {sample[2]}")
    print(f"Scaled log time: {sample[3]}")
    print(f"Move mask: Shape {sample[4].shape}, Sum {sample[4].sum()}")
    print(f"Output move: {sample[5]}")
    
    print("Last item:")
    sample = _dataset[len(_dataset) - 1]
    print(f"Recent moves tokens: {sample[0]}")
    print(f"FEN tokens: {sample[1]}")
    print(f"Scaled rating: {sample[2]}")
    print(f"Scaled log time: {sample[3]}")
    print(f"Move mask: Shape {sample[4].shape}, Sum {sample[4].sum()}")
    print(f"Output move: {sample[5]}")
    
    # Sample and ensure consistent sizes
    sizes = [_dataset[0][0].shape, _dataset[0][1].shape]
    for i in tqdm(range(min(10_000, len(_dataset))), desc="Checking sizes (first samples)"):
        sample = _dataset[i]
        assert sizes == [sample[0].shape, sample[1].shape]
        assert isinstance(sample[2], float)  # Rating
        assert isinstance(sample[3], float)  # Log time
    
    if len(_dataset) > 10_000:
        for i in tqdm(range(min(10_000, len(_dataset))), desc="Checking sizes (last samples)"):
            sample = _dataset[len(_dataset) - 1 - i]
            assert sizes == [sample[0].shape, sample[1].shape]
            assert isinstance(sample[2], float)  # Rating
            assert isinstance(sample[3], float)  # Log time
    
    # Benchmark dataset loading
    from torch.utils.data import DataLoader
    import os
    batch_size = 2048
    _data_loader = DataLoader(
        _dataset,
        batch_size=batch_size,
        shuffle=False,
        pin_memory=True,
        persistent_workers=True,
        num_workers=os.cpu_count(),
        # num_workers=0,
    )
    num_batches = 1000
    import time
    start_time = time.time()
    first_batch_time = None
    for i, batch in tqdm(enumerate(_data_loader), total=num_batches):
        if i == 0:
            first_batch_time = time.time()
            print(f"First batch loaded in {first_batch_time - start_time:.2f} seconds")
        if i >= num_batches:
            break
        _ = batch
    end_time = time.time()
    print(f"Loaded {num_batches} batches in {end_time - start_time:.2f} seconds")
    print(f"Average time per batch: {(end_time - start_time) / num_batches:.2f} seconds")
    print(f"Samples/second: {num_batches * batch_size / (end_time - start_time):.2f}")
    print(f"After first batch, loaded {num_batches-1} batches in {end_time - first_batch_time:.2f} seconds")
    print(f"After first batch, Average time per batch: {(end_time - first_batch_time) / (num_batches-1):.2f} seconds")
    print(f"After first batch, Samples/second: {(num_batches-1) * batch_size / (end_time - first_batch_time):.2f}")
    del _data_loader