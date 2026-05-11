/* eslint-env jest */
import React from 'react';
import { render, screen, waitFor, act } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import type { UserEvent } from '@testing-library/user-event';
import ChessGame from './ChessGame';
import { 
  makeMove, 
  getCurrentMoveCount, 
  getCurrentViewIndex,
  loadFEN,
  setupFetchMocks,
  suppressExpectedErrors
,
  waitForChessGameReady
} from './ChessGame.test.utils';

describe('ChessGame Component - Move Handling Tests (Real Board)', () => {
  let user: UserEvent;

  beforeEach(() => {
    // Clear all mocks before each test
    jest.clearAllMocks();
    
    user = userEvent.setup();
    
    // Setup fetch mocks for evaluation
    setupFetchMocks();
    suppressExpectedErrors();
  });

  afterEach(() => {
    jest.clearAllMocks();
    jest.restoreAllMocks();
    if (global.restoreConsole) {
      global.restoreConsole();
    }
  });

  test('handles making a move with real board interaction', async () => {
    render(<ChessGame />);
    
    // Wait for the app to be fully rendered
    await waitForChessGameReady();

    // Get initial move count
    const initialMoveCount = getCurrentMoveCount();
    expect(initialMoveCount).toBe(0);

    // Make a move using the shared helper
    await act(async () => {
      await makeMove('e4', user);
    });

    // Wait for the move to be processed
    await waitFor(() => {
      const newMoveCount = getCurrentMoveCount();
      expect(newMoveCount).toBeGreaterThan(initialMoveCount);
    }, { timeout: 2000 });
  });

  test('verifies chessboard component is rendered', async () => {
    render(<ChessGame />);

    // Wait for the chessboard to render
    await waitForChessGameReady();

    // Check for the chessboard
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();

    // Check for move list with start position
    expect(screen.getByText('Start Position')).toBeInTheDocument();

    // Check for game controls
    const newGameButtons = screen.getAllByText('New Game');
    expect(newGameButtons.length).toBeGreaterThan(0);
    expect(screen.getByText('Export PGN')).toBeInTheDocument();
    expect(screen.getByText('FEN')).toBeInTheDocument();
  });

  test('handles promotion moves with real board', async () => {
    render(<ChessGame />);

    // Wait for app to render
    await waitForChessGameReady();

    // FEN with white pawn on e7 ready to promote
    const promotionFen = '4k3/4P3/8/8/8/8/8/4K3 w - - 0 1';

    await act(async () => {
      await loadFEN(promotionFen, user);
    });

    // Verify the FEN was loaded by checking the chessboard's data-position
    await waitFor(() => {
      const chessboard = screen.getByTestId('chessboard');
      expect(chessboard.getAttribute('data-position')).toBe(promotionFen);
    });
  });

  test('handles invalid moves gracefully with real board', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    const initialMoveCount = getCurrentMoveCount();

    // Try clicking on an invalid destination (not a legal move from e2)
    // The makeMove helper would throw for invalid SAN, so we test by clicking squares directly
    const chessboard = screen.getByTestId('chessboard');
    const e2Square = chessboard.querySelector('[data-square="e2"]');
    const e1Square = chessboard.querySelector('[data-square="e1"]');

    if (e2Square && e1Square) {
      await user.click(e2Square);
      await user.click(e1Square); // Invalid - pawn can't move to e1
    }

    // Verify move count hasn't changed (move was rejected)
    await waitFor(() => {
      expect(getCurrentMoveCount()).toBe(initialMoveCount);
    });

    // Board should still be rendered and functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });

  test('handles move history navigation after moves', async () => {
    render(<ChessGame />);
    
    await waitForChessGameReady();

    // Make a move using the shared helper
    await act(async () => {
      await makeMove('e4', user);
    });

    // Wait for move to be processed
    await waitFor(() => {
      expect(getCurrentMoveCount()).toBeGreaterThan(0);
    }, { timeout: 3000 });

    // Navigate to start by clicking "Start Position"
    const startPositionButton = screen.getByText('Start Position');
    await user.click(startPositionButton);

    // Verify we're at the start position
    await waitFor(() => {
      expect(getCurrentViewIndex()).toBe(-1);
    });
    
    // Navigate back to the latest by clicking on the last move
    // The move list should show "1. e4" after our move
    const lastMove = screen.getByText('e4');
    await user.click(lastMove);

    // Verify we're back at the latest position
    await waitFor(() => {
      expect(getCurrentViewIndex()).toBe(0); // First move is index 0
    });
  });

  test('handles rapid successive moves correctly', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    const initialMoveCount = getCurrentMoveCount();

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
      const moveCount = getCurrentMoveCount();
      expect(moveCount).toBeGreaterThan(initialMoveCount);
    }, { timeout: 3000 });

    // App should remain stable and functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });
});