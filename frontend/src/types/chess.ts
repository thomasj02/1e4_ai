/**
 * Core chess-related type definitions for ChessMimic
 */

import type { Chess, Square } from 'chess.js';

// Re-export chess.js types for convenience
export type { Chess, Square };

/**
 * Chess.js move type (based on what the library actually returns)
 */
export interface ChessJsMove {
  color: 'w' | 'b';
  from: string;
  to: string;
  flags: string;
  piece: string;
  san: string;
  captured?: string;
  promotion?: string;
  lan?: string;
}

/**
 * Simple move representation for UI interactions
 */
export interface ChessMove {
  from: string;
  to: string;
  promotion?: string;
}

/**
 * Extended move with metadata
 */
export interface ExtendedChessMove extends ChessMove {
  san?: string;
  piece?: string;
  captured?: string;
  flags?: string;
}

/**
 * Game settings configuration
 */
export interface GameSettings {
  playerColor: 'white' | 'black' | 'random';
  soundEnabled: boolean;
  botRating: number;
}

/**
 * Time control configuration (in milliseconds)
 */
export interface TimeControl {
  initial: number;
  increment: number;
}

/**
 * Evaluation state for the evaluation bar
 */
export interface EvalState {
  evaluation: number;
  loading: boolean;
  error: string | null;
  blackProb?: number;
  drawProb?: number;
  whiteProb?: number;
}

/**
 * UI state for input controls
 */
export interface UiState {
  fenInput: string;
  fenError: string;
}

/**
 * Chess colors type
 */
export type ChessColor = 'white' | 'black';

/**
 * Chess piece types
 */
export type ChessPiece = 'p' | 'n' | 'b' | 'r' | 'q' | 'k' | 'P' | 'N' | 'B' | 'R' | 'Q' | 'K';

/**
 * Promotion piece types
 */
export type PromotionPiece = 'q' | 'r' | 'b' | 'n';

/**
 * Square highlighting styles
 */
export interface SquareStyles {
  [square: string]: React.CSSProperties;
}

/**
 * Move timing data
 */
export interface MoveTimes {
  white: number[];
  black: number[];
}

/**
 * Game state for tracking overall game status
 */
export type GameState = 
  | { status: 'playing'; activeColor: ChessColor }
  | { status: 'checkmate'; winner: ChessColor }
  | { status: 'draw'; reason: 'stalemate' | 'repetition' | 'insufficient' | '50-move' | 'agreement' }
  | { status: 'timeForfeit'; winner: ChessColor };

/**
 * Clock state for time management
 */
export interface ClockState {
  whiteTime: number;
  blackTime: number;
  activeColor: ChessColor | null;
  clockRunning: boolean;
  gameEndedByTime: boolean;
  timeWinner: ChessColor | null;
}

/**
 * Move to promote (for promotion dialog)
 */
export interface MoveToPromote {
  from: string;
  to: string;
  piece: string;
}

/**
 * Time warning levels
 */
export type TimeWarningLevel = 'none' | 'caution' | 'low' | 'critical';

/**
 * Bot move response from backend
 */
export interface BotMoveResponse {
  move: string;
  thinking_time: number;
}

/**
 * Evaluation response from backend
 */
export interface EvaluationResponse {
  evaluation: number;
  raw_value: number;
  black_prob?: number | null;
  draw_prob?: number | null;
  white_prob?: number | null;
  error?: string | null;
}

/**
 * Backend error response
 */
export interface BackendError {
  detail: string;
}

/**
 * PGN generation parameters
 */
export interface PGNGenerationParams {
  game: Chess;
  moveHistory: ChessJsMove[];
  moveTimes: MoveTimes;
  playerColor: ChessColor;
  botRating: number;
  timeControl: TimeControl;
  gameEndedByTime: boolean;
  timeWinner: ChessColor | null;
}

/**
 * FEN validation result
 */
export interface FENValidationResult {
  valid: boolean;
  error?: string;
  fen?: string;
}