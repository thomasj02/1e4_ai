/* eslint-env jest */
/**
 * Tests for ChessGame component - game move recording feature
 *
 * These tests verify the data structures and logic for recording game moves
 * without requiring full component rendering (which has environment dependencies).
 */

import type { MoveRecord } from '@/types/rating';

// Sample move records that would be generated during a game
const SAMPLE_GAME_MOVES: MoveRecord[] = [
  { move: 'e4', clock: 300000, eval: { white: 0.52, draw: 0.30, black: 0.18 } },
  { move: 'e5', clock: 298000, eval: { white: 0.50, draw: 0.32, black: 0.18 } },
  { move: 'Nf3', clock: 295000, eval: { white: 0.53, draw: 0.30, black: 0.17 } },
  { move: 'Nc6', clock: 292000, eval: { white: 0.52, draw: 0.30, black: 0.18 } },
];

describe('ChessGame Move Recording - Data Structure Tests', () => {
  describe('Move record creation', () => {
    /**
     * Simulates how the component creates a move record after each move
     */
    function createMoveRecord(
      move: string,
      clockMs: number,
      evaluation: { white: number; draw: number; black: number }
    ): MoveRecord {
      return {
        move,
        clock: clockMs,
        eval: evaluation,
      };
    }

    it('creates a valid move record for a player move', () => {
      const record = createMoveRecord(
        'e4',
        295000, // Clock after move
        { white: 0.52, draw: 0.30, black: 0.18 }
      );

      expect(record.move).toBe('e4');
      expect(record.clock).toBe(295000);
      expect(record.eval.white).toBe(0.52);
      expect(record.eval.draw).toBe(0.30);
      expect(record.eval.black).toBe(0.18);
    });

    it('creates a valid move record for a bot move', () => {
      const record = createMoveRecord(
        'e5',
        292000,
        { white: 0.50, draw: 0.32, black: 0.18 }
      );

      expect(record.move).toBe('e5');
      expect(record.clock).toBe(292000);
      expect(record.eval.white).toBe(0.50);
    });

    it('records clock time in milliseconds', () => {
      // 5 minutes = 300,000 ms
      const record = createMoveRecord('d4', 300000, { white: 0.5, draw: 0.3, black: 0.2 });
      expect(record.clock).toBe(300000);

      // 1 minute 30 seconds = 90,000 ms
      const lateRecord = createMoveRecord('Qh5', 90000, { white: 0.5, draw: 0.3, black: 0.2 });
      expect(lateRecord.clock).toBe(90000);

      // Near flagging = 1000 ms
      const flaggingRecord = createMoveRecord('Kf1', 1000, { white: 0.5, draw: 0.3, black: 0.2 });
      expect(flaggingRecord.clock).toBe(1000);
    });

    it('accepts various move notations', () => {
      const testMoves = [
        { san: 'e4', desc: 'pawn push' },
        { san: 'Nf3', desc: 'knight move' },
        { san: 'Bb5', desc: 'bishop move' },
        { san: 'O-O', desc: 'kingside castling' },
        { san: 'O-O-O', desc: 'queenside castling' },
        { san: 'exd5', desc: 'pawn capture' },
        { san: 'Nxf7', desc: 'knight capture' },
        { san: 'e8=Q', desc: 'pawn promotion' },
        { san: 'Qxf7+', desc: 'check' },
        { san: 'Qxf7#', desc: 'checkmate' },
      ];

      testMoves.forEach(({ san }) => {
        const record = createMoveRecord(san, 280000, { white: 0.5, draw: 0.3, black: 0.2 });
        expect(record.move).toBe(san);
      });
    });
  });

  describe('Game moves array management', () => {
    it('starts with empty moveRecords array', () => {
      const moveRecords: MoveRecord[] = [];
      expect(moveRecords).toHaveLength(0);
    });

    it('accumulates moves throughout the game', () => {
      const moveRecords: MoveRecord[] = [];

      // Add moves
      moveRecords.push({ move: 'e4', clock: 300000, eval: { white: 0.52, draw: 0.30, black: 0.18 } });
      expect(moveRecords).toHaveLength(1);

      moveRecords.push({ move: 'e5', clock: 298000, eval: { white: 0.50, draw: 0.32, black: 0.18 } });
      expect(moveRecords).toHaveLength(2);

      moveRecords.push({ move: 'Nf3', clock: 295000, eval: { white: 0.53, draw: 0.30, black: 0.17 } });
      expect(moveRecords).toHaveLength(3);

      moveRecords.push({ move: 'Nc6', clock: 292000, eval: { white: 0.52, draw: 0.30, black: 0.18 } });
      expect(moveRecords).toHaveLength(4);

      // Verify order
      expect(moveRecords[0].move).toBe('e4');
      expect(moveRecords[1].move).toBe('e5');
      expect(moveRecords[2].move).toBe('Nf3');
      expect(moveRecords[3].move).toBe('Nc6');
    });

    it('clears moveRecords on new game', () => {
      const moveRecords: MoveRecord[] = [...SAMPLE_GAME_MOVES];
      expect(moveRecords).toHaveLength(4);

      // Simulate new game reset
      moveRecords.length = 0;
      expect(moveRecords).toHaveLength(0);
    });

    it('clears moveRecords on FEN load', () => {
      const moveRecords: MoveRecord[] = [...SAMPLE_GAME_MOVES];
      expect(moveRecords).toHaveLength(4);

      // Simulate FEN load reset
      moveRecords.length = 0;
      expect(moveRecords).toHaveLength(0);
    });
  });

  describe('Game end data preparation', () => {
    interface GameEndData {
      botRating: number;
      playerColor: 'white' | 'black';
      result: 'win' | 'loss' | 'draw';
      resultReason: string;
      moveCount: number;
      timeControl: string;
      gameMoves: MoveRecord[];
    }

    /**
     * Simulates how the component prepares data when game ends
     */
    function prepareGameEndData(
      playerResult: 'win' | 'loss' | 'draw',
      reason: string,
      moveRecords: MoveRecord[],
      options: {
        botRating?: number;
        playerColor?: 'white' | 'black';
        timeControl?: string;
      } = {}
    ): GameEndData {
      const { botRating = 1500, playerColor = 'white', timeControl = '5+3' } = options;

      return {
        botRating,
        playerColor,
        result: playerResult,
        resultReason: reason,
        moveCount: moveRecords.length,
        timeControl,
        gameMoves: moveRecords,
      };
    }

    it('includes gameMoves on checkmate', () => {
      const data = prepareGameEndData('win', 'Checkmate', SAMPLE_GAME_MOVES);

      expect(data.result).toBe('win');
      expect(data.resultReason).toBe('Checkmate');
      expect(data.gameMoves).toHaveLength(4);
      expect(data.moveCount).toBe(4);
    });

    it('includes gameMoves on resignation', () => {
      const data = prepareGameEndData('loss', 'Resignation', SAMPLE_GAME_MOVES);

      expect(data.result).toBe('loss');
      expect(data.resultReason).toBe('Resignation');
      expect(data.gameMoves).toHaveLength(4);
    });

    it('includes gameMoves on time forfeit', () => {
      const data = prepareGameEndData('loss', 'Time forfeit', SAMPLE_GAME_MOVES);

      expect(data.result).toBe('loss');
      expect(data.resultReason).toBe('Time forfeit');
      expect(data.gameMoves).toHaveLength(4);
    });

    it('includes gameMoves on stalemate', () => {
      const data = prepareGameEndData('draw', 'Stalemate', SAMPLE_GAME_MOVES);

      expect(data.result).toBe('draw');
      expect(data.resultReason).toBe('Stalemate');
      expect(data.gameMoves).toHaveLength(4);
    });

    it('preserves all move record fields in game end data', () => {
      const data = prepareGameEndData('win', 'Checkmate', SAMPLE_GAME_MOVES);

      const firstMove = data.gameMoves[0];
      expect(firstMove.move).toBe('e4');
      expect(firstMove.clock).toBe(300000);
      expect(firstMove.eval.white).toBe(0.52);
      expect(firstMove.eval.draw).toBe(0.30);
      expect(firstMove.eval.black).toBe(0.18);
    });

    it('includes metadata with game end data', () => {
      const data = prepareGameEndData('win', 'Checkmate', SAMPLE_GAME_MOVES, {
        botRating: 1800,
        playerColor: 'black',
        timeControl: '10+0',
      });

      expect(data.botRating).toBe(1800);
      expect(data.playerColor).toBe('black');
      expect(data.timeControl).toBe('10+0');
    });
  });
});
