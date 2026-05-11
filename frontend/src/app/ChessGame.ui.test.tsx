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
  disableBotMoves,
  waitForGameStable,
  waitForChessGameReady
} from './ChessGame.test.utils';

describe('ChessGame Component - UI Tests (Real Board)', () => {
  let user: UserEvent;

  beforeEach(() => {
    user = userEvent.setup();

    // Setup fetch mocks
    setupFetchMocks();

    // Disable bot moves for UI tests to prevent race conditions
    disableBotMoves();
  });

  afterEach(() => {
    jest.clearAllMocks();
    jest.restoreAllMocks();
  });

  test('renders the app title', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Check that the chessboard is rendered
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });

  test('renders the chessboard and is interactive', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Check that the board is rendered
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();

    // Check initial move count
    expect(getCurrentMoveCount()).toBe(0);

    // Make a move by clicking squares
    const chessboard = screen.getByTestId('chessboard');
    const e2Square = chessboard.querySelector('[data-square="e2"]');
    const e4Square = chessboard.querySelector('[data-square="e4"]');

    if (e2Square && e4Square) {
      await user.click(e2Square);
      await user.click(e4Square);
    }

    // Verify the board responded to interaction
    await waitFor(() => {
      expect(getCurrentMoveCount()).toBeGreaterThan(0);
    });
  });

  test('renders the move list and updates with moves', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Check initial state
    expect(getCurrentMoveCount()).toBe(0);
    expect(getCurrentViewIndex()).toBe(-1);

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
      expect(getCurrentMoveCount()).toBeGreaterThan(0);
    });

    // Check that move appears in move list
    const moveListItem = screen.getByText('e4');
    expect(moveListItem).toBeInTheDocument();

    // Click on the move to navigate
    await user.click(moveListItem);

    // Check that view index updated
    expect(getCurrentViewIndex()).toBe(0);
  });

  test('displays game status area correctly', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // The chessboard should exist
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();

    // Initially, no game over messages should be shown
    expect(screen.queryByText('Game Over!')).not.toBeInTheDocument();
    expect(screen.queryByText(/Checkmate/)).not.toBeInTheDocument();
    expect(screen.queryByText('Draw!')).not.toBeInTheDocument();
  });

  test('handles game reset via New Game button', async () => {
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
      expect(getCurrentMoveCount()).toBeGreaterThan(0);
    });

    // Click New Game button - be specific about which one
    const newGameButtons = screen.getAllByText('New Game');
    const newGameButton = newGameButtons.find(button =>
      button.classList.contains('btn-primary')
    ) || newGameButtons[0];
    await user.click(newGameButton);

    // Should show dialog - look for Start Game button
    await waitFor(() => {
      expect(screen.getByText('Start Game')).toBeInTheDocument();
    });

    // Click Start Game to reset
    const startGameButton = screen.getByText('Start Game');
    await user.click(startGameButton);

    // Check that the game is reset
    await waitFor(() => {
      expect(getCurrentMoveCount()).toBe(0);
      expect(getCurrentViewIndex()).toBe(-1);
    });
  });

  test('handles new game dialog with settings', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Open new game dialog - be specific about which button
    const newGameButtons = screen.getAllByText('New Game');
    const newGameButton = newGameButtons.find(button =>
      button.classList.contains('btn-primary')
    ) || newGameButtons[0];
    await user.click(newGameButton);

    // Should see dialog with options
    await waitFor(() => {
      expect(screen.getByText('Start Game')).toBeInTheDocument();
    });

    // Cancel dialog
    const cancelButton = screen.getByText('Cancel');
    await user.click(cancelButton);

    // Dialog should close
    await waitFor(() => {
      expect(screen.queryByText('Start Game')).not.toBeInTheDocument();
    });

    // App should still be functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });

  test('handles game ending states', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Set up a checkmate position directly using the FEN dialog
    const checkmateFen = '4k3/4Q3/4K3/8/8/8/8/8 b - - 0 1';

    await act(async () => {
      await loadFEN(checkmateFen, user);
    });

    // Should immediately show checkmate message
    await waitFor(() => {
      const checkmateMsg = screen.getByText(/Checkmate/);
      expect(checkmateMsg).toBeInTheDocument();
      expect(checkmateMsg.textContent).toContain('White wins!');
    });
  });

  test('displays draw state correctly', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Set up an actual stalemate position - black king trapped with no legal moves
    const stalemateFen = '5k2/5P2/5K2/8/8/8/8/8 b - - 0 1';

    await act(async () => {
      await loadFEN(stalemateFen, user);
    });

    // Should immediately show draw
    await waitFor(() => {
      expect(screen.getByText('Draw!')).toBeInTheDocument();
    });
  });

  test('maintains accessibility with keyboard navigation', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Make a move to create history
    const chessboard = screen.getByTestId('chessboard');
    const e2Square = chessboard.querySelector('[data-square="e2"]');
    const e4Square = chessboard.querySelector('[data-square="e4"]');

    if (e2Square && e4Square) {
      await user.click(e2Square);
      await user.click(e4Square);
    }

    await waitFor(() => {
      expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(1);
    }, { timeout: 3000 });

    // Wait for game to stabilize
    await waitForGameStable();

    // Small delay to ensure all state updates have settled
    await act(async () => {
      await new Promise(resolve => setTimeout(resolve, 100));
    });

    // Test keyboard navigation - press ArrowLeft to go to start
    await act(async () => {
      await user.keyboard('{ArrowLeft}');
    });

    // Should go to start position
    await waitFor(() => {
      expect(getCurrentViewIndex()).toBe(-1);
    }, { timeout: 3000 });

    // Small delay before next navigation
    await act(async () => {
      await new Promise(resolve => setTimeout(resolve, 50));
    });

    // Navigate forward
    await act(async () => {
      await user.keyboard('{ArrowRight}');
    });

    // Should go to first move
    await waitFor(() => {
      expect(getCurrentViewIndex()).toBe(0);
    }, { timeout: 3000 });

    // Should still be functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });

  test('displays current FEN and can load new FEN', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Check that FEN button exists
    expect(screen.getByRole('button', { name: /^FEN$/i })).toBeInTheDocument();

    // Make a move to change position
    const chessboard = screen.getByTestId('chessboard');
    const e2Square = chessboard.querySelector('[data-square="e2"]');
    const e4Square = chessboard.querySelector('[data-square="e4"]');

    if (e2Square && e4Square) {
      await user.click(e2Square);
      await user.click(e4Square);
    }

    // Wait for game to stabilize
    await waitForGameStable();

    // FEN should update - check the chessboard's data-position
    await waitFor(() => {
      const board = screen.getByTestId('chessboard');
      const position = board.getAttribute('data-position');
      expect(position).toContain('4P3'); // e4 pawn
    });
  });

  test('shows evaluation bar when available', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Check that evaluation bar component is rendered by looking for the meter role
    const evalBar = screen.getByRole('meter');
    expect(evalBar).toBeInTheDocument();
  });
});
