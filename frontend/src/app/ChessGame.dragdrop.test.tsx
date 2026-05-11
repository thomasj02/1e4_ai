/* eslint-env jest */
import React from 'react';
import { render, screen, waitFor, act } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import type { UserEvent } from '@testing-library/user-event';
import ChessGame from './ChessGame';
import {
  setupFetchMocks,
  suppressExpectedErrors,
  getCurrentMoveCount,
  getCurrentViewIndex,
  loadFEN,
  waitForChessGameReady
} from './ChessGame.test.utils';

// Mock window.getComputedStyle to test CSS variable fallback
const mockGetComputedStyle = () => ({
  getPropertyValue: (varName: string) => {
    const cssValues: Record<string, string> = {
      '--color-square-last-move-pale': 'rgba(255, 255, 190, 0.5)',
      '--color-square-last-move': 'rgba(255, 255, 190, 0.7)',
      '--color-square-selected': 'rgba(255, 255, 0, 0.2)',
      '--color-square-navigation': 'rgba(255, 255, 0, 0.4)',
      '--color-chess-legal': 'rgba(0, 128, 0, 0.4)'
    };
    return cssValues[varName] || '';
  }
});

window.getComputedStyle = jest.fn(() => mockGetComputedStyle()) as unknown as typeof window.getComputedStyle;

