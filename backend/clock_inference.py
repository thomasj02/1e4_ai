"""Clock model inference for predicting thinking times."""

import os
import time
import torch
import torch.nn as nn
import torch.nn.functional as F
import numpy as np
import pickle
import math
from pathlib import Path
import sys
from logging_config import get_logger

logger = get_logger(__name__)

# Add Training directory to path to import modules
training_path = Path(__file__).parent.parent / 'Training'
sys.path.insert(0, str(training_path))

# Import clock bucket utilities
from clock_bucket_utils import load_bucket_info

# Import tokenization and move mappings from the C++ module
from chessmimic_core import (
    tokenize as tokenize_fen,
    MOVE_TO_ACTION,
    NUM_ACTIONS,
    INPUT_VOCAB_SIZE,
    SEQUENCE_LENGTH,
    PAD_TOKEN,
    prepare_recent_moves_tokens
)

# Import model architecture from Training module
from ClockTrainer import ClockPatzerModel


class ClockPredictor:
    """Clock time predictor using the trained ClockPatzerModel."""
    
    def __init__(self, model_path: Path | str, scalers_path: Path | str, bucket_info_path: Path | str):
        """Initialize the clock predictor.
        
        Args:
            model_path: Path to the model checkpoint
            scalers_path: Path to the scalers pickle file
            bucket_info_path: Path to the bucket info JSON file
        """
        # Convert to Path objects
        model_path = Path(model_path)
        scalers_path = Path(scalers_path)
        bucket_info_path = Path(bucket_info_path)
        
        # Check files exist
        if not model_path.exists():
            raise FileNotFoundError(f"Model file not found: {model_path}")
        if not scalers_path.exists():
            raise FileNotFoundError(f"Scalers file not found: {scalers_path}")
        if not bucket_info_path.exists():
            raise FileNotFoundError(f"Bucket info file not found: {bucket_info_path}")
        
        # Check environment variable for device preference
        force_cpu = os.getenv('CHESSMIMIC_FORCE_CPU', 'true').lower() == 'true'
        
        if force_cpu:
            self.device = torch.device('cpu')
            logger.info("Clock model device: CPU (forced via CHESSMIMIC_FORCE_CPU=true)")
        else:
            self.device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
            logger.info(f"Clock model device: {self.device}")
        
        # Load scalers
        with open(scalers_path, 'rb') as f:
            scalers = pickle.load(f)
        
        self.rating_mean = float(scalers['rating']['mean'])
        self.rating_std = float(scalers['rating']['std'])
        self.log_player_clock_mean = float(scalers['log_player_clock']['mean'])
        self.log_player_clock_std = float(scalers['log_player_clock']['std'])
        self.log_opponent_clock_mean = float(scalers['log_opponent_clock']['mean'])
        self.log_opponent_clock_std = float(scalers['log_opponent_clock']['std'])
        self.log_increment_mean = float(scalers['log_increment']['mean'])
        self.log_increment_std = float(scalers['log_increment']['std'])
        
        # Load bucket info
        self.bucket_info = load_bucket_info(str(bucket_info_path))
        logger.debug(f"Loaded clock bucket info: {self.bucket_info}")
        
        # Create model
        # We need to infer sequence lengths from the training setup
        recent_moves_length = 12  # Standard from training
        board_length = SEQUENCE_LENGTH
        
        self.model = ClockPatzerModel(
            recent_moves_sequence_length=recent_moves_length,
            recent_moves_vocab_size=NUM_ACTIONS,
            board_sequence_length=board_length,
            board_input_vocab_size=INPUT_VOCAB_SIZE,
            embedding_dim=256,
            widening_factor=4,
            num_layers=8,
            num_heads=8,
            n_buckets=self.bucket_info.n_buckets
        )
        
        # Load checkpoint
        checkpoint = torch.load(model_path, map_location=self.device)
        state_dict = checkpoint.get('state_dict', checkpoint)
        
        # Remove prefixes from keys
        new_state_dict = {}
        for k, v in state_dict.items():
            # Remove 'model.' prefix if present
            if k.startswith('model.'):
                k = k[6:]
            # Remove '_orig_mod.' prefix if present
            if k.startswith('_orig_mod.'):
                k = k[10:]
            new_state_dict[k] = v
        
        self.model.load_state_dict(new_state_dict)
        self.model.to(self.device)
        self.model.eval()
    
    def predict_bucket(self, fen: str, recent_moves: list[str], rating: int,
                      player_clock: float, opponent_clock: float, increment: float) -> np.ndarray:
        """Predict bucket probabilities for the given position.
        
        Args:
            fen: Chess position in FEN format
            recent_moves: List of recent moves in UCI format
            rating: Player rating
            player_clock: Player's remaining clock time in seconds
            opponent_clock: Opponent's remaining clock time in seconds
            increment: Time increment per move in seconds
            
        Returns:
            Array of bucket probabilities (shape: n_buckets)
        """
        with torch.no_grad():
            # Prepare recent moves tokens
            if recent_moves is None:
                recent_moves = []
            
            # Use C++ function to prepare recent moves
            recent_moves_array = prepare_recent_moves_tokens(recent_moves[-12:])
            recent_moves_tokens = torch.tensor(recent_moves_array[np.newaxis, :], dtype=torch.long).to(self.device)
            
            # Tokenize FEN
            fen_tokens_array = tokenize_fen(fen)
            fen_tokens = torch.tensor(fen_tokens_array[np.newaxis, :], dtype=torch.long).to(self.device)
            
            # Combine tokens for input_ids
            input_ids = torch.cat([recent_moves_tokens, fen_tokens], dim=1)
            
            # Create attention mask (all 1s)
            attention_mask = torch.ones_like(input_ids)
            
            # Scale rating
            scaled_rating = (rating - self.rating_mean) / self.rating_std
            scaled_rating = torch.tensor([scaled_rating], dtype=torch.float32).to(self.device)
            
            # Prepare 3D clock features with log transforms
            player_clock_scaled = (math.log(player_clock + 1) - self.log_player_clock_mean) / self.log_player_clock_std
            opponent_clock_scaled = (math.log(opponent_clock + 1) - self.log_opponent_clock_mean) / self.log_opponent_clock_std
            increment_scaled = (math.log(increment + 1) - self.log_increment_mean) / self.log_increment_std
            
            clock_features = torch.tensor(
                [[player_clock_scaled, opponent_clock_scaled, increment_scaled]], 
                dtype=torch.float32
            ).to(self.device)
            
            # Run model
            logits = self.model(input_ids, attention_mask, scaled_rating, clock_features)
            
            # Apply softmax to get probabilities (NO MASKING!)
            probabilities = F.softmax(logits, dim=1)
            
            return probabilities.cpu().numpy()[0]
    
    def sample_thinking_time(self, bucket_probabilities: np.ndarray, min_probability: float = 0.0) -> tuple[float, int, int]:
        """Sample a thinking time given bucket probabilities.
        
        Args:
            bucket_probabilities: Array of probabilities for each bucket
            min_probability: Minimum probability threshold for buckets (0.0-1.0)
            
        Returns:
            Tuple of (thinking_time in seconds, sampled_bucket_idx, num_buckets_filtered)
        """
        # Keep track of original probabilities for filtering stats
        original_buckets = list(enumerate(bucket_probabilities))
        
        # Filter by min_probability if specified
        num_filtered = 0
        if min_probability > 0:
            filtered_buckets = [(idx, prob) for idx, prob in original_buckets if prob >= min_probability]
            num_filtered = len(original_buckets) - len(filtered_buckets)
            
            if filtered_buckets:
                # Use filtered buckets
                indices, probs = zip(*filtered_buckets)
                probs_array = np.array(probs)
                # Normalize to ensure they sum to 1
                probs_array = probs_array / probs_array.sum()
                bucket_idx = indices[np.random.choice(len(indices), p=probs_array)]
            else:
                # If no buckets meet threshold, use all buckets (fallback)
                bucket_idx = np.random.choice(len(bucket_probabilities), p=bucket_probabilities)
        else:
            # No filtering
            bucket_idx = np.random.choice(len(bucket_probabilities), p=bucket_probabilities)
        
        # Sample from the bucket's empirical distribution
        thinking_time = self.bucket_info.sample_from_bucket_empirical(bucket_idx)
        
        return thinking_time, bucket_idx, num_filtered
    
    def predict_thinking_time(self, fen: str, recent_moves: list[str] | None = None,
                            rating: int = 1500, player_clock: float = 300.0,
                            opponent_clock: float = 300.0, increment: float = 0.0,
                            min_probability: float = 0.0) -> tuple[float, dict]:
        """Predict thinking time for the given position.
        
        Args:
            fen: Chess position in FEN format
            recent_moves: List of recent moves in UCI format (optional)
            rating: Player rating to simulate
            player_clock: Player's remaining clock time in seconds
            opponent_clock: Opponent's remaining clock time in seconds
            increment: Time increment per move in seconds
            min_probability: Minimum probability threshold for buckets (0.0-1.0)
            
        Returns:
            Tuple of (thinking_time in seconds, info dict)
        """
        # Get bucket probabilities
        bucket_probs = self.predict_bucket(
            fen, recent_moves or [], rating, 
            player_clock, opponent_clock, increment
        )
        
        # Sample thinking time and get actual bucket used
        thinking_time, sampled_bucket, num_filtered = self.sample_thinking_time(bucket_probs, min_probability)
        
        # Find highest probability bucket for reference
        highest_prob_bucket = int(np.argmax(bucket_probs))
        
        # Check if this would cause time forfeit
        would_forfeit = thinking_time > (player_clock + increment)
        
        # Count buckets above threshold for statistics
        num_buckets_above_threshold = sum(1 for p in bucket_probs if p >= min_probability) if min_probability > 0 else len(bucket_probs)
        
        # Build info dict
        info = {
            'predicted_bucket': sampled_bucket,  # The actual bucket we sampled from
            'bucket_probability': float(bucket_probs[sampled_bucket]),  # Probability of the sampled bucket
            'highest_prob_bucket': highest_prob_bucket,  # Bucket with highest probability
            'highest_probability': float(bucket_probs[highest_prob_bucket]),  # Its probability
            'top_buckets': [(i, float(p)) for i, p in enumerate(bucket_probs)],  # All buckets
            'player_clock': player_clock,
            'opponent_clock': opponent_clock,
            'increment': increment,
            'would_forfeit': would_forfeit,
            'rating_used': rating,
            'min_probability_used': min_probability,
            'num_buckets_after_filter': num_buckets_above_threshold,
            'num_buckets_filtered_out': num_filtered
        }
        
        return thinking_time, info