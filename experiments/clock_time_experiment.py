#!/usr/bin/env python3
"""Experiment to test how clock time affects move quality.

Two players with the same rating (1750) play against each other:
- Player 1: Always has 300 seconds on clock (no time pressure)
- Player 2: Always has 1 second on clock (extreme time pressure)
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


class ClockTimeExperiment:
    """Run experiments comparing move quality under different time constraints."""
    
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
    
    def get_move_with_quality_metrics(self, board: chess.Board, moves_san: list[str], 
                                    rating: int, clock_time: float) -> tuple[str | None, dict]:
        """Get a move and quality metrics for analysis.
        
        Returns:
            Tuple of (move in SAN format, metrics dict)
        """
        if board.is_game_over():
            return None, {}
        
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
                    return board.san(move), {
                        "source": "common_moves",
                        "top_move_prob": None,
                        "chosen_move_prob": None,
                        "chosen_move_rank": None,
                        "num_legal_moves": len(list(board.legal_moves)),
                        "clock_time": clock_time
                    }
            except:
                pass
        
        # Use model prediction
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
        
        # Get model prediction with detailed info
        model_move_uci, model_info = self.model_predictor.predict_move(
            fen, 
            uci_moves,
            rating=rating,
            clock_time=clock_time,
            min_probability=self.min_probability
        )
        
        if model_move_uci and "top_moves" in model_info:
            move = chess.Move.from_uci(model_move_uci)
            if move in board.legal_moves:
                # Find rank of chosen move
                chosen_rank = None
                for i, (move_uci, prob) in enumerate(model_info["top_moves"]):
                    if move_uci == model_move_uci:
                        chosen_rank = i + 1
                        break
                
                # Get top move probability
                top_move_prob = model_info["top_moves"][0][1] if model_info["top_moves"] else 0.0
                
                return board.san(move), {
                    "source": "model",
                    "top_move_prob": top_move_prob,
                    "chosen_move_prob": model_info["chosen_probability"],
                    "chosen_move_rank": chosen_rank,
                    "num_legal_moves": model_info["num_legal_moves"],
                    "clock_time": clock_time,
                    "num_moves_after_filter": model_info.get("num_moves_after_filter", 0),
                    "num_moves_filtered_out": model_info.get("num_moves_filtered_out", 0)
                }
        
        # Fallback
        legal_moves = list(board.legal_moves)
        if legal_moves:
            return board.san(legal_moves[0]), {
                "source": "fallback",
                "top_move_prob": None,
                "chosen_move_prob": None,
                "chosen_move_rank": None,
                "num_legal_moves": len(legal_moves),
                "clock_time": clock_time
            }
        
        return None, {}
    
    def simulate_game(self, rating: int = 1750, abundant_time: float = 300.0, 
                     scarce_time: float = 1.0, game_num: int = 0) -> dict:
        """Simulate a game between players with different time constraints.
        
        Args:
            rating: Rating for both players
            abundant_time: Time for Player 1 (always abundant)
            scarce_time: Time for Player 2 (always scarce)
            game_num: Game number to determine player colors
        
        Returns:
            Dict with game results and move quality metrics
        """
        board = chess.Board()
        moves_san = []
        
        # Track metrics for each player
        abundant_metrics = []  # Player with lots of time
        scarce_metrics = []    # Player with little time
        
        # Determine player colors based on game number
        player1_is_white = (game_num % 2 == 0)
        
        move_count = 0
        
        while True:
            # Determine which player's turn it is
            white_to_move = (move_count % 2 == 0)
            
            # Player 1 always has abundant time, Player 2 always has scarce time
            if player1_is_white:
                # Game 0, 2, 4...: Player 1 is white, Player 2 is black
                if white_to_move:
                    clock_time = abundant_time  # Player 1 (white)
                    is_abundant = True
                else:
                    clock_time = scarce_time    # Player 2 (black)
                    is_abundant = False
            else:
                # Game 1, 3, 5...: Player 2 is white, Player 1 is black  
                if white_to_move:
                    clock_time = scarce_time    # Player 2 (white)
                    is_abundant = False
                else:
                    clock_time = abundant_time  # Player 1 (black)
                    is_abundant = True
            
            # Get move with quality metrics
            move, metrics = self.get_move_with_quality_metrics(
                board, moves_san, rating, clock_time
            )
            
            if move is None:
                break
            
            # Store metrics
            if is_abundant:
                abundant_metrics.append(metrics)
            else:
                scarce_metrics.append(metrics)
            
            moves_san.append(move)
            board.push_san(move)
            
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
                
                return {
                    "result": result,
                    "num_moves": move_count + 1,
                    "moves_san": moves_san,
                    "termination": termination,
                    "abundant_metrics": abundant_metrics,
                    "scarce_metrics": scarce_metrics,
                    "abundant_time": abundant_time,
                    "scarce_time": scarce_time
                }
            
            move_count += 1
            
            # Safety check
            if move_count > 500:
                return {
                    "result": "1/2-1/2",
                    "num_moves": move_count,
                    "moves_san": moves_san,
                    "termination": "safety_limit",
                    "abundant_metrics": abundant_metrics,
                    "scarce_metrics": scarce_metrics,
                    "abundant_time": abundant_time,
                    "scarce_time": scarce_time
                }
    
    def analyze_metrics(self, metrics_list: list[dict]) -> dict:
        """Analyze move quality metrics."""
        if not metrics_list:
            return {}
        
        # Filter to only model moves
        model_moves = [m for m in metrics_list if m["source"] == "model"]
        
        if not model_moves:
            return {
                "num_moves": len(metrics_list),
                "num_model_moves": 0,
                "num_common_moves": len([m for m in metrics_list if m["source"] == "common_moves"]),
                "num_fallback_moves": len([m for m in metrics_list if m["source"] == "fallback"])
            }
        
        # Extract probabilities and ranks
        top_probs = [m["top_move_prob"] for m in model_moves if m["top_move_prob"] is not None]
        chosen_probs = [m["chosen_move_prob"] for m in model_moves if m["chosen_move_prob"] is not None]
        chosen_ranks = [m["chosen_move_rank"] for m in model_moves if m["chosen_move_rank"] is not None]
        
        return {
            "num_moves": len(metrics_list),
            "num_model_moves": len(model_moves),
            "num_common_moves": len([m for m in metrics_list if m["source"] == "common_moves"]),
            "num_fallback_moves": len([m for m in metrics_list if m["source"] == "fallback"]),
            "avg_top_move_prob": np.mean(top_probs) if top_probs else 0,
            "avg_chosen_move_prob": np.mean(chosen_probs) if chosen_probs else 0,
            "avg_chosen_rank": np.mean(chosen_ranks) if chosen_ranks else 0,
            "pct_top_move_chosen": sum(1 for r in chosen_ranks if r == 1) / len(chosen_ranks) * 100 if chosen_ranks else 0,
            "pct_top3_chosen": sum(1 for r in chosen_ranks if r <= 3) / len(chosen_ranks) * 100 if chosen_ranks else 0,
        }
    
    def run_experiment(self, num_games: int = 100, rating: int = 1750,
                      abundant_time: float = 300.0, scarce_time: float = 1.0,
                      output_dir: str | None = None):
        """Run the clock time experiment.
        
        Args:
            num_games: Number of games to simulate
            rating: Rating for both players
            abundant_time: Clock time for time-rich player
            scarce_time: Clock time for time-poor player
            output_dir: Output directory for results
        """
        print(f"\nRunning Clock Time Experiment")
        print(f"Both players rated: {rating}")
        print(f"Player 1 (White in game 1): Always {abundant_time}s on clock")
        print(f"Player 2 (Black in game 1): Always {scarce_time}s on clock")
        print("=" * 60)
        
        # Create output directory
        if output_dir is None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            output_dir = Path(f"clock_experiment_{timestamp}")
        else:
            output_dir = Path(output_dir)
        
        output_dir.mkdir(exist_ok=True)
        print(f"Output directory: {output_dir}")
        
        # Create CSV file
        csv_path = output_dir / "game_log.csv"
        csv_file = open(csv_path, 'w', newline='')
        csv_writer = csv.writer(csv_file)
        csv_writer.writerow([
            "game_num", "result", "winner", "termination", "num_moves",
            "abundant_player", "scarce_player",
            "abundant_avg_top_prob", "abundant_avg_chosen_prob", "abundant_avg_rank", "abundant_pct_top_move",
            "scarce_avg_top_prob", "scarce_avg_chosen_prob", "scarce_avg_rank", "scarce_pct_top_move"
        ])
        
        # Track overall statistics
        results = defaultdict(int)
        all_abundant_metrics = []
        all_scarce_metrics = []
        terminations = defaultdict(int)
        
        start_time = time.time()
        
        # Create progress bar
        pbar = tqdm(range(num_games), desc="Starting games...")
        
        # Run games
        for game_num in pbar:
            # Alternate who starts with white
            if game_num % 2 == 0:
                abundant_player = "Player1"
                scarce_player = "Player2"
            else:
                abundant_player = "Player2"
                scarce_player = "Player1"
            
            # Simulate game
            game_result = self.simulate_game(rating, abundant_time, scarce_time, game_num)
            
            # Analyze metrics
            abundant_analysis = self.analyze_metrics(game_result["abundant_metrics"])
            scarce_analysis = self.analyze_metrics(game_result["scarce_metrics"])
            
            # Determine winner
            result = game_result["result"]
            if result == "1-0":  # White wins
                if game_num % 2 == 0:  # Player1 was white (abundant time)
                    winner = "abundant"
                    results["abundant_wins"] += 1
                else:  # Player2 was white (scarce time)
                    winner = "scarce"
                    results["scarce_wins"] += 1
            elif result == "0-1":  # Black wins
                if game_num % 2 == 0:  # Player2 was black (scarce time)
                    winner = "scarce"
                    results["scarce_wins"] += 1
                else:  # Player1 was black (abundant time)
                    winner = "abundant"
                    results["abundant_wins"] += 1
            else:
                winner = "draw"
                results["draws"] += 1
            
            terminations[game_result["termination"]] += 1
            
            # Store metrics
            all_abundant_metrics.extend(game_result["abundant_metrics"])
            all_scarce_metrics.extend(game_result["scarce_metrics"])
            
            # Calculate current statistics for progress bar
            abundant_wins = results.get("abundant_wins", 0)
            scarce_wins = results.get("scarce_wins", 0)
            draws = results.get("draws", 0)
            
            # Calculate move quality stats so far
            if all_abundant_metrics:
                temp_abundant = self.analyze_metrics(all_abundant_metrics)
                abundant_top_pct = temp_abundant.get("pct_top_move_chosen", 0)
            else:
                abundant_top_pct = 0
                
            if all_scarce_metrics:
                temp_scarce = self.analyze_metrics(all_scarce_metrics)
                scarce_top_pct = temp_scarce.get("pct_top_move_chosen", 0)
            else:
                scarce_top_pct = 0
            
            # Update progress bar with current stats
            pbar.set_description(
                f"A:{abundant_wins}W/{draws}D/{scarce_wins}L "
                f"[Top: {abundant_time}s={abundant_top_pct:.0f}% vs {scarce_time}s={scarce_top_pct:.0f}%]"
            )
            
            # Write to CSV
            csv_writer.writerow([
                game_num + 1, result, winner, game_result["termination"], game_result["num_moves"],
                abundant_player, scarce_player,
                abundant_analysis.get("avg_top_move_prob", 0),
                abundant_analysis.get("avg_chosen_move_prob", 0),
                abundant_analysis.get("avg_chosen_rank", 0),
                abundant_analysis.get("pct_top_move_chosen", 0),
                scarce_analysis.get("avg_top_move_prob", 0),
                scarce_analysis.get("avg_chosen_move_prob", 0),
                scarce_analysis.get("avg_chosen_rank", 0),
                scarce_analysis.get("pct_top_move_chosen", 0)
            ])
        
        csv_file.close()
        
        # Calculate overall statistics
        total_time = time.time() - start_time
        overall_abundant = self.analyze_metrics(all_abundant_metrics)
        overall_scarce = self.analyze_metrics(all_scarce_metrics)
        
        print(f"\nCompleted {num_games} games in {total_time:.1f} seconds")
        print(f"Average time per game: {total_time/num_games:.2f} seconds")
        
        print("\n" + "=" * 60)
        print("RESULTS SUMMARY")
        print("=" * 60)
        
        print(f"\nWin Statistics:")
        print(f"Abundant time ({abundant_time}s) wins: {results['abundant_wins']} ({results['abundant_wins']/num_games*100:.1f}%)")
        print(f"Scarce time ({scarce_time}s) wins: {results['scarce_wins']} ({results['scarce_wins']/num_games*100:.1f}%)")
        print(f"Draws: {results['draws']} ({results['draws']/num_games*100:.1f}%)")
        
        print(f"\nMove Quality - Abundant Time ({abundant_time}s):")
        print(f"  Total moves: {overall_abundant['num_moves']}")
        print(f"  Model moves: {overall_abundant['num_model_moves']} ({overall_abundant['num_model_moves']/overall_abundant['num_moves']*100:.1f}%)")
        print(f"  Avg top move probability: {overall_abundant.get('avg_top_move_prob', 0):.3f}")
        print(f"  Avg chosen move probability: {overall_abundant.get('avg_chosen_move_prob', 0):.3f}")
        print(f"  Avg chosen move rank: {overall_abundant.get('avg_chosen_rank', 0):.2f}")
        print(f"  % choosing top move: {overall_abundant.get('pct_top_move_chosen', 0):.1f}%")
        print(f"  % choosing top 3 move: {overall_abundant.get('pct_top3_chosen', 0):.1f}%")
        
        print(f"\nMove Quality - Scarce Time ({scarce_time}s):")
        print(f"  Total moves: {overall_scarce['num_moves']}")
        print(f"  Model moves: {overall_scarce['num_model_moves']} ({overall_scarce['num_model_moves']/overall_scarce['num_moves']*100:.1f}%)")
        print(f"  Avg top move probability: {overall_scarce.get('avg_top_move_prob', 0):.3f}")
        print(f"  Avg chosen move probability: {overall_scarce.get('avg_chosen_move_prob', 0):.3f}")
        print(f"  Avg chosen move rank: {overall_scarce.get('avg_chosen_rank', 0):.2f}")
        print(f"  % choosing top move: {overall_scarce.get('pct_top_move_chosen', 0):.1f}%")
        print(f"  % choosing top 3 move: {overall_scarce.get('pct_top3_chosen', 0):.1f}%")
        
        print("\nGame terminations:")
        for term, count in sorted(terminations.items(), key=lambda x: x[1], reverse=True):
            print(f"  {term}: {count} ({count/num_games*100:.1f}%)")
        
        # Create visualization
        self.create_visualizations(overall_abundant, overall_scarce, results, 
                                 abundant_time, scarce_time, output_dir)
        
        print(f"\nResults saved to: {output_dir}")
    
    def create_visualizations(self, abundant_stats, scarce_stats, results, 
                            abundant_time, scarce_time, output_dir):
        """Create visualizations of the experiment results."""
        fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(12, 10))
        
        # Win rate comparison
        labels = [f'Abundant\n({abundant_time}s)', f'Scarce\n({scarce_time}s)', 'Draws']
        sizes = [results['abundant_wins'], results['scarce_wins'], results['draws']]
        colors = ['green', 'red', 'gray']
        ax1.pie(sizes, labels=labels, colors=colors, autopct='%1.1f%%')
        ax1.set_title('Win Rate by Time Availability')
        
        # Move quality comparison
        metrics = ['Top Move %', 'Top 3 %', 'Avg Rank']
        abundant_values = [
            abundant_stats.get('pct_top_move_chosen', 0),
            abundant_stats.get('pct_top3_chosen', 0),
            abundant_stats.get('avg_chosen_rank', 0) * 10  # Scale for visibility
        ]
        scarce_values = [
            scarce_stats.get('pct_top_move_chosen', 0),
            scarce_stats.get('pct_top3_chosen', 0),
            scarce_stats.get('avg_chosen_rank', 0) * 10  # Scale for visibility
        ]
        
        x = np.arange(len(metrics))
        width = 0.35
        
        ax2.bar(x - width/2, abundant_values, width, label=f'Abundant ({abundant_time}s)', color='green')
        ax2.bar(x + width/2, scarce_values, width, label=f'Scarce ({scarce_time}s)', color='red')
        ax2.set_xlabel('Metric')
        ax2.set_ylabel('Value')
        ax2.set_title('Move Quality Comparison')
        ax2.set_xticks(x)
        ax2.set_xticklabels(metrics)
        ax2.legend()
        
        # Probability comparison
        prob_metrics = ['Avg Top Move Prob', 'Avg Chosen Prob']
        abundant_probs = [
            abundant_stats.get('avg_top_move_prob', 0),
            abundant_stats.get('avg_chosen_move_prob', 0)
        ]
        scarce_probs = [
            scarce_stats.get('avg_top_move_prob', 0),
            scarce_stats.get('avg_chosen_move_prob', 0)
        ]
        
        x = np.arange(len(prob_metrics))
        ax3.bar(x - width/2, abundant_probs, width, label=f'Abundant ({abundant_time}s)', color='green')
        ax3.bar(x + width/2, scarce_probs, width, label=f'Scarce ({scarce_time}s)', color='red')
        ax3.set_xlabel('Metric')
        ax3.set_ylabel('Probability')
        ax3.set_title('Move Probability Comparison')
        ax3.set_xticks(x)
        ax3.set_xticklabels(prob_metrics)
        ax3.legend()
        
        # Move source distribution
        sources = ['Model', 'Common', 'Fallback']
        abundant_sources = [
            abundant_stats.get('num_model_moves', 0),
            abundant_stats.get('num_common_moves', 0),
            abundant_stats.get('num_fallback_moves', 0)
        ]
        scarce_sources = [
            scarce_stats.get('num_model_moves', 0),
            scarce_stats.get('num_common_moves', 0),
            scarce_stats.get('num_fallback_moves', 0)
        ]
        
        x = np.arange(len(sources))
        ax4.bar(x - width/2, abundant_sources, width, label=f'Abundant ({abundant_time}s)', color='green')
        ax4.bar(x + width/2, scarce_sources, width, label=f'Scarce ({scarce_time}s)', color='red')
        ax4.set_xlabel('Move Source')
        ax4.set_ylabel('Count')
        ax4.set_title('Move Source Distribution')
        ax4.set_xticks(x)
        ax4.set_xticklabels(sources)
        ax4.legend()
        
        plt.tight_layout()
        plt.savefig(output_dir / 'clock_time_analysis.png', dpi=150)
        print(f"\nVisualization saved to: {output_dir / 'clock_time_analysis.png'}")


def main():
    """Main function to run clock time experiments."""
    import argparse
    
    parser = argparse.ArgumentParser(
        description="Analyze the effect of clock time on move quality",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    
    parser.add_argument("--num-games", type=int, default=100,
                       help="Number of games to simulate")
    parser.add_argument("--rating", type=int, default=1750,
                       help="Rating for both players")
    parser.add_argument("--abundant-time", type=float, default=300.0,
                       help="Clock time for time-rich player (seconds)")
    parser.add_argument("--scarce-time", type=float, default=1.0,
                       help="Clock time for time-poor player (seconds)")
    parser.add_argument("--min-probability", type=float, default=0.01,
                       help="Minimum probability threshold for moves")
    parser.add_argument("--output-dir", type=str, default=None,
                       help="Output directory for results")
    
    args = parser.parse_args()
    
    # Display configuration
    print("=" * 60)
    print("CLOCK TIME EXPERIMENT CONFIGURATION")
    print("=" * 60)
    print(f"\nExperiment Parameters:")
    print(f"  Number of games: {args.num_games}")
    print(f"  Player rating: {args.rating}")
    print(f"  Abundant clock time: {args.abundant_time}s")
    print(f"  Scarce clock time: {args.scarce_time}s")
    print(f"  Min probability threshold: {args.min_probability:.1%}")
    
    # Create and run experiment
    try:
        experiment = ClockTimeExperiment(min_probability=args.min_probability)
        
        experiment.run_experiment(
            num_games=args.num_games,
            rating=args.rating,
            abundant_time=args.abundant_time,
            scarce_time=args.scarce_time,
            output_dir=args.output_dir
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
