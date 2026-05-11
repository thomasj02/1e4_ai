"""
Fuzz testing for C++ and Python implementations of chess functions.

This script plays a series of random chess games and verifies that the C++ and Python 
implementations of tokenization and move handling functions produce identical results 
for each position. It's particularly useful for finding edge cases and unusual positions 
that might expose differences between implementations.
"""

import os
import numpy as np
import torch
import random
import chess
import time
import sys
from tqdm import tqdm

# First, load the Python implementation
os.environ['CHESSMIMIC_USE_PYTHON_TOKENIZER'] = 'true'
import MoveDataset as py_module
print(f"Using Python implementation: {not py_module.USING_CPP}")

# Then, load the C++ implementation 
del os.environ['CHESSMIMIC_USE_PYTHON_TOKENIZER'] 
import importlib
if 'MoveDataset' in sys.modules:
    del sys.modules['MoveDataset']
import MoveDataset as cpp_module
print(f"Using C++ implementation: {cpp_module.USING_CPP}")

# Define the functions to compare
def compare_prepare_recent_moves_tokens(moves):
    """Compare the Python and C++ implementations of _prepare_recent_moves_tokens."""
    py_result = py_module.BagzDataset._prepare_recent_moves_tokens(moves)
    cpp_result = cpp_module.BagzDataset._prepare_recent_moves_tokens(moves)
    
    if not np.array_equal(py_result, cpp_result):
        print(f"MISMATCH in _prepare_recent_moves_tokens for moves: {moves}")
        print(f"Python result: {py_result}")
        print(f"C++ result: {cpp_result}")
        return False
    return True

def compare_recent_moves_and_fen_to_inputs(moves, fen):
    """Compare the Python and C++ implementations of recent_moves_and_fen_to_inputs."""
    try:
        py_recent_moves, py_fen_tokens, py_move_mask = py_module.BagzDataset.recent_moves_and_fen_to_inputs(moves, fen)
        cpp_recent_moves, cpp_fen_tokens, cpp_move_mask = cpp_module.BagzDataset.recent_moves_and_fen_to_inputs(moves, fen)
        
        # Convert tensors to numpy arrays for comparison
        py_recent_moves_np = py_recent_moves.numpy() if isinstance(py_recent_moves, torch.Tensor) else py_recent_moves
        cpp_recent_moves_np = cpp_recent_moves.numpy() if isinstance(cpp_recent_moves, torch.Tensor) else cpp_recent_moves
        
        py_fen_tokens_np = py_fen_tokens.numpy() if isinstance(py_fen_tokens, torch.Tensor) else py_fen_tokens
        cpp_fen_tokens_np = cpp_fen_tokens.numpy() if isinstance(cpp_fen_tokens, torch.Tensor) else cpp_fen_tokens
        
        # Check recent moves
        if not np.array_equal(py_recent_moves_np, cpp_recent_moves_np):
            print(f"MISMATCH in recent_moves_tokens for moves: {moves}, fen: {fen}")
            print(f"Python result: {py_recent_moves_np}")
            print(f"C++ result: {cpp_recent_moves_np}")
            return False
        
        # Check FEN tokens
        if not np.array_equal(py_fen_tokens_np, cpp_fen_tokens_np):
            print(f"MISMATCH in fen_tokens for moves: {moves}, fen: {fen}")
            print(f"Python result shape: {py_fen_tokens_np.shape}")
            print(f"C++ result shape: {cpp_fen_tokens_np.shape}")
            print(f"First few differences:")
            for i in range(min(len(py_fen_tokens_np), len(cpp_fen_tokens_np))):
                if py_fen_tokens_np[i] != cpp_fen_tokens_np[i]:
                    print(f"  Index {i}: Python={py_fen_tokens_np[i]}, C++={cpp_fen_tokens_np[i]}")
            return False
        
        # Check move mask
        if not np.array_equal(py_move_mask, cpp_move_mask):
            print(f"MISMATCH in move_mask for moves: {moves}, fen: {fen}")
            print(f"Python result sum: {py_move_mask.sum()}")
            print(f"C++ result sum: {cpp_move_mask.sum()}")
            
            # Check which specific moves differ
            py_legal_moves = np.where(py_move_mask > 0)[0]
            cpp_legal_moves = np.where(cpp_move_mask > 0)[0]
            
            py_only = set(py_legal_moves) - set(cpp_legal_moves)
            cpp_only = set(cpp_legal_moves) - set(py_legal_moves)
            
            if py_only:
                print(f"Moves only in Python: {py_only}")
                for move_idx in py_only:
                    print(f"  {py_module.ACTION_TO_MOVE[move_idx]}")
            
            if cpp_only:
                print(f"Moves only in C++: {cpp_only}")
                for move_idx in cpp_only:
                    print(f"  {cpp_module.ACTION_TO_MOVE[move_idx]}")
                    
            return False
        
        return True
    except Exception as e:
        print(f"ERROR comparing recent_moves_and_fen_to_inputs for moves: {moves}, fen: {fen}")
        print(f"Exception: {e}")
        return False

