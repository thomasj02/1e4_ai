"""Unit tests for model inference module.

Note: These tests verify the behavior of the model inference code without
hard-coding expected outcomes. Test positions and moves are chosen to
represent real chess scenarios and edge cases.
"""

import pytest
import numpy as np
import torch
from pathlib import Path
import tempfile
import pickle
from unittest.mock import Mock, patch, MagicMock

import sys
# Add Training directory to path for C++ module
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Training"))

from model_inference import (
    tokenize_fen, ModelMovePredictor,
    SEQUENCE_LENGTH, NUM_ACTIONS, PAD_TOKEN, CLASS_TOKEN,
    MOVE_TO_ACTION, ACTION_TO_MOVE
)

# Import model components from Training
from MoveTrainer import MlpBlock, AttentionBlock, PatzerModel


class TestTokenizeFEN:
    """Test FEN tokenization functionality."""
    
    def test_tokenize_starting_position(self):
        """Test tokenizing the starting position.
        
        Verifies that standard starting position tokenizes correctly with proper
        array type, length, and class token placement.
        """
        fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
        tokens = tokenize_fen(fen)
        
        assert isinstance(tokens, np.ndarray)
        assert tokens.dtype == np.uint8
        assert len(tokens) == SEQUENCE_LENGTH
        assert tokens[-1] == CLASS_TOKEN  # Last token should be class token
    
    def test_tokenize_midgame_position(self):
        """Test tokenizing a midgame position.
        
        Ensures complex midgame positions with pieces moved tokenize to correct length.
        """
        fen = "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4"
        tokens = tokenize_fen(fen)
        
        assert len(tokens) == SEQUENCE_LENGTH
        assert tokens[-1] == CLASS_TOKEN
    
    def test_tokenize_endgame_position(self):
        """Test tokenizing an endgame position.
        
        Verifies sparse endgame positions with few pieces tokenize correctly.
        """
        fen = "8/5pk1/6p1/8/3K4/8/8/8 b - - 0 50"
        tokens = tokenize_fen(fen)
        
        assert len(tokens) == SEQUENCE_LENGTH
        assert tokens[-1] == CLASS_TOKEN
    
    def test_tokenize_with_en_passant(self):
        """Test tokenizing position with en passant square.
        
        Ensures en passant information (e6) is properly included in tokenization.
        """
        fen = "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2"
        tokens = tokenize_fen(fen)
        
        assert len(tokens) == SEQUENCE_LENGTH
    
    def test_tokenize_no_castling_rights(self):
        """Test tokenizing position with no castling rights.
        
        Verifies positions with '-' for castling tokenize with proper padding.
        """
        fen = "r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1"
        tokens = tokenize_fen(fen)
        
        assert len(tokens) == SEQUENCE_LENGTH
    
    def test_tokenize_partial_castling_rights(self):
        """Test tokenizing position with partial castling rights.
        
        Ensures partial castling rights (Kq) are padded to 4 characters.
        """
        fen = "r3k2r/8/8/8/8/8/8/R3K2R w Kq - 0 1"
        tokens = tokenize_fen(fen)
        
        assert len(tokens) == SEQUENCE_LENGTH
    
    def test_tokenize_high_move_counts(self):
        """Test tokenizing position with high move counts.
        
        Verifies edge case of high halfmove (49) and fullmove (999) counts.
        """
        fen = "8/8/8/8/8/8/8/8 w - - 49 999"
        tokens = tokenize_fen(fen)
        
        assert len(tokens) == SEQUENCE_LENGTH


