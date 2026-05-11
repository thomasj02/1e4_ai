#!/usr/bin/env python3
"""Unit tests for Glicko-2 implementation against the worked example from the paper.

Tests against the example from:
http://www.glicko.net/glicko/glicko2.pdf
"""

import math
import pytest
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT / "experiments"))

from glicko2 import Glicko2Player


class TestGlicko2WorkedExample:
    """Test our implementation against Glickman's worked example."""
    
    def test_worked_example_from_paper(self):
        """Test against the exact values from the Glicko-2 paper example.
        
        The example player has:
        - Rating: 1500
        - RD: 200  
        - Volatility: 0.06
        
        They play against three opponents and get results:
        1. Opponent 1400±30, result: win (1.0)
        2. Opponent 1550±100, result: loss (0.0) 
        3. Opponent 1700±300, result: loss (0.0)
        """
        # Create player with exact values from paper
        player = Glicko2Player(rating=1500, rd=200, vol=0.06)
        
        # Verify initial conversion to Glicko-2 scale
        assert abs(player.mu - 0.0) < 1e-6  # (1500 - 1500) / 173.7178 = 0
        assert abs(player.phi - 1.1513) < 1e-4  # 200 / 173.7178
        
        # Create opponents
        opponent1 = Glicko2Player(rating=1400, rd=30, vol=0.06)
        opponent2 = Glicko2Player(rating=1550, rd=100, vol=0.06)
        opponent3 = Glicko2Player(rating=1700, rd=300, vol=0.06)
        
        # Results
        results = [1.0, 0.0, 0.0]  # Win, Loss, Loss
        
        # Update rating
        player.update_rating([opponent1, opponent2, opponent3], results)
        
        # Expected values from paper (after conversion back to original scale):
        # New rating: 1464.05 (paper shows 1464.06 due to rounding)
        # New RD: 151.52 (paper shows 151.52)
        # New volatility: 0.05999 (paper shows 0.05999)
        
        # Allow small tolerance for floating point arithmetic
        assert abs(player.rating - 1464.05) < 0.1, f"Expected rating ~1464.05, got {player.rating}"
        assert abs(player.rd - 151.52) < 0.01, f"Expected RD ~151.52, got {player.rd}"
        assert abs(player.vol - 0.05999) < 0.00001, f"Expected volatility ~0.05999, got {player.vol}"
    
    def test_intermediate_calculations(self):
        """Test intermediate calculations match the paper's values."""
        player = Glicko2Player(rating=1500, rd=200, vol=0.06)
        
        # Create opponents
        opponent1 = Glicko2Player(rating=1400, rd=30, vol=0.06)
        opponent2 = Glicko2Player(rating=1550, rd=100, vol=0.06) 
        opponent3 = Glicko2Player(rating=1700, rd=300, vol=0.06)
        
        # Test g function
        g1 = player._g(opponent1.phi)
        g2 = player._g(opponent2.phi)
        g3 = player._g(opponent3.phi)
        
        # Expected g values from paper
        assert abs(g1 - 0.9955) < 0.0001
        assert abs(g2 - 0.9531) < 0.0001
        assert abs(g3 - 0.7242) < 0.0001
        
        # Test E function
        e1 = player._E(opponent1.mu, opponent1.phi)
        e2 = player._E(opponent2.mu, opponent2.phi)
        e3 = player._E(opponent3.mu, opponent3.phi)
        
        # Expected E values from paper
        assert abs(e1 - 0.639) < 0.001
        assert abs(e2 - 0.432) < 0.001
        assert abs(e3 - 0.303) < 0.001
        
        # Test v calculation
        v = player._compute_v([opponent1, opponent2, opponent3])
        
        # Expected v from paper: 1.7785
        # Note: Small differences in floating point precision can cause minor variations
        assert abs(v - 1.7785) < 0.001
        
        # Test delta calculation
        delta = player._compute_delta([opponent1, opponent2, opponent3], [1.0, 0.0, 0.0], v)
        
        # Expected delta from paper: -0.4834
        # Note: Small differences due to accumulated floating point precision
        assert abs(delta - (-0.4834)) < 0.001
    
    def test_no_games_rd_increase(self):
        """Test that RD increases when no games are played."""
        player = Glicko2Player(rating=1500, rd=200, vol=0.06)
        
        initial_rd = player.rd
        
        # Update with no opponents (simulating a rating period with no games)
        player.update_rating([], [], time_since_last_game=1.0)
        
        # RD should have increased
        assert player.rd > initial_rd
        
        # Rating should remain unchanged
        assert player.rating == 1500
        
        # Volatility should remain unchanged
        assert player.vol == 0.06


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
