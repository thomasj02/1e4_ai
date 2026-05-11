/**
 * Component prop type definitions for ChessMimic React components
 */

import type { Move as ChessJsMove } from 'chess.js';
import type { 
  ChessColor, 
  TimeControl, 
  TimeWarningLevel,
  SquareStyles
} from './chess';

/**
 * Props for ChessClock component
 */
export interface ChessClockProps {
  whiteTime: number;
  blackTime: number;
  activeColor: ChessColor | null;
  clockRunning: boolean;
  onTimeWarning?: (color: ChessColor, level: TimeWarningLevel) => void;
}

/**
 * Props for ChessboardArea component
 */
export interface ChessboardAreaProps {
  displayedFen: string;
  evaluation: number;
  evalLoading: boolean;
  evalError: string | null;
  evalBlackProb?: number;
  evalDrawProb?: number;
  evalWhiteProb?: number;
  isWhiteToMove: boolean;
  onPieceDrop: (sourceSquare: string, targetSquare: string, piece: string) => boolean;
  onPromotionCheck: (sourceSquare: string, targetSquare: string, piece: string) => boolean;
  onPromotionPieceSelect: (piece?: string, promoteFromSquare?: string, promoteToSquare?: string) => boolean;
  showLegalMoves: (piece: string, square: string) => boolean;
  onDragEnd: () => void;
  onMouseDown: (piece: string, square: string) => void;
  onSquareClick: (square: string) => void;
  squareStyles: SquareStyles;
  lastMoveSquares: {
    from: string | null;
    to: string | null;
  };
  playerColor: ChessColor;
  isViewingLatest: boolean;
  onBoardHeightChange?: (height: number) => void;
  className?: string;
  /** Controls whether to show the "Browse History" overlay. Defaults to showing when not viewing latest. */
  showHistoryOverlay?: boolean;
}

/**
 * Props for ClockArea component
 */
export interface ClockAreaProps {
  whiteTime: number;
  blackTime: number;
  activeColor: ChessColor | null;
  clockRunning: boolean;
  onTimeWarning: (color: ChessColor, level: TimeWarningLevel) => void;
  soundEnabled: boolean;
  onSoundEnabledChange: (enabled: boolean) => void;
}

/**
 * Props for EvalBar component
 */
export interface EvalBarProps {
  evaluation?: number;
  isLoading?: boolean;
  error?: string | null;
  className?: string;
  vertical?: boolean;
  isWhiteToMove?: boolean;
  blackProb?: number;
  drawProb?: number;
  whiteProb?: number;
  playerColor?: 'white' | 'black';
}

/**
 * Props for GameControls component
 */
export interface GameControlsProps {
  onOpenFENDialog: () => void;
  onNewGame: () => void;
  onExportPGN: () => void;
  onResign: () => void;
  gameInProgress: boolean;
  soundEnabled?: boolean;
  onSoundEnabledChange?: (enabled: boolean) => void;
}

/**
 * Props for GameStatus component
 */
export interface GameStatusProps {
  game: {
    isGameOver: () => boolean;
    isCheckmate: () => boolean;
    isDraw: () => boolean;
    isCheck: () => boolean;
    isStalemate: () => boolean;
    isInsufficientMaterial: () => boolean;
    isThreefoldRepetition: () => boolean;
    turn: () => 'w' | 'b';
  };
  gameEndedByTime: boolean;
  timeWinner: ChessColor | null;
  gameEndedByResignation: boolean;
  resignedPlayer: ChessColor | null;
}

/**
 * Props for MoveHistory component
 */
export interface MoveHistoryProps {
  moveHistory: import('./hooks').EnhancedHistoryEntry[];
  viewedPlyIndex: number;
  onGoToPly: (plyIndex: number) => void;
  boardHeight: number | null;
  isMobileLayout?: boolean;
}

/**
 * Props for NewGameDialog component
 */
export interface NewGameDialogProps {
  isOpen: boolean;
  onClose: () => void;
  onConfirm: (settings: {
    rating: number;
    color: ChessColor | 'random';
    timeControl: TimeControl;
  }) => void;
  currentRating?: number;
  currentColor?: ChessColor | 'random';
  currentTimeControl?: TimeControl;
}

/**
 * Event handler types
 */
export type PieceDropHandler = (sourceSquare: string, targetSquare: string, piece: string) => boolean;
export type PromotionCheckHandler = (sourceSquare: string, targetSquare: string, piece: string) => boolean;
export type PromotionSelectHandler = (piece: string, promoteFromSquare: string, promoteToSquare: string) => boolean;
export type SquareClickHandler = (square: string) => void;
export type PieceClickHandler = (piece: string, square: string) => void;
export type TimeWarningHandler = (color: ChessColor, level: TimeWarningLevel) => void;
export type MoveHandler = (moveResult: ChessJsMove & { lan: string; before: string; after: string }) => void;