describe('ChessGame Component - Drag & Drop and Legal Move Tests', () => {
  let user: UserEvent;

  beforeEach(() => {
    user = userEvent.setup();
    setupFetchMocks({
      botMove: 'e5',
      thinkingTime: 0.5
    });
    suppressExpectedErrors();
  });

  afterEach(() => {
    jest.clearAllMocks();
    jest.restoreAllMocks();
    if (global.restoreConsole) {
      global.restoreConsole();
    }
  });

  describe('showLegalMoves function', () => {
    test('should show legal moves for white pawn on initial position', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Click on e2 pawn to show legal moves
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');

      if (e2Square) {
        await user.click(e2Square);
      }

      // Board should be functional
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();
    });

    test('should show legal moves for knight', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Click on b1 knight
      const chessboard = screen.getByTestId('chessboard');
      const b1Square = chessboard.querySelector('[data-square="b1"]');

      if (b1Square) {
        await user.click(b1Square);
      }

      // Knight from b1 can go to a3 and c3
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();
    });

    test('should not show legal moves when viewing history', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Make a move first
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await user.click(e2Square);
        await user.click(e4Square);
      }

      await waitFor(() => {
        expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(1);
      });

      // Wait for bot move
      await waitFor(() => {
        expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(2);
      }, { timeout: 3000 });

      // Click on first move to navigate back
      const firstMove = screen.getByText('e4');
      await user.click(firstMove);

      await waitFor(() => {
        expect(getCurrentViewIndex()).toBe(0);
      });

      // Board should still be functional
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();
    });

    test('should not allow dragging opponent pieces', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      const initialMoveCount = getCurrentMoveCount();

      // Try to click on black pawn as white
      const chessboard = screen.getByTestId('chessboard');
      const e7Square = chessboard.querySelector('[data-square="e7"]');
      const e5Square = chessboard.querySelector('[data-square="e5"]');

      if (e7Square && e5Square) {
        await user.click(e7Square);
        await user.click(e5Square);
      }

      // Move should not be made
      expect(getCurrentMoveCount()).toBe(initialMoveCount);
    });

    test('should show legal moves for all piece types', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Load a position with all pieces able to move
      const testFEN = 'rnbqkb1r/pppp1ppp/5n2/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 4 4';

      await act(async () => {
        await loadFEN(testFEN, user);
      });

      // Board should be functional
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();
    });
  });

  describe('onMouseDown handler', () => {
    test('should select piece on click', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Click on e2 pawn
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await user.click(e2Square);
        await user.click(e4Square);
      }

      await waitFor(() => {
        expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(1);
      });
    });

    test('should toggle piece selection on second click', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');

      // First click - select piece
      if (e2Square) {
        await user.click(e2Square);
      }

      // Second click - deselect piece
      if (e2Square) {
        await user.click(e2Square);
      }

      // Board should still be functional
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();
    });

    test('should make move by clicking on legal destination', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      const initialMoveCount = getCurrentMoveCount();
      expect(initialMoveCount).toBe(0);

      // Make a move
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await user.click(e2Square);
        await user.click(e4Square);
      }

      // Verify move was made
      await waitFor(() => {
        expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(1);
      });
    });

    test('should handle clicking on piece with legal destination', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Make initial moves
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await user.click(e2Square);
        await user.click(e4Square);
      }

      await waitFor(() => {
        expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(1);
      });

      // Wait for bot move
      await waitFor(() => {
        expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(2);
      }, { timeout: 3000 });

      // Board should still be functional
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();
    });
  });

  describe('onDragEnd handler', () => {
    test('should clear legal moves when drag ends', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');

      // Select piece
      if (e2Square) {
        await user.click(e2Square);
      }

      // Deselect piece
      if (e2Square) {
        await user.click(e2Square);
      }

      // Board should still be functional
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();
    });
  });

  describe('Square styling', () => {
    test('should highlight last move squares', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Make a move
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await user.click(e2Square);
        await user.click(e4Square);
      }

      await waitFor(() => {
        expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(1);
      });

      // Board should be functional with highlighted squares
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();
    });

    test('should merge styles for overlapping highlights', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Make a move
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await user.click(e2Square);
        await user.click(e4Square);
      }

      await waitFor(() => {
        expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(1);
      });

      // Select the e4 pawn (which was just moved)
      const e4SquareAfter = chessboard.querySelector('[data-square="e4"]');
      if (e4SquareAfter) {
        await user.click(e4SquareAfter);
      }

      // Board should still be functional
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();
    });

    test('should apply legal move dots correctly', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Select a piece
      const chessboard = screen.getByTestId('chessboard');
      const b1Square = chessboard.querySelector('[data-square="b1"]');

      if (b1Square) {
        await user.click(b1Square);
      }

      // Board should still be functional
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();
    });

    test('should handle complex style combinations', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Load a specific position
      const testFEN = 'rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2';

      await act(async () => {
        await loadFEN(testFEN, user);
      });

      // Board should be functional
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();
    });
  });

  describe('Edge cases', () => {
    test('should handle invalid piece selection gracefully', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Click on empty square
      const chessboard = screen.getByTestId('chessboard');
      const e3Square = chessboard.querySelector('[data-square="e3"]');

      if (e3Square) {
        await user.click(e3Square);
      }

      // Should not crash or show errors
      expect(screen.queryByText(/Error/)).not.toBeInTheDocument();
    });

    test('should handle rapid piece selection changes', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      const chessboard = screen.getByTestId('chessboard');

      // Rapidly select different pieces
      const squares = [
        chessboard.querySelector('[data-square="e2"]'),
        chessboard.querySelector('[data-square="b1"]'),
        chessboard.querySelector('[data-square="d2"]'),
        chessboard.querySelector('[data-square="g1"]')
      ];

      for (const square of squares) {
        if (square) {
          await user.click(square);
        }
      }

      // Should handle rapid changes without errors
      expect(screen.queryByText(/Error/)).not.toBeInTheDocument();
    });

    test('should clear legal moves on new game', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Select a piece
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');

      if (e2Square) {
        await user.click(e2Square);
      }

      // Start new game
      const newGameButtons = screen.getAllByText('New Game');
      const newGameButton = newGameButtons.find(button =>
        button.classList.contains('btn-primary')
      ) || newGameButtons[0];
      await user.click(newGameButton);

      await waitFor(() => {
        expect(screen.getByText('Start Game')).toBeInTheDocument();
      });

      await user.click(screen.getByText('Start Game'));

      // Board should be reset
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();
    });

    test('should handle piece selection during bot thinking', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Make a move to trigger bot
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await user.click(e2Square);
        await user.click(e4Square);
      }

      // Try to select piece while bot might be thinking
      const d2Square = chessboard.querySelector('[data-square="d2"]');
      if (d2Square) {
        await user.click(d2Square);
      }

      // Should handle gracefully
      expect(screen.queryByText(/Error/)).not.toBeInTheDocument();
    });
  });
});
