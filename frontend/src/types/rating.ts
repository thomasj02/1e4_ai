/**
 * Types for the user rating system.
 */

export interface MoveEval {
  white: number;  // White win probability (0-1)
  draw: number;   // Draw probability (0-1)
  black: number;  // Black win probability (0-1)
}

export interface MoveRecord {
  move: string;      // SAN notation: "e4", "Nf3", etc.
  clock: number;     // Milliseconds remaining after this move
  eval: MoveEval;    // Position evaluation after this move
}

export interface UserRating {
  rating: number;
  ratingDeviation: number;
  gamesPlayed: number;
  wins: number;
  losses: number;
  draws: number;
  ratingTier: string;
}

export interface GameResult {
  botRating: number;
  playerColor: 'white' | 'black';
  result: 'win' | 'loss' | 'draw';
  resultReason?: string;
  moveCount?: number;
  timeControl?: string;
  gameMoves?: MoveRecord[];
}

export interface RatingUpdate {
  newRating: number;
  ratingChange: number;
  newRd: number;
  ratingTier: string;
}

export interface GameHistoryItem {
  id: string;
  botRating: number;
  playerColor: 'white' | 'black';
  result: 'win' | 'loss' | 'draw';
  resultReason?: string;
  moveCount?: number;
  timeControl?: string;
  ratingBefore: number;
  ratingAfter: number;
  ratingChange: number;
  playedAt: string;
  gameMoves?: MoveRecord[];
}

export interface GameHistory {
  games: GameHistoryItem[];
  totalCount: number;
}