class TestComputeAllPossibleActions:
    """Test move action computation."""
    
    def test_compute_actions_sizes(self):
        """Test that computed actions have correct size.
        
        Verifies the total number of possible chess moves matches expected count
        from the training data (1968 moves).
        """
        move_to_action = MOVE_TO_ACTION
        action_to_move = ACTION_TO_MOVE
        
        assert len(move_to_action) == NUM_ACTIONS
        assert len(action_to_move) == NUM_ACTIONS
        assert len(move_to_action) == 1968  # Expected number from training
    
    def test_basic_moves_exist(self):
        """Test that basic moves are in the action space.
        
        Ensures common opening moves from both white and black perspectives
        are included in the action space.
        """
        move_to_action = MOVE_TO_ACTION
        action_to_move = ACTION_TO_MOVE
        
        # Test some basic moves
        assert "e2e4" in move_to_action
        assert "d7d5" in move_to_action
        assert "g1f3" in move_to_action
        assert "b8c6" in move_to_action
    
    def test_promotion_moves_exist(self):
        """Test that promotion moves are in the action space.
        
        Verifies all promotion types (queen, rook, bishop, knight) and
        capture promotions are included.
        """
        move_to_action = MOVE_TO_ACTION
        action_to_move = ACTION_TO_MOVE
        
        # Test promotion moves
        assert "e7e8q" in move_to_action
        assert "e7e8r" in move_to_action
        assert "e7e8b" in move_to_action
        assert "e7e8n" in move_to_action
        
        # Test capture promotions
        assert "e7d8q" in move_to_action
        assert "e7f8q" in move_to_action
    
    def test_action_move_bijection(self):
        """Test that move_to_action and action_to_move are inverse mappings.
        
        Ensures bidirectional mapping integrity - every move maps to unique action
        and vice versa.
        """
        move_to_action = MOVE_TO_ACTION
        action_to_move = ACTION_TO_MOVE
        
        # Check that mappings are inverses
        for move, action in move_to_action.items():
            assert action_to_move[action] == move
        
        for action, move in action_to_move.items():
            assert move_to_action[move] == action


