'use client';

import React, { useState, useEffect, useCallback } from 'react';
import Link from 'next/link';
import { useRouter } from 'next/navigation';
import { SignedIn, SignedOut, RedirectToSignIn } from '@clerk/nextjs';
import { useUserRating } from '@/hooks/useUserRating';
import type { GameHistory, GameHistoryItem } from '@/types/rating';

function ProfileContent() {
  const router = useRouter();
  const {
    rating,
    loading,
    error,
    editRating,
    fetchGameHistory,
  } = useUserRating();

  const [editMode, setEditMode] = useState(false);
  const [editValue, setEditValue] = useState<number>(1500);
  const [editError, setEditError] = useState<string | null>(null);
  const [gameHistory, setGameHistory] = useState<GameHistory | null>(null);
  const [historyLoading, setHistoryLoading] = useState(false);

  // Load game history on mount
  useEffect(() => {
    const loadHistory = async () => {
      setHistoryLoading(true);
      const history = await fetchGameHistory(20, 0);
      setGameHistory(history);
      setHistoryLoading(false);
    };
    loadHistory();
  }, [fetchGameHistory]);

  // Update edit value when rating changes
  useEffect(() => {
    if (rating) {
      setEditValue(Math.round(rating.rating));
    }
  }, [rating]);

  const handleEditSubmit = useCallback(async () => {
    setEditError(null);
    try {
      await editRating(editValue);
      setEditMode(false);
    } catch (err) {
      setEditError(err instanceof Error ? err.message : 'Failed to update rating');
    }
  }, [editRating, editValue]);

  const formatDate = (dateString: string) => {
    const date = new Date(dateString);
    return date.toLocaleDateString(undefined, {
      month: 'short',
      day: 'numeric',
      hour: '2-digit',
      minute: '2-digit',
    });
  };

  const getResultIcon = (result: string) => {
    switch (result) {
      case 'win':
        return <span className="text-success">W</span>;
      case 'loss':
        return <span className="text-danger">L</span>;
      case 'draw':
        return <span className="text-warning">D</span>;
      default:
        return <span>?</span>;
    }
  };

  const formatRatingChange = (change: number) => {
    if (change === 0) return <span className="text-secondary">0</span>;
    if (change > 0) return <span className="text-success">+{change}</span>;
    return <span className="text-danger">{change}</span>;
  };

  if (loading && !rating) {
    return (
      <div className="d-flex justify-content-center align-items-center h-100">
        <div className="spinner-border text-primary" role="status">
          <span className="visually-hidden">Loading...</span>
        </div>
      </div>
    );
  }

  if (error) {
    return (
      <div className="container py-4">
        <div className="alert alert-danger">{error}</div>
        <Link href="/" className="btn btn-primary">Back to Game</Link>
      </div>
    );
  }

  if (!rating) {
    return (
      <div className="container py-4">
        <div className="alert alert-info">No rating data available. Play a game to get started!</div>
        <Link href="/" className="btn btn-primary">Play Now</Link>
      </div>
    );
  }

  const winRate = rating.gamesPlayed > 0
    ? ((rating.wins / rating.gamesPlayed) * 100).toFixed(1)
    : '0';

  return (
    <div className="container py-4" style={{ maxWidth: '800px' }}>
      {/* Header */}
      <div className="d-flex justify-content-between align-items-center mb-4">
        <h1 className="h3 mb-0">Your Profile</h1>
        <Link href="/" className="btn btn-outline-primary">
          <i className="bi bi-arrow-left me-2"></i>
          Back to Game
        </Link>
      </div>

      {/* Rating Card */}
      <div className="card bg-dark border-secondary mb-4">
        <div className="card-body">
          <div className="row align-items-center">
            <div className="col-md-6 text-center text-md-start mb-3 mb-md-0">
              {editMode ? (
                <div className="d-flex flex-column align-items-center align-items-md-start">
                  <label className="small text-muted mb-2">Set Your Rating (100-3500)</label>
                  <div className="d-flex gap-2 align-items-center">
                    <input
                      type="number"
                      className="form-control bg-secondary text-light border-0"
                      style={{ width: '120px' }}
                      value={editValue}
                      onChange={(e) => setEditValue(Number(e.target.value))}
                      min={100}
                      max={3500}
                    />
                    <button
                      className="btn btn-success btn-sm"
                      onClick={handleEditSubmit}
                      disabled={loading}
                    >
                      Save
                    </button>
                    <button
                      className="btn btn-outline-secondary btn-sm"
                      onClick={() => {
                        setEditMode(false);
                        setEditValue(Math.round(rating.rating));
                        setEditError(null);
                      }}
                    >
                      Cancel
                    </button>
                  </div>
                  {editError && (
                    <div className="text-danger small mt-2">{editError}</div>
                  )}
                  <div className="mt-3">
                    <input
                      type="range"
                      className="form-range"
                      min={100}
                      max={3500}
                      step={50}
                      value={editValue}
                      onChange={(e) => setEditValue(Number(e.target.value))}
                      style={{ width: '250px' }}
                    />
                  </div>
                </div>
              ) : (
                <>
                  <div className="small text-muted">Current Rating</div>
                  <div className="d-flex align-items-baseline gap-2 justify-content-center justify-content-md-start">
                    <span className="display-4 fw-bold">{Math.round(rating.rating)}</span>
                    <button
                      className="btn btn-link text-muted p-0"
                      onClick={() => setEditMode(true)}
                      title="Edit rating"
                    >
                      <i className="bi bi-pencil"></i>
                    </button>
                  </div>
                </>
              )}
            </div>
            <div className="col-md-6">
              <div className="row text-center">
                <div className="col-4">
                  <div className="small text-muted">Games</div>
                  <div className="fs-4 fw-semibold">{rating.gamesPlayed}</div>
                </div>
                <div className="col-4">
                  <div className="small text-muted">Win Rate</div>
                  <div className="fs-4 fw-semibold">{winRate}%</div>
                </div>
                <div className="col-4">
                  <div className="small text-muted">W/L/D</div>
                  <div className="fs-5">
                    <span className="text-success">{rating.wins}</span>
                    <span className="text-muted">/</span>
                    <span className="text-danger">{rating.losses}</span>
                    <span className="text-muted">/</span>
                    <span className="text-warning">{rating.draws}</span>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>

      {/* Rating Deviation Info */}
      <div className="card bg-dark border-secondary mb-4">
        <div className="card-body">
          <div className="d-flex justify-content-between align-items-center">
            <div>
              <div className="small text-muted">Rating Uncertainty</div>
              <div className="fw-semibold">{Math.round(rating.ratingDeviation)} RD</div>
            </div>
            <div className="text-end">
              <div className="small text-muted">
                {rating.ratingDeviation > 200 ? (
                  <span className="text-warning">High uncertainty - play more games</span>
                ) : rating.ratingDeviation > 100 ? (
                  <span className="text-info">Moderate uncertainty</span>
                ) : (
                  <span className="text-success">Rating is stable</span>
                )}
              </div>
            </div>
          </div>
        </div>
      </div>

      {/* Game History */}
      <div className="card bg-dark border-secondary">
        <div className="card-header border-secondary">
          <h2 className="h5 mb-0">Recent Games</h2>
        </div>
        <div className="card-body p-0">
          {historyLoading ? (
            <div className="text-center py-4">
              <div className="spinner-border spinner-border-sm text-primary" role="status">
                <span className="visually-hidden">Loading...</span>
              </div>
            </div>
          ) : gameHistory && gameHistory.games.length > 0 ? (
            <div className="table-responsive">
              <table className="table table-dark table-hover mb-0">
                <thead>
                  <tr className="text-muted small">
                    <th>Result</th>
                    <th>Bot Rating</th>
                    <th>Color</th>
                    <th>Rating Change</th>
                    <th>New Rating</th>
                    <th>Date</th>
                  </tr>
                </thead>
                <tbody>
                  {gameHistory.games.map((game: GameHistoryItem) => (
                    <tr
                      key={game.id}
                      onClick={() => router.push(`/review/${game.id}`)}
                      style={{ cursor: 'pointer' }}
                      className="table-row-hover"
                    >
                      <td>{getResultIcon(game.result)}</td>
                      <td>{game.botRating}</td>
                      <td>
                        <span className={game.playerColor === 'white' ? 'text-light' : 'text-secondary'}>
                          {game.playerColor === 'white' ? 'W' : 'B'}
                        </span>
                      </td>
                      <td>{formatRatingChange(game.ratingChange)}</td>
                      <td>{Math.round(game.ratingAfter)}</td>
                      <td className="small text-muted">{formatDate(game.playedAt)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          ) : (
            <div className="text-center py-4 text-muted">
              No games played yet. Start playing to build your history!
            </div>
          )}
        </div>
        {gameHistory && gameHistory.totalCount > 20 && (
          <div className="card-footer border-secondary text-center text-muted small">
            Showing 20 of {gameHistory.totalCount} games
          </div>
        )}
      </div>
    </div>
  );
}

export default function ProfilePage() {
  if (process.env.NEXT_PUBLIC_DISABLE_AUTH === 'true') {
    return (
      <div className="container py-4">
        <div className="alert alert-info">
          Profile and rating history are unavailable while authentication is disabled.
        </div>
        <Link href="/" className="btn btn-primary">Back to Game</Link>
      </div>
    );
  }

  return (
    <>
      <SignedIn>
        <ProfileContent />
      </SignedIn>
      <SignedOut>
        <RedirectToSignIn />
      </SignedOut>
    </>
  );
}
