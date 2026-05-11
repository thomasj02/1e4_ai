'use client';

import React, { useState, useEffect, useCallback, useRef } from 'react';
import Link from 'next/link';
import { Chess } from 'chess.js';
import { useUserRating } from '@/hooks/useUserRating';
import ChessboardArea from './ChessboardArea';
import type { GameHistoryItem, MoveRecord } from '@/types/rating';

interface GameReviewProps {
  gameId: string;
}

interface MovePair {
  moveNumber: number;
  white: MoveRecord;
  black: MoveRecord | null;
  whiteIndex: number;
  blackIndex: number;
}

function formatClock(ms: number): string {
  const totalSeconds = Math.floor(ms / 1000);
  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;
  return `${minutes}:${seconds.toString().padStart(2, '0')}`;
}

function formatDate(dateString: string): string {
  const date = new Date(dateString);
  return date.toLocaleDateString(undefined, {
    year: 'numeric',
    month: 'short',
    day: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
  });
}

function parseTimeControl(timeControl: string | undefined): number {
  if (!timeControl) return 300000; // Default 5 minutes
  const match = timeControl.match(/^(\d+)/);
  if (match) {
    return parseInt(match[1], 10) * 60 * 1000; // Convert minutes to ms
  }
  return 300000;
}

function getClockTimes(
  plyIndex: number,
  gameMoves: MoveRecord[],
  initialTime: number
): { white: number; black: number } {
  if (plyIndex <= 0) return { white: initialTime, black: initialTime };

  let whiteTime = initialTime;
  let blackTime = initialTime;

  for (let i = 0; i < plyIndex; i++) {
    if (gameMoves[i]?.clock !== undefined) {
      if (i % 2 === 0) {
        whiteTime = gameMoves[i].clock;
      } else {
        blackTime = gameMoves[i].clock;
      }
    }
  }

  return { white: whiteTime, black: blackTime };
}

// Move List Component - extracted outside GameReview to prevent remounting on every render
interface GameReviewMoveListProps {
  horizontal?: boolean;
  viewedPlyIndex: number;
  movePairs: MovePair[];
  onGoToPly: (plyIndex: number) => void;
}

const GameReviewMoveList: React.FC<GameReviewMoveListProps> = ({
  horizontal = false,
  viewedPlyIndex,
  movePairs,
  onGoToPly
}) => {
  const activeMoveRef = useRef<HTMLElement>(null);

  // Scroll active move into view
  useEffect(() => {
    if (activeMoveRef.current) {
      activeMoveRef.current.scrollIntoView({
        behavior: 'smooth',
        block: 'nearest',
        inline: 'nearest'
      });
    }
  }, [viewedPlyIndex]);

  if (horizontal) {
    // Horizontal scrolling move list for mobile
    return (
      <div className="d-flex align-items-center gap-2 overflow-x-auto p-2" style={{ whiteSpace: 'nowrap' }}>
        <span
          ref={viewedPlyIndex === -1 ? activeMoveRef : null}
          className={`px-2 py-1 rounded flex-shrink-0 ${
            viewedPlyIndex === -1
              ? 'fw-bold bg-primary text-white'
              : 'bg-secondary text-light'
          }`}
          style={{ cursor: 'pointer' }}
          onClick={() => onGoToPly(-1)}
        >
          Start
        </span>
        {movePairs.map((pair) => {
          const isWhiteActive = pair.whiteIndex === viewedPlyIndex;
          const isBlackActive = pair.black && pair.blackIndex === viewedPlyIndex;
          return (
            <React.Fragment key={pair.moveNumber}>
              <span
                ref={isWhiteActive ? activeMoveRef : null}
                className={`px-2 py-1 rounded flex-shrink-0 ${
                  isWhiteActive ? 'fw-bold bg-info text-white' : 'bg-secondary text-light'
                }`}
                style={{ cursor: 'pointer' }}
                onClick={() => onGoToPly(pair.whiteIndex)}
              >
                {pair.moveNumber}. {pair.white.move}
              </span>
              {pair.black && (
                <span
                  ref={isBlackActive ? activeMoveRef : null}
                  className={`px-2 py-1 rounded flex-shrink-0 ${
                    isBlackActive ? 'fw-bold bg-info text-white' : 'bg-secondary text-light'
                  }`}
                  style={{ cursor: 'pointer' }}
                  onClick={() => onGoToPly(pair.blackIndex)}
                >
                  {pair.black.move}
                </span>
              )}
            </React.Fragment>
          );
        })}
      </div>
    );
  }

  // Vertical move list for desktop
  return (
    <>
      <div
        ref={viewedPlyIndex === -1 ? activeMoveRef as React.RefObject<HTMLDivElement> : null}
        className={`d-inline-block px-1 py-1 mb-2 rounded ${
          viewedPlyIndex === -1
            ? 'fw-bold bg-primary text-white'
            : 'bg-secondary text-light'
        }`}
        style={{ cursor: 'pointer' }}
        onClick={() => onGoToPly(-1)}
      >
        Start Position
      </div>
      {movePairs.map((pair) => {
        const isWhiteActive = pair.whiteIndex === viewedPlyIndex;
        const isBlackActive = pair.black && pair.blackIndex === viewedPlyIndex;
        return (
          <div key={pair.moveNumber} className="d-flex mb-1 align-items-center">
            <span
              className="text-muted me-2"
              style={{ width: '40px', minWidth: '40px', textAlign: 'right' }}
            >
              {pair.moveNumber}.
            </span>
            <span
              ref={isWhiteActive ? activeMoveRef : null}
              className={`px-1 py-1 rounded me-2 text-start ${
                isWhiteActive ? 'fw-bold bg-info text-white' : 'bg-secondary text-light'
              }`}
              style={{ width: '70px', minWidth: '70px', cursor: 'pointer' }}
              onClick={() => onGoToPly(pair.whiteIndex)}
            >
              {pair.white.move}
            </span>
            <span
              ref={isBlackActive ? activeMoveRef : null}
              className={`px-1 py-1 rounded text-start ${
                isBlackActive ? 'fw-bold bg-info text-white' : 'bg-secondary text-light'
              }`}
              style={{
                width: '70px',
                minWidth: '70px',
                cursor: pair.black ? 'pointer' : 'default',
                minHeight: '1em',
              }}
              onClick={() => pair.black && onGoToPly(pair.blackIndex)}
            >
              {pair.black ? pair.black.move : ''}
            </span>
          </div>
        );
      })}
    </>
  );
};

