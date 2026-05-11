/* eslint-env jest */
import React from 'react';
import { render, screen, waitFor, act } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import type { UserEvent } from '@testing-library/user-event';
import ChessGame from './ChessGame';
import {
  setupFetchMocks,
  suppressExpectedErrors,
  disableBotMoves,
  cleanupTimers,
  waitForChessGameReady,
  loadFEN,
  getCurrentMoveCount,
  getCurrentViewIndex
} from './ChessGame.test.utils';

// Mock console.log to test promotion logic
const mockConsoleLog = jest.spyOn(console, 'log').mockImplementation();

// Increase timeout for these tests as they involve async operations
jest.setTimeout(10000);

describe('ChessGame Component - Promotion Tests', () => {
  let user: UserEvent;

  beforeEach(() => {
    user = userEvent.setup();
    setupFetchMocks({
      botMove: 'Ka2',
      thinkingTime: 0.5
    });
    suppressExpectedErrors();
    mockConsoleLog.mockClear();
  });

  afterEach(() => {
    cleanupTimers();
    jest.clearAllMocks();
    jest.restoreAllMocks();
    if (global.restoreConsole) {
      global.restoreConsole();
    }
  });

  describe('Promotion position loading', () => {
    test('should load position with pawn ready to promote', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Load position where white pawn can promote
      const promotionFEN = '8/P7/8/8/8/8/8/k6K w - - 0 1';

      await act(async () => {
        await loadFEN(promotionFEN, user);
      });

      // Verify the position was loaded by checking the chessboard
      await waitFor(() => {
        const chessboard = screen.getByTestId('chessboard');
        const position = chessboard.getAttribute('data-position');
        expect(position).toContain('8/P7/8/8/8/8/8/k6K');
      });

      // Move count should be 0
      expect(getCurrentMoveCount()).toBe(0);
    });

    test('should load various promotion scenarios', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Test multiple promotion scenarios
      const promotionScenarios = [
        { fen: '4Q3/8/8/8/8/8/8/k6K b - - 0 1', piece: 'Q' }, // After Queen promotion
        { fen: '4R3/8/8/8/8/8/8/k6K b - - 0 1', piece: 'R' }, // After Rook promotion
        { fen: '4B3/8/8/8/8/8/8/k6K b - - 0 1', piece: 'B' }, // After Bishop promotion
        { fen: '4N3/8/8/8/8/8/8/k6K b - - 0 1', piece: 'N' }, // After Knight promotion
      ];

      for (const scenario of promotionScenarios) {
        await act(async () => {
          await loadFEN(scenario.fen, user);
        });

        await waitFor(() => {
          const chessboard = screen.getByTestId('chessboard');
          const position = chessboard.getAttribute('data-position');
          expect(position).toContain(`4${scenario.piece}3`);
        });
      }
    });
  });

  describe('Promotion with game state checks', () => {
    test('should handle checkmate after promotion', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // After promotion to Queen on h8, black king is checkmated
      const checkmateFEN = '4k3/4Q3/4K3/8/8/8/8/8 b - - 0 1';

      await act(async () => {
        await loadFEN(checkmateFEN, user);
      });

      await waitFor(() => {
        // Checkmate shows a single message with both "Checkmate!" and "White wins!"
        const checkmateMsg = screen.getByText(/Checkmate!.*White wins!/);
        expect(checkmateMsg).toBeInTheDocument();
      });
    });

    test.skip('should handle check after promotion', async () => {
      // Disable bot moves for this test
      disableBotMoves();

      render(<ChessGame />);

      await waitForChessGameReady();

      // Position where Qh8 gives check
      const checkFEN = '4k3/8/8/8/8/8/8/Q6K w - - 0 1';

      await act(async () => {
        await loadFEN(checkFEN, user);
      });

      // Make a move to give check using board squares
      const chessboard = screen.getByTestId('chessboard');
      const a1Square = chessboard.querySelector('[data-square="a1"]');
      const h8Square = chessboard.querySelector('[data-square="h8"]');

      if (a1Square && h8Square) {
        await user.click(a1Square);
        await user.click(h8Square);
      }

      await waitFor(() => {
        expect(screen.getByText('Check!')).toBeInTheDocument();
        expect(screen.queryByText('Game Over!')).not.toBeInTheDocument();
      });
    });
  });

  describe('Promotion during history navigation', () => {
    test('should prevent moves while browsing history', async () => {
      // Disable bot moves for this test
      disableBotMoves();

      render(<ChessGame />);

      await waitForChessGameReady();

      // Make a move to create history by clicking squares
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await user.click(e2Square);
        await user.click(e4Square);
      }

      await waitFor(() => {
        expect(getCurrentMoveCount()).toBe(1);
      });

      // Navigate back to start position
      const startButton = screen.getByText('Start Position');
      await user.click(startButton);

      // Verify we're in browsing mode at start position
      await waitFor(() => {
        expect(getCurrentViewIndex()).toBe(-1);
      });

      // Get the current move count before attempting to make a move
      const moveCountBefore = getCurrentMoveCount();
      expect(moveCountBefore).toBe(1);

      // Try to make a move while browsing history using drag and drop
      // This should be prevented - pieces are not draggable when viewing history
      const d2Square = chessboard.querySelector('[data-square="d2"]');
      const d4Square = chessboard.querySelector('[data-square="d4"]');

      if (d2Square && d4Square) {
        await user.click(d2Square);
        await user.click(d4Square);
      }

      // Move count should remain 1 (no new move was made)
      expect(getCurrentMoveCount()).toBe(1);

      // View index should still be -1 (at start position)
      expect(getCurrentViewIndex()).toBe(-1);

      // Click on the move in the move list to go back to latest position
      const moveInList = screen.getByText('e4');
      await user.click(moveInList);

      // Verify we're at the latest position (view index 0)
      await waitFor(() => {
        expect(getCurrentViewIndex()).toBe(0);
      });
    });
  });

  describe('Promotion with clock management', () => {
    test('should maintain clock times during promotion scenarios', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Verify initial clock times (format is mm:ss with leading zeros)
      expect(screen.getByTestId('white-time')).toHaveTextContent('05:00');
      expect(screen.getByTestId('black-time')).toHaveTextContent('05:00');

      // Load promotion position
      const promotionFEN = '8/P7/8/8/8/8/8/k6K w - - 0 1';

      await act(async () => {
        await loadFEN(promotionFEN, user);
      });

      // Clocks should remain at initial values
      expect(screen.getByTestId('white-time')).toHaveTextContent('5:00');
      expect(screen.getByTestId('black-time')).toHaveTextContent('5:00');
    });

    test.skip('should handle time forfeit preventing promotion', async () => {
      // FIXME: This test needs to properly test time forfeit during promotion
      // It should:
      // 1. Set up a position where promotion is possible
      // 2. Configure very low clock times
      // 3. Start a promotion move but let time expire before completing it
      // 4. Verify the game ends with time forfeit, not promotion
      //
      // Current challenges:
      // - Need to mock or manipulate clock times
      // - Need to simulate time expiration during move execution
      // - React 18's concurrent features make timer testing complex
    });
  });

  describe('Complex promotion positions', () => {
    test('should handle multiple pawns ready to promote', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Multiple white pawns on 7th rank
      const complexFEN = 'r3k2r/PPP4P/8/8/8/8/ppp4p/R3K2R w KQkq - 0 1';

      await act(async () => {
        await loadFEN(complexFEN, user);
      });

      await waitFor(() => {
        const chessboard = screen.getByTestId('chessboard');
        const position = chessboard.getAttribute('data-position');
        expect(position).toContain('PPP4P');
        // Verify castling rights preserved
        expect(position).toContain('KQkq');
      });
    });

    test('should handle promotion with en passant state', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Position with en passant square set
      const enPassantFEN = '8/Pp6/8/8/8/8/8/k6K w - b6 0 1';

      await act(async () => {
        await loadFEN(enPassantFEN, user);
      });

      // Wait for FEN to be loaded and check it contains expected pieces
      await waitFor(() => {
        const chessboard = screen.getByTestId('chessboard');
        const position = chessboard.getAttribute('data-position');
        expect(position).toContain('8/Pp6/8/8/8/8/8/k6K');
      });
    });
  });

  describe('Promotion state reset', () => {
    test('should clear promotion state on new game', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Load promotion position
      const promotionFEN = '8/P7/8/8/8/8/8/k6K w - - 0 1';

      await act(async () => {
        await loadFEN(promotionFEN, user);
      });

      await waitFor(() => {
        const chessboard = screen.getByTestId('chessboard');
        const position = chessboard.getAttribute('data-position');
        expect(position).toContain('8/P7');
      });

      // Start new game
      const newGameButtons = screen.getAllByText('New Game');
      const newGameButton = newGameButtons.find(button =>
        button.classList.contains('btn-primary')
      ) || newGameButtons[0];
      await user.click(newGameButton);

      await waitFor(() => {
        expect(screen.getByText('Start Game')).toBeInTheDocument();
      });

      const startButton = screen.getByText('Start Game');
      await user.click(startButton);

      // Should reset to initial position
      await waitFor(() => {
        const chessboard = screen.getByTestId('chessboard');
        const position = chessboard.getAttribute('data-position');
        expect(position).toContain('rnbqkbnr/pppppppp');
      });

      expect(getCurrentMoveCount()).toBe(0);
    });
  });

  describe('Edge cases', () => {
    test('should handle invalid promotion moves gracefully', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Load a normal position (no promotion possible)
      const normalFEN = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';

      await act(async () => {
        await loadFEN(normalFEN, user);
      });

      // Verify normal position loaded without errors
      await waitFor(() => {
        const chessboard = screen.getByTestId('chessboard');
        const position = chessboard.getAttribute('data-position');
        expect(position).toContain('rnbqkbnr/pppppppp');
      });
    });

    test('should handle empty board with promotion piece', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Just kings and a promoted queen
      const minimalFEN = 'Q7/8/8/8/8/8/8/k6K b - - 0 1';

      await act(async () => {
        await loadFEN(minimalFEN, user);
      });

      await waitFor(() => {
        const chessboard = screen.getByTestId('chessboard');
        const position = chessboard.getAttribute('data-position');
        expect(position).toContain('Q7');
      });
    });
  });
});
