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

class TestTokenizerCompare(unittest.TestCase):
    """Compare results between Python and C++ tokenizers in MoveDataset."""
    
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
        
    def test_compare_python_and_cpp_datasets(self):
        """Test that Python and C++ implementations produce identical results."""
        # Import MoveDataset twice - once with Python tokenizer, once with C++
        
        # First, use Python tokenizer
        os.environ['CHESSMIMIC_USE_PYTHON_TOKENIZER'] = 'true'
        import importlib
        import sys
        
        # Remove any existing imports to force reimport
        if 'MoveDataset' in sys.modules:
            del sys.modules['MoveDataset']
            
        import MoveDataset as py_module
        self.assertFalse(py_module.USING_CPP, "Should be using Python tokenizer")
        py_dataset = py_module.BagzDataset(self.bagz_path, self.scalers)
        
        # Now, use C++ tokenizer
        os.environ.pop('CHESSMIMIC_USE_PYTHON_TOKENIZER', None)
        if 'MoveDataset' in sys.modules:
            del sys.modules['MoveDataset']
            
        import MoveDataset as cpp_module
        self.assertTrue(cpp_module.USING_CPP, "Should be using C++ tokenizer")
        cpp_dataset = cpp_module.BagzDataset(self.bagz_path, self.scalers)
        
        # Compare results for all entries
        for i in range(len(py_dataset)):
            py_item = py_dataset[i]
            cpp_item = cpp_dataset[i]
            
            # Should have same number of elements
            self.assertEqual(len(py_item), len(cpp_item), f"Item {i} should have same number of elements")
            
            # Check each element
            for j in range(len(py_item)):
                if isinstance(py_item[j], (torch.Tensor, np.ndarray)):
                    # For tensors and arrays, contents should be equal
                    if isinstance(py_item[j], torch.Tensor):
                        py_array = py_item[j].numpy()
                        cpp_array = cpp_item[j].numpy()
                    else:
                        py_array = py_item[j]
                        cpp_array = cpp_item[j]
                        
                    np.testing.assert_array_equal(
                        py_array, cpp_array, 
                        f"Item {i}, element {j} should be identical between Python and C++ versions"
                    )
                else:
                    # For scalars, values should be equal
                    self.assertEqual(
                        py_item[j], cpp_item[j],
                        f"Item {i}, element {j} should be identical between Python and C++ versions"
                    )


if __name__ == '__main__':
    unittest.main()
