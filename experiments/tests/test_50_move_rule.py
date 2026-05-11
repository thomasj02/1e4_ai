#!/usr/bin/env python3
"""Test the 50-move rule with positions designed to avoid repetition."""

import chess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT / "backend"))
sys.path.insert(0, str(PROJECT_ROOT / "experiments"))

from rating_experiment import RatingExperiment

def test_50_move_positions():
    """Test various positions that should trigger the 50-move rule."""
    
    # Test positions designed to trigger 50-move rule
    test_positions = [
        # Position 1: Bishops on opposite colors with blocked pawns
        ("8/p1p1p1p1/1p1p1p1p/8/8/1P1P1P1P/P1P1P1P1/2B1B1K1 w - - 0 1", "Opposite color bishops, all pawns blocked"),
        
        # Position 2: Knights and kings with blocked pawns  
        ("8/pppppppp/8/8/8/8/PPPPPPPP/RN2K1NR w - - 0 1", "Knights and rooks, pawns blocked on starting ranks"),
        
        # Position 3: Bishops and knights with scattered blocked pawns
        ("r1b2b1r/1p1p1p1p/p1p1p1p1/8/8/P1P1P1P1/1P1P1P1P/R1B2B1R w - - 0 1", "Mixed pieces with blocked pawn chains"),
        
        # Position 4: Just kings and bishops on same color
        ("8/8/4k3/8/8/5K2/8/B5B1 w - - 0 1", "Same color bishops (can't capture black king)"),
    ]
    
    experiment = RatingExperiment(min_probability=0.01)
    
    for fen, description in test_positions:
        print(f"\nTesting: {description}")
        print(f"FEN: {fen}")
        print("="*60)
        
        # Test if position is valid
        try:
            board = chess.Board(fen)
            print(f"Starting position valid: ✓")
            print(f"Legal moves available: {len(list(board.legal_moves))}")
            
            # Count piece types
            piece_counts = {}
            for square in chess.SQUARES:
                piece = board.piece_at(square)
                if piece:
                    piece_type = piece.symbol().upper()
                    piece_counts[piece_type] = piece_counts.get(piece_type, 0) + 1
            print(f"Pieces: {piece_counts}")
            
            # Check if any pawns can move
            pawn_moves = 0
            for move in board.legal_moves:
                piece = board.piece_at(move.from_square)
                if piece and piece.piece_type == chess.PAWN:
                    pawn_moves += 1
            print(f"Pawn moves available: {pawn_moves}")
            
            # Simulate game from this position
            result, num_moves, moves, termination = simulate_from_position(
                experiment, fen, 1500, 1500
            )
            
            print(f"\nResult: {result}")
            print(f"Total moves: {num_moves}")
            print(f"Termination: {termination}")
            
            if termination == "fifty_moves":
                print("✓ Successfully triggered 50-move rule!")
                
                # Verify by replaying
                verify_board = chess.Board(fen)
                halfmove_at_end = None
                for i, move in enumerate(moves):
                    verify_board.push_san(move)
                    if i == len(moves) - 1:
                        halfmove_at_end = verify_board.halfmove_clock
                
                print(f"Halfmove clock at end: {halfmove_at_end}")
            else:
                print(f"✗ Did not trigger 50-move rule (ended with {termination})")
                
        except Exception as e:
            print(f"Error testing position: {e}")

def simulate_from_position(experiment, fen, white_rating, black_rating, initial_clock=600.0):
    """Simulate a game from a specific position."""
    board = chess.Board(fen)
    moves_san = []
    
    white_clock = initial_clock
    black_clock = initial_clock
    
    move_count = 0
    max_moves = 300  # Safety limit
    
    print("\nSimulating game...")
    last_halfmove_clock = 0
    
    while move_count < max_moves:
        # Track halfmove clock progress
        if board.halfmove_clock != last_halfmove_clock and board.halfmove_clock % 10 == 0:
            print(f"  Halfmove clock: {board.halfmove_clock}")
            last_halfmove_clock = board.halfmove_clock
        
        # White's turn
        if move_count % 2 == 0:
            move = experiment.get_move_for_player(board, moves_san, white_rating, white_clock)
            if move is None:
                break
            
            moves_san.append(move)
            board.push_san(move)
            
            white_clock -= 1.0
            if white_clock <= 0:
                return "0-1", move_count + 1, moves_san, "timeout"
        
        # Black's turn  
        else:
            move = experiment.get_move_for_player(board, moves_san, black_rating, black_clock)
            if move is None:
                break
            
            moves_san.append(move)
            board.push_san(move)
            
            black_clock -= 1.0
            if black_clock <= 0:
                return "1-0", move_count + 1, moves_san, "timeout"
        
        # Check if game is over
        if board.is_game_over():
            result = board.result()
            # Determine termination reason
            if board.is_checkmate():
                termination = "checkmate"
            elif board.is_stalemate():
                termination = "stalemate"
            elif board.is_insufficient_material():
                termination = "insufficient_material"
            elif board.is_fifty_moves():
                termination = "fifty_moves"
            elif board.is_repetition():
                termination = "repetition"
            else:
                termination = "other"
            return result, move_count + 1, moves_san, termination
        
        move_count += 1
    
    return "1/2-1/2", move_count, moves_san, "max_moves_safety"

def create_endgame_position():
    """Create a position more likely to reach 50-move rule: rook endgame."""
    print("\n" + "="*80)
    print("SPECIAL TEST: Rook endgame (often reaches 50-move rule)")
    print("="*80)
    
    # Rook endgame with no pawns - these often go 50 moves
    fen = "8/8/4k3/8/8/4K3/8/R6r w - - 0 1"
    
    experiment = RatingExperiment(min_probability=0.01)
    
    print(f"Testing rook endgame: {fen}")
    board = chess.Board(fen)
    print(f"Legal moves: {len(list(board.legal_moves))}")
    
    # Run multiple simulations to see if any trigger 50-move rule
    fifty_move_count = 0
    termination_counts = {}
    
    print("\nRunning 10 simulations...")
    for i in range(10):
        result, num_moves, moves, termination = simulate_from_position(
            experiment, fen, 1200, 1200  # Lower rating might help
        )
        
        termination_counts[termination] = termination_counts.get(termination, 0) + 1
        
        if termination == "fifty_moves":
            fifty_move_count += 1
            print(f"  Simulation {i+1}: ✓ 50-move rule! ({num_moves} moves)")
        else:
            print(f"  Simulation {i+1}: {termination} ({num_moves} moves)")
    
    print(f"\nResults: {fifty_move_count}/10 games ended with 50-move rule")
    print("Termination types:", termination_counts)

if __name__ == "__main__":
    print("Testing positions designed to trigger 50-move rule...")
    print("Note: Lower-rated play might be more likely to trigger it")
    
    test_50_move_positions()
    create_endgame_position()
