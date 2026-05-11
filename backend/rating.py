"""
Simplified Glicko-2 rating system implementation.

This module implements a simplified version of Glicko-2 that tracks:
- Rating (starting at 1500)
- Rating Deviation (RD) - uncertainty in the rating

We omit volatility for simplicity.
"""
import math
from datetime import datetime, timezone
from typing import NamedTuple


# Constants
INITIAL_RATING = 1500.0
INITIAL_RD = 350.0
MIN_RD = 30.0
MAX_RD = 350.0
BOT_RD = 50.0  # Bots have low uncertainty (consistent play)

# Glicko-2 constants
Q = math.log(10) / 400  # ln(10)/400
C_SQUARED = 34.64  # RD increase per rating period (day)


class RatingUpdate(NamedTuple):
    """Result of a rating calculation."""
    new_rating: float
    new_rd: float
    rating_change: float


def _g(rd: float) -> float:
    """Glicko g-function: reduces impact of opponent's rating based on their RD."""
    return 1 / math.sqrt(1 + 3 * (Q ** 2) * (rd ** 2) / (math.pi ** 2))


def _e(rating: float, opponent_rating: float, opponent_rd: float) -> float:
    """Expected score against opponent."""
    g_rd = _g(opponent_rd)
    exponent = -g_rd * (rating - opponent_rating) / 400
    return 1 / (1 + math.pow(10, exponent))


def calculate_rating_update(
    player_rating: float,
    player_rd: float,
    opponent_rating: float,
    result: float,  # 1.0 = win, 0.5 = draw, 0.0 = loss
    opponent_rd: float = BOT_RD
) -> RatingUpdate:
    """
    Calculate new rating and RD after a single game.

    Args:
        player_rating: Player's current rating
        player_rd: Player's current rating deviation
        opponent_rating: Opponent's rating (bot rating)
        result: Game result (1.0=win, 0.5=draw, 0.0=loss)
        opponent_rd: Opponent's rating deviation (default: BOT_RD for bots)

    Returns:
        RatingUpdate with new rating, new RD, and rating change
    """
    # Calculate g-function for opponent
    g_opp = _g(opponent_rd)

    # Calculate expected score
    expected = _e(player_rating, opponent_rating, opponent_rd)

    # Calculate d-squared (estimation error variance)
    d_squared = 1 / (Q ** 2 * g_opp ** 2 * expected * (1 - expected))

    # Calculate new RD
    new_rd_squared = 1 / (1 / player_rd ** 2 + 1 / d_squared)
    new_rd = math.sqrt(new_rd_squared)

    # Calculate rating change
    rating_change = Q / (1 / player_rd ** 2 + 1 / d_squared) * g_opp * (result - expected)

    # Apply changes
    new_rating = player_rating + rating_change

    # Clamp RD to valid range
    new_rd = max(MIN_RD, min(MAX_RD, new_rd))

    # Clamp rating to reasonable range (100-3500)
    new_rating = max(100.0, min(3500.0, new_rating))

    return RatingUpdate(
        new_rating=round(new_rating, 1),
        new_rd=round(new_rd, 1),
        rating_change=round(rating_change, 1)
    )


def increase_rd_for_inactivity(
    current_rd: float,
    last_game_at: datetime | None,
    now: datetime | None = None
) -> float:
    """
    Increase RD based on time since last game.

    RD increases when a player is inactive to reflect increased uncertainty.
    Increases by approximately 1 point per day, capped at MAX_RD.

    Args:
        current_rd: Current rating deviation
        last_game_at: Timestamp of last game played
        now: Current timestamp (defaults to now)

    Returns:
        Updated rating deviation
    """
    if last_game_at is None:
        return current_rd

    if now is None:
        now = datetime.now(timezone.utc)

    # Calculate days since last game
    days_inactive = (now - last_game_at).days

    if days_inactive <= 0:
        return current_rd

    # Increase RD based on inactivity
    # Using simplified formula: RD_new = sqrt(RD_old^2 + c^2 * t)
    # where c is about 1 rating point per day and t is days
    new_rd_squared = current_rd ** 2 + C_SQUARED * days_inactive
    new_rd = math.sqrt(new_rd_squared)

    return min(MAX_RD, round(new_rd, 1))


def result_string_to_float(result: str) -> float:
    """Convert result string to numeric value."""
    result_map = {
        'win': 1.0,
        'loss': 0.0,
        'draw': 0.5
    }
    return result_map.get(result.lower(), 0.5)


def get_rating_tier(rating: float) -> str:
    """Get a descriptive tier name for a rating."""
    if rating < 800:
        return "Beginner"
    elif rating < 1000:
        return "Novice"
    elif rating < 1200:
        return "Intermediate"
    elif rating < 1400:
        return "Club Player"
    elif rating < 1600:
        return "Tournament Player"
    elif rating < 1800:
        return "Expert"
    elif rating < 2000:
        return "Candidate Master"
    elif rating < 2200:
        return "Master"
    elif rating < 2400:
        return "International Master"
    else:
        return "Grandmaster"
