/**
 * Custom hook return type definitions for ChessMimic
 */

import type { Chess } from 'chess.js';
import type { 
  ChessColor, 
  MoveToPromote,
  SquareStyles,
  MoveTimes,
  FENValidationResult,
  ChessJsMove
} from './chess';

/**
 * Enhanced move history entry with board positions
 */
export interface EnhancedHistoryEntry extends ChessJsMove {
  before: string;
  after: string;
  customStartFen?: string | null;
}

/**
 * Return type for useChessGame hook
 */
export interface UseChessGameReturn {
  // State
  game: Chess;
  moveHistory: ChessJsMove[];
  viewedPlyIndex: number;
  moveToPromote: MoveToPromote | null;
  legalSquares: string[];
  squareStyles: SquareStyles;
  selectedPiece: string | null;
  lastMoveSquares: {
    from: string | null;
    to: string | null;
  };
  enhancedHistory: EnhancedHistoryEntry[];
  isViewingLatest: boolean;

  // State setters
  setGame: React.Dispatch<React.SetStateAction<Chess>>;
  setMoveHistory: React.Dispatch<React.SetStateAction<ChessJsMove[]>>;
  setViewedPlyIndex: React.Dispatch<React.SetStateAction<number>>;
  setMoveToPromote: React.Dispatch<React.SetStateAction<MoveToPromote | null>>;
  setLegalSquares: React.Dispatch<React.SetStateAction<string[]>>;
  setSquareStyles: React.Dispatch<React.SetStateAction<SquareStyles>>;
  setSelectedPiece: React.Dispatch<React.SetStateAction<string | null>>;
  setLastMoveSquares: React.Dispatch<React.SetStateAction<{
    from: string | null;
    to: string | null;
  }>>;

  // Methods
  resetGame: () => Chess;
  loadGameFromFEN: (fen: string) => FENValidationResult & { game?: Chess };
  makeMove: (move: ChessJsMove) => boolean;
  navigateToPly: (plyIndex: number) => void;
  getViewedFEN: () => string;
}

/**
 * Return type for useChessClock hook
 */
import type { TimeWarningLevel } from './chess';

export interface UseChessClockReturn {
  // State
  whiteTime: number;
  blackTime: number;
  activeColor: ChessColor | null;
  clockRunning: boolean;
  gameEndedByTime: boolean;
  timeWinner: ChessColor | null;
  moveTimes: MoveTimes;
  turnStartTime: number | null;

  // Methods
  handleMoveCompletion: (
    playerWhoJustMoved: ChessColor,
    currentTurn: ChessColor,
    skipIncrement?: boolean
  ) => void;
  stopClock: () => void;
  resetClock: (newInitialTime?: number) => void;
  addMoveTime: (color: ChessColor, time: number) => void;
  onTimeWarning?: (player: ChessColor, warningLevel: TimeWarningLevel) => void;

  // State setters
  setWhiteTime: React.Dispatch<React.SetStateAction<number>>;
  setBlackTime: React.Dispatch<React.SetStateAction<number>>;
  setClockRunning: React.Dispatch<React.SetStateAction<boolean>>;
  setActiveColor: React.Dispatch<React.SetStateAction<ChessColor | null>>;
}

/**
 * Generic state setter type
 */
export type StateSetterType<T> = React.Dispatch<React.SetStateAction<T>>;

/**
 * Hook that returns a value and its setter
 */
export type UseStateReturn<T> = [T, StateSetterType<T>];