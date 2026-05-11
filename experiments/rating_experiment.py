#!/usr/bin/env python3
"""Experiment to test how rating affects model performance.

Simulates games between different rated models and tracks rating changes using Glicko-2.
"""

import chess
import chess.pgn
import time
from pathlib import Path
from collections import defaultdict
import numpy as np
import matplotlib.pyplot as plt
from tqdm import tqdm
import csv
import os
from datetime import datetime

import sys

PROJECT_ROOT = Path(__file__).resolve().parents[1]
BACKEND_DIR = PROJECT_ROOT / "backend"
sys.path.insert(0, str(BACKEND_DIR))

from common_moves_db import load_common_moves_db
from model_inference import ModelMovePredictor

# Import glicko2 from local experiments directory
from glicko2 import Glicko2Player


class RatingExperiment:
    """Run experiments comparing different rating levels."""
    
    def __init__(self, min_probability: float = 0.01):
        """Initialize the experiment with model and common moves database.
        
        Args:
            min_probability: Minimum probability threshold for model moves (default 1%)
        """
        self.min_probability = min_probability
        
        # Load common moves database
        self.common_moves_db = load_common_moves_db()
        
        # Load model
        model_dir = Path(os.getenv(
            "CHESSMIMIC_EXPERIMENT_MOVE_MODEL_DIR",
            PROJECT_ROOT / "backend" / "models" / "move_model" / "1800_1900_brier",
        ))
        model_path = model_dir / "model.ckpt"
        scalers_path = model_dir / "scalers.pkl"
        
        if not model_path.exists() or not scalers_path.exists():
            raise FileNotFoundError(
                f"Model files not found in {model_dir}. Set CHESSMIMIC_EXPERIMENT_MOVE_MODEL_DIR "
                "to a directory containing model.ckpt and scalers.pkl."
            )
        
        print("Loading chess model...")
        self.model_predictor = ModelMovePredictor(model_path, scalers_path)
        print("Model loaded successfully")
        print(f"Minimum probability threshold: {min_probability:.1%}")
    
    def get_move_for_player(self, board: chess.Board, moves_san: list[str], 
                           rating: int, clock_time: float) -> str | None:
        """Get a move for a player with the given rating.
        
        Uses the same priority as the backend:
        1. Common moves database
        2. Model prediction
        """
        if board.is_game_over():
            return None
        
        fen = board.fen()
        
        # Try common moves database first
        common_move_uci, strategy, info = self.common_moves_db.sample_move(
            fen, 
            moves_san,
            move_format="san"
        )
        
        if common_move_uci and info:
            try:
                move = chess.Move.from_uci(common_move_uci)
                if move in board.legal_moves:
                    return board.san(move)
            except:
                pass
        
        # Use model prediction
        # Convert SAN moves to UCI for model
        uci_moves = None
        if moves_san:
            temp_board = chess.Board()
            uci_moves = []
            try:
                for san_move in moves_san:
                    move = temp_board.push_san(san_move)
                    uci_moves.append(move.uci())
            except:
                uci_moves = None
        
        # Get model prediction
        model_move_uci, model_info = self.model_predictor.predict_move(
            fen, 
            uci_moves,
            rating=rating,
            clock_time=clock_time,
            min_probability=self.min_probability
        )
        
        if model_move_uci:
            move = chess.Move.from_uci(model_move_uci)
            if move in board.legal_moves:
                return board.san(move)
        
        # Fallback - should not happen
        legal_moves = list(board.legal_moves)
        if legal_moves:
            return board.san(legal_moves[0])
        
        return None
    
    def simulate_game(self, white_rating: int, black_rating: int, 
                     initial_clock: float = 300.0) -> tuple[str, int, list[str], str]:
        """Simulate a single game between two players.
        
        Returns:
            Tuple of (result, num_moves, moves_san, termination_reason)
            result is "1-0", "0-1", "1/2-1/2"
            termination_reason is "checkmate", "stalemate", "insufficient_material", etc.
        """
        board = chess.Board()
        moves_san = []
        
        # Simple clock simulation - each player loses 1-2 seconds per move
        white_clock = initial_clock
        black_clock = initial_clock
        
        move_count = 0
        while True:
            # White's turn
            if move_count % 2 == 0:
                move = self.get_move_for_player(board, moves_san, white_rating, white_clock)
                if move is None:
                    break
                
                moves_san.append(move)
                board.push_san(move)
                
                # Update clock (random time between 1-2 seconds)
                white_clock -= np.random.uniform(1, 2)
                if white_clock <= 0:
                    return "0-1", move_count + 1, moves_san, "timeout"  # White loses on time
            
            # Black's turn
            else:
                move = self.get_move_for_player(board, moves_san, black_rating, black_clock)
                if move is None:
                    break
                
                moves_san.append(move)
                board.push_san(move)
                
                # Update clock
                black_clock -= np.random.uniform(1, 2)
                if black_clock <= 0:
                    return "1-0", move_count + 1, moves_san, "timeout"  # Black loses on time
            
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
            
            # Increment move counter
            move_count += 1
            
            # Safety check to prevent infinite games (should not happen with proper draw rules)
            if move_count > 500:
                return "1/2-1/2", move_count, moves_san, "safety_limit"
    
    def run_experiment_with_glicko2(self, num_games: int = 1000, rating1: int = 1700, rating2: int = 1800,
                                    initial_rd1: float = 500.0, initial_rd2: float = 500.0, 
                                    initial_vol: float = 0.06,
                                    initial_clock: float = 300.0, starting_rating: int = 1500,
                                    output_dir: str | None = None, save_games: bool = True,
                                    generate_plot: bool = True):
        """Run the rating experiment with Glicko-2 rating updates.
        
        Args:
            num_games: Number of games to simulate
            rating1: Model strength for player 1 (used for move selection)
            rating2: Model strength for player 2 (used for move selection)
            initial_rd1: Initial rating deviation for player 1
            initial_rd2: Initial rating deviation for player 2
            initial_vol: Initial volatility
            initial_clock: Initial clock time in seconds for each player
            starting_rating: Starting Glicko-2 rating for both players
            output_dir: Output directory for results (None for auto-generated)
            save_games: Whether to save individual game PGN files
            generate_plot: Whether to generate rating progression plot
        """
        
        print(f"\nRunning {num_games} games with Glicko-2 rating system")
        print(f"Model strengths: Player1={rating1} vs Player2={rating2}")
        print(f"Starting Glicko-2 ratings: Both players start at {starting_rating}±{1.96*max(initial_rd1,initial_rd2):.0f}")
        print(f"Initial volatility: {initial_vol}")
        print("=" * 60)
        
        # Create output directory
        if output_dir is None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            output_dir = Path(f"rating_experiment_{timestamp}")
        else:
            output_dir = Path(output_dir)
        
        output_dir.mkdir(exist_ok=True)
        
        if save_games:
            games_dir = output_dir / "games"
            games_dir.mkdir(exist_ok=True)
        
        print(f"Output directory: {output_dir}")
        
        # Create CSV file
        csv_path = output_dir / "game_log.csv"
        csv_file = open(csv_path, 'w', newline='')
        csv_writer = csv.writer(csv_file)
        csv_writer.writerow([
            "game_num", "white_player", "black_player", "white_model_strength", "black_model_strength",
            "result", "termination", "num_moves", 
            "p1_rating_before", "p2_rating_before", "p1_rd_before", "p2_rd_before",
            "p1_rating_after", "p2_rating_after", "p1_rd_after", "p2_rd_after",
            "p1_rating_change", "p2_rating_change",
            "p1_expected_score", "p2_expected_score",
            "game_phase", "p1_cumulative_score", "p2_cumulative_score"
        ])
        
        # Initialize Glicko-2 players at midpoint
        glicko_player1 = Glicko2Player(starting_rating, initial_rd1, initial_vol)
        glicko_player2 = Glicko2Player(starting_rating, initial_rd2, initial_vol)
        
        # Track rating history (starting from midpoint)
        rating1_history = [starting_rating]
        rating2_history = [starting_rating]
        rd1_history = [initial_rd1]
        rd2_history = [initial_rd2]
        
        results = defaultdict(int)
        total_moves = []
        game_terminations = defaultdict(int)
        
        # Detailed tracking for debugging
        game_log = []  # Store details of each game
        rating_changes_p1 = []  # Track rating changes for P1
        rating_changes_p2 = []  # Track rating changes for P2
        wins_by_phase = {"early": {"p1": 0, "p2": 0}, "mid": {"p1": 0, "p2": 0}, "late": {"p1": 0, "p2": 0}}
        
        start_time = time.time()
        
        # Create progress bar
        pbar = tqdm(range(num_games), desc="Starting games...")
        
        for game_num in pbar:
            # Alternate who plays white
            if game_num % 2 == 0:
                white_rating = rating1  # Use model strength, not Glicko rating
                black_rating = rating2
                white_glicko = glicko_player1
                black_glicko = glicko_player2
                white_player = "Player1"
                black_player = "Player2"
            else:
                white_rating = rating2
                black_rating = rating1
                white_glicko = glicko_player2
                black_glicko = glicko_player1
                white_player = "Player2"
                black_player = "Player1"
            
            # Store ratings before game
            p1_rating_before = glicko_player1.rating
            p2_rating_before = glicko_player2.rating
            p1_rd_before = glicko_player1.rd
            p2_rd_before = glicko_player2.rd
            
            # Calculate expected scores before game
            p1_expected = glicko_player1.expected_score(glicko_player2)
            p2_expected = glicko_player2.expected_score(glicko_player1)
            
            # Simulate game using model strength ratings
            result, num_moves, moves, termination = self.simulate_game(
                white_rating, black_rating, initial_clock=initial_clock
            )
            
            # Track results
            if result == "1-0":
                results[f"{white_player}_wins"] += 1
                white_score, black_score = 1.0, 0.0
            elif result == "0-1":
                results[f"{black_player}_wins"] += 1
                white_score, black_score = 0.0, 1.0
            else:
                results["draws"] += 1
                white_score, black_score = 0.5, 0.5
            
            # Capture pre-period snapshots for both players
            white_snapshot = Glicko2Player(white_glicko.rating, white_glicko.rd, white_glicko.vol, white_glicko.games_played)
            black_snapshot = Glicko2Player(black_glicko.rating, black_glicko.rd, black_glicko.vol, black_glicko.games_played)
            
            # Update ratings using snapshots to ensure simultaneous updates
            white_glicko.update_rating([black_snapshot], [white_score])
            black_glicko.update_rating([white_snapshot], [black_score])
            
            # Calculate rating changes
            p1_change = glicko_player1.rating - p1_rating_before
            p2_change = glicko_player2.rating - p2_rating_before
            rating_changes_p1.append(p1_change)
            rating_changes_p2.append(p2_change)
            
            # Determine game phase
            if game_num < num_games * 0.2:
                phase = "early"
            elif game_num < num_games * 0.8:
                phase = "mid"
            else:
                phase = "late"
            
            # Track wins by phase
            if result == "1-0":
                if white_player == "Player1":
                    wins_by_phase[phase]["p1"] += 1
                else:
                    wins_by_phase[phase]["p2"] += 1
            elif result == "0-1":
                if black_player == "Player1":
                    wins_by_phase[phase]["p1"] += 1
                else:
                    wins_by_phase[phase]["p2"] += 1
            
            # Calculate cumulative scores
            p1_wins = results.get("Player1_wins", 0)
            p2_wins = results.get("Player2_wins", 0)
            draws = results.get("draws", 0)
            p1_cumulative_score = p1_wins + 0.5 * draws
            p2_cumulative_score = p2_wins + 0.5 * draws
            
            # Write to CSV
            csv_writer.writerow([
                game_num + 1, white_player, black_player, white_rating, black_rating,
                result, termination, num_moves,
                f"{p1_rating_before:.1f}", f"{p2_rating_before:.1f}", 
                f"{p1_rd_before:.1f}", f"{p2_rd_before:.1f}",
                f"{glicko_player1.rating:.1f}", f"{glicko_player2.rating:.1f}",
                f"{glicko_player1.rd:.1f}", f"{glicko_player2.rd:.1f}",
                f"{p1_change:.1f}", f"{p2_change:.1f}",
                f"{p1_expected:.3f}", f"{p2_expected:.3f}",
                phase, p1_cumulative_score, p2_cumulative_score
            ])
            
            # Create and save PGN if requested
            if save_games:
                pgn_game = chess.pgn.Game()
                pgn_game.headers["Event"] = f"Rating Experiment {rating1} vs {rating2}"
                pgn_game.headers["Site"] = "ChessMimic"
                pgn_game.headers["Date"] = datetime.now().strftime("%Y.%m.%d")
                pgn_game.headers["Round"] = str(game_num + 1)
                pgn_game.headers["White"] = f"{white_player} (Model {white_rating})"
                pgn_game.headers["Black"] = f"{black_player} (Model {black_rating})"
                pgn_game.headers["Result"] = result
                pgn_game.headers["WhiteElo"] = f"{p1_rating_before:.0f}" if white_player == "Player1" else f"{p2_rating_before:.0f}"
                pgn_game.headers["BlackElo"] = f"{p2_rating_before:.0f}" if black_player == "Player2" else f"{p1_rating_before:.0f}"
                pgn_game.headers["Termination"] = termination
                
                # Add moves
                node = pgn_game
                board = chess.Board()
                for san_move in moves:
                    move = board.push_san(san_move)
                    node = node.add_variation(move)
                
                # Save PGN
                pgn_path = games_dir / f"game_{game_num+1:04d}.pgn"
                with open(pgn_path, 'w') as pgn_file:
                    print(pgn_game, file=pgn_file)
            
            # Log game details for internal tracking
            game_details = {
                "game_num": game_num + 1,
                "white": white_player,
                "black": black_player,
                "result": result,
                "p1_rating_before": p1_rating_before,
                "p2_rating_before": p2_rating_before,
                "p1_rating_after": glicko_player1.rating,
                "p2_rating_after": glicko_player2.rating,
                "p1_change": p1_change,
                "p2_change": p2_change,
                "p1_rd": glicko_player1.rd,
                "p2_rd": glicko_player2.rd,
                "phase": phase
            }
            game_log.append(game_details)
            
            # Track rating history
            rating1_history.append(glicko_player1.rating)
            rating2_history.append(glicko_player2.rating)
            rd1_history.append(glicko_player1.rd)
            rd2_history.append(glicko_player2.rd)
            
            # Debug first 10 games and critical moments
            if game_num < 10 or (game_num > 0 and abs(p1_change) > 50) or (game_num > 0 and abs(p2_change) > 50):
                print(f"\nGame {game_num+1}: {white_player} (white, strength={white_rating}) vs {black_player} (black, strength={black_rating})")
                print(f"Result: {result}")
                print(f"Before: P1={p1_rating_before:.0f}±{p1_rd_before:.0f}, P2={p2_rating_before:.0f}±{p2_rd_before:.0f}")
                print(f"After:  P1={glicko_player1.rating:.0f}±{glicko_player1.rd:.0f} ({p1_change:+.0f}), P2={glicko_player2.rating:.0f}±{glicko_player2.rd:.0f} ({p2_change:+.0f})")
            
            total_moves.append(num_moves)
            
            # Track termination reason
            game_terminations[termination] += 1
            
            # Update progress bar with current stats
            player1_wins = results.get("Player1_wins", 0)
            player2_wins = results.get("Player2_wins", 0)
            draws = results.get("draws", 0)
            
            pbar.set_description(
                f"P1 {player1_wins}W/{draws}D/{player2_wins}L "
                f"[{glicko_player1.rating:.0f}±{glicko_player1.rd:.0f}] vs "
                f"P2 [{glicko_player2.rating:.0f}±{glicko_player2.rd:.0f}]"
            )
        
        # Close CSV file
        csv_file.close()
        
        # Calculate statistics
        total_time = time.time() - start_time
        
        print(f"\nCompleted {num_games} games in {total_time:.1f} seconds")
        print(f"Average time per game: {total_time/num_games:.2f} seconds")
        print("\n" + "=" * 60)
        print("RESULTS SUMMARY")
        print("=" * 60)
        
        # Win rates
        player1_wins = results.get("Player1_wins", 0)
        player2_wins = results.get("Player2_wins", 0)
        draws = results.get("draws", 0)
        
        print(f"\nPlayer 1 (started {rating1}) wins: {player1_wins} ({player1_wins/num_games*100:.1f}%)")
        print(f"Player 2 (started {rating2}) wins: {player2_wins} ({player2_wins/num_games*100:.1f}%)")
        print(f"Draws: {draws} ({draws/num_games*100:.1f}%)")
        
        # Score calculation (1 point for win, 0.5 for draw)
        player1_score = player1_wins + 0.5 * draws
        player2_score = player2_wins + 0.5 * draws
        
        print(f"\nPlayer 1 total score: {player1_score:.1f}/{num_games} ({player1_score/num_games*100:.1f}%)")
        print(f"Player 2 total score: {player2_score:.1f}/{num_games} ({player2_score/num_games*100:.1f}%)")
        
        # Expected score based on Elo
        rating_diff = rating2 - rating1
        expected_score_rating2 = 1 / (1 + 10**(-rating_diff/400))
        expected_score_rating1 = 1 - expected_score_rating2
        
        print(f"\nExpected scores based on {rating_diff} rating difference:")
        print(f"{rating1} expected: {expected_score_rating1*100:.1f}%")
        print(f"{rating2} expected: {expected_score_rating2*100:.1f}%")
        
        # Game length statistics
        avg_moves = np.mean(total_moves)
        std_moves = np.std(total_moves)
        min_moves = np.min(total_moves)
        max_moves = np.max(total_moves)
        
        print(f"\nGame length statistics:")
        print(f"Average moves: {avg_moves:.1f} ± {std_moves:.1f}")
        print(f"Min moves: {min_moves}")
        print(f"Max moves: {max_moves}")
        
        # Termination types
        print("\nGame terminations:")
        for termination, count in sorted(game_terminations.items(), key=lambda x: x[1], reverse=True):
            print(f"  {termination}: {count} ({count/num_games*100:.1f}%)")
        
        # Final Glicko-2 ratings
        print(f"\nFinal Glicko-2 ratings:")
        print(f"Player 1 (model strength {rating1}): {glicko_player1.rating:.0f} ± {glicko_player1.rd:.0f}")
        print(f"Player 2 (model strength {rating2}): {glicko_player2.rating:.0f} ± {glicko_player2.rd:.0f}")
        print(f"\nRating changes from midpoint ({starting_rating}):")
        print(f"Player 1: {glicko_player1.rating - starting_rating:+.0f}")
        print(f"Player 2: {glicko_player2.rating - starting_rating:+.0f}")
        
        # Plot rating progression if requested
        if generate_plot:
            plot_path = output_dir / f"rating_progression_{rating1}_vs_{rating2}.png"
            self._plot_rating_progression(rating1_history, rating2_history, rd1_history, rd2_history,
                                        rating1, rating2, num_games, plot_path)
        
        # Print additional analysis
        print(f"\n\nDETAILED ANALYSIS:")
        print("=" * 60)
        
        # Wins by phase
        print("\nWins by game phase:")
        for phase in ["early", "mid", "late"]:
            p1_phase_wins = wins_by_phase[phase]["p1"]
            p2_phase_wins = wins_by_phase[phase]["p2"]
            total_phase = p1_phase_wins + p2_phase_wins
            if total_phase > 0:
                print(f"  {phase.capitalize():5s}: P1={p1_phase_wins:3d} ({p1_phase_wins/total_phase*100:5.1f}%), "
                      f"P2={p2_phase_wins:3d} ({p2_phase_wins/total_phase*100:5.1f}%)")
        
        # Average rating changes
        avg_p1_change = np.mean(rating_changes_p1) if rating_changes_p1 else 0
        avg_p2_change = np.mean(rating_changes_p2) if rating_changes_p2 else 0
        
        print(f"\nAverage rating change per game:")
        print(f"  Player 1: {avg_p1_change:+.2f}")
        print(f"  Player 2: {avg_p2_change:+.2f}")
        
        # Early vs late rating changes
        if len(rating_changes_p1) >= 20:
            early_changes_p1 = rating_changes_p1[:10]
            late_changes_p1 = rating_changes_p1[-10:]
            early_changes_p2 = rating_changes_p2[:10]
            late_changes_p2 = rating_changes_p2[-10:]
            
            print(f"\nRating volatility (average absolute change):")
            print(f"  First 10 games: P1={np.mean(np.abs(early_changes_p1)):.1f}, P2={np.mean(np.abs(early_changes_p2)):.1f}")
            print(f"  Last 10 games:  P1={np.mean(np.abs(late_changes_p1)):.1f}, P2={np.mean(np.abs(late_changes_p2)):.1f}")
        
        print(f"\nCSV file saved to: {csv_path}")
        if save_games:
            print(f"PGN files saved to: {games_dir}")
            files_created = num_games + 1  # games + CSV
        else:
            files_created = 1  # just CSV
        
        if generate_plot:
            files_created += 1  # add plot
            
        print(f"Total files created: {files_created}")


    def _plot_rating_progression(self, rating1_history, rating2_history, rd1_history, rd2_history,
                                model_strength1, model_strength2, num_games, save_path):
        """Plot the rating progression over games."""
        games = list(range(len(rating1_history)))
        
        plt.figure(figsize=(12, 6))
        
        # Plot ratings with confidence bands
        plt.subplot(1, 2, 1)
        r1 = np.array(rating1_history)
        rd1 = np.array(rd1_history)
        r2 = np.array(rating2_history)
        rd2 = np.array(rd2_history)
        
        plt.plot(games, r1, 'b-', label=f'Player 1')
        plt.fill_between(games, r1 - rd1, r1 + rd1, alpha=0.2, color='blue')
        
        plt.plot(games, r2, 'r-', label=f'Player 2')
        plt.fill_between(games, r2 - rd2, r2 + rd2, alpha=0.2, color='red')
        
        # Show model strengths as horizontal lines
        plt.axhline(y=model_strength1, color='blue', linestyle='--', alpha=0.5, 
                   label=f'Player 1 model strength: {model_strength1}')
        plt.axhline(y=model_strength2, color='red', linestyle='--', alpha=0.5,
                   label=f'Player 2 model strength: {model_strength2}')
        
        plt.xlabel('Game Number')
        plt.ylabel('Rating')
        plt.title('Rating Progression with Glicko-2')
        plt.legend()
        plt.grid(True, alpha=0.3)
        
        # Plot rating deviation
        plt.subplot(1, 2, 2)
        plt.plot(games, rd1, 'b-', label='Player 1 RD')
        plt.plot(games, rd2, 'r-', label='Player 2 RD')
        
        plt.xlabel('Game Number')
        plt.ylabel('Rating Deviation')
        plt.title('Rating Deviation Over Time')
        plt.legend()
        plt.grid(True, alpha=0.3)
        
        plt.tight_layout()
        plt.savefig(save_path, dpi=150)
        print(f"\nRating progression plot saved to: {save_path}")


