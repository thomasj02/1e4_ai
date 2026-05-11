import { Chess } from 'chess.js';
import { CSS_VARIABLE_FALLBACKS, CSSVariableName } from '../constants/chess';
import type { EnhancedHistoryEntry, StateSetterType, ChessJsMove } from '../types';

/**
 * Helper to wrap state updates in act() during testing
 */
export const updateStateInTest = <T>(setter: StateSetterType<T> | (() => void), value?: T): void => {
  // In production builds, just call the setter directly
  if (process.env.NODE_ENV === 'production') {
    if (typeof setter === 'function' && setter.length > 0 && value !== undefined) {
      (setter as StateSetterType<T>)(value);
    } else {
      (setter as () => void)();
    }
    return;
  }

  // In development/test, check for act environment
  if (typeof global !== 'undefined' && (global as typeof globalThis & { IS_REACT_ACT_ENVIRONMENT?: boolean }).IS_REACT_ACT_ENVIRONMENT) {
    // Only import act when in test environment
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    const { act } = require('@testing-library/react');
    act(() => {
      if (typeof setter === 'function' && setter.length > 0 && value !== undefined) {
        (setter as StateSetterType<T>)(value);
      } else {
        (setter as () => void)();
      }
    });
  } else {
    if (typeof setter === 'function' && setter.length > 0 && value !== undefined) {
      (setter as StateSetterType<T>)(value);
    } else {
      (setter as () => void)();
    }
  }
};

/**
 * Utility to get CSS variable values
 */
export const getCSSVariable = (varName: CSSVariableName | string): string => {
  if (typeof window !== 'undefined' && typeof window.getComputedStyle === 'function') {
    return getComputedStyle(document.documentElement).getPropertyValue(varName).trim();
  }
  // Fallback values for testing environment
  return CSS_VARIABLE_FALLBACKS[varName as CSSVariableName] || '';
};

/**
 * Format time for PGN export
 */
export const formatClockTime = (milliseconds: number): string => {
  const totalSeconds = Math.floor(milliseconds / 1000);
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  return `${hours}:${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}`;
};

/**
 * Only returns the chosen move SAN, doesn't modify the instance
 */
export function getAIMoveSAN(currentGameInstance: Chess): string | null {
  // Create a temporary copy just for finding the move
  const tempGame = new Chess(currentGameInstance.fen());
  const possibleMoves = tempGame.moves({ verbose: true });
  if (tempGame.isGameOver() || tempGame.isDraw() || possibleMoves.length === 0) {
    return null;
  }
  const randomIndex = Math.floor(Math.random() * possibleMoves.length);
  const randomMoveSAN = possibleMoves[randomIndex].san;
  return randomMoveSAN;
}

/**
 * Helper function to generate enhanced move history with board positions
 */
export const generateEnhancedHistory = (chessInstance: Chess): EnhancedHistoryEntry[] => {
  try {
    // Get full move history
    const moves = chessInstance.history({ verbose: true }) as ChessJsMove[];

    if (moves.length === 0) {
      return [];
    }

    // We need to start from the STARTING position, not the current position
    // This is because the moves in history are relative to the starting position
    const enhancedHistory: EnhancedHistoryEntry[] = [];

    // Get the starting position from the chess instance
    let startingFen: string;
    try {
      // Try to get the FEN directly from the headers first, but check if header() method exists
      if (typeof (chessInstance as Chess & { header?: () => Record<string, string> }).header === 'function') {
        const headers = (chessInstance as Chess & { header: () => Record<string, string> }).header();
        if (headers && headers.FEN) {
          startingFen = headers.FEN;
          console.log("Found FEN in headers:", startingFen);
        } else {
          // Try to extract the FEN from the PGN as fallback
          const pgn = chessInstance.pgn();
          const fenMatch = pgn.match(/\[FEN "([^"]+)"\]/);
          if (fenMatch && fenMatch[1]) {
            startingFen = fenMatch[1];
            console.log("Extracted FEN from PGN:", startingFen);
          } else {
            // If no FEN in PGN or headers, use the standard starting position
            startingFen = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
            console.log("No custom FEN found, using standard starting position");
          }
        }
      } else {
        // If header method is not available (e.g., in tests), try to get from PGN
        const pgn = chessInstance.pgn();
        const fenMatch = pgn.match(/\[FEN "([^"]+)"\]/);
        if (fenMatch && fenMatch[1]) {
          startingFen = fenMatch[1];
          console.log("Extracted FEN from PGN:", startingFen);
        } else {
          // If no FEN in PGN, use the standard starting position
          startingFen = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
          console.log("No custom FEN found, using standard starting position");
        }
      }
    } catch (e: unknown) {
      console.warn("Could not extract starting position, using standard starting position:", e instanceof Error ? e.message : 'Unknown error');
      startingFen = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
    }

    console.log("Using starting FEN for replay:", startingFen);

    // Create a fresh game with the correct starting position
    const replayGame = new Chess(startingFen);
    
    // Store the starting FEN to be used in the moveList component
    const isCustomStartPosition = startingFen !== 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
    const customStartFen = isCustomStartPosition ? startingFen : null;

    // Track positions after each move
    for (let i = 0; i < moves.length; i++) {
      const move = moves[i];

      // Create move data object with from/to/promotion properties
      const moveData = {
        from: move.from,
        to: move.to,
        promotion: move.promotion
      };

      try {
        // Make the move using direct coordinates rather than SAN
        replayGame.move(moveData);

        // Store the enhanced move with the FEN position and custom start position if applicable
        enhancedHistory.push({
          ...move,
          before: i === 0 ? startingFen : enhancedHistory[i - 1]?.after || startingFen,
          after: replayGame.fen(),
          customStartFen: i === 0 ? customStartFen : undefined
        });
      } catch (e: unknown) {
        console.error(`Error replaying move ${i}:`, e instanceof Error ? e.message : 'Unknown error', moveData);

        // Even if we can't replay, still add the move to history with empty after position
        enhancedHistory.push({
          ...move,
          before: i === 0 ? startingFen : enhancedHistory[i - 1]?.after || startingFen,
          after: '',
          customStartFen: i === 0 ? customStartFen : undefined
        });
      }
    }

    return enhancedHistory;
  } catch (e) {
    console.error("Error generating enhanced history:", e);
    return [];
  }
};