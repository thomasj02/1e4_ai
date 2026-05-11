"""Model inference for chess move prediction."""

import os
import time
import torch
import torch.nn as nn
import torch.nn.functional as F
import numpy as np
import pickle
import math
from pathlib import Path
import chess
import sys
from logging_config import get_logger

logger = get_logger(__name__)

# Add Training directory to path to import C++ module
training_path = Path(__file__).parent.parent / 'Training'
sys.path.insert(0, str(training_path))

# Import tokenization and move mappings from the C++ module
from chessmimic_core import (
    tokenize as tokenize_fen,  # Rename for consistency with existing code
    ACTION_TO_MOVE,
    MOVE_TO_ACTION,
    NUM_ACTIONS,
    INPUT_VOCAB_SIZE,
    SEQUENCE_LENGTH,
    CLASS_TOKEN,
    PAD_TOKEN
)

# Import model architecture from Training module to avoid duplication
from MoveTrainer import PatzerModel, MlpBlock, AttentionBlock


class ModelMovePredictor:
    """Chess move predictor using the trained PatzerModel."""
    
    def __init__(self, model_path: Path | str, scalers_path: Path | str):
        """Initialize the model predictor.
        
        Args:
            model_path: Path to the model checkpoint
            scalers_path: Path to the scalers pickle file
        """
        # Check environment variable for device preference
        force_cpu = os.getenv('CHESSMIMIC_FORCE_CPU', 'true').lower() == 'true'
        
        if force_cpu:
            self.device = torch.device('cpu')
            logger.info("Model device: CPU (forced via CHESSMIMIC_FORCE_CPU=true)")
        else:
            self.device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
            logger.info(f"Model device: {self.device}")
        
        # Load scalers
        with open(scalers_path, 'rb') as f:
            scalers = pickle.load(f)
        
        self.rating_mean = float(scalers['rating']['mean'])
        self.rating_std = float(scalers['rating']['std'])
        self.log_time_mean = float(scalers['log_time']['mean'])
        self.log_time_std = float(scalers['log_time']['std'])
        
        # Create model
        self.model = PatzerModel(
            recent_moves_sequence_length=12,
            recent_moves_vocab_size=NUM_ACTIONS,
            board_sequence_length=SEQUENCE_LENGTH,
            board_input_vocab_size=INPUT_VOCAB_SIZE,
            embedding_dim=256,
            widening_factor=4,
            num_layers=8,
            num_heads=8,
            output_vocab_size=NUM_ACTIONS
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
    
    def predict_move(self, fen: str, move_history: list[str] | None = None, 
                     rating: int = 1500, clock_time: float = 300.0,
                     min_probability: float = 0.0) -> tuple[str | None, dict[str, any]]:
        """Predict a chess move for the given position.
        
        Args:
            fen: Chess position in FEN format
            move_history: List of recent moves in UCI format (optional)
            rating: Player rating to simulate
            clock_time: Remaining clock time in seconds
            min_probability: Minimum probability threshold for moves (0.0-1.0)
            
        Returns:
            Tuple of (move in UCI format, info dict with probabilities)
        """
        # Start timing
        start_time = time.time()
        
        with torch.no_grad():
            # Prepare recent moves tokens
            if move_history is None:
                move_history = []
            
            # Convert UCI moves to action indices
            recent_moves_indices = []
            for move in move_history[-12:]:  # Take last 12 moves
                if move in MOVE_TO_ACTION:
                    recent_moves_indices.append(MOVE_TO_ACTION[move])
            
            # Pad to length 12
            while len(recent_moves_indices) < 12:
                recent_moves_indices.insert(0, PAD_TOKEN)
            
            recent_moves_tokens = torch.tensor([recent_moves_indices], dtype=torch.long).to(self.device)
            
            # Tokenize FEN
            fen_tokens_array = tokenize_fen(fen)
            fen_tokens = torch.tensor(fen_tokens_array[np.newaxis, :], dtype=torch.long).to(self.device)
            
            # Scale rating
            scaled_rating = (rating - self.rating_mean) / self.rating_std
            scaled_rating = torch.tensor([scaled_rating], dtype=torch.float32).to(self.device)
            
            # Scale log time
            log_time = math.log(1 + clock_time)
            scaled_log_time = (log_time - self.log_time_mean) / self.log_time_std
            scaled_log_time = torch.tensor([scaled_log_time], dtype=torch.float32).to(self.device)
            
            # Get legal moves mask
            board = chess.Board(fen)
            move_mask = torch.zeros(NUM_ACTIONS, dtype=torch.float32)
            legal_moves_uci = []
            
            for move in board.legal_moves:
                move_uci = move.uci()
                if move_uci in MOVE_TO_ACTION:
                    action_idx = MOVE_TO_ACTION[move_uci]
                    move_mask[action_idx] = 1.0
                    legal_moves_uci.append(move_uci)
            
            move_mask = move_mask.unsqueeze(0).to(self.device)
            
            # Run model
            model_start_time = time.time()
            logits = self.model(recent_moves_tokens, fen_tokens, scaled_rating, scaled_log_time)
            model_time = (time.time() - model_start_time) * 1000  # Convert to ms
            
            # Apply softmax and mask
            probabilities = F.softmax(logits, dim=1)
            probabilities = probabilities * move_mask
            probabilities = probabilities / probabilities.sum(dim=1, keepdim=True)
            
            # Sample from distribution
            probs_np = probabilities.cpu().numpy()[0]
            
            # Get top moves
            top_moves = []
            for move_uci in legal_moves_uci:
                action_idx = MOVE_TO_ACTION[move_uci]
                prob = probs_np[action_idx]
                if prob > 0:
                    top_moves.append((move_uci, prob))
            
            top_moves.sort(key=lambda x: x[1], reverse=True)
            
            if not top_moves:
                return None, {"error": "No legal moves found"}
            
            # Keep all moves for the info dict
            all_top_moves = top_moves.copy()
            
            # Filter by min_probability if specified
            if min_probability > 0:
                filtered_moves = [(move, prob) for move, prob in top_moves if prob >= min_probability]
                if filtered_moves:
                    top_moves = filtered_moves
                # If no moves meet the threshold, keep all moves (fallback)
            
            # Sample from distribution
            moves, probs = zip(*top_moves)
            # Normalize probabilities to ensure they sum to 1 (fixing floating point issues)
            probs_array = np.array(probs)
            probs_array = probs_array / probs_array.sum()
            chosen_idx = np.random.choice(len(moves), p=probs_array)
            chosen_move = moves[chosen_idx]
            
            # Calculate total inference time
            total_time = (time.time() - start_time) * 1000  # Convert to ms
            
            info = {
                "top_moves": all_top_moves[:5],  # Top 5 moves with probabilities (unfiltered)
                "chosen_probability": probs[chosen_idx],
                "rating_used": rating,
                "clock_time_used": clock_time,
                "inference_time_ms": round(total_time, 2),
                "model_forward_time_ms": round(model_time, 2),
                "num_legal_moves": len(legal_moves_uci),
                "min_probability_used": min_probability,
                "num_moves_after_filter": len(top_moves),
                "num_moves_filtered_out": len(all_top_moves) - len(top_moves)
            }
            
            return chosen_move, info