def play_random_game():
    """Play a random chess game and compare implementations at each position."""
    board = chess.Board()
    moves_played = []
    
    # Track verification results
    all_match = True
    positions_verified = 0
    
    # Play random moves until the game ends or reaches 100 moves
    max_moves = 100
    for _ in range(max_moves):
        # Get current position
        fen = board.fen()
        
        # Verify implementations match for this position
        prepare_match = compare_prepare_recent_moves_tokens(moves_played)
        inputs_match = compare_recent_moves_and_fen_to_inputs(moves_played, fen)
        
        position_match = prepare_match and inputs_match
        all_match = all_match and position_match
        positions_verified += 1
        
        if not position_match:
            print(f"Failed at position {positions_verified} in game")
            print(f"FEN: {fen}")
            print(f"Moves played: {moves_played}")
            return False, positions_verified
        
        # Get legal moves and check if game is over
        legal_moves = list(board.legal_moves)
        if not legal_moves or board.is_game_over():
            break
        
        # Play a random move
        move = random.choice(legal_moves)
        moves_played.append(str(move))
        board.push(move)
    
    return all_match, positions_verified

def run_fuzz_testing(num_games=100):
    """Run fuzz testing with the specified number of random games."""
    print(f"Running fuzz testing with {num_games} random games...")
    
    total_positions = 0
    failures = 0
    
    start_time = time.time()
    
    for game in tqdm(range(num_games)):
        game_match, positions = play_random_game()
        total_positions += positions
        
        if not game_match:
            failures += 1
            print(f"Game {game+1} failed verification!")
    
    end_time = time.time()
    elapsed = end_time - start_time
    
    # Print summary
    print("\nFuzz Testing Summary:")
    print(f"Total games: {num_games}")
    print(f"Total positions verified: {total_positions}")
    print(f"Failed games: {failures}")
    print(f"Verification rate: {100 * (1 - failures/num_games):.2f}%")
    print(f"Time elapsed: {elapsed:.2f} seconds")
    print(f"Average positions per second: {total_positions / elapsed:.2f}")
    
    return failures == 0

def test_pathological_cases():
    """Test some specific pathological cases."""
    print("Testing pathological cases...")
    cases = [
        # Empty move list with various positions
        ([], 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1'),  # Initial position
        ([], 'rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2'),  # After e4 e5
        ([], '8/8/8/8/8/8/8/k6K w - - 0 100'),  # King endgame with high move number
        
        # Castling positions
        (['e2e4', 'e7e5', 'g1f3', 'b8c6', 'f1c4', 'g8f6', 'd2d3', 'f8c5', 'c2c3', 'd7d6', 'e1g1'], 
         'r1bqk2r/ppp2ppp/2np1n2/2b1p3/2B1P3/2PP1N2/PP3PPP/RNBQ1RK1 b kq - 1 6'),  # After white castling
        
        # En passant positions
        (['e2e4', 'c7c5', 'e4e5', 'd7d5'], 'rnbqkbnr/pp2pppp/8/2ppP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3'),  # En passant possible
        
        # Complex positions
        (['d2d4', 'g8f6', 'c2c4', 'e7e6', 'b1c3', 'f8b4', 'e2e3', 'e8g8', 'f1d3', 'd7d5', 'g1f3', 'c7c5'],
         'rnbq1rk1/pp3ppp/4pn2/2pp4/1bPP4/2NBPN2/PP3PPP/R1BQK2R w KQ - 0 7'),  # Complex middlegame
    ]
    
    all_pass = True
    for moves, fen in cases:
        print(f"Testing case: FEN={fen}, moves={moves[:3]}... (total={len(moves)})")
        prepare_match = compare_prepare_recent_moves_tokens(moves)
        inputs_match = compare_recent_moves_and_fen_to_inputs(moves, fen)
        
        case_pass = prepare_match and inputs_match
        all_pass = all_pass and case_pass
        
        if not case_pass:
            print(f"Pathological case failed: FEN={fen}")
    
    print(f"Pathological cases {'all pass' if all_pass else 'have failures'}.")
    return all_pass

if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description='Fuzz testing for chess implementations')
    parser.add_argument('--games', type=int, default=100, help='Number of random games to play')
    parser.add_argument('--pathological', action='store_true', help='Run pathological case testing')
    parser.add_argument('--seed', type=int, default=42, help='Random seed for reproducibility')
    
    args = parser.parse_args()
    
    # Set random seed for reproducibility
    random.seed(args.seed)
    np.random.seed(args.seed)
    
    all_tests_pass = True
    
    # Run pathological tests if requested
    if args.pathological:
        pathological_pass = test_pathological_cases()
        all_tests_pass = all_tests_pass and pathological_pass
    
    # Run standard fuzz testing
    fuzz_pass = run_fuzz_testing(args.games)
    all_tests_pass = all_tests_pass and fuzz_pass
    
    # Return success or failure
    if all_tests_pass:
        print("🎉 All tests passed! The implementations are equivalent.")
        sys.exit(0)
    else:
        print("❌ Some tests failed. The implementations differ.")
        sys.exit(1)