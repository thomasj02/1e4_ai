/* eslint-env jest */
/**
 * Tests for useUserRating hook - game_moves feature
 *
 * These tests verify the data transformation and structure of game_moves
 * when sending/receiving from the backend.
 */

import type { MoveRecord, GameResult } from '../types/rating';

// Sample move records for testing
const SAMPLE_MOVE_RECORDS: MoveRecord[] = [
  { move: 'e4', clock: 300000, eval: { white: 0.52, draw: 0.30, black: 0.18 } },
  { move: 'e5', clock: 298500, eval: { white: 0.50, draw: 0.32, black: 0.18 } },
  { move: 'Nf3', clock: 295000, eval: { white: 0.53, draw: 0.30, black: 0.17 } },
];

describe('game_moves Data Structure Tests', () => {
  describe('Request body transformation', () => {
    /**
     * Simulates how the hook transforms GameResult to API request body
     */
    function buildRequestBody(result: GameResult): Record<string, unknown> {
      return {
        bot_rating: result.botRating,
        player_color: result.playerColor,
        result: result.result,
        result_reason: result.resultReason,
        move_count: result.moveCount,
        time_control: result.timeControl,
        game_moves: result.gameMoves,
      };
    }

    it('includes game_moves in request body when provided', () => {
      const gameResult: GameResult = {
        botRating: 1800,
        playerColor: 'white',
        result: 'win',
        resultReason: 'Checkmate',
        moveCount: 40,
        timeControl: '5+3',
        gameMoves: SAMPLE_MOVE_RECORDS,
      };

      const body = buildRequestBody(gameResult);

      expect(body.game_moves).toBeDefined();
      expect(body.game_moves).toHaveLength(3);
      expect((body.game_moves as MoveRecord[])[0].move).toBe('e4');
      expect((body.game_moves as MoveRecord[])[0].clock).toBe(300000);
      expect((body.game_moves as MoveRecord[])[0].eval.white).toBe(0.52);
    });

    it('game_moves is undefined when not provided', () => {
      const gameResult: GameResult = {
        botRating: 1600,
        playerColor: 'black',
        result: 'loss',
        resultReason: 'Time forfeit',
      };

      const body = buildRequestBody(gameResult);

      expect(body.game_moves).toBeUndefined();
    });

    it('handles empty gameMoves array', () => {
      const gameResult: GameResult = {
        botRating: 1500,
        playerColor: 'white',
        result: 'draw',
        resultReason: 'Stalemate',
        gameMoves: [],
      };

      const body = buildRequestBody(gameResult);

      expect(body.game_moves).toBeDefined();
      expect(body.game_moves).toHaveLength(0);
    });

    it('preserves all move record fields', () => {
      const gameResult: GameResult = {
        botRating: 1800,
        playerColor: 'white',
        result: 'win',
        gameMoves: SAMPLE_MOVE_RECORDS,
      };

      const body = buildRequestBody(gameResult);
      const moves = body.game_moves as MoveRecord[];

      // Verify all fields are present for each move
      moves.forEach((move: MoveRecord, index: number) => {
        expect(move.move).toBe(SAMPLE_MOVE_RECORDS[index].move);
        expect(move.clock).toBe(SAMPLE_MOVE_RECORDS[index].clock);
        expect(move.eval.white).toBe(SAMPLE_MOVE_RECORDS[index].eval.white);
        expect(move.eval.draw).toBe(SAMPLE_MOVE_RECORDS[index].eval.draw);
        expect(move.eval.black).toBe(SAMPLE_MOVE_RECORDS[index].eval.black);
      });
    });
  });

  describe('Response parsing', () => {
    /**
     * Simulates how the hook transforms API response to GameHistory
     */
    function parseHistoryResponse(data: Record<string, unknown>) {
      const games = data.games as Record<string, unknown>[];
      return {
        games: games.map((g: Record<string, unknown>) => ({
          id: g.id,
          botRating: g.bot_rating,
          playerColor: g.player_color,
          result: g.result,
          resultReason: g.result_reason,
          moveCount: g.move_count,
          timeControl: g.time_control,
          ratingBefore: g.rating_before,
          ratingAfter: g.rating_after,
          ratingChange: g.rating_change,
          playedAt: g.played_at,
          gameMoves: g.game_moves as MoveRecord[] | undefined,
        })),
        totalCount: data.total_count,
      };
    }

    it('parses game_moves from response correctly', () => {
      const apiResponse = {
        games: [
          {
            id: 'game-1',
            bot_rating: 1800,
            player_color: 'white',
            result: 'win',
            result_reason: 'Checkmate',
            move_count: 40,
            time_control: '5+3',
            rating_before: 1500,
            rating_after: 1525,
            rating_change: 25,
            played_at: '2024-01-15T12:00:00Z',
            game_moves: SAMPLE_MOVE_RECORDS,
          },
        ],
        total_count: 1,
      };

      const history = parseHistoryResponse(apiResponse);

      expect(history.games).toHaveLength(1);
      expect(history.games[0].gameMoves).toBeDefined();
      expect(history.games[0].gameMoves).toHaveLength(3);
      expect(history.games[0].gameMoves![0].move).toBe('e4');
      expect(history.games[0].gameMoves![0].clock).toBe(300000);
      expect(history.games[0].gameMoves![0].eval.white).toBe(0.52);
    });

    it('handles games with null game_moves', () => {
      const apiResponse = {
        games: [
          {
            id: 'game-1',
            bot_rating: 1600,
            player_color: 'black',
            result: 'loss',
            result_reason: 'Time forfeit',
            move_count: 25,
            time_control: '3+0',
            rating_before: 1500,
            rating_after: 1475,
            rating_change: -25,
            played_at: '2024-01-14T10:00:00Z',
            game_moves: null,
          },
        ],
        total_count: 1,
      };

      const history = parseHistoryResponse(apiResponse);

      expect(history.games).toHaveLength(1);
      expect(history.games[0].gameMoves).toBeNull();
    });

    it('handles games with undefined game_moves', () => {
      const apiResponse = {
        games: [
          {
            id: 'game-1',
            bot_rating: 1700,
            player_color: 'white',
            result: 'draw',
            result_reason: 'Stalemate',
            move_count: 60,
            time_control: '10+0',
            rating_before: 1475,
            rating_after: 1485,
            rating_change: 10,
            played_at: '2024-01-13T08:00:00Z',
            // game_moves not present
          },
        ],
        total_count: 1,
      };

      const history = parseHistoryResponse(apiResponse);

      expect(history.games).toHaveLength(1);
      expect(history.games[0].gameMoves).toBeUndefined();
    });

    it('handles mixed games with and without game_moves', () => {
      const apiResponse = {
        games: [
          {
            id: 'game-1',
            bot_rating: 1800,
            player_color: 'white',
            result: 'win',
            result_reason: 'Checkmate',
            move_count: 40,
            time_control: '5+3',
            rating_before: 1550,
            rating_after: 1575,
            rating_change: 25,
            played_at: '2024-01-15T14:00:00Z',
            game_moves: SAMPLE_MOVE_RECORDS,
          },
          {
            id: 'game-2',
            bot_rating: 1600,
            player_color: 'black',
            result: 'draw',
            result_reason: 'Stalemate',
            move_count: 60,
            time_control: '10+0',
            rating_before: 1525,
            rating_after: 1550,
            rating_change: 25,
            played_at: '2024-01-15T12:00:00Z',
            game_moves: null,
          },
        ],
        total_count: 2,
      };

      const history = parseHistoryResponse(apiResponse);

      expect(history.games).toHaveLength(2);
      expect(history.games[0].gameMoves).toBeDefined();
      expect(history.games[0].gameMoves).toHaveLength(3);
      expect(history.games[1].gameMoves).toBeNull();
    });
  });

  describe('MoveRecord type validation', () => {
    it('validates complete MoveRecord structure', () => {
      const moveRecord: MoveRecord = {
        move: 'e4',
        clock: 300000,
        eval: {
          white: 0.52,
          draw: 0.30,
          black: 0.18,
        },
      };

      expect(moveRecord.move).toBe('e4');
      expect(moveRecord.clock).toBe(300000);
      expect(typeof moveRecord.eval.white).toBe('number');
      expect(typeof moveRecord.eval.draw).toBe('number');
      expect(typeof moveRecord.eval.black).toBe('number');
    });

    it('accepts various SAN move notations', () => {
      const sanMoves = [
        'e4',     // Pawn move
        'Nf3',    // Knight move
        'Bb5',    // Bishop move
        'O-O',    // Kingside castling
        'O-O-O',  // Queenside castling
        'e8=Q',   // Pawn promotion
        'exd5',   // Pawn capture
        'Nxe5',   // Knight capture
        'Qh7+',   // Check
        'Qh7#',   // Checkmate
      ];

      sanMoves.forEach((san) => {
        const record: MoveRecord = {
          move: san,
          clock: 300000,
          eval: { white: 0.5, draw: 0.3, black: 0.2 },
        };
        expect(record.move).toBe(san);
      });
    });
  });
});
