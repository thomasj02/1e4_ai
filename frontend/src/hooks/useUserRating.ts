'use client';

import { useState, useCallback, useEffect } from 'react';
import { useAuth } from '@utils/auth';
import { BACKEND_URL } from '@constants/chess';
import type { UserRating, GameResult, RatingUpdate, GameHistory, GameHistoryItem, MoveRecord } from '@/types/rating';

interface UseUserRatingResult {
  rating: UserRating | null;
  loading: boolean;
  error: string | null;
  lastRatingUpdate: RatingUpdate | null;
  fetchRating: () => Promise<void>;
  recordGameResult: (result: GameResult) => Promise<RatingUpdate | null>;
  editRating: (newRating: number) => Promise<void>;
  fetchGameHistory: (limit?: number, offset?: number) => Promise<GameHistory | null>;
  fetchGame: (gameId: string) => Promise<GameHistoryItem | null>;
  clearLastRatingUpdate: () => void;
}

/**
 * Hook for managing user rating state and API interactions.
 */
export function useUserRating(): UseUserRatingResult {
  const { getToken, isSignedIn } = useAuth();
  const [rating, setRating] = useState<UserRating | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [lastRatingUpdate, setLastRatingUpdate] = useState<RatingUpdate | null>(null);

  const fetchRating = useCallback(async () => {
    if (!isSignedIn) {
      setRating(null);
      return;
    }

    setLoading(true);
    setError(null);

    try {
      const token = await getToken();
      const response = await fetch(`${BACKEND_URL}/rating`, {
        method: 'GET',
        headers: {
          'Content-Type': 'application/json',
          ...(token ? { Authorization: `Bearer ${token}` } : {}),
        },
      });

      if (!response.ok) {
        throw new Error(`Failed to fetch rating: ${response.status}`);
      }

      const data = await response.json();
      setRating({
        rating: data.rating,
        ratingDeviation: data.rating_deviation,
        gamesPlayed: data.games_played,
        wins: data.wins,
        losses: data.losses,
        draws: data.draws,
        ratingTier: data.rating_tier,
      });
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Failed to fetch rating';
      setError(message);
      console.error('Error fetching rating:', err);
    } finally {
      setLoading(false);
    }
  }, [getToken, isSignedIn]);

  const recordGameResult = useCallback(async (result: GameResult): Promise<RatingUpdate | null> => {
    if (!isSignedIn) {
      console.log('Cannot record game result: user not signed in');
      return null;
    }

    setLoading(true);
    setError(null);

    try {
      const token = await getToken();
      const response = await fetch(`${BACKEND_URL}/rating/game`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          ...(token ? { Authorization: `Bearer ${token}` } : {}),
        },
        body: JSON.stringify({
          bot_rating: result.botRating,
          player_color: result.playerColor,
          result: result.result,
          result_reason: result.resultReason,
          move_count: result.moveCount,
          time_control: result.timeControl,
          game_moves: result.gameMoves,
        }),
      });

      if (!response.ok) {
        throw new Error(`Failed to record game result: ${response.status}`);
      }

      const data = await response.json();
      const update: RatingUpdate = {
        newRating: data.new_rating,
        ratingChange: data.rating_change,
        newRd: data.new_rd,
        ratingTier: data.rating_tier,
      };

      setLastRatingUpdate(update);

      // Update local rating state
      setRating(prev => prev ? {
        ...prev,
        rating: update.newRating,
        ratingDeviation: update.newRd,
        ratingTier: update.ratingTier,
        gamesPlayed: prev.gamesPlayed + 1,
        wins: prev.wins + (result.result === 'win' ? 1 : 0),
        losses: prev.losses + (result.result === 'loss' ? 1 : 0),
        draws: prev.draws + (result.result === 'draw' ? 1 : 0),
      } : null);

      return update;
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Failed to record game result';
      setError(message);
      console.error('Error recording game result:', err);
      return null;
    } finally {
      setLoading(false);
    }
  }, [getToken, isSignedIn]);

  const editRating = useCallback(async (newRating: number): Promise<void> => {
    if (!isSignedIn) {
      throw new Error('Must be signed in to edit rating');
    }

    // Validate range
    if (newRating < 100 || newRating > 3500) {
      throw new Error('Rating must be between 100 and 3500');
    }

    setLoading(true);
    setError(null);

    try {
      const token = await getToken();
      const response = await fetch(`${BACKEND_URL}/rating/edit`, {
        method: 'PUT',
        headers: {
          'Content-Type': 'application/json',
          ...(token ? { Authorization: `Bearer ${token}` } : {}),
        },
        body: JSON.stringify({ new_rating: newRating }),
      });

      if (!response.ok) {
        throw new Error(`Failed to edit rating: ${response.status}`);
      }

      const data = await response.json();
      setRating({
        rating: data.rating,
        ratingDeviation: data.rating_deviation,
        gamesPlayed: data.games_played,
        wins: data.wins,
        losses: data.losses,
        draws: data.draws,
        ratingTier: data.rating_tier,
      });
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Failed to edit rating';
      setError(message);
      console.error('Error editing rating:', err);
      throw err;
    } finally {
      setLoading(false);
    }
  }, [getToken, isSignedIn]);

  const fetchGameHistory = useCallback(async (
    limit: number = 20,
    offset: number = 0
  ): Promise<GameHistory | null> => {
    if (!isSignedIn) {
      return null;
    }

    try {
      const token = await getToken();
      const response = await fetch(
        `${BACKEND_URL}/rating/history?limit=${limit}&offset=${offset}`,
        {
          method: 'GET',
          headers: {
            'Content-Type': 'application/json',
            ...(token ? { Authorization: `Bearer ${token}` } : {}),
          },
        }
      );

      if (!response.ok) {
        throw new Error(`Failed to fetch game history: ${response.status}`);
      }

      const data = await response.json();
      return {
        games: data.games.map((g: Record<string, unknown>) => ({
          id: g.id,
          botRating: g.bot_rating,
          playerColor: g.player_color,
          result: g.result,
          resultReason: g.result_reason,
          moveCount: g.move_count,
          timeControl: g.time_control,
          ratingBefore: g.rating_before,
          ratingAfter: g.rating_after,
          ratingChange: g.rating_change,
          playedAt: g.played_at,
          gameMoves: g.game_moves as MoveRecord[] | undefined,
        })),
        totalCount: data.total_count,
      };
    } catch (err) {
      console.error('Error fetching game history:', err);
      return null;
    }
  }, [getToken, isSignedIn]);

  const fetchGame = useCallback(async (gameId: string): Promise<GameHistoryItem | null> => {
    if (!isSignedIn) return null;

    try {
      const token = await getToken();
      const response = await fetch(`${BACKEND_URL}/rating/game/${gameId}`, {
        headers: {
          'Content-Type': 'application/json',
          ...(token ? { Authorization: `Bearer ${token}` } : {}),
        },
      });

      if (!response.ok) {
        if (response.status === 404) return null;
        throw new Error(`Failed to fetch game: ${response.status}`);
      }

      const data = await response.json();
      return {
        id: data.id,
        botRating: data.bot_rating,
        playerColor: data.player_color,
        result: data.result,
        resultReason: data.result_reason,
        moveCount: data.move_count,
        timeControl: data.time_control,
        ratingBefore: data.rating_before,
        ratingAfter: data.rating_after,
        ratingChange: data.rating_change,
        playedAt: data.played_at,
        gameMoves: data.game_moves as MoveRecord[] | undefined,
      };
    } catch (err) {
      console.error('Error fetching game:', err);
      return null;
    }
  }, [getToken, isSignedIn]);

  const clearLastRatingUpdate = useCallback(() => {
    setLastRatingUpdate(null);
  }, []);

  // Fetch rating on mount if signed in
  useEffect(() => {
    if (isSignedIn) {
      fetchRating();
    }
  }, [isSignedIn, fetchRating]);

  return {
    rating,
    loading,
    error,
    lastRatingUpdate,
    fetchRating,
    recordGameResult,
    editRating,
    fetchGameHistory,
    fetchGame,
    clearLastRatingUpdate,
  };
}
