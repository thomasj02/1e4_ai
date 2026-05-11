"""
Tests to verify that the Python and C++ implementations produce identical results.
"""

import os
import numpy as np
import torch
import random
import unittest
import chess
import pytest


pytest.importorskip("chessmimic_core")
pytestmark = pytest.mark.cpp

# First, load the Python implementation
os.environ['CHESSMIMIC_USE_PYTHON_TOKENIZER'] = 'true'
import MoveDataset as py_module

# Then, load the C++ implementation 
del os.environ['CHESSMIMIC_USE_PYTHON_TOKENIZER'] 
import importlib
import sys
if 'MoveDataset' in sys.modules:
    del sys.modules['MoveDataset']
import MoveDataset as cpp_module

# Also import the direct C++ functions for comparison
import chessmimic_core


class TestImplementationEquivalence(unittest.TestCase):
    """Test that both implementations produce identical results."""
    
    def setUp(self):
        """Set up test data."""
        # Valid UCI moves from MOVE_TO_ACTION
        self.valid_moves = list(py_module.MOVE_TO_ACTION.keys())
        
        # Valid FEN positions for testing
        self.test_fens = [
            'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1',  # Starting position
            'rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1',  # After 1.e4
            'rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2',  # After 1.e4 e5
            'rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2',  # After 1.e4 e5 2.Nf3
        ]
        
        # Generate test cases with different move counts (0 to 15 moves)
        self.test_cases = []
        for i in range(30):
            num_moves = random.randint(0, 15)
            moves = random.sample(self.valid_moves, min(num_moves, len(self.valid_moves)))
            fen = random.choice(self.test_fens)
            self.test_cases.append((moves, fen))
    
    def test_prepare_recent_moves_tokens(self):
        """Test that both _prepare_recent_moves_tokens implementations produce identical results."""
        for moves, _ in self.test_cases:
            # Python implementation (through MoveDataset interface)
            py_result = py_module.BagzDataset._prepare_recent_moves_tokens(moves)
            
            # C++ implementation (through MoveDataset interface)
            cpp_result = cpp_module.BagzDataset._prepare_recent_moves_tokens(moves)
            
            # Direct C++ implementation
            direct_cpp_result = chessmimic_core.prepare_recent_moves_tokens(moves)
            
            # Compare Python vs C++ through MoveDataset
            np.testing.assert_array_equal(
                py_result, cpp_result,
                f"Python and C++ implementations differ for moves: {moves}"
            )
            
            # Compare C++ through MoveDataset vs direct C++
            np.testing.assert_array_equal(
                cpp_result, direct_cpp_result,
                f"C++ implementations differ for moves: {moves}"
            )
    
    def test_recent_moves_and_fen_to_inputs(self):
        """Test that both recent_moves_and_fen_to_inputs implementations produce identical results."""
        for moves, fen in self.test_cases:
            # Python implementation (through MoveDataset interface)
            py_recent_moves, py_fen_tokens, py_move_mask = py_module.BagzDataset.recent_moves_and_fen_to_inputs(moves, fen)
            
            # C++ implementation (through MoveDataset interface)
            cpp_recent_moves, cpp_fen_tokens, cpp_move_mask = cpp_module.BagzDataset.recent_moves_and_fen_to_inputs(moves, fen)
            
            # Direct C++ implementation
            result_tuple = chessmimic_core.recent_moves_and_fen_to_inputs(moves, fen)
            direct_cpp_recent_moves, direct_cpp_fen_tokens, direct_cpp_move_mask = result_tuple
            
            # Convert to numpy arrays for comparison
            py_recent_moves_np = py_recent_moves.numpy()
            cpp_recent_moves_np = cpp_recent_moves.numpy()
            
            py_fen_tokens_np = py_fen_tokens.numpy()
            cpp_fen_tokens_np = cpp_fen_tokens.numpy()
            
            # Compare Python vs C++ through MoveDataset - recent moves tokens
            np.testing.assert_array_equal(
                py_recent_moves_np, cpp_recent_moves_np,
                f"Python and C++ recent_moves_tokens differ for moves: {moves}, fen: {fen}"
            )
            
            # Compare Python vs C++ through MoveDataset - FEN tokens
            np.testing.assert_array_equal(
                py_fen_tokens_np, cpp_fen_tokens_np,
                f"Python and C++ fen_tokens differ for moves: {moves}, fen: {fen}"
            )
            
            # Compare Python vs C++ through MoveDataset - move mask
            np.testing.assert_array_equal(
                py_move_mask, cpp_move_mask,
                f"Python and C++ move_mask differ for moves: {moves}, fen: {fen}"
            )
            
            # Compare C++ through MoveDataset vs direct C++ - recent moves tokens
            np.testing.assert_array_equal(
                cpp_recent_moves_np, direct_cpp_recent_moves,
                f"C++ implementations of recent_moves_tokens differ for moves: {moves}, fen: {fen}"
            )
            
            # Compare C++ through MoveDataset vs direct C++ - FEN tokens
            np.testing.assert_array_equal(
                cpp_fen_tokens_np, direct_cpp_fen_tokens,
                f"C++ implementations of fen_tokens differ for moves: {moves}, fen: {fen}"
            )
            
            # Compare C++ through MoveDataset vs direct C++ - move mask
            np.testing.assert_array_equal(
                cpp_move_mask, direct_cpp_move_mask,
                f"C++ implementations of move_mask differ for moves: {moves}, fen: {fen}"
            )


if __name__ == '__main__':
    unittest.main()
