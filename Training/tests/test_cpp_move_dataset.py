import unittest
import numpy as np
import torch
import os
import tempfile
import pickle
import json
import chess
import pytest
from bagz import BagWriter, BagReader


pytest.importorskip("chessmimic_core")
pytestmark = pytest.mark.cpp

# Ensure we use the C++ module (default behavior now)
os.environ.pop('CHESSMIMIC_USE_PYTHON_TOKENIZER', None)

# Now import MoveDataset which should use C++ tokenizer
from MoveDataset import BagzDataset, USING_CPP
from chessmimic_core import tokenize, MOVE_TO_ACTION, ACTION_TO_MOVE, NUM_ACTIONS


class TestCppMoveDataset(unittest.TestCase):
    """Tests for MoveDataset.py with C++ tokenizer."""
    
    @classmethod
    def setUpClass(cls):
        """Set up test fixtures that are used by all tests."""
        # Create a temporary bagz file for testing
        cls.temp_dir = tempfile.TemporaryDirectory()
        cls.bagz_path = os.path.join(cls.temp_dir.name, "test.bagz")
        
        # Create test data and save to bagz file
        cls._create_test_bagz(cls.bagz_path)
        
        # Create fake scalers
        cls.scalers = {
            'rating': {'mean': 1500, 'std': 300},
            'log_time': {'mean': 1.0, 'std': 0.5}
        }
        
        # Save scalers to a pickle file
        cls.scalers_path = os.path.join(cls.temp_dir.name, "scalers.pkl")
        with open(cls.scalers_path, 'wb') as f:
            pickle.dump(cls.scalers, f)
        
        # Create dataset
        cls.dataset = BagzDataset(cls.bagz_path, cls.scalers)
        
    @classmethod
    def tearDownClass(cls):
        """Clean up after all tests."""
        cls.temp_dir.cleanup()
        
    @staticmethod
    def _create_test_bagz(bagz_path):
        """Create a test bagz file with sample data."""
        writer = BagWriter(bagz_path)
        
        # Add 10 sample entries
        for i in range(10):
            # Create a position and moves
            board = chess.Board()
            
            # Make some random moves
            recent_moves = []
            for _ in range(5):
                legal_moves = list(board.legal_moves)
                if not legal_moves:
                    break
                move = np.random.choice(legal_moves)
                recent_moves.append(str(move))
                board.push(move)
            
            # Get FEN from current position
            fen = board.fen()
            
            # Create moves data
            legal_moves = list(board.legal_moves)
            if not legal_moves:
                continue  # Skip if no legal moves
                
            moves = {}
            for move in legal_moves:
                move_str = str(move)
                # Each move has clock times
                clock_times = {}
                for clock_time in [0.1, 0.5, 1.0]:
                    # Each clock time has ratings
                    ratings = {}
                    for rating in [1200, 1500, 1800]:
                        # Number of times this move was played at this rating and clock time
                        count = np.random.randint(1, 10)
                        ratings[str(rating)] = count
                    clock_times[str(clock_time)] = ratings
                moves[move_str] = clock_times
            
            # Create entry
            entry = {
                'recent_and_fen': [recent_moves, fen],
                'moves': moves
            }
            
            # Add to bagz file
            writer.write(json.dumps(entry).encode())
        
        writer.close()
        
    def test_using_cpp_module(self):
        """Verify that MoveDataset is using the C++ module."""
        from MoveDataset import USING_CPP
        self.assertTrue(USING_CPP, "MoveDataset should be using the C++ module")
        
    def test_dataset_functionality(self):
        """Test basic dataset functionality."""
        # Get an item from the dataset
        item = self.dataset[0]
        
        # Check basic structure
        self.assertEqual(len(item), 6, "Should return 6 elements")
        
        # Check types
        recent_moves_tokens, fen_tokens, scaled_rating, scaled_log_time, move_mask, output_move = item
        self.assertIsInstance(recent_moves_tokens, torch.Tensor)
        self.assertIsInstance(fen_tokens, torch.Tensor)
        self.assertIsInstance(scaled_rating, float)
        self.assertIsInstance(scaled_log_time, float)
        self.assertIsInstance(move_mask, np.ndarray)
        self.assertIsInstance(output_move, int)
        
        # Check shapes
        self.assertEqual(recent_moves_tokens.shape, (12,), "recent_moves should have shape (12,)")
        self.assertTrue(fen_tokens.dim() == 1, "fen_tokens should be a 1D tensor")
        self.assertEqual(move_mask.shape, (NUM_ACTIONS,), f"move_mask should have shape ({NUM_ACTIONS},)")
        
        # Check that move mask has valid values
        self.assertTrue(np.all((move_mask == 0) | (move_mask == 1)), 
                        "Move mask should only contain 0 and 1")
        self.assertGreater(move_mask.sum(), 0, "Move mask should have at least one legal move")
        
    def test_dataset_iteration(self):
        """Test that we can iterate through the entire dataset."""
        for i in range(len(self.dataset)):
            item = self.dataset[i]
            self.assertEqual(len(item), 6, f"Item {i} should have 6 elements")
            
    def test_fen_tokenization(self):
        """Test that FEN tokenization works correctly with C++ module."""
        # Get a sample FEN
        item = self.dataset[0]
        
        # Extract the original FEN from the bagz file
        reader = BagReader(self.bagz_path)
        entry = reader[0]
        data = json.loads(entry)
        _, fen = data['recent_and_fen']
        
        # Tokenize the FEN with the C++ module directly
        cpp_tokens = tokenize(fen)
        
        # Get the tokens from the dataset item
        dataset_tokens = item[1].numpy()
        
        # Compare the tokens
        np.testing.assert_array_equal(cpp_tokens, dataset_tokens,
                                     "FEN tokens from dataset should match direct C++ tokenization")


if __name__ == '__main__':
    unittest.main()
