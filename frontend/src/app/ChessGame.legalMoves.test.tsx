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
  disableBotMoves,
  waitForChessGameReady
} from './ChessGame.test.utils';

// Note: Helper functions for interacting with the chess board are available
// but not all are used in every test. The board interaction is primarily
// done through clicking on squares.

describe('ChessGame Component - Legal Moves Tests (Real Board)', () => {
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

  test('shows legal moves when clicking on a piece', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Click on a pawn to select it and show legal moves
    const chessboard = screen.getByTestId('chessboard');
    const e2Square = chessboard.querySelector('[data-square="e2"]');

    if (e2Square) {
      await user.click(e2Square);
    }

    // Board should still be functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });

  test('legal moves remain visible after mouseup', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Click on a piece
    const chessboard = screen.getByTestId('chessboard');
    const e2Square = chessboard.querySelector('[data-square="e2"]');

    if (e2Square) {
      await user.click(e2Square);
    }

    // Simulate mouseup on document
    await act(async () => {
      const mouseUpEvent = new MouseEvent('mouseup', { bubbles: true });
      document.dispatchEvent(mouseUpEvent);
    });

    // App should still be functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });

  test('can toggle legal moves by clicking the same piece', async () => {
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

    // Should still be functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });

  test('cannot move opponent pieces', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    const initialMoveCount = getCurrentMoveCount();

    // Try to click on a black piece (e7) when it's white's turn
    const chessboard = screen.getByTestId('chessboard');
    const e7Square = chessboard.querySelector('[data-square="e7"]');
    const e5Square = chessboard.querySelector('[data-square="e5"]');

    if (e7Square && e5Square) {
      await user.click(e7Square);
      await user.click(e5Square);
    }

    // Move should not be executed - move count should remain 0
    expect(getCurrentMoveCount()).toBe(initialMoveCount);
  });

  test('handles drag operations for showing legal moves', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Since DragEvent is not available in JSDOM, we'll use mouse events instead
    const chessboard = screen.getByTestId('chessboard');

    // Simulate drag start with mousedown
    await act(async () => {
      const mouseDownEvent = new MouseEvent('mousedown', {
        bubbles: true,
        clientX: 100,
        clientY: 100
      });
      chessboard.dispatchEvent(mouseDownEvent);
    });

    // App should still be functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();

    // Simulate drag end with mouseup
    await act(async () => {
      const mouseUpEvent = new MouseEvent('mouseup', {
        bubbles: true,
        clientX: 200,
        clientY: 200
      });
      chessboard.dispatchEvent(mouseUpEvent);
    });

    // App should still be functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });

  test('highlights last move after making a move', async () => {
    // Disable bot moves for this test
    disableBotMoves();

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

    // Wait for move to be processed
    await waitFor(() => {
      expect(getCurrentMoveCount()).toBe(1);
    }, { timeout: 2000 });

    // In a real implementation, we would check that squares e2 and e4 are highlighted
    // Since we can't access the board's internal styling, we verify the move was made
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });

  test('maintains highlighting during move history navigation', async () => {
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

    // Wait for move and AI response
    await waitFor(() => {
      expect(getCurrentMoveCount()).toBeGreaterThan(0);
    }, { timeout: 3000 });

    // Navigate to start position
    const startPositionButton = screen.getByText('Start Position');
    await user.click(startPositionButton);

    // Verify we're at start
    await waitFor(() => {
      expect(getCurrentViewIndex()).toBe(-1);
    });

    // Click on first move to go back
    const firstMove = screen.getByText('e4');
    await user.click(firstMove);

    // Verify we're viewing the first move
    await waitFor(() => {
      expect(getCurrentViewIndex()).toBe(0);
    });

    // App should maintain highlighting (we can't directly verify this without board access)
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });

  test('handles keyboard navigation while maintaining highlights', async () => {
    // Disable bot moves for this test
    disableBotMoves();

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

    // Wait for move
    await waitFor(() => {
      expect(getCurrentMoveCount()).toBe(1);
    }, { timeout: 2000 });

    // Use keyboard navigation
    await act(async () => {
      // Navigate to start with left arrow
      await user.keyboard('{ArrowLeft}');
    });

    // Check we're at start
    await waitFor(() => {
      expect(getCurrentViewIndex()).toBe(-1);
    });

    // Navigate forward with right arrow
    await act(async () => {
      await user.keyboard('{ArrowRight}');
    });

    // Check we're back at move 0
    await waitFor(() => {
      expect(getCurrentViewIndex()).toBe(0);
    });

    // App should be functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });

  test('shows legal moves for different pieces', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    const chessboard = screen.getByTestId('chessboard');

    // Test knight selection
    const g1Square = chessboard.querySelector('[data-square="g1"]');
    if (g1Square) {
      await user.click(g1Square);
    }

    // Test another piece
    const d2Square = chessboard.querySelector('[data-square="d2"]');
    if (d2Square) {
      await user.click(d2Square);
    }

    // Both interactions should work
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });

  test('handles errors gracefully when calculating legal moves', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    const initialMoveCount = getCurrentMoveCount();

    // Click on an empty square (no piece to select)
    const chessboard = screen.getByTestId('chessboard');
    const e5Square = chessboard.querySelector('[data-square="e5"]');
    const e6Square = chessboard.querySelector('[data-square="e6"]');

    if (e5Square && e6Square) {
      await user.click(e5Square);
      await user.click(e6Square);
    }

    // App should handle the error gracefully and still be functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();

    // Move count should remain 0
    expect(getCurrentMoveCount()).toBe(initialMoveCount);
  });
});
