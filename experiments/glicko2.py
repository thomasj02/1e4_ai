#!/usr/bin/env python3
"""Implementation of the Glicko-2 rating system.

Based on Professor Mark E. Glickman's paper:
"The Glicko-2 rating system"
"""

import math


class Glicko2Player:
    """A player in the Glicko-2 rating system (Lichess parameters)."""
    
    # Lichess system constants
    TAU = 0.5          # System volatility constraint
    EPSILON = 1e-6     # Convergence tolerance
    FLOOR = 600        # Minimum rating allowed
    
    # Lichess defaults
    INITIAL_RATING = 1500
    INITIAL_RD = 500   # Lichess starts with RD=500 (shown as ±1000)
    INITIAL_VOL_PROVISIONAL = 0.06  # For new players
    INITIAL_VOL_ESTABLISHED = 0.05  # After ~30 games
    ESTABLISHED_RD_THRESHOLD = 110  # RD <= 110 removes the "?" mark
    
    def __init__(self, rating: float = 1500.0, rd: float = 500.0, vol: float = 0.06, 
                 games_played: int = 0):
        """Initialize a player with Lichess defaults.
        
        Args:
            rating: The player's rating (default 1500)
            rd: Rating deviation (default 500 - Lichess value)
            vol: Volatility (default 0.06 for provisional)
            games_played: Number of games played (for volatility adjustment)
        """
        self.rating = max(round(rating), self.FLOOR)
        self.rd = rd
        self.vol = vol
        self.games_played = games_played
        
        # Convert to Glicko-2 scale
        self.mu = (self.rating - 1500) / 173.7178
        self.phi = rd / 173.7178
        self.sigma = vol
    
    def update_rating(self, opponents: list['Glicko2Player'], results: list[float],
                     time_since_last_game: float = 0.0):
        """Update rating based on match results.
        
        Args:
            opponents: List of opponent Glicko2Player objects
            results: List of results (1.0 = win, 0.5 = draw, 0.0 = loss)
            time_since_last_game: Time elapsed since last game (in rating periods)
        """
        if not opponents:
            # If no games played, only rating deviation increases
            self._update_rating_deviation_no_games(time_since_last_game)
            return
        
        # Increment games played
        self.games_played += len(opponents)
        
        # Step 1: Apply continuous RD inflation for time since last game
        if time_since_last_game > 0:
            self.phi = math.sqrt(self.phi ** 2 + time_since_last_game * self.sigma ** 2)
        
        # Precompute g and E values for efficiency
        cached_values = []
        for opp in opponents:
            g_j = self._g(opp.phi)
            e_j = self._E(opp.mu, opp.phi)
            cached_values.append((g_j, e_j))
        
        # Step 2: Compute v using cached values
        v = 1 / sum(
            g_j ** 2 * e_j * (1 - e_j)
            for g_j, e_j in cached_values
        )
        
        # Step 3: Compute delta using cached values
        delta = v * sum(
            g_j * (result - e_j)
            for (g_j, e_j), result in zip(cached_values, results)
        )
        
        # Step 4: Compute new volatility
        sigma_prime = self._compute_new_volatility(delta, v)
        
        # Apply Lichess volatility rule for established players after computing sigma_prime
        if self.games_played >= 30 and self.rd < self.INITIAL_RD / 2:
            sigma_prime = min(sigma_prime, self.INITIAL_VOL_ESTABLISHED)
        
        # Step 5: Update rating deviation to new pre-rating period value
        phi_star = math.sqrt(self.phi ** 2 + sigma_prime ** 2)
        
        # Step 6: Update rating and RD using cached values
        phi_prime = 1 / math.sqrt(1 / phi_star ** 2 + 1 / v)
        mu_prime = self.mu + phi_prime ** 2 * sum(
            g_j * (result - e_j)
            for (g_j, e_j), result in zip(cached_values, results)
        )
        
        # Step 7: Convert back to original scale
        self.mu = mu_prime
        self.phi = phi_prime
        self.sigma = sigma_prime
        
        self.rating = 173.7178 * mu_prime + 1500
        self.rd = 173.7178 * phi_prime
        self.vol = sigma_prime
        
        # Apply rating floor (Lichess rule - floor after rounding)
        self.rating = max(round(self.rating), self.FLOOR)
    
    def _update_rating_deviation_no_games(self, time_elapsed: float = 1.0):
        """Update rating deviation when no games are played.
        
        Args:
            time_elapsed: Time elapsed in rating periods
        """
        phi_prime = math.sqrt(self.phi ** 2 + time_elapsed * self.sigma ** 2)
        self.phi = phi_prime
        self.rd = 173.7178 * phi_prime
    
    def _g(self, phi: float) -> float:
        """The g function."""
        return 1 / math.sqrt(1 + 3 * phi ** 2 / math.pi ** 2)
    
    def _E(self, mu: float, phi: float) -> float:
        """The E function - expected score."""
        return 1 / (1 + math.exp(-self._g(phi) * (self.mu - mu)))
    
    def _compute_v(self, opponents: list['Glicko2Player']) -> float:
        """Compute the quantity v."""
        return 1 / sum(
            self._g(opp.phi) ** 2 * self._E(opp.mu, opp.phi) * (1 - self._E(opp.mu, opp.phi))
            for opp in opponents
        )
    
    def _compute_delta(self, opponents: list['Glicko2Player'], results: list[float], v: float) -> float:
        """Compute the quantity Delta."""
        return v * sum(
            self._g(opp.phi) * (result - self._E(opp.mu, opp.phi))
            for opp, result in zip(opponents, results)
        )
    
    def _compute_new_volatility(self, delta: float, v: float) -> float:
        """Compute new volatility using the Illinois algorithm (modified regula falsi).
        
        This is a robust root-finding method that avoids the convergence issues
        that can occur with Newton-Raphson in certain edge cases.
        """
        a = math.log(self.sigma ** 2)
        phi = self.phi
        tau = self.TAU
        
        def f(x):
            ex = math.exp(x)
            return (ex * (delta ** 2 - phi ** 2 - v - ex)) / (2 * (phi ** 2 + v + ex) ** 2) - (x - a) / tau ** 2
        
        # Find appropriate bounds
        A = a
        if delta ** 2 > phi ** 2 + v:
            B = math.log(delta ** 2 - phi ** 2 - v)
        else:
            k = 1
            while f(a - k * tau) < 0:
                k += 1
            B = a - k * tau
        
        # Illinois algorithm
        fA = f(A)
        fB = f(B)
        
        while abs(B - A) > self.EPSILON:
            C = A + (A - B) * fA / (fB - fA)
            fC = f(C)
            
            if fC * fB <= 0:
                A = B
                fA = fB
            else:
                fA = fA / 2
            
            B = C
            fB = fC
        
        # Return the root at B (where convergence occurred)
        return math.exp(B / 2)
    
    def expected_score(self, opponent: 'Glicko2Player') -> float:
        """Calculate expected score against an opponent."""
        return self._E(opponent.mu, opponent.phi)
    
    def is_provisional(self) -> bool:
        """Check if player is still provisional (has "?" mark on Lichess)."""
        return self.rd > self.ESTABLISHED_RD_THRESHOLD
    
    def confidence_interval(self) -> tuple[float, float]:
        """Get the 95% confidence interval (displayed as ± on Lichess)."""
        interval = 1.96 * self.rd
        return (self.rating - interval, self.rating + interval)
    
    def __str__(self):
        """String representation."""
        provisional = "?" if self.is_provisional() else ""
        return f"Glicko2(rating={self.rating:.0f}{provisional} ±{1.96*self.rd:.0f}, vol={self.vol:.4f}, games={self.games_played})"


