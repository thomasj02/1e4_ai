'use client';

import React from 'react';
import Link from 'next/link';
import type { RatingUpdate } from '@/types/rating';

interface RatingChangeModalProps {
  isOpen: boolean;
  onClose: () => void;
  onPlayAgain: () => void;
  ratingUpdate: RatingUpdate | null;
  gameResult: 'win' | 'loss' | 'draw';
  botRating: number;
  resultReason?: string;
}

const RatingChangeModal: React.FC<RatingChangeModalProps> = ({
  isOpen,
  onClose,
  onPlayAgain,
  ratingUpdate,
  gameResult,
  botRating,
  resultReason,
}) => {
  if (!isOpen || !ratingUpdate) return null;

  const isPositive = ratingUpdate.ratingChange > 0;
  const isNeutral = ratingUpdate.ratingChange === 0;

  const getResultText = () => {
    switch (gameResult) {
      case 'win':
        return 'You won!';
      case 'loss':
        return 'You lost';
      case 'draw':
        return 'Draw';
    }
  };

  const getResultColor = () => {
    switch (gameResult) {
      case 'win':
        return 'text-success';
      case 'loss':
        return 'text-danger';
      case 'draw':
        return 'text-warning';
    }
  };

  const getRatingChangeColor = () => {
    if (isNeutral) return 'text-secondary';
    return isPositive ? 'text-success' : 'text-danger';
  };

  const formatRatingChange = () => {
    if (isNeutral) return '0';
    return isPositive ? `+${ratingUpdate.ratingChange}` : `${ratingUpdate.ratingChange}`;
  };

  return (
    <div className="modal d-block" tabIndex={-1} style={{ backgroundColor: 'rgba(0,0,0,0.7)' }}>
      <div className="modal-dialog modal-dialog-centered">
        <div className="modal-content bg-dark text-light border-secondary">
          <div className="modal-header border-secondary">
            <h5 className={`modal-title ${getResultColor()}`}>
              {getResultText()}
            </h5>
            <button
              type="button"
              className="btn-close btn-close-white"
              onClick={onClose}
              aria-label="Close"
            />
          </div>
          <div className="modal-body text-center">
            {/* Rating Display */}
            <div className="mb-4">
              <div className="small text-muted mb-1">Your Rating</div>
              <div className="d-flex justify-content-center align-items-baseline gap-2">
                <span className="fs-1 fw-bold">{Math.round(ratingUpdate.newRating)}</span>
                <span className={`fs-4 fw-semibold ${getRatingChangeColor()}`}>
                  ({formatRatingChange()})
                </span>
              </div>
            </div>

            {/* Game Summary */}
            <div className="bg-secondary bg-opacity-25 rounded p-3 mb-3">
              <div className="small text-muted mb-1">vs Bot ({botRating})</div>
              {resultReason && (
                <div className="small">{resultReason}</div>
              )}
            </div>
          </div>
          <div className="modal-footer border-secondary justify-content-center gap-2">
            <button
              type="button"
              className="btn btn-primary"
              onClick={onPlayAgain}
            >
              Play Again
            </button>
            <Link href="/profile" className="btn btn-outline-secondary">
              View Profile
            </Link>
          </div>
        </div>
      </div>
    </div>
  );
};

export default RatingChangeModal;
