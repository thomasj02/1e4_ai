import unittest
import numpy as np
import torch
import pickle
import os
import tokenizer
import tempfile
import chess
import json
import pytest

pytest.importorskip("chessmimic_core")
pytestmark = pytest.mark.cpp

from MoveDataset import BagzDataset
from bagz import BagWriter, BagReader

class TestMoveDataset(unittest.TestCase):
    """Tests for MoveDataset.py to ensure compatibility when switching to C++ tokenizer."""
    
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
        
    def test_dataset_length(self):
        """Test that the dataset length is correct."""
        # Dataset length should match the number of entries in the bagz file
        self.assertGreaterEqual(len(self.dataset), 1, "Dataset should have at least one entry")
        
    def test_prepare_recent_moves_tokens(self):
        """Test the _prepare_recent_moves_tokens method."""
        # Test with fewer than 12 moves
        recent_moves = ["e2e4", "e7e5", "g1f3"]
        tokens = BagzDataset._prepare_recent_moves_tokens(recent_moves)
        self.assertEqual(len(tokens), 12, "Should have 12 tokens")
        
        # Check that the last 3 tokens correspond to the moves
        self.assertEqual(tokens[-3:].tolist(), 
                         [tokenizer.MOVE_TO_ACTION[m] for m in recent_moves],
                         "Last 3 tokens should match the moves")
        
        # Check that the padding is correct
        self.assertTrue(np.all(tokens[:-3] == tokenizer.PAD_TOKEN), 
                        "First 9 tokens should be padded")
        
        # Test with more than 12 moves
        recent_moves = ["e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "g8f6", 
                        "d2d3", "f8c5", "c2c3", "d7d6", "e1g1", "e8g8",
                        "h2h3", "a7a6"]
        tokens = BagzDataset._prepare_recent_moves_tokens(recent_moves)
        self.assertEqual(len(tokens), 12, "Should have 12 tokens")
        
        # Check that only the last 12 moves are included
        self.assertEqual(tokens.tolist(), 
                         [tokenizer.MOVE_TO_ACTION[m] for m in recent_moves[-12:]],
                         "Should include only the last 12 moves")
    
    def test_recent_moves_and_fen_to_inputs(self):
        """Test the recent_moves_and_fen_to_inputs method."""
        # Test with a standard position
        recent_moves = ["e2e4", "e7e5"]
        fen = "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2"
        
        recent_moves_tokens, fen_tokens, move_mask = BagzDataset.recent_moves_and_fen_to_inputs(
            recent_moves, fen)
        
        # Check types and shapes
        self.assertIsInstance(recent_moves_tokens, torch.Tensor, "recent_moves_tokens should be a torch.Tensor")
        self.assertIsInstance(fen_tokens, torch.Tensor, "fen_tokens should be a torch.Tensor")
        self.assertIsInstance(move_mask, np.ndarray, "move_mask should be a numpy array")
        
        self.assertEqual(recent_moves_tokens.shape, (12,), "recent_moves_tokens should have shape (12,)")
        self.assertEqual(fen_tokens.shape, (tokenizer.SEQUENCE_LENGTH,), 
                        f"fen_tokens should have shape ({tokenizer.SEQUENCE_LENGTH},)")
        self.assertEqual(move_mask.shape, (tokenizer.NUM_ACTIONS,), 
                        f"move_mask should have shape ({tokenizer.NUM_ACTIONS},)")
        
        # Check that the move mask has some legal moves
        self.assertGreater(move_mask.sum(), 0, "Move mask should have at least one legal move")
        
    def test_getitem(self):
        """Test the __getitem__ method."""
        # Get an item from the dataset
        item = self.dataset[0]
        
        # Check that it returns the expected number of elements
        self.assertEqual(len(item), 6, "Should return 6 elements")
        
        # Check the types and shapes of each element
        recent_moves_tokens, fen_tokens, scaled_rating, scaled_log_time, move_mask, output_move = item
        
        self.assertIsInstance(recent_moves_tokens, torch.Tensor, "recent_moves_tokens should be a torch.Tensor")
        self.assertIsInstance(fen_tokens, torch.Tensor, "fen_tokens should be a torch.Tensor")
        self.assertIsInstance(scaled_rating, float, "scaled_rating should be a float")
        self.assertIsInstance(scaled_log_time, float, "scaled_log_time should be a float")
        self.assertIsInstance(move_mask, np.ndarray, "move_mask should be a numpy array")
        self.assertIsInstance(output_move, int, "output_move should be an int")
        
        self.assertEqual(recent_moves_tokens.shape, (12,), "recent_moves_tokens should have shape (12,)")
        self.assertEqual(fen_tokens.shape, (tokenizer.SEQUENCE_LENGTH,), 
                        f"fen_tokens should have shape ({tokenizer.SEQUENCE_LENGTH},)")
        self.assertEqual(move_mask.shape, (tokenizer.NUM_ACTIONS,), 
                        f"move_mask should have shape ({tokenizer.NUM_ACTIONS},)")
        
    def test_dataset_iteration(self):
        """Test that we can iterate through the dataset without errors."""
        # Iterate through the entire dataset
        for i in range(len(self.dataset)):
            item = self.dataset[i]
            
            # Check types and shapes for consistency
            self.assertEqual(len(item), 6, f"Item {i} should have 6 elements")
            self.assertEqual(item[0].shape, (12,), f"Item {i}: recent_moves_tokens should have shape (12,)")
            self.assertEqual(item[1].shape, (tokenizer.SEQUENCE_LENGTH,), 
                            f"Item {i}: fen_tokens should have shape ({tokenizer.SEQUENCE_LENGTH},)")
            self.assertIsInstance(item[2], float, f"Item {i}: scaled_rating should be a float")
            self.assertIsInstance(item[3], float, f"Item {i}: scaled_log_time should be a float")
            self.assertEqual(item[4].shape, (tokenizer.NUM_ACTIONS,), 
                            f"Item {i}: move_mask should have shape ({tokenizer.NUM_ACTIONS},)")
            self.assertIsInstance(item[5], int, f"Item {i}: output_move should be an int")


