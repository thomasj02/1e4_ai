#!/usr/bin/env python3
"""Winner prediction inference for chess positions."""

import sys
from pathlib import Path

# Add Training directory to path
training_path = Path(__file__).parent.parent / 'Training'
sys.path.insert(0, str(training_path))

import torch
import torch.nn as nn
import torch.nn.functional as F
import numpy as np
import pickle
import os
import math
from logging_config import get_logger

logger = get_logger(__name__)

# Import the model architecture from Training
from WinnerTrainer import WinnerPatzerModel

# Import chess processing utilities
import chessmimic_core


class WinnerPredictor:
    """Predicts game outcome probabilities for chess positions."""
    
    def __init__(self, model_path: Path, scalers_path: Path):
        """Initialize the winner predictor.
        
        Args:
            model_path: Path to the winner model checkpoint
            scalers_path: Path to the scalers pickle file
        """
        # Check if files exist
        if not model_path.exists():
            raise FileNotFoundError(f"Model file not found: {model_path}")
        if not scalers_path.exists():
            raise FileNotFoundError(f"Scalers file not found: {scalers_path}")
        
        # Determine device
        force_cpu = os.environ.get('CHESSMIMIC_FORCE_CPU', 'true').lower() == 'true'
        if torch.cuda.is_available() and not force_cpu:
            self.device = torch.device('cuda')
            logger.info("Winner model device: CUDA")
        else:
            self.device = torch.device('cpu')
            if force_cpu:
                logger.info("Winner model device: CPU (forced via CHESSMIMIC_FORCE_CPU=true)")
            else:
                logger.info("Winner model device: CPU")
        
        # Load scalers
        with open(scalers_path, 'rb') as f:
            self.scalers = pickle.load(f)
        
        # Load model checkpoint
        checkpoint = torch.load(model_path, map_location=self.device)
        
        # Extract model configuration
        if 'model_config' in checkpoint:
            config = checkpoint['model_config']
            prefix = "model."
        else:
            # Infer configuration from state dict shapes
            state_dict = checkpoint.get('state_dict', checkpoint.get('model_state_dict', {}))
            
            # Get learned_positional_encoding shape to determine sequence length
            if 'model.learned_positional_encoding' in state_dict:
                prefix = "model."
            elif "model._orig_mod.learned_positional_encoding" in state_dict:
                prefix = "model._orig_mod."
            else:
                raise ValueError("Could not find learned positional encoding in state dict")
            pos_enc_key = f"{prefix}learned_positional_encoding"
            if pos_enc_key not in state_dict:
                raise ValueError("Could not find learned positional encoding key in state dict")
            
            # Based on actual data: input_ids shape is [90]
            # recent_moves: 12, board: 78
            recent_moves_seq_len = 12
            board_seq_len = 78
            
            # Get vocab sizes from embedding weights
            board_emb_key = f"{prefix}board_embedding.weight"
            if board_emb_key not in state_dict:
                raise ValueError("Could not find board embedding key in state dict")
            board_vocab_size = state_dict[board_emb_key].shape[0]
            
            moves_emb_key = f'{prefix}recent_moves_embedding.weight'
            if moves_emb_key not in state_dict:
                moves_emb_key = 'recent_moves_embedding.weight'
            moves_vocab_size = state_dict[moves_emb_key].shape[0] if moves_emb_key in state_dict else 1968
            
            config = {
                'recent_moves_sequence_length': recent_moves_seq_len,
                'recent_moves_vocab_size': moves_vocab_size,
                'board_sequence_length': board_seq_len,
                'board_input_vocab_size': board_vocab_size,
                'embedding_dim': 256,
                'widening_factor': 4,
                'num_layers': 8,
                'num_heads': 8
            }
        
        # Create model
        self.model = WinnerPatzerModel(
            recent_moves_sequence_length=config['recent_moves_sequence_length'],
            recent_moves_vocab_size=config['recent_moves_vocab_size'],
            board_sequence_length=config['board_sequence_length'],
            board_input_vocab_size=config['board_input_vocab_size'],
            embedding_dim=config['embedding_dim'],
            widening_factor=config['widening_factor'],
            num_layers=config['num_layers'],
            num_heads=config['num_heads']
        )
        
        # Store config for later use
        self.config = config
        
        # Load model weights
        if 'model_state_dict' in checkpoint:
            self.model.load_state_dict(checkpoint['model_state_dict'], strict=False)
        elif 'state_dict' in checkpoint:
            # Handle different checkpoint formats
            state_dict = checkpoint['state_dict']
            # Remove 'model.' prefix if present
            cleaned_state_dict = {}
            for k, v in state_dict.items():
                cleaned_state_dict[k.replace(prefix, '')] = v
            self.model.load_state_dict(cleaned_state_dict, strict=True)
        else:
            raise ValueError("No model state dict found in checkpoint")
        
        # Set model to evaluation mode and move to device
        self.model.eval()
        self.model.to(self.device)
    
    def prepare_features(self, white_rating: int, black_rating: int,
                        white_clock: float, black_clock: float, 
                        increment: float) -> np.ndarray:
        """Prepare and scale features for the model.
        
        Args:
            white_rating: White player rating
            black_rating: Black player rating
            white_clock: White clock time in seconds
            black_clock: Black clock time in seconds
            increment: Time increment per move in seconds
            
        Returns:
            Scaled feature array of shape (5,)
        """
        # Scale ratings
        scaled_white_rating = (white_rating - self.scalers['white_rating']['mean']) / self.scalers['white_rating']['std']
        scaled_black_rating = (black_rating - self.scalers['black_rating']['mean']) / self.scalers['black_rating']['std']
        
        # Scale clocks with log transform (add 1 to avoid log(0))
        white_clock_scaled = (math.log(white_clock + 1) - self.scalers['log_white_clock']['mean']) / self.scalers['log_white_clock']['std']
        black_clock_scaled = (math.log(black_clock + 1) - self.scalers['log_black_clock']['mean']) / self.scalers['log_black_clock']['std']
        increment_scaled = (math.log(increment + 1) - self.scalers['log_increment']['mean']) / self.scalers['log_increment']['std']
        
        return np.array([
            scaled_white_rating,
            scaled_black_rating,
            white_clock_scaled,
            black_clock_scaled,
            increment_scaled
        ], dtype=np.float32)
    
    def predict_winner(self, fen: str, recent_moves: list[str] | None = None,
                      white_rating: int = 1500, black_rating: int = 1500,
                      white_clock: float = 300.0, black_clock: float = 300.0,
                      increment: float = 0.0, debug: bool = False) -> tuple[float, dict]:
        """Predict game outcome probability.
        
        Args:
            fen: Board position in FEN notation
            recent_moves: List of recent moves in UCI notation (optional)
            white_rating: White player rating
            black_rating: Black player rating  
            white_clock: White clock time in seconds
            black_clock: Black clock time in seconds
            increment: Time increment per move
            debug: If True, print debug information
            
        Returns:
            Tuple of (win_probability, info_dict)
            win_probability: Value from 0 to 1 (0=black wins, 0.5=draw, 1=white wins)
            info_dict: Additional information including confidence and per-outcome probabilities
        """
        # Parse position and moves
        if recent_moves is None:
            recent_moves = []
        
        if debug:
            print(f"[WINNER DEBUG] Processing position: {fen[:30]}...")
            print(f"[WINNER DEBUG] Recent moves: {recent_moves[-5:] if recent_moves else 'None'}")

        # Convert to model inputs using C++ implementation
        cpp_result = chessmimic_core.recent_moves_and_fen_to_inputs(recent_moves, fen)
        move_tokens_array, fen_tokens_uint8, move_mask = cpp_result
        
        # Convert to tensors
        recent_moves_tokens = torch.LongTensor(move_tokens_array).unsqueeze(0)
        fen_tokens = torch.LongTensor(fen_tokens_uint8).unsqueeze(0)
        
        if debug:
            print(f"[WINNER DEBUG] Input shape: {torch.cat([recent_moves_tokens, fen_tokens], dim=1).shape}")
            print(f"[WINNER DEBUG] Recent moves tokens: {recent_moves_tokens.shape}")
            print(f"[WINNER DEBUG] Board tokens: {fen_tokens.shape}")
        
        # Combine tokens
        input_ids = torch.cat([recent_moves_tokens, fen_tokens], dim=1)
        attention_mask = torch.ones_like(input_ids)
        
        # Prepare features
        features = self.prepare_features(
            white_rating, black_rating, 
            white_clock, black_clock, increment
        )
        features_tensor = torch.FloatTensor(features).unsqueeze(0)
        
        if debug:
            print(f"[WINNER DEBUG] Features: WR={white_rating}, BR={black_rating}, "
                  f"WC={white_clock:.1f}s, BC={black_clock:.1f}s, Inc={increment}s")
            print(f"[WINNER DEBUG] Scaled features: {features}")
        
        # Move to device
        input_ids = input_ids.to(self.device)
        attention_mask = attention_mask.to(self.device)
        features_tensor = features_tensor.to(self.device)
        
        # Run inference
        import time
        inference_start = time.time()
        
        with torch.no_grad():
            logits = self.model(input_ids, attention_mask, features_tensor)
            # Get probabilities for each class: 0=black win, 1=draw, 2=white win
            probs = F.softmax(logits, dim=-1)
            black_prob = probs[0, 0].item()
            draw_prob = probs[0, 1].item()
            white_prob = probs[0, 2].item()
        
        inference_time_ms = (time.time() - inference_start) * 1000
        
        if debug:
            print(f"[WINNER DEBUG] Model inference time: {inference_time_ms:.1f}ms")
            print(f"[WINNER DEBUG] Logits: black={logits[0, 0].item():.4f}, draw={logits[0, 1].item():.4f}, white={logits[0, 2].item():.4f}")
            print(f"[WINNER DEBUG] Probabilities: black={black_prob:.4f}, draw={draw_prob:.4f}, white={white_prob:.4f}")
        
        # Calculate eval value using the formula:
        # eval_value = black_prob * -1 + white_prob + draw_prob * 0.5
        # This maps to [-1, 1] range:
        # -1 = black wins, 0 = draw, +1 = white wins
        eval_value = black_prob * -1 + white_prob + draw_prob * 0.5
        
        info = {
            'eval_value': eval_value,
            'black_prob': black_prob,
            'draw_prob': draw_prob,
            'white_prob': white_prob,
            'white_rating': white_rating,
            'black_rating': black_rating,
            'white_clock': white_clock,
            'black_clock': black_clock,
            'increment': increment,
            'inference_time_ms': inference_time_ms
        }
        
        return eval_value, info