export default function GameReview({ gameId }: GameReviewProps) {
  const { fetchGame } = useUserRating();
  const [game, setGame] = useState<GameHistoryItem | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [viewedPlyIndex, setViewedPlyIndex] = useState(-1);
  const [positions, setPositions] = useState<string[]>([]);
  const [fenCopied, setFenCopied] = useState(false);
  const [pgnCopied, setPgnCopied] = useState(false);
  const [isMobile, setIsMobile] = useState(false);

  // Mobile detection
  useEffect(() => {
    const checkMobile = () => setIsMobile(window.innerWidth <= 767);
    checkMobile();
    window.addEventListener('resize', checkMobile);
    return () => window.removeEventListener('resize', checkMobile);
  }, []);

  // Fetch game data on mount
  useEffect(() => {
    const loadGame = async () => {
      setLoading(true);
      setError(null);
      try {
        const gameData = await fetchGame(gameId);
        if (!gameData) {
          setError('Game not found');
        } else {
          setGame(gameData);
        }
      } catch (err) {
        setError(err instanceof Error ? err.message : 'Failed to load game');
      } finally {
        setLoading(false);
      }
    };
    loadGame();
  }, [gameId, fetchGame]);

  // Build positions array from gameMoves
  useEffect(() => {
    if (!game?.gameMoves || game.gameMoves.length === 0) {
      setPositions([]);
      return;
    }

    const chess = new Chess();
    const fens = [chess.fen()]; // Starting position

    for (const moveRecord of game.gameMoves) {
      try {
        chess.move(moveRecord.move);
        fens.push(chess.fen());
      } catch (e) {
        console.error('Invalid move:', moveRecord.move, e);
        break;
      }
    }

    setPositions(fens);
    setViewedPlyIndex(fens.length - 1); // Start at final position
  }, [game]);

  // Keyboard navigation
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'ArrowLeft') {
        e.preventDefault();
        setViewedPlyIndex(prev => Math.max(-1, prev - 1));
      } else if (e.key === 'ArrowRight') {
        e.preventDefault();
        setViewedPlyIndex(prev => Math.min(positions.length - 1, prev + 1));
      } else if (e.key === 'Home') {
        e.preventDefault();
        setViewedPlyIndex(-1);
      } else if (e.key === 'End') {
        e.preventDefault();
        setViewedPlyIndex(positions.length - 1);
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [positions.length]);

  const handleGoToPly = useCallback((plyIndex: number) => {
    setViewedPlyIndex(plyIndex);
  }, []);

  // Get current evaluation
  const currentEval = viewedPlyIndex > 0 && game?.gameMoves
    ? game.gameMoves[viewedPlyIndex - 1]?.eval
    : null;

  // Get current FEN
  const currentFen = viewedPlyIndex === -1 || positions.length === 0
    ? 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1'
    : positions[viewedPlyIndex] || positions[positions.length - 1];

  const handleCopyFEN = useCallback(async () => {
    try {
      await navigator.clipboard.writeText(currentFen);
      setFenCopied(true);
      setTimeout(() => setFenCopied(false), 2000);
    } catch (err) {
      console.error('Failed to copy FEN:', err);
    }
  }, [currentFen]);

  const handleCopyPGN = useCallback(async () => {
    if (!game) return;
    try {
      const date = new Date(game.playedAt);
      const dateStr = `${date.getFullYear()}.${String(date.getMonth() + 1).padStart(2, '0')}.${String(date.getDate()).padStart(2, '0')}`;
      const resultStr = game.result === 'win'
        ? (game.playerColor === 'white' ? '1-0' : '0-1')
        : game.result === 'loss'
          ? (game.playerColor === 'white' ? '0-1' : '1-0')
          : '1/2-1/2';

      const headers = [
        `[Event "ChessMimic Game"]`,
        `[Site "ChessMimic"]`,
        `[Date "${dateStr}"]`,
        `[White "${game.playerColor === 'white' ? 'Player' : `Bot (${game.botRating})`}"]`,
        `[Black "${game.playerColor === 'black' ? 'Player' : `Bot (${game.botRating})`}"]`,
        `[Result "${resultStr}"]`,
        ...(game.timeControl ? [`[TimeControl "${game.timeControl}"]`] : []),
      ].join('\n');

      // Build move text with clock annotations
      let moveText = '';
      if (game.gameMoves) {
        for (let i = 0; i < game.gameMoves.length; i++) {
          const m = game.gameMoves[i];
          if (i % 2 === 0) {
            moveText += `${Math.floor(i / 2) + 1}. `;
          }
          moveText += m.move;
          if (m.clock !== undefined) {
            const totalSec = Math.floor(m.clock / 1000);
            const h = Math.floor(totalSec / 3600);
            const min = Math.floor((totalSec % 3600) / 60);
            const sec = totalSec % 60;
            moveText += ` {[%clk ${h}:${String(min).padStart(2, '0')}:${String(sec).padStart(2, '0')}]}`;
          }
          moveText += ' ';
        }
        moveText += resultStr;
      }

      const pgn = `${headers}\n\n${moveText.trim()}`;
      await navigator.clipboard.writeText(pgn);
      setPgnCopied(true);
      setTimeout(() => setPgnCopied(false), 2000);
    } catch (err) {
      console.error('Failed to copy PGN:', err);
    }
  }, [game]);

  // Calculate dual clock times
  const initialTime = parseTimeControl(game?.timeControl);
  const clockTimes = game?.gameMoves
    ? getClockTimes(viewedPlyIndex, game.gameMoves, initialTime)
    : { white: initialTime, black: initialTime };

  // Group moves into pairs for display
  const movePairs: MovePair[] = [];
  if (game?.gameMoves) {
    for (let i = 0; i < game.gameMoves.length; i += 2) {
      movePairs.push({
        moveNumber: Math.floor(i / 2) + 1,
        white: game.gameMoves[i],
        black: game.gameMoves[i + 1] || null,
        whiteIndex: i,
        blackIndex: i + 1,
      });
    }
  }

  // Determine board orientation based on player color
  const boardOrientation = game?.playerColor === 'black' ? 'black' : 'white';

  // Get result display
  const getResultDisplay = () => {
    if (!game) return '';
    const resultText = game.result === 'win' ? 'Win' : game.result === 'loss' ? 'Loss' : 'Draw';
    const reasonText = game.resultReason ? ` - ${game.resultReason}` : '';
    return `${resultText}${reasonText}`;
  };

  if (loading) {
    return (
      <div className="container py-4">
        <div className="d-flex justify-content-center align-items-center" style={{ minHeight: '400px' }}>
          <div className="spinner-border text-primary" role="status">
            <span className="visually-hidden">Loading...</span>
          </div>
        </div>
      </div>
    );
  }

  if (error) {
    return (
      <div className="container py-4">
        <div className="alert alert-danger">{error}</div>
        <Link href="/profile" className="btn btn-primary">Back to Profile</Link>
      </div>
    );
  }

  if (!game) {
    return (
      <div className="container py-4">
        <div className="alert alert-info">Game not found</div>
        <Link href="/profile" className="btn btn-primary">Back to Profile</Link>
      </div>
    );
  }

  if (!game.gameMoves || game.gameMoves.length === 0) {
    return (
      <div className="container py-4">
        <div className="d-flex justify-content-between align-items-center mb-4">
          <h1 className="h4 mb-0">Game Review</h1>
          <Link href="/profile" className="btn btn-outline-primary btn-sm">
            Back to Profile
          </Link>
        </div>
        <div className="alert alert-warning">
          This game does not have move data available for review.
        </div>
        <div className="card bg-dark border-secondary">
          <div className="card-body">
            <p><strong>Result:</strong> {getResultDisplay()}</p>
            <p><strong>Bot Rating:</strong> {game.botRating}</p>
            <p><strong>Played:</strong> {formatDate(game.playedAt)}</p>
            <p><strong>Rating Change:</strong> {game.ratingChange > 0 ? '+' : ''}{game.ratingChange}</p>
          </div>
        </div>
      </div>
    );
  }

  // Navigation Controls Component (reused in both layouts)
  const NavigationControls = ({ compact = false }: { compact?: boolean }) => (
    <div className={`d-flex justify-content-center align-items-center ${compact ? 'gap-1' : 'gap-2'}`}>
      <button
        className={`btn btn-outline-secondary ${compact ? 'btn-sm px-2' : 'btn-sm'}`}
        onClick={() => setViewedPlyIndex(-1)}
        disabled={viewedPlyIndex === -1}
        title="Go to start (Home)"
      >
        <i className="bi bi-skip-backward-fill"></i>
      </button>
      <button
        className={`btn btn-outline-secondary ${compact ? 'btn-sm px-2' : 'btn-sm'}`}
        onClick={() => setViewedPlyIndex(prev => Math.max(-1, prev - 1))}
        disabled={viewedPlyIndex === -1}
        title="Previous move (Left Arrow)"
      >
        <i className="bi bi-caret-left-fill"></i>
      </button>
      {!compact && (
        <span className="text-muted small px-2">
          {viewedPlyIndex === -1 ? 'Start' : `Move ${viewedPlyIndex + 1}`} of {positions.length - 1}
        </span>
      )}
      <button
        className={`btn btn-outline-secondary ${compact ? 'btn-sm px-2' : 'btn-sm'}`}
        onClick={() => setViewedPlyIndex(prev => Math.min(positions.length - 1, prev + 1))}
        disabled={viewedPlyIndex === positions.length - 1}
        title="Next move (Right Arrow)"
      >
        <i className="bi bi-caret-right-fill"></i>
      </button>
      <button
        className={`btn btn-outline-secondary ${compact ? 'btn-sm px-2' : 'btn-sm'}`}
        onClick={() => setViewedPlyIndex(positions.length - 1)}
        disabled={viewedPlyIndex === positions.length - 1}
        title="Go to end (End)"
      >
        <i className="bi bi-skip-forward-fill"></i>
      </button>
      {!compact && (
        <>
          <span className="border-start border-secondary mx-2" style={{ height: '20px' }}></span>
          <button
            className={`btn btn-sm ${fenCopied ? 'btn-success' : 'btn-outline-secondary'}`}
            onClick={handleCopyFEN}
            title="Copy FEN to clipboard"
          >
            <i className={`bi ${fenCopied ? 'bi-check' : 'bi-clipboard'} me-1`}></i>
            FEN
          </button>
          <button
            className={`btn btn-sm ${pgnCopied ? 'btn-success' : 'btn-outline-secondary'}`}
            onClick={handleCopyPGN}
            title="Copy PGN to clipboard"
          >
            <i className={`bi ${pgnCopied ? 'bi-check' : 'bi-file-text'} me-1`}></i>
            PGN
          </button>
        </>
      )}
    </div>
  );

  // Clock Display Component
  const ClockDisplay = ({ compact = false }: { compact?: boolean }) => (
    <div className={`d-flex justify-content-between ${compact ? 'gap-2' : 'gap-3'}`}>
      <div className={`text-center flex-fill ${compact ? '' : ''}`}>
        <div className="text-muted small">Black</div>
        <div className={`font-monospace text-light ${compact ? 'fs-6' : 'fs-4'}`}>
          {formatClock(clockTimes.black)}
        </div>
      </div>
      <div className={`text-center flex-fill ${compact ? '' : ''}`}>
        <div className="text-muted small">White</div>
        <div className={`font-monospace text-light ${compact ? 'fs-6' : 'fs-4'}`}>
          {formatClock(clockTimes.white)}
        </div>
      </div>
    </div>
  );

  // Game Info Footer Component
  const GameInfoFooter = ({ compact = false }: { compact?: boolean }) => (
    <div className={`d-flex flex-wrap justify-content-center ${compact ? 'gap-2' : 'gap-4'} text-muted small`}>
      <span>
        <strong>Color:</strong> {game.playerColor === 'white' ? 'White' : 'Black'}
      </span>
      <span>
        <strong>Time:</strong> {game.timeControl || 'Unknown'}
      </span>
      {!compact && (
        <span>
          <strong>Moves:</strong> {game.moveCount || game.gameMoves?.length || 0}
        </span>
      )}
      <span>
        <strong>Rating:</strong> {Math.round(game.ratingBefore)} → {Math.round(game.ratingAfter)}
        <span className={game.ratingChange > 0 ? 'text-success ms-1' : game.ratingChange < 0 ? 'text-danger ms-1' : 'ms-1'}>
          ({game.ratingChange > 0 ? '+' : ''}{Math.round(game.ratingChange)})
        </span>
      </span>
    </div>
  );

  if (isMobile) {
    // Mobile Layout - Stacked
    return (
      <div className="d-flex flex-column h-100 w-100" style={{ minHeight: 0, maxHeight: '100%' }}>
        {/* Compact Header */}
        <div className="flex-shrink-0 bg-dark border-bottom border-secondary p-2">
          <div className="d-flex justify-content-between align-items-center">
            <Link href="/profile" className="btn btn-outline-secondary btn-sm px-2">
              <i className="bi bi-arrow-left"></i>
            </Link>
            <span className="text-light small text-truncate mx-2">
              vs Bot ({game.botRating}) - {getResultDisplay()}
            </span>
            <div className="d-flex gap-1">
              <button
                className={`btn btn-sm ${fenCopied ? 'btn-success' : 'btn-outline-secondary'} px-2`}
                onClick={handleCopyFEN}
                title="Copy FEN"
              >
                <i className={`bi ${fenCopied ? 'bi-check' : 'bi-clipboard'}`}></i>
              </button>
              <button
                className={`btn btn-sm ${pgnCopied ? 'btn-success' : 'btn-outline-secondary'} px-2`}
                onClick={handleCopyPGN}
                title="Copy PGN"
              >
                <i className={`bi ${pgnCopied ? 'bi-check' : 'bi-file-text'}`}></i>
              </button>
            </div>
          </div>
        </div>

        {/* Main Content Area */}
        <div className="flex-grow-1 d-flex flex-column overflow-hidden" style={{ minHeight: 0 }}>
          {/* Chessboard with ChessboardArea */}
          <div className="flex-grow-1 d-flex align-items-center justify-content-center p-1" style={{ minHeight: 0 }}>
            <ChessboardArea
              className="w-100 h-100"
              displayedFen={currentFen}
              evaluation={currentEval ? currentEval.white - currentEval.black : 0}
              evalLoading={false}
              evalError={null}
              evalBlackProb={currentEval?.black}
              evalDrawProb={currentEval?.draw}
              evalWhiteProb={currentEval?.white}
              isWhiteToMove={currentFen.includes(' w ')}
              onPieceDrop={() => false}
              onPromotionCheck={() => false}
              onPromotionPieceSelect={() => false}
              showLegalMoves={() => false}
              onDragEnd={() => {}}
              onMouseDown={() => false}
              onSquareClick={() => {}}
              squareStyles={{}}
              lastMoveSquares={{ from: null, to: null }}
              playerColor={boardOrientation}
              isViewingLatest={false}
              showHistoryOverlay={false}
            />
          </div>

          {/* Clock Display */}
          <div className="flex-shrink-0 bg-dark border-top border-secondary px-3 py-2">
            <ClockDisplay compact />
          </div>

          {/* Navigation Controls */}
          <div className="flex-shrink-0 bg-dark border-top border-secondary px-2 py-2">
            <NavigationControls compact />
          </div>
        </div>

        {/* Horizontal Move List at Bottom */}
        <div className="flex-shrink-0 bg-dark border-top border-secondary font-monospace small" style={{ height: '60px' }}>
          <GameReviewMoveList
            horizontal
            viewedPlyIndex={viewedPlyIndex}
            movePairs={movePairs}
            onGoToPly={handleGoToPly}
          />
        </div>

        {/* Compact Game Info Footer */}
        <div className="flex-shrink-0 bg-dark border-top border-secondary px-2 py-1">
          <GameInfoFooter compact />
        </div>
      </div>
    );
  }

  // Desktop Layout - Side by Side
  return (
    <div className="d-flex flex-column h-100 w-100 p-3" style={{ minHeight: 0, maxHeight: '100%' }}>
      {/* Header */}
      <div className="flex-shrink-0 d-flex flex-wrap justify-content-between align-items-center mb-3 gap-2">
        <Link href="/profile" className="btn btn-outline-secondary btn-sm">
          <i className="bi bi-arrow-left me-1"></i>
          Back to Profile
        </Link>
        <h1 className="h5 mb-0 flex-grow-1 text-center">
          vs Bot ({game.botRating}) - {getResultDisplay()}
        </h1>
        <span className="text-muted small">{formatDate(game.playedAt)}</span>
      </div>

      {/* Main Content Row */}
      <div className="flex-grow-1 d-flex gap-3 overflow-hidden" style={{ minHeight: 0 }}>
        {/* Left: Board and Controls */}
        <div className="d-flex flex-column gap-3" style={{ flex: '1 1 60%', minWidth: 0, minHeight: 0 }}>
          {/* Chessboard with ChessboardArea */}
          <div className="flex-grow-1 overflow-hidden" style={{ minHeight: 0 }}>
            <ChessboardArea
              className="w-100 h-100"
              displayedFen={currentFen}
              evaluation={currentEval ? currentEval.white - currentEval.black : 0}
              evalLoading={false}
              evalError={null}
              evalBlackProb={currentEval?.black}
              evalDrawProb={currentEval?.draw}
              evalWhiteProb={currentEval?.white}
              isWhiteToMove={currentFen.includes(' w ')}
              onPieceDrop={() => false}
              onPromotionCheck={() => false}
              onPromotionPieceSelect={() => false}
              showLegalMoves={() => false}
              onDragEnd={() => {}}
              onMouseDown={() => false}
              onSquareClick={() => {}}
              squareStyles={{}}
              lastMoveSquares={{ from: null, to: null }}
              playerColor={boardOrientation}
              isViewingLatest={false}
              showHistoryOverlay={false}
            />
          </div>

          {/* Clock and Navigation */}
          <div className="flex-shrink-0">
            {/* Dual Clock Display */}
            <div className="card bg-dark border-secondary mb-3">
              <div className="card-body py-2">
                <ClockDisplay />
              </div>
            </div>

            {/* Navigation Controls */}
            <div className="card bg-dark border-secondary">
              <div className="card-body py-2">
                <NavigationControls />
              </div>
            </div>
          </div>
        </div>

        {/* Right: Move List */}
        <div className="d-flex flex-column overflow-hidden" style={{ flex: '0 1 380px', minWidth: '300px' }}>
          <div className="card bg-dark border-secondary h-100 d-flex flex-column overflow-hidden">
            <div className="card-header border-secondary py-2 flex-shrink-0">
              <h2 className="h6 mb-0">Moves</h2>
            </div>
            <div
              className="card-body p-2 font-monospace small overflow-auto flex-grow-1"
              style={{ minHeight: 0 }}
            >
              <GameReviewMoveList
                viewedPlyIndex={viewedPlyIndex}
                movePairs={movePairs}
                onGoToPly={handleGoToPly}
              />
            </div>
          </div>
        </div>
      </div>

      {/* Game Info Footer */}
      <div className="flex-shrink-0 mt-3">
        <div className="card bg-dark border-secondary">
          <div className="card-body py-2">
            <GameInfoFooter />
          </div>
        </div>
      </div>
    </div>
  );
}