def main():
    """Main function to run rating experiments with configurable parameters."""
    import argparse
    
    parser = argparse.ArgumentParser(
        description="Run rating experiments with chess models using Glicko-2 rating system",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    
    # Model parameters
    model_group = parser.add_argument_group('Model Parameters')
    model_group.add_argument("--min-probability", type=float, default=0.01,
                            help="Minimum probability threshold for moves (0.01 = 1%%)")
    model_group.add_argument("--rating1", type=int, default=1700,
                            help="Model strength for player 1")
    model_group.add_argument("--rating2", type=int, default=1800,
                            help="Model strength for player 2")
    
    # Experiment parameters
    exp_group = parser.add_argument_group('Experiment Parameters')
    exp_group.add_argument("--num-games", type=int, default=1000,
                          help="Number of games to simulate")
    exp_group.add_argument("--initial-clock", type=float, default=300.0,
                          help="Initial clock time in seconds")
    
    # Glicko-2 parameters
    glicko_group = parser.add_argument_group('Glicko-2 Rating Parameters')
    glicko_group.add_argument("--initial-rating", type=int, default=1500,
                             help="Starting Glicko-2 rating for both players")
    glicko_group.add_argument("--initial-rd1", type=float, default=500.0,
                             help="Initial rating deviation for player 1")
    glicko_group.add_argument("--initial-rd2", type=float, default=500.0,
                             help="Initial rating deviation for player 2")
    glicko_group.add_argument("--initial-volatility", type=float, default=0.06,
                             help="Initial volatility for Glicko-2")
    
    # Output parameters
    output_group = parser.add_argument_group('Output Parameters')
    output_group.add_argument("--output-dir", type=str, default=None,
                             help="Output directory for results (default: auto-generated with timestamp)")
    output_group.add_argument("--no-plot", action="store_true",
                             help="Skip generating the rating progression plot")
    output_group.add_argument("--save-games", action="store_true", default=True,
                             help="Save individual game PGN files")
    output_group.add_argument("--no-save-games", dest="save_games", action="store_false",
                             help="Don't save individual game PGN files")
    
    args = parser.parse_args()
    
    # Validate arguments
    if args.min_probability < 0 or args.min_probability > 1:
        parser.error("min_probability must be between 0 and 1")
    
    if args.num_games < 1:
        parser.error("num_games must be at least 1")
    
    if args.rating1 < 0 or args.rating2 < 0:
        parser.error("Ratings must be non-negative")
    
    if args.initial_rd1 <= 0 or args.initial_rd2 <= 0:
        parser.error("Rating deviations must be positive")
    
    if args.initial_volatility <= 0:
        parser.error("Volatility must be positive")
    
    # Display experiment configuration
    print("=" * 60)
    print("CHESS RATING EXPERIMENT CONFIGURATION")
    print("=" * 60)
    print(f"\nModel Parameters:")
    print(f"  Player 1 strength: {args.rating1}")
    print(f"  Player 2 strength: {args.rating2}")
    print(f"  Min probability threshold: {args.min_probability:.1%}")
    
    print(f"\nExperiment Parameters:")
    print(f"  Number of games: {args.num_games}")
    print(f"  Initial clock time: {args.initial_clock}s")
    
    print(f"\nGlicko-2 Parameters:")
    print(f"  Starting rating: {args.initial_rating}")
    print(f"  Player 1 RD: {args.initial_rd1}")
    print(f"  Player 2 RD: {args.initial_rd2}")
    print(f"  Initial volatility: {args.initial_volatility}")
    
    print(f"\nOutput Parameters:")
    print(f"  Output directory: {args.output_dir or 'auto-generated'}")
    print(f"  Generate plot: {not args.no_plot}")
    print(f"  Save game PGNs: {args.save_games}")
    
    # Create and run experiment
    try:
        print("\n" + "=" * 60)
        print("STARTING EXPERIMENT")
        print("=" * 60)
        
        experiment = RatingExperiment(min_probability=args.min_probability)
        
        # Run the experiment with all parameters
        experiment.run_experiment_with_glicko2(
            num_games=args.num_games,
            rating1=args.rating1,
            rating2=args.rating2,
            initial_rd1=args.initial_rd1,
            initial_rd2=args.initial_rd2,
            initial_vol=args.initial_volatility,
            initial_clock=args.initial_clock,
            starting_rating=args.initial_rating,
            output_dir=args.output_dir,
            save_games=args.save_games,
            generate_plot=not args.no_plot
        )
        
        print("\n" + "=" * 60)
        print("EXPERIMENT COMPLETED SUCCESSFULLY")
        print("=" * 60)
        
    except KeyboardInterrupt:
        print("\n\nExperiment interrupted by user.")
        return 1
    except Exception as e:
        print(f"\n\nError running experiment: {e}")
        import traceback
        traceback.print_exc()
        return 1
    
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