class TestMoveDatasetReferenceCompatibility(unittest.TestCase):
    """Test compatibility between original implementation and C++ module version."""
    
    def setUp(self):
        """Create test data."""
        # Test with a standard position
        self.recent_moves = ["e2e4", "e7e5", "g1f3", "b8c6"]
        self.fen = "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3"
    
    def test_tokenize_compatibility(self):
        """Test that tokenize outputs are identical between Python and C++ versions."""
        # Import both versions
        import tokenizer as py_tokenizer
        from chessmimic_core import tokenize as cpp_tokenize
        
        # Tokenize the FEN with both versions
        py_tokens = py_tokenizer.tokenize(self.fen)
        cpp_tokens = cpp_tokenize(self.fen)
        
        # Check that the outputs are identical
        np.testing.assert_array_equal(py_tokens, cpp_tokens, 
                                    "Python and C++ tokenize outputs should be identical")
    
    def test_move_to_action_compatibility(self):
        """Test that MOVE_TO_ACTION mappings are identical between Python and C++ versions."""
        # Import both versions
        import tokenizer as py_tokenizer
        from chessmimic_core import MOVE_TO_ACTION as cpp_move_to_action
        
        # Check that the dictionaries are identical
        self.assertEqual(dict(py_tokenizer.MOVE_TO_ACTION), dict(cpp_move_to_action),
                        "Python and C++ MOVE_TO_ACTION dictionaries should be identical")
        
        # Check specific moves
        for move in self.recent_moves:
            self.assertEqual(py_tokenizer.MOVE_TO_ACTION[move], cpp_move_to_action[move],
                           f"MOVE_TO_ACTION['{move}'] should be identical")
    
    def test_action_to_move_compatibility(self):
        """Test that ACTION_TO_MOVE mappings are identical between Python and C++ versions."""
        # Import both versions
        import tokenizer as py_tokenizer
        from chessmimic_core import ACTION_TO_MOVE as cpp_action_to_move
        
        # Check that the dictionaries are identical
        self.assertEqual(dict(py_tokenizer.ACTION_TO_MOVE), dict(cpp_action_to_move),
                        "Python and C++ ACTION_TO_MOVE dictionaries should be identical")
        
        # Check specific actions
        for move in self.recent_moves:
            action = py_tokenizer.MOVE_TO_ACTION[move]
            self.assertEqual(py_tokenizer.ACTION_TO_MOVE[action], cpp_action_to_move[action],
                           f"ACTION_TO_MOVE[{action}] should be identical")
    
    def test_recent_moves_and_fen_to_inputs_compatibility(self):
        """Test compatibility for recent_moves_and_fen_to_inputs function."""
        # Import tokenizer for original implementation reference
        import tokenizer as py_tokenizer
        
        # Create helper function to use Python tokenizer
        def reference_implementation(recent_moves, fen):
            # 1. Process recent moves efficiently
            num_moves = len(recent_moves)
            if num_moves < 12:
                # Create array for padding + moves
                move_tokens_array = np.full(12, py_tokenizer.PAD_TOKEN, dtype=np.int64)
                # Fill in only the necessary values, starting at the right position
                for i, move in enumerate(recent_moves):
                    move_tokens_array[12-num_moves+i] = py_tokenizer.MOVE_TO_ACTION[move]
            else:
                # Only use the last 12 moves
                move_tokens_array = np.zeros(12, dtype=np.int64)
                for i, move in enumerate(recent_moves[-12:]):
                    move_tokens_array[i] = py_tokenizer.MOVE_TO_ACTION[move]

            # 2. Parse board and get legal moves directly
            board = chess.Board(fen)

            # 3. Create move mask efficiently
            move_mask = np.zeros(py_tokenizer.NUM_ACTIONS, dtype=np.float32)
            # Process legal moves directly
            for move in board.legal_moves:
                move_str = str(move)
                move_mask[py_tokenizer.MOVE_TO_ACTION[move_str]] = 1.0

            # 4. Create tensors
            recent_moves_tokens = torch.LongTensor(move_tokens_array)
            fen_tokens = torch.LongTensor(py_tokenizer.tokenize(fen))

            return recent_moves_tokens, fen_tokens, move_mask
        
        # Get original implementation results
        orig_recent_moves_tokens, orig_fen_tokens, orig_move_mask = reference_implementation(
            self.recent_moves, self.fen)
        
        # Get MoveDataset.py implementation results
        md_recent_moves_tokens, md_fen_tokens, md_move_mask = BagzDataset.recent_moves_and_fen_to_inputs(
            self.recent_moves, self.fen)
        
        # Check that outputs are identical
        self.assertTrue(torch.equal(orig_recent_moves_tokens, md_recent_moves_tokens),
                       "recent_moves_tokens should be identical")
        self.assertTrue(torch.equal(orig_fen_tokens, md_fen_tokens),
                       "fen_tokens should be identical")
        np.testing.assert_array_equal(orig_move_mask, md_move_mask,
                                    "move_mask should be identical")


if __name__ == '__main__':
    unittest.main()
