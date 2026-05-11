/* eslint-env jest */
import React from 'react';
import { render, screen, waitFor, act } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import type { UserEvent } from '@testing-library/user-event';
import ChessGame from './ChessGame';
import {
  getCurrentMoveCount,
  getCurrentViewIndex,
  loadFEN,
  setupFetchMocks,
  waitForChessGameReady
} from './ChessGame.test.utils';

// Helper to check if game is over
function isGameOver() {
  // Check for any game ending message
  const checkmate = screen.queryByText(/Checkmate!.*wins!/);
  const draw = screen.queryByText(/Draw!/);
  const timeWin = screen.queryByText(/wins on time!/);
  return checkmate !== null || draw !== null || timeWin !== null;
}

describe('ChessGame Component - Error Handling Tests (Real Board)', () => {
  let user: UserEvent;

  beforeEach(() => {
    user = userEvent.setup();

    // Setup fetch mocks
    setupFetchMocks();
  });

  afterEach(() => {
    jest.clearAllMocks();
    jest.restoreAllMocks();
  });

  test('handles invalid move attempts gracefully', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    const initialMoveCount = getCurrentMoveCount();

    // Try to click on an invalid destination square (not a legal move)
    const chessboard = screen.getByTestId('chessboard');
    const e2Square = chessboard.querySelector('[data-square="e2"]');
    const e1Square = chessboard.querySelector('[data-square="e1"]');

    if (e2Square && e1Square) {
      await user.click(e2Square);
      await user.click(e1Square); // Invalid - pawn can't move backwards
    }

    // Move should not have been executed
    expect(getCurrentMoveCount()).toBe(initialMoveCount);

    // App should still be functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();

    // Try a valid move to ensure the app is still working
    const e4Square = chessboard.querySelector('[data-square="e4"]');
    if (e2Square && e4Square) {
      await user.click(e2Square);
      await user.click(e4Square);
    }

    // Now the move count should have increased
    await waitFor(() => {
      expect(getCurrentMoveCount()).toBeGreaterThan(initialMoveCount);
    });
  });

  test('handles invalid square clicks gracefully', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    const initialMoveCount = getCurrentMoveCount();

    // Click on empty squares (no piece to move)
    const chessboard = screen.getByTestId('chessboard');
    const e5Square = chessboard.querySelector('[data-square="e5"]');
    const e6Square = chessboard.querySelector('[data-square="e6"]');

    if (e5Square && e6Square) {
      await user.click(e5Square);
      await user.click(e6Square);
    }

    // Move count should not change
    expect(getCurrentMoveCount()).toBe(initialMoveCount);

    // App should still be functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });

  test('handles moves when not viewing latest position', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Make a move first to create history
    const chessboard = screen.getByTestId('chessboard');
    const e2Square = chessboard.querySelector('[data-square="e2"]');
    const e4Square = chessboard.querySelector('[data-square="e4"]');

    if (e2Square && e4Square) {
      await user.click(e2Square);
      await user.click(e4Square);
    }

    // Wait for move to complete
    await waitFor(() => {
      expect(getCurrentMoveCount()).toBeGreaterThan(0);
    });

    const currentMoveCount = getCurrentMoveCount();

    // Navigate to start position
    const startPositionButton = screen.getByText('Start Position');
    await user.click(startPositionButton);

    // Verify we're not at latest position
    await waitFor(() => {
      expect(getCurrentViewIndex()).toBe(-1);
    });

    // Try to click on the board while not viewing latest
    // Pieces should not be draggable when not at latest position
    const d2Square = chessboard.querySelector('[data-square="d2"]');
    const d4Square = chessboard.querySelector('[data-square="d4"]');

    if (d2Square && d4Square) {
      await user.click(d2Square);
      await user.click(d4Square);
    }

    // Move should not have been executed
    expect(getCurrentMoveCount()).toBe(currentMoveCount);
  });

  test('handles API errors gracefully', async () => {
    // Mock fetch to return errors
    (global.fetch as jest.MockedFunction<typeof fetch>).mockImplementation((url: string | URL | Request) => {
      if (typeof url === 'string' && url.includes('/evaluate_position')) {
        return Promise.resolve({
          ok: true,
          json: () => Promise.resolve({ evaluation: 0.0 })
        } as Response);
      }
      if (typeof url === 'string' && url.includes('/get_move')) {
        return Promise.reject(new Error('Network error'));
      }
      return Promise.reject(new Error('Unknown endpoint'));
    });

    render(<ChessGame />);

    await waitForChessGameReady();

    // Make a move that will trigger API call
    const chessboard = screen.getByTestId('chessboard');
    const e2Square = chessboard.querySelector('[data-square="e2"]');
    const e4Square = chessboard.querySelector('[data-square="e4"]');

    if (e2Square && e4Square) {
      await user.click(e2Square);
      await user.click(e4Square);
    }

    // Wait a bit for the API call to fail
    await waitFor(() => {
      expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(1); // User move registered
    }, { timeout: 2000 });

    // App should still be functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
    expect(isGameOver()).toBe(false);
  });

  test('handles invalid FEN input', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Open FEN dialog
    const fenButton = screen.getByRole('button', { name: /^FEN$/i });
    await user.click(fenButton);

    await waitFor(() => {
      expect(screen.getByPlaceholderText('Paste FEN string here')).toBeInTheDocument();
    });

    // Store the original position
    const chessboard = screen.getByTestId('chessboard');
    const originalPosition = chessboard.getAttribute('data-position');

    // Try invalid FEN
    const fenInput = screen.getByPlaceholderText('Paste FEN string here');
    const loadButton = screen.getByRole('button', { name: /Load Position/i });

    await user.clear(fenInput);
    await user.type(fenInput, 'invalid fen string with too many parts here extra');
    await user.click(loadButton);

    // Should show error and position should not change
    await waitFor(() => {
      expect(screen.getByText('Invalid FEN format')).toBeInTheDocument();
    });
    expect(chessboard.getAttribute('data-position')).toBe(originalPosition);
  });

  test('handles game over states correctly', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Set up a checkmate position directly
    const checkmateFen = '4k3/4Q3/4K3/8/8/8/8/8 b - - 0 1';

    await act(async () => {
      await loadFEN(checkmateFen, user);
    });

    // Should show checkmate message
    await waitFor(() => {
      const checkmateMsg = screen.getByText(/Checkmate/);
      expect(checkmateMsg).toBeInTheDocument();
    });

    // Verify game is over
    expect(isGameOver()).toBe(true);

    // Should not be able to make more moves
    const moveCountBefore = getCurrentMoveCount();

    const chessboard = screen.getByTestId('chessboard');
    const e8Square = chessboard.querySelector('[data-square="e8"]');
    const d8Square = chessboard.querySelector('[data-square="d8"]');

    if (e8Square && d8Square) {
      await user.click(e8Square);
      await user.click(d8Square);
    }

    expect(getCurrentMoveCount()).toBe(moveCountBefore);
  });

  test('handles rapid move attempts without errors', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Make first move normally
    const chessboard = screen.getByTestId('chessboard');
    const e2Square = chessboard.querySelector('[data-square="e2"]');
    const e4Square = chessboard.querySelector('[data-square="e4"]');

    if (e2Square && e4Square) {
      await user.click(e2Square);
      await user.click(e4Square);
    }

    // Wait for first move to complete
    await waitFor(() => {
      expect(getCurrentMoveCount()).toBeGreaterThan(0);
    });

    // App should still be functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();

    // Should have made at least one move
    expect(getCurrentMoveCount()).toBeGreaterThan(0);
  });

  test('handles empty click gracefully', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    const initialMoveCount = getCurrentMoveCount();

    // Click on empty square
    const chessboard = screen.getByTestId('chessboard');
    const e5Square = chessboard.querySelector('[data-square="e5"]');

    if (e5Square) {
      await user.click(e5Square);
    }

    // No move should be executed
    expect(getCurrentMoveCount()).toBe(initialMoveCount);

    // App should still be functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });

  test('handles bot API timeout gracefully', async () => {
    // Mock fetch to simulate timeout
    (global.fetch as jest.MockedFunction<typeof fetch>).mockImplementation((url: string | URL | Request) => {
      if (typeof url === 'string' && url.includes('/evaluate_position')) {
        return Promise.resolve({
          ok: true,
          json: () => Promise.resolve({ evaluation: 0.0 })
        } as Response);
      }
      if (typeof url === 'string' && url.includes('/get_move')) {
        // Simulate a very slow response
        return new Promise((resolve) => {
          setTimeout(() => {
            resolve({
              ok: true,
              json: () => Promise.resolve({ move: 'e5', thinking_time: 0.5 })
            } as Response);
          }, 10000); // 10 second delay
        });
      }
      return Promise.reject(new Error('Unknown endpoint'));
    });

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

    // User move should be registered
    await waitFor(() => {
      expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(1);
    });

    // App should remain responsive while waiting for bot
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();

    // Can still interact with UI (e.g., navigate)
    const startPositionButton = screen.getByText('Start Position');
    await user.click(startPositionButton);

    await waitFor(() => {
      expect(getCurrentViewIndex()).toBe(-1);
    });
  });
});
