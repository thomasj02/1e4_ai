"""
Tests for MoveDataset methods to be converted to C++:
- _prepare_recent_moves_tokens
- recent_moves_and_fen_to_inputs
"""

import unittest
import numpy as np
import torch
import chess
import random
import time
from MoveDataset import BagzDataset
import os
# Ensure we use the Python tokenizer for reference implementation
os.environ['CHESSMIMIC_USE_PYTHON_TOKENIZER'] = 'true'
import tokenizer


class TestMoveDatasetMethods(unittest.TestCase):
    """Tests for MoveDataset methods that will be converted to C++."""
    
    def setUp(self):
        """Set up test data."""
        # Sample chess moves for testing
        self.sample_moves = [
            'e2e4', 'e7e5', 'g1f3', 'b8c6', 'f1c4', 'g8f6', 
            'd2d3', 'f8c5', 'c2c3', 'd7d6', 'e1g1', 'e8g8'
        ]
        
        # Sample FEN strings for testing
        self.sample_fens = [
            'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1',  # Starting position
            'rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2',  # After 1.e4 e5
            'r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3',  # After 1.e4 e5 2.Nf3 Nc6
            'r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2PP1N2/PP3PPP/RNBQK2R b KQkq - 0 5',  # After several more moves
            '8/8/8/8/8/8/8/4K3 w - - 0 1',  # King only endgame
            '8/8/8/8/8/8/8/4K3 w - - 0 100',  # King only endgame with high move number
            'r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1',  # Complex middle game position
        ]
    
    def test_prepare_recent_moves_tokens_empty(self):
        """Test _prepare_recent_moves_tokens with empty input."""
        tokens = BagzDataset._prepare_recent_moves_tokens([])
        self.assertEqual(tokens.shape, (12,), "Should have shape (12,)")
        self.assertEqual(tokens.dtype, np.int64, "Should have dtype int64")
        self.assertTrue(np.all(tokens == tokenizer.PAD_TOKEN), "All tokens should be PAD_TOKEN")
    
    def test_prepare_recent_moves_tokens_small_input(self):
        """Test _prepare_recent_moves_tokens with fewer than 12 moves."""
        for i in range(1, 12):
            moves = self.sample_moves[:i]
            tokens = BagzDataset._prepare_recent_moves_tokens(moves)
            
            # Check shape, type
            self.assertEqual(tokens.shape, (12,), f"With {i} moves, should have shape (12,)")
            self.assertEqual(tokens.dtype, np.int64, f"With {i} moves, should have dtype int64")
            
            # Check padding
            pad_count = 12 - i
            self.assertTrue(np.all(tokens[:pad_count] == tokenizer.PAD_TOKEN), 
                            f"With {i} moves, first {pad_count} tokens should be PAD_TOKEN")
            
            # Check move tokens
            for j in range(i):
                self.assertEqual(tokens[pad_count + j], tokenizer.MOVE_TO_ACTION[moves[j]],
                                f"With {i} moves, token at position {pad_count + j} should match move {moves[j]}")
    
    def test_prepare_recent_moves_tokens_exact_12(self):
        """Test _prepare_recent_moves_tokens with exactly 12 moves."""
        moves = self.sample_moves[:12]
        tokens = BagzDataset._prepare_recent_moves_tokens(moves)
        
        # Check shape, type
        self.assertEqual(tokens.shape, (12,), "With 12 moves, should have shape (12,)")
        self.assertEqual(tokens.dtype, np.int64, "With 12 moves, should have dtype int64")
        
        # Check move tokens
        for i in range(12):
            self.assertEqual(tokens[i], tokenizer.MOVE_TO_ACTION[moves[i]],
                            f"With 12 moves, token at position {i} should match move {moves[i]}")
    
    def test_prepare_recent_moves_tokens_large_input(self):
        """Test _prepare_recent_moves_tokens with more than 12 moves."""
        # Get valid moves from tokenizer
        valid_moves = list(tokenizer.MOVE_TO_ACTION.keys())

        for extra in range(1, 10):
            # Generate moves list with more than 12 moves using valid moves
            base_moves = self.sample_moves[:min(len(self.sample_moves), 12)]
            # Add additional valid moves
            additional_moves = [m for m in valid_moves if m not in base_moves][:extra]
            moves = base_moves + additional_moves
            self.assertGreater(len(moves), 12, "Should have more than 12 moves for this test")
            
            tokens = BagzDataset._prepare_recent_moves_tokens(moves)
            
            # Check shape, type
            self.assertEqual(tokens.shape, (12,), f"With {len(moves)} moves, should have shape (12,)")
            self.assertEqual(tokens.dtype, np.int64, f"With {len(moves)} moves, should have dtype int64")
            
            # Check that we only get the last 12 moves
            expected_moves = moves[-12:]
            for i in range(12):
                self.assertEqual(tokens[i], tokenizer.MOVE_TO_ACTION[expected_moves[i]],
                                f"With {len(moves)} moves, token at position {i} should match move {expected_moves[i]}")
    
    def test_recent_moves_and_fen_to_inputs_basic(self):
        """Test recent_moves_and_fen_to_inputs with basic inputs."""
        for fen in self.sample_fens:
            for num_moves in [0, 1, 3, 5, 10]:
                moves = self.sample_moves[:num_moves]
                
                # Call the method
                recent_moves_tokens, fen_tokens, move_mask = BagzDataset.recent_moves_and_fen_to_inputs(moves, fen)
                
                # Check types
                self.assertIsInstance(recent_moves_tokens, torch.Tensor, "recent_moves_tokens should be a torch.Tensor")
                self.assertIsInstance(fen_tokens, torch.Tensor, "fen_tokens should be a torch.Tensor")
                self.assertIsInstance(move_mask, np.ndarray, "move_mask should be a numpy.ndarray")
                
                # Check shapes
                self.assertEqual(recent_moves_tokens.shape, (12,), 
                                f"recent_moves_tokens should have shape (12,)")
                self.assertEqual(fen_tokens.shape, (tokenizer.SEQUENCE_LENGTH,), 
                                f"fen_tokens should have shape ({tokenizer.SEQUENCE_LENGTH},)")
                self.assertEqual(move_mask.shape, (tokenizer.NUM_ACTIONS,), 
                                f"move_mask should have shape ({tokenizer.NUM_ACTIONS},)")
                
                # Check dtypes
                self.assertEqual(recent_moves_tokens.dtype, torch.int64, 
                                "recent_moves_tokens should have dtype torch.int64")
                self.assertEqual(fen_tokens.dtype, torch.int64, 
                                "fen_tokens should have dtype torch.int64")
                self.assertEqual(move_mask.dtype, np.float32, 
                                "move_mask should have dtype np.float32")
                
                # Check move mask values
                self.assertTrue(np.all(np.logical_or(move_mask == 0.0, move_mask == 1.0)), 
                                "move_mask values should be either 0.0 or 1.0")
                
                # Check that legal moves have a 1.0 in the mask if they are in the dictionary
                board = chess.Board(fen)
                legal_moves_count = 0
                for move in board.legal_moves:
                    move_str = str(move)
                    move_idx = tokenizer.MOVE_TO_ACTION[move_str]
                    # Only count the move if it's in the dictionary
                    if move_mask[move_idx] == 1.0:
                        legal_moves_count += 1
                
                # Ensure the mask values match expectations
                self.assertEqual(np.sum(move_mask), legal_moves_count, 
                                f"Sum of move_mask should equal number of marked legal moves ({legal_moves_count})")
    
    def test_recent_moves_and_fen_to_inputs_complex(self):
        """Test recent_moves_and_fen_to_inputs with complex inputs."""
        # Test with many moves (only use valid moves from MOVE_TO_ACTION)
        valid_moves = [move for move in self.sample_moves if move in tokenizer.MOVE_TO_ACTION]
        # Add some additional valid moves if we have them
        additional_moves = []
        for move in tokenizer.MOVE_TO_ACTION:
            if len(additional_moves) >= 4:
                break
            if move not in valid_moves:
                additional_moves.append(move)

        moves = valid_moves + additional_moves
        for fen in self.sample_fens:
            # Call the method
            recent_moves_tokens, fen_tokens, move_mask = BagzDataset.recent_moves_and_fen_to_inputs(moves, fen)
            
            # Check the recent moves match expectations
            expected_moves = moves[-12:] if len(moves) > 12 else moves
            expected_tokens = np.zeros(12, dtype=np.int64)
            if len(expected_moves) < 12:
                expected_tokens[:12-len(expected_moves)] = tokenizer.PAD_TOKEN
                for i, move in enumerate(expected_moves):
                    expected_tokens[12-len(expected_moves)+i] = tokenizer.MOVE_TO_ACTION[move]
            else:
                for i, move in enumerate(expected_moves):
                    expected_tokens[i] = tokenizer.MOVE_TO_ACTION[move]
            
            # Compare the tensors
            np.testing.assert_array_equal(recent_moves_tokens.numpy(), expected_tokens,
                                        "recent_moves_tokens doesn't match expected values")
    
    def test_recent_moves_and_fen_to_inputs_edge_cases(self):
        """Test recent_moves_and_fen_to_inputs with edge cases."""
        # Empty moves list
        for fen in self.sample_fens:
            recent_moves_tokens, fen_tokens, move_mask = BagzDataset.recent_moves_and_fen_to_inputs([], fen)
            expected_tokens = np.full(12, tokenizer.PAD_TOKEN, dtype=np.int64)
            np.testing.assert_array_equal(recent_moves_tokens.numpy(), expected_tokens,
                                        "recent_moves_tokens for empty moves list doesn't match expected values")
        
        # FEN with large numbers
        fen_large_numbers = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 99 999'
        recent_moves_tokens, fen_tokens, move_mask = BagzDataset.recent_moves_and_fen_to_inputs([], fen_large_numbers)
        self.assertEqual(fen_tokens.shape, (tokenizer.SEQUENCE_LENGTH,), 
                        "fen_tokens should have correct shape even with large numbers")
        
        # Complicated FEN with lots of pieces
        fen_complex = 'rnbqkbnr/pppppppp/nnnnnnnn/bbbbbbbb/BBBBBBBB/NNNNNNNN/PPPPPPPP/RNBQKBNR w KQkq - 0 1'
        recent_moves_tokens, fen_tokens, move_mask = BagzDataset.recent_moves_and_fen_to_inputs([], fen_complex)
        self.assertEqual(fen_tokens.shape, (tokenizer.SEQUENCE_LENGTH,), 
                        "fen_tokens should have correct shape even with complex FEN")