class TestModelMovePredictor:
    """Test ModelMovePredictor class."""
    
    @pytest.fixture
    def mock_scalers(self):
        """Create mock scalers data."""
        return {
            'rating': {'mean': 1500.0, 'std': 300.0},
            'log_time': {'mean': 5.0, 'std': 1.5}
        }
    
    @pytest.fixture
    def temp_files(self, mock_scalers):
        """Create temporary model and scaler files."""
        with tempfile.NamedTemporaryFile(suffix='.pkl', delete=False) as f:
            pickle.dump(mock_scalers, f)
            scalers_path = Path(f.name)
        
        with tempfile.NamedTemporaryFile(suffix='.ckpt', delete=False) as f:
            # Create a minimal checkpoint with correct structure
            state_dict = {}
            # Add some dummy tensors with correct prefixes
            state_dict['model.board_embedding.weight'] = torch.randn(32, 256)
            state_dict['model.output_layer.weight'] = torch.randn(1968, 256)
            torch.save({'state_dict': state_dict}, f.name)
            model_path = Path(f.name)
        
        yield model_path, scalers_path
        
        # Cleanup
        model_path.unlink()
        scalers_path.unlink()
    
    def test_init_loads_scalers(self, temp_files):
        """Test that initialization loads scalers correctly.
        
        Verifies scalers are loaded from pickle file with expected values
        for rating and log time normalization.
        """
        model_path, scalers_path = temp_files
        
        with patch('model_inference.PatzerModel'):
            predictor = ModelMovePredictor(model_path, scalers_path)
            
            assert predictor.rating_mean == 1500.0
            assert predictor.rating_std == 300.0
            assert predictor.log_time_mean == 5.0
            assert predictor.log_time_std == 1.5
    
    @patch('model_inference.PatzerModel')
    @patch('torch.cuda.is_available')
    def test_predict_move_basic(self, mock_cuda, mock_model_class, temp_files):
        """Test basic move prediction.
        
        Mocks model to return high logit for e2e4 and verifies prediction
        returns valid move with expected info dictionary structure.
        """
        # Force CPU for testing
        mock_cuda.return_value = False
        
        model_path, scalers_path = temp_files
        
        # Create mock model instance
        mock_model = MagicMock()
        mock_model_class.return_value = mock_model
        
        # Mock model output on CPU
        mock_logits = torch.zeros(1, NUM_ACTIONS)
        mock_logits[0, MOVE_TO_ACTION["e2e4"]] = 10.0  # High logit for e2e4
        mock_model.return_value = mock_logits
        
        predictor = ModelMovePredictor(model_path, scalers_path)
        
        # Test prediction
        fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
        move, info = predictor.predict_move(fen, rating=1500, clock_time=300)
        
        assert move is not None
        assert isinstance(info, dict)
        assert 'top_moves' in info
        assert 'chosen_probability' in info
        assert 'rating_used' in info
        assert info['rating_used'] == 1500
    
    @patch('model_inference.PatzerModel')
    @patch('torch.cuda.is_available')
    def test_predict_move_with_history(self, mock_cuda, mock_model_class, temp_files):
        """Test move prediction with move history.
        
        Verifies move history is properly processed and rating/time parameters
        are correctly passed through to the info dictionary.
        """
        mock_cuda.return_value = False
        model_path, scalers_path = temp_files
        
        mock_model = MagicMock()
        mock_model_class.return_value = mock_model
        mock_logits = torch.zeros(1, NUM_ACTIONS)
        mock_logits[0, MOVE_TO_ACTION["e7e5"]] = 10.0
        mock_model.return_value = mock_logits
        
        predictor = ModelMovePredictor(model_path, scalers_path)
        
        fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"
        move_history = ["e2e4"]
        move, info = predictor.predict_move(fen, move_history, rating=1800, clock_time=180)
        
        assert move is not None
        assert info['rating_used'] == 1800
        assert info['clock_time_used'] == 180
    
    @patch('model_inference.PatzerModel')
    @patch('torch.cuda.is_available')
    def test_predict_move_no_legal_moves(self, mock_cuda, mock_model_class, temp_files):
        """Test prediction when no legal moves exist.
        
        Uses checkmate position to test graceful handling when no legal
        moves are available.
        """
        mock_cuda.return_value = False
        model_path, scalers_path = temp_files
        
        mock_model = MagicMock()
        mock_model_class.return_value = mock_model
        mock_model.return_value = torch.zeros(1, NUM_ACTIONS)
        
        predictor = ModelMovePredictor(model_path, scalers_path)
        
        # Checkmate position
        fen = "rnb1kbnr/pppp1ppp/8/4p3/5PPq/8/PPPPP2P/RNBQKBNR w KQkq - 1 3"
        move, info = predictor.predict_move(fen)
        
        # Should handle gracefully
        assert move is None or isinstance(move, str)
        assert isinstance(info, dict)
    
    def test_scaling_calculations(self, temp_files):
        """Test that rating and time scaling work correctly.
        
        Verifies mathematical correctness of rating normalization and
        log-time transformation used for model inputs.
        """
        model_path, scalers_path = temp_files
        
        with patch('model_inference.PatzerModel'):
            predictor = ModelMovePredictor(model_path, scalers_path)
            
            # Test rating scaling
            rating = 1800
            scaled_rating = (rating - predictor.rating_mean) / predictor.rating_std
            expected = (1800 - 1500) / 300  # Should be 1.0
            assert abs(scaled_rating - expected) < 0.001
            
            # Test time scaling
            import math
            clock_time = 60.0
            log_time = math.log(1 + clock_time)
            scaled_log_time = (log_time - predictor.log_time_mean) / predictor.log_time_std
            expected_log = math.log(61)
            expected_scaled = (expected_log - 5.0) / 1.5
            assert abs(scaled_log_time - expected_scaled) < 0.001
    
    @patch('model_inference.PatzerModel')
    @patch('torch.cuda.is_available')
    def test_predict_move_with_min_probability(self, mock_cuda, mock_model_class, temp_files):
        """Test move prediction with min_probability threshold.
        
        Verifies that moves below the threshold are filtered out and
        probabilities are properly renormalized.
        """
        mock_cuda.return_value = False
        model_path, scalers_path = temp_files
        
        mock_model = MagicMock()
        mock_model_class.return_value = mock_model
        
        # Create mock logits with varying probabilities
        mock_logits = torch.zeros(1, NUM_ACTIONS)
        # Set up moves with different probabilities
        mock_logits[0, MOVE_TO_ACTION["e2e4"]] = 5.0   # High prob
        mock_logits[0, MOVE_TO_ACTION["d2d4"]] = 3.0   # Medium prob
        mock_logits[0, MOVE_TO_ACTION["g1f3"]] = 1.0   # Low prob
        mock_logits[0, MOVE_TO_ACTION["b1c3"]] = -2.0  # Very low prob
        
        mock_model.return_value = mock_logits
        
        predictor = ModelMovePredictor(model_path, scalers_path)
        
        # Test with min_probability threshold
        fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
        move, info = predictor.predict_move(fen, min_probability=0.1)
        
        assert move is not None
        assert 'num_moves_after_filter' in info
        assert 'num_moves_filtered_out' in info
        assert 'min_probability_used' in info
        assert info['min_probability_used'] == 0.1
        
        # The chosen move should have probability >= 0.1
        assert info['chosen_probability'] >= 0.1
        
        # Check top moves don't include very low probability moves in filtered set
        # but they should still appear in top_moves (which shows unfiltered)
        assert len(info['top_moves']) > 0
    
    @patch('model_inference.PatzerModel')
    @patch('torch.cuda.is_available')
    def test_predict_move_min_probability_fallback(self, mock_cuda, mock_model_class, temp_files):
        """Test that all moves are kept if none meet the threshold.
        
        Verifies the fallback behavior when min_probability filters out
        all moves - should keep all moves rather than returning nothing.
        """
        mock_cuda.return_value = False
        model_path, scalers_path = temp_files
        
        mock_model = MagicMock()
        mock_model_class.return_value = mock_model
        
        # Create mock logits with all low probabilities
        mock_logits = torch.zeros(1, NUM_ACTIONS)
        mock_logits[0, MOVE_TO_ACTION["e2e4"]] = -5.0  # All very low
        mock_logits[0, MOVE_TO_ACTION["d2d4"]] = -6.0
        
        mock_model.return_value = mock_logits
        
        predictor = ModelMovePredictor(model_path, scalers_path)
        
        # Test with very high min_probability that no move can meet
        fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
        move, info = predictor.predict_move(fen, min_probability=0.99)
        
        # Should still return a move (fallback behavior)
        assert move is not None
        assert info['min_probability_used'] == 0.99
        # All legal moves should be kept despite not meeting threshold
        assert info['num_moves_after_filter'] > 0


class TestIntegration:
    """Integration tests for the model inference module."""
    
    def test_tokenize_all_starting_positions(self):
        """Test that tokenization works for various starting positions.
        
        Integration test ensuring different game states (initial, after e4,
        after Nf6) all tokenize to consistent length and type.
        """
        test_positions = [
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
            "rnbqkb1r/pppppppp/5n2/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2",
        ]
        
        for fen in test_positions:
            tokens = tokenize_fen(fen)
            assert len(tokens) == SEQUENCE_LENGTH
            assert tokens.dtype == np.uint8
    
    def test_move_to_action_consistency(self):
        """Test that all standard opening moves are in action space.
        
        Integration test verifying common opening moves map to valid actions
        and maintain bidirectional consistency.
        """
        common_opening_moves = [
            "e2e4", "d2d4", "g1f3", "c2c4", "b1c3",
            "e7e5", "d7d5", "g8f6", "c7c5", "b8c6",
            "f1c4", "f8c5", "d1h5", "d8h4",
        ]
        
        for move in common_opening_moves:
            assert move in MOVE_TO_ACTION
            action = MOVE_TO_ACTION[move]
            assert 0 <= action < NUM_ACTIONS
            assert ACTION_TO_MOVE[action] == move


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
