#!/usr/bin/env python3
"""Test to understand the Glicko-2 rating issue in the experiment."""

import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT / "experiments"))

from glicko2 import Glicko2Player

def test_rating_convergence():
    """Test how Glicko-2 ratings behave with alternating wins."""
    
    # Initialize two players at 1500
    player1 = Glicko2Player(1500, 500, 0.06)
    player2 = Glicko2Player(1500, 500, 0.06)
    
    print("Initial ratings:")
    print(f"Player 1: {player1.rating:.0f} ± {player1.rd:.0f}")
    print(f"Player 2: {player2.rating:.0f} ± {player2.rd:.0f}")
    print()
    
    # Simulate that Player 2 is actually stronger (wins 60% of games)
    # But see what happens to ratings
    
    wins_p1 = 0
    wins_p2 = 0
    
    for game in range(20):
        # For first 10 games, P2 wins 70% of the time
        # For last 10 games, P1 wins 60% of the time (simulating the late game reversal)
        
        if game < 10:
            # Early phase - P2 is dominant
            if game % 10 < 7:  # P2 wins 7 out of 10
                # P2 wins
                player1.update_rating([player2], [0.0])
                player2.update_rating([player1], [1.0])
                wins_p2 += 1
                result = "P2 wins"
            else:
                # P1 wins
                player1.update_rating([player2], [1.0])
                player2.update_rating([player1], [0.0])
                wins_p1 += 1
                result = "P1 wins"
        else:
            # Late phase - P1 suddenly becomes stronger
            if game % 10 < 6:  # P1 wins 6 out of 10
                # P1 wins
                player1.update_rating([player2], [1.0])
                player2.update_rating([player1], [0.0])
                wins_p1 += 1
                result = "P1 wins"
            else:
                # P2 wins
                player1.update_rating([player2], [0.0])
                player2.update_rating([player1], [1.0])
                wins_p2 += 1
                result = "P2 wins"
        
        print(f"Game {game+1}: {result}")
        print(f"  P1: {player1.rating:.0f} ± {player1.rd:.0f}")
        print(f"  P2: {player2.rating:.0f} ± {player2.rd:.0f}")
        print(f"  Expected scores: P1={player1.expected_score(player2):.3f}, P2={player2.expected_score(player1):.3f}")
        
        if game == 9:
            print("\n--- PHASE CHANGE: P1 starts winning more ---\n")
    
    print(f"\nFinal results:")
    print(f"Player 1: {wins_p1} wins, rating = {player1.rating:.0f} ± {player1.rd:.0f}")
    print(f"Player 2: {wins_p2} wins, rating = {player2.rating:.0f} ± {player2.rd:.0f}")
    print(f"\nDespite P2 winning {wins_p2} games vs P1's {wins_p1} games,")
    print(f"P1 ended with {'higher' if player1.rating > player2.rating else 'lower'} rating!")
    
    # Explain why this happens
    print("\nWHY THIS HAPPENS:")
    print("1. In early games, both players have high RD (rating deviation)")
    print("2. P2 wins more early, gaining rating but RD decreases slowly")
    print("3. When P1 starts winning in late games:")
    print("   - P2 has lower RD (more 'established' rating)")
    print("   - P1 still has relatively high RD")
    print("   - P1's wins against higher-rated P2 give big rating gains")
    print("   - P2's losses hurt less because their RD is lower")
    print("4. The late comeback by P1 is worth more in rating points!")

def test_rating_deviation_effect():
    """Test how rating deviation affects rating changes."""
    print("\n\n=== RATING DEVIATION EFFECT TEST ===\n")
    
    # High RD player vs Low RD player, same rating
    high_rd = Glicko2Player(1500, 300, 0.06)  # Less certain rating
    low_rd = Glicko2Player(1500, 100, 0.06)   # More certain rating
    
    print("Before game (same rating, different RD):")
    print(f"High RD: {high_rd.rating:.0f} ± {high_rd.rd:.0f}")
    print(f"Low RD:  {low_rd.rating:.0f} ± {low_rd.rd:.0f}")
    
    # High RD player wins
    high_rd_before = high_rd.rating
    low_rd_before = low_rd.rating
    
    high_rd.update_rating([low_rd], [1.0])
    low_rd.update_rating([high_rd], [0.0])
    
    print("\nAfter High RD player wins:")
    print(f"High RD: {high_rd.rating:.0f} ± {high_rd.rd:.0f} (gained {high_rd.rating - high_rd_before:.0f})")
    print(f"Low RD:  {low_rd.rating:.0f} ± {low_rd.rd:.0f} (lost {low_rd_before - low_rd.rating:.0f})")
    print("\nNote: High RD player gains MORE than Low RD player loses!")

if __name__ == "__main__":
    test_rating_convergence()
    test_rating_deviation_effect()
