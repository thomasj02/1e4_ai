/* eslint-env jest */
import React from 'react';
import { render, screen, waitFor, act } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import type { UserEvent } from '@testing-library/user-event';
import ChessGame from './ChessGame';
import {
  getCurrentMoveCount,
  getCurrentViewIndex,
  setupFetchMocks,
  waitForChessGameReady
} from './ChessGame.test.utils';

describe('ChessGame Component - Navigation Tests (Real Board)', () => {
  let user: UserEvent;

  beforeEach(() => {
    user = userEvent.setup();

    // Setup fetch mocks with bot moves
    setupFetchMocks({
      botMove: 'e5',
      thinkingTime: 0.5
    });
  });

  afterEach(() => {
    jest.clearAllMocks();
    jest.restoreAllMocks();
  });

  test('handles navigation through move history', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Make a move by clicking squares
    const chessboard = screen.getByTestId('chessboard');
    const e2Square = chessboard.querySelector('[data-square="e2"]');
    const e4Square = chessboard.querySelector('[data-square="e4"]');

    if (e2Square && e4Square) {
      await user.click(e2Square);
      await user.click(e4Square);
    }

    // Wait for move to be processed and bot to respond
    await waitFor(() => {
      expect(getCurrentMoveCount()).toBeGreaterThan(0);
    }, { timeout: 2000 });

    // Go to start position by clicking "Start Position"
    const startPositionButton = screen.getByText('Start Position');
    await user.click(startPositionButton);

    // Check that viewed index is -1 (start position)
    expect(getCurrentViewIndex()).toBe(-1);

    // Go to first move by clicking on "e4" in move list
    const firstMove = screen.getByText('e4');
    await user.click(firstMove);

    // Check that viewed index is 0 (first move)
    expect(getCurrentViewIndex()).toBe(0);
  });

  test('handles keyboard navigation', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Make a move by clicking squares
    const chessboard = screen.getByTestId('chessboard');
    const e2Square = chessboard.querySelector('[data-square="e2"]');
    const e4Square = chessboard.querySelector('[data-square="e4"]');

    if (e2Square && e4Square) {
      await user.click(e2Square);
      await user.click(e4Square);
    }

    // Wait for move and bot response
    await waitFor(() => {
      expect(getCurrentMoveCount()).toBeGreaterThan(0);
    }, { timeout: 2000 });

    // Navigate to first move
    const firstMove = screen.getByText('e4');
    await user.click(firstMove);
    expect(getCurrentViewIndex()).toBe(0);

    // Press left arrow to go back
    await act(async () => {
      await user.keyboard('{ArrowLeft}');
    });

    // Check that viewed index decreased to -1
    await waitFor(() => {
      expect(getCurrentViewIndex()).toBe(-1);
    });

    // Press right arrow to go forward
    await act(async () => {
      await user.keyboard('{ArrowRight}');
    });

    // Check that viewed index increased to 0
    await waitFor(() => {
      expect(getCurrentViewIndex()).toBe(0);
    });
  });

  test('handles moves when not viewing latest position', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Make a move by clicking squares
    const chessboard = screen.getByTestId('chessboard');
    const e2Square = chessboard.querySelector('[data-square="e2"]');
    const e4Square = chessboard.querySelector('[data-square="e4"]');

    if (e2Square && e4Square) {
      await user.click(e2Square);
      await user.click(e4Square);
    }

    // Wait for move and bot response
    await waitFor(() => {
      expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(2);
    }, { timeout: 2000 });

    const initialMoveCount = getCurrentMoveCount();

    // Go to start position (not viewing latest)
    const startPositionButton = screen.getByText('Start Position');
    await user.click(startPositionButton);

    // Verify we're not viewing latest
    expect(getCurrentViewIndex()).toBe(-1);

    // Attempt to make another move when not on latest position
    // Pieces should not be draggable when not at latest position
    const d2Square = chessboard.querySelector('[data-square="d2"]');
    const d4Square = chessboard.querySelector('[data-square="d4"]');

    if (d2Square && d4Square) {
      await user.click(d2Square);
      await user.click(d4Square);
    }

    // Move count should not have changed (move was rejected)
    expect(getCurrentMoveCount()).toBe(initialMoveCount);

    // We should still be viewing the start position
    expect(getCurrentViewIndex()).toBe(-1);
  });

  test('preserves position when navigating through history', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Make first move
    const chessboard = screen.getByTestId('chessboard');
    const e2Square = chessboard.querySelector('[data-square="e2"]');
    const e4Square = chessboard.querySelector('[data-square="e4"]');

    if (e2Square && e4Square) {
      await user.click(e2Square);
      await user.click(e4Square);
    }

    await waitFor(() => {
      expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(2);
    }, { timeout: 2000 });

    // Make second move
    const d2Square = chessboard.querySelector('[data-square="d2"]');
    const d4Square = chessboard.querySelector('[data-square="d4"]');

    if (d2Square && d4Square) {
      await user.click(d2Square);
      await user.click(d4Square);
    }

    await waitFor(() => {
      expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(3);
    }, { timeout: 2000 });

    // Navigate to start
    const startPositionButton = screen.getByText('Start Position');
    await user.click(startPositionButton);
    expect(getCurrentViewIndex()).toBe(-1);

    // Navigate to first move
    const firstMove = screen.getByText('e4');
    await user.click(firstMove);
    expect(getCurrentViewIndex()).toBe(0);

    // Navigate to second white move
    const secondMove = screen.getByText('d4');
    await user.click(secondMove);
    expect(getCurrentViewIndex()).toBe(2);

    // App should still be functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });

  test('displays correct FEN when navigating', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Get initial FEN
    const startingFen = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
    const chessboard = screen.getByTestId('chessboard');
    expect(chessboard.getAttribute('data-position')).toContain('rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR');

    // Make a move
    const e2Square = chessboard.querySelector('[data-square="e2"]');
    const e4Square = chessboard.querySelector('[data-square="e4"]');

    if (e2Square && e4Square) {
      await user.click(e2Square);
      await user.click(e4Square);
    }

    // Wait for move to be processed
    await waitFor(() => {
      expect(getCurrentMoveCount()).toBeGreaterThan(0);
    }, { timeout: 2000 });

    // FEN should have changed
    expect(chessboard.getAttribute('data-position')).not.toBe(startingFen);

    // Navigate back to start
    const startPositionButton = screen.getByText('Start Position');
    await user.click(startPositionButton);

    // FEN should be back to starting position
    await waitFor(() => {
      expect(chessboard.getAttribute('data-position')).toContain('rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR');
    });

    // Navigate forward to first move
    const firstMove = screen.getByText('e4');
    await user.click(firstMove);

    // FEN should match the position after first move
    await waitFor(() => {
      expect(chessboard.getAttribute('data-position')).toContain('4P3'); // e4 pawn
    });
  });

  test('handles navigation with special moves', async () => {
    // Mock bot to respond with different moves
    let moveCounter = 0;
    (global.fetch as jest.MockedFunction<typeof fetch>).mockImplementation((url: string | URL | Request) => {
      if (typeof url === 'string' && url.includes('/evaluate_position')) {
        return Promise.resolve({
          ok: true,
          json: () => Promise.resolve({ evaluation: 0.0 })
        } as Response);
      }
      if (typeof url === 'string' && url.includes('/get_move')) {
        moveCounter++;
        const moves = ['e5', 'Nc6']; // Different moves for each response
        return Promise.resolve({
          ok: true,
          json: () => Promise.resolve({
            move: moves[moveCounter - 1] || 'Nf6',
            thinking_time: 0.5
          })
        } as Response);
      }
      // Default return for unmatched URLs
      return Promise.reject(new Error('Unknown endpoint'));
    });

    render(<ChessGame />);

    await waitForChessGameReady();

    // Make knight move by clicking squares
    const chessboard = screen.getByTestId('chessboard');
    const g1Square = chessboard.querySelector('[data-square="g1"]');
    const f3Square = chessboard.querySelector('[data-square="f3"]');

    if (g1Square && f3Square) {
      await user.click(g1Square);
      await user.click(f3Square);
    }

    // Wait for bot response
    await waitFor(() => {
      expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(2);
    }, { timeout: 3000 });

    // Make another move (e4)
    const e2Square = chessboard.querySelector('[data-square="e2"]');
    const e4Square = chessboard.querySelector('[data-square="e4"]');

    if (e2Square && e4Square) {
      await user.click(e2Square);
      await user.click(e4Square);
    }

    // Wait for second bot response
    await waitFor(() => {
      expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(4);
    }, { timeout: 3000 });

    // Test navigation through these moves
    const startPositionButton = screen.getByText('Start Position');
    await user.click(startPositionButton);

    // Should be at start position
    expect(getCurrentViewIndex()).toBe(-1);

    // Navigate to knight move
    const knightMove = screen.getByText('Nf3');
    await user.click(knightMove);

    // Should be viewing the knight move
    expect(getCurrentViewIndex()).toBe(0);

    // Navigate to pawn move
    const pawnMove = screen.getByText('e4');
    await user.click(pawnMove);

    // Should be viewing the pawn move (3rd move in the game)
    expect(getCurrentViewIndex()).toBe(2);

    // Use keyboard to go back
    await act(async () => {
      await user.keyboard('{ArrowLeft}');
      await user.keyboard('{ArrowLeft}');
    });

    // Should be back at first move
    await waitFor(() => {
      expect(getCurrentViewIndex()).toBe(0);
    });
  });

  test('handles rapid navigation without errors', async () => {
    // Mock bot to respond with different moves
    let moveCounter = 0;
    (global.fetch as jest.MockedFunction<typeof fetch>).mockImplementation((url: string | URL | Request) => {
      if (typeof url === 'string' && url.includes('/evaluate_position')) {
        return Promise.resolve({
          ok: true,
          json: () => Promise.resolve({ evaluation: 0.0 })
        } as Response);
      }
      if (typeof url === 'string' && url.includes('/get_move')) {
        moveCounter++;
        const moves = ['e5', 'd5'];
        return Promise.resolve({
          ok: true,
          json: () => Promise.resolve({
            move: moves[moveCounter - 1] || 'Nf6',
            thinking_time: 0.1
          })
        } as Response);
      }
      // Default return for unmatched URLs
      return Promise.reject(new Error('Unknown endpoint'));
    });

    render(<ChessGame />);

    await waitForChessGameReady();

    // Make first move
    const chessboard = screen.getByTestId('chessboard');
    const e2Square = chessboard.querySelector('[data-square="e2"]');
    const e4Square = chessboard.querySelector('[data-square="e4"]');

    if (e2Square && e4Square) {
      await user.click(e2Square);
      await user.click(e4Square);
    }

    // Wait for first move and bot response
    await waitFor(() => {
      expect(getCurrentMoveCount()).toBe(2); // e4 + e5
    }, { timeout: 3000 });

    // Make second move
    const d2Square = chessboard.querySelector('[data-square="d2"]');
    const d4Square = chessboard.querySelector('[data-square="d4"]');

    if (d2Square && d4Square) {
      await user.click(d2Square);
      await user.click(d4Square);
    }

    // Wait for second move and bot response
    await waitFor(() => {
      expect(getCurrentMoveCount()).toBe(4); // e4, e5, d4, d5
    }, { timeout: 3000 });

    // Rapidly navigate through history
    const startPositionButton = screen.getByText('Start Position');

    await act(async () => {
      // Rapid navigation actions
      await user.click(startPositionButton);
      await user.keyboard('{ArrowRight}');
      await user.keyboard('{ArrowRight}');
      await user.keyboard('{ArrowLeft}');
      await user.click(startPositionButton);
      await user.keyboard('{ArrowRight}');
    });

    // App should still be functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();

    // View index should be valid
    const viewIndex = getCurrentViewIndex();
    const moveCount = getCurrentMoveCount();
    expect(viewIndex).toBeGreaterThanOrEqual(-1);
    expect(viewIndex).toBeLessThanOrEqual(moveCount);

    // Should have 4 moves total
    expect(moveCount).toBe(4);
  });
});
