import { useState, useCallback, useMemo } from 'react';
import { Chess } from 'chess.js';
import { generateEnhancedHistory } from '../utils/chess';
import type { 
  ChessJsMove,
  MoveToPromote,
  SquareStyles,
  UseChessGameReturn,
  FENValidationResult
} from '../types';

/**
 * Custom hook for managing chess game state
 */
export const useChessGame = (): UseChessGameReturn => {
  const [game, setGame] = useState<Chess>(new Chess());
  const [moveHistory, setMoveHistory] = useState<ChessJsMove[]>([]);
  const [viewedPlyIndex, setViewedPlyIndex] = useState<number>(-1);
  const [moveToPromote, setMoveToPromote] = useState<MoveToPromote | null>(null);
  const [legalSquares, setLegalSquares] = useState<string[]>([]);
  const [squareStyles, setSquareStyles] = useState<SquareStyles>({});
  const [selectedPiece, setSelectedPiece] = useState<string | null>(null);
  const [lastMoveSquares, setLastMoveSquares] = useState<{ from: string | null; to: string | null }>({ 
    from: null, 
    to: null 
  });

  // Compute enhanced history with board positions
  const enhancedHistory = useMemo(() => {
    return generateEnhancedHistory(game);
  }, [game]);

  // Helper to check if viewing the latest position
  const isViewingLatest = viewedPlyIndex === moveHistory.length - 1;

  // Reset game state
  const resetGame = useCallback((): Chess => {
    const newGame = new Chess();
    setGame(newGame);
    setMoveHistory([]);
    setViewedPlyIndex(-1);
    setSquareStyles({});
    setLegalSquares([]);
    setSelectedPiece(null);
    setLastMoveSquares({ from: null, to: null });
    setMoveToPromote(null);
    return newGame;
  }, []);

  // Load game from FEN
  const loadGameFromFEN = useCallback((fen: string): FENValidationResult & { game?: Chess } => {
    try {
      const newGame = new Chess(fen);
      setGame(newGame);
      setMoveHistory([]);
      setViewedPlyIndex(-1);
      setSquareStyles({});
      setLegalSquares([]);
      setSelectedPiece(null);
      setLastMoveSquares({ from: null, to: null });
      setMoveToPromote(null);
      return { valid: true, game: newGame, fen: newGame.fen() };
    } catch (err: unknown) {
      return { valid: false, error: err instanceof Error ? err.message : 'Unknown error' };
    }
  }, []);

  // Make a move on the board
  const makeMove = useCallback((move: ChessJsMove): boolean => {
    try {
      const gameCopy = new Chess(game.fen());
      const result = gameCopy.move(move);
      
      if (result) {
        setGame(gameCopy);
        setMoveHistory(gameCopy.history({ verbose: true }) as ChessJsMove[]);
        setViewedPlyIndex(gameCopy.history().length - 1);
        setLastMoveSquares({ from: result.from, to: result.to });
        return true;
      }
      
      return false;
    } catch {
      return false;
    }
  }, [game]);

  // Navigate to a specific ply in the game history
  const navigateToPly = useCallback((plyIndex: number): void => {
    if (plyIndex < -1 || plyIndex >= moveHistory.length) return;
    
    setViewedPlyIndex(plyIndex);
    
    // Update last move squares for highlighting
    if (plyIndex >= 0 && enhancedHistory[plyIndex]) {
      const move = enhancedHistory[plyIndex];
      setLastMoveSquares({ from: move.from, to: move.to });
    } else {
      setLastMoveSquares({ from: null, to: null });
    }
  }, [moveHistory.length, enhancedHistory]);

  // Get FEN for current viewed position
  const getViewedFEN = useCallback((): string => {
    if (viewedPlyIndex === -1) {
      // If we're at the start position and there's no move history,
      // return the current game's FEN (which could be a custom position)
      if (moveHistory.length === 0) {
        return game.fen();
      }
      // Otherwise check for custom start FEN in enhanced history
      if (enhancedHistory.length > 0 && enhancedHistory[0].customStartFen) {
        return enhancedHistory[0].customStartFen;
      }
      return 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
    } else if (viewedPlyIndex < enhancedHistory.length) {
      return enhancedHistory[viewedPlyIndex].after;
    }
    return game.fen();
  }, [viewedPlyIndex, enhancedHistory, game, moveHistory.length]);

  return {
    game,
    moveHistory,
    viewedPlyIndex,
    moveToPromote,
    legalSquares,
    squareStyles,
    selectedPiece,
    lastMoveSquares,
    enhancedHistory,
    isViewingLatest,
    setGame,
    setMoveHistory,
    setViewedPlyIndex,
    setMoveToPromote,
    setLegalSquares,
    setSquareStyles,
    setSelectedPiece,
    setLastMoveSquares,
    resetGame,
    loadGameFromFEN,
    makeMove,
    navigateToPly,
    getViewedFEN
  };
};