class BenchmarkMoveDatasetMethods(unittest.TestCase):
    """Benchmark for MoveDataset methods to be converted to C++."""
    
    def setUp(self):
        """Set up test data."""
        # Use only valid moves from the tokenizer's MOVE_TO_ACTION dictionary
        self.all_moves = list(tokenizer.MOVE_TO_ACTION.keys())
        
        # Generate random FEN strings
        self.test_fens = []
        
        # Add some valid FENs
        self.test_fens.extend([
            'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1',
            'rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2',
            'r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3',
        ])
        
        # Use real chess positions for accuracy
        board = chess.Board()
        for _ in range(50):
            legal_moves = list(board.legal_moves)
            if not legal_moves:
                break
            move = random.choice(legal_moves)
            board.push(move)
            self.test_fens.append(board.fen())
    
    def benchmark_prepare_recent_moves_tokens(self, num_iterations=10000):
        """Benchmark _prepare_recent_moves_tokens method."""
        # Prepare test data - various lengths of move lists
        test_cases = []
        for _ in range(num_iterations):
            num_moves = random.randint(0, 30)  # 0 to 30 moves
            moves = random.sample(self.all_moves, num_moves)
            test_cases.append(moves)
        
        # Run benchmark
        start_time = time.time()
        for moves in test_cases:
            BagzDataset._prepare_recent_moves_tokens(moves)
        end_time = time.time()
        
        total_time = end_time - start_time
        avg_time = total_time / num_iterations
        
        print(f"\n_prepare_recent_moves_tokens benchmark:")
        print(f"  Total time for {num_iterations} iterations: {total_time:.6f} seconds")
        print(f"  Average time per call: {avg_time*1000:.6f} ms")
        print(f"  Calls per second: {num_iterations/total_time:.1f}")
        
        return avg_time
    
    def benchmark_recent_moves_and_fen_to_inputs(self, num_iterations=1000):
        """Benchmark recent_moves_and_fen_to_inputs method."""
        # Prepare test data - various combinations of moves and FENs
        test_cases = []
        for _ in range(num_iterations):
            num_moves = random.randint(0, 20)  # 0 to 20 moves
            moves = random.sample(self.all_moves, num_moves)
            fen = random.choice(self.test_fens)
            test_cases.append((moves, fen))
        
        # Run benchmark
        start_time = time.time()
        for moves, fen in test_cases:
            BagzDataset.recent_moves_and_fen_to_inputs(moves, fen)
        end_time = time.time()
        
        total_time = end_time - start_time
        avg_time = total_time / num_iterations
        
        print(f"\nrecent_moves_and_fen_to_inputs benchmark:")
        print(f"  Total time for {num_iterations} iterations: {total_time:.6f} seconds")
        print(f"  Average time per call: {avg_time*1000:.6f} ms")
        print(f"  Calls per second: {num_iterations/total_time:.1f}")
        
        return avg_time


if __name__ == '__main__':
    # Run tests
    unittest.main(argv=['first-arg-is-ignored'], exit=False)
    
    # Run benchmarks
    print("\n=== Running Python Implementation Benchmarks ===")
    benchmark = BenchmarkMoveDatasetMethods()
    benchmark.setUp()
    benchmark.benchmark_prepare_recent_moves_tokens()
    benchmark.benchmark_recent_moves_and_fen_to_inputs()