def simulate_game_result(player1: Glicko2Player, player2: Glicko2Player) -> float:
    """Simulate a game result based on expected scores.
    
    Returns:
        1.0 if player1 wins, 0.0 if player1 loses, 0.5 for draw
    """
    import random
    
    expected = player1.expected_score(player2)
    
    # Add some noise to make it more realistic
    # Use a draw probability that increases when players are close in strength
    rating_diff = abs(player1.rating - player2.rating)
    base_draw_prob = 0.1  # Base draw probability
    
    # Increase draw probability when ratings are close
    if rating_diff < 50:
        draw_prob = 0.3
    elif rating_diff < 100:
        draw_prob = 0.2
    else:
        draw_prob = base_draw_prob
    
    rand = random.random()
    
    if rand < draw_prob:
        return 0.5  # Draw
    else:
        # Adjust probabilities for win/loss
        win_prob = expected * (1 - draw_prob)
        if random.random() < win_prob / (1 - draw_prob):
            return 1.0  # Player 1 wins
        else:
            return 0.0  # Player 1 loses


if __name__ == "__main__":
    # Example usage with Lichess defaults
    print("=== Lichess-style Glicko-2 Implementation Test ===\n")
    
    # Test 1: New players (Lichess defaults)
    print("Test 1: Two new players")
    player1 = Glicko2Player()  # Uses Lichess defaults: 1500, RD=500, vol=0.06
    player2 = Glicko2Player()
    
    print("Before game:")
    print(f"Player 1: {player1}")
    print(f"Player 2: {player2}")
    print(f"Expected score for Player 1: {player1.expected_score(player2):.3f}")
    
    # Capture pre-period snapshots
    player1_snapshot = Glicko2Player(player1.rating, player1.rd, player1.vol, player1.games_played)
    player2_snapshot = Glicko2Player(player2.rating, player2.rd, player2.vol, player2.games_played)
    
    # Player 1 wins - update using snapshots
    player1.update_rating([player2_snapshot], [1.0])
    player2.update_rating([player1_snapshot], [0.0])
    
    print("\nAfter Player 1 wins:")
    print(f"Player 1: {player1}")
    print(f"Player 2: {player2}")
    print(f"Rating change: Player 1 gained {player1.rating - 1500:.0f} points")
    print(f"Rating change: Player 2 lost {1500 - player2.rating:.0f} points")
    
    # Test 2: Established vs new player
    print("\n\nTest 2: Established player (1700, RD=80) vs new player")
    established = Glicko2Player(1700, 80, 0.05, games_played=100)
    newbie = Glicko2Player()
    
    print("Before game:")
    print(f"Established: {established}")
    print(f"Newbie: {newbie}")
    print(f"Expected score for established player: {established.expected_score(newbie):.3f}")
    
    # Newbie wins (upset!)
    old_est_rating = established.rating
    old_new_rating = newbie.rating
    
    # Capture pre-period snapshots
    established_snapshot = Glicko2Player(established.rating, established.rd, established.vol, established.games_played)
    newbie_snapshot = Glicko2Player(newbie.rating, newbie.rd, newbie.vol, newbie.games_played)
    
    # Update using snapshots
    established.update_rating([newbie_snapshot], [0.0])
    newbie.update_rating([established_snapshot], [1.0])
    
    print("\nAfter newbie wins (upset!):")
    print(f"Established: {established}")
    print(f"Newbie: {newbie}")
    print(f"Rating change: Established lost {old_est_rating - established.rating:.0f} points")
    print(f"Rating change: Newbie gained {newbie.rating - old_new_rating:.0f} points")
    
    # Test 3: Rating floor
    print("\n\nTest 3: Rating floor test")
    low_rated = Glicko2Player(650, 150, 0.06)
    print(f"Player created with 650 rating: {low_rated}")
    
    # Lose many games
    for _ in range(5):
        opponent = Glicko2Player(800, 100, 0.06)
        low_rated.update_rating([opponent], [0.0])
    
    print(f"After losing 5 games: {low_rated}")
    print(f"Rating is clamped at floor: {low_rated.rating >= Glicko2Player.FLOOR}")