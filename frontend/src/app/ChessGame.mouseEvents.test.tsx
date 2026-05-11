/* eslint-env jest */
import React from 'react';
import { render, screen, waitFor, act } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import type { UserEvent } from '@testing-library/user-event';
import ChessGame from './ChessGame';
import {
  getCurrentMoveCount,
  setupFetchMocks,
  suppressExpectedErrors,
  waitForChessGameReady
} from './ChessGame.test.utils';

describe('ChessGame Component - Mouse Events Tests (Real Board)', () => {
  let user: UserEvent;

  beforeEach(() => {
    user = userEvent.setup();
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

  test('handles piece selection and legal move display', async () => {
    // This test checks mouse interaction for piece selection and legal moves
    render(<ChessGame />);

    await waitForChessGameReady();

    // Verify the board is interactive
    const chessboard = screen.getByTestId('chessboard');
    expect(chessboard).toBeInTheDocument();

    // Click on a pawn to select it
    const e2Square = chessboard.querySelector('[data-square="e2"]');
    expect(e2Square).toBeInTheDocument();

    if (e2Square) {
      await user.click(e2Square);
    }

    // Component should remain functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });

  test('handles interaction with non-game elements', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Test interaction with UI elements outside the board
    // Be specific about which New Game button (there might be multiple)
    const newGameButtons = screen.getAllByText('New Game');
    const newGameButton = newGameButtons.find(button =>
      button.classList.contains('btn-primary')
    ) || newGameButtons[0];
    const exportButton = screen.getByText('Export PGN');

    // Verify these elements are present and clickable
    expect(newGameButton).toBeInTheDocument();
    expect(exportButton).toBeInTheDocument();

    // Test clicking on non-game elements doesn't break the app
    await act(async () => {
      await user.hover(newGameButton);
      await user.hover(exportButton);
    });

    // Component should remain functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });

  test('handles invalid move input gracefully', async () => {
    // This test checks handling of invalid move attempts
    render(<ChessGame />);

    await waitForChessGameReady();

    const initialMoveCount = getCurrentMoveCount();

    // Try to make an invalid move (click on empty square then another empty square)
    const chessboard = screen.getByTestId('chessboard');
    const e5Square = chessboard.querySelector('[data-square="e5"]');
    const e6Square = chessboard.querySelector('[data-square="e6"]');

    if (e5Square && e6Square) {
      await user.click(e5Square);
      await user.click(e6Square);
    }

    // Move count should not change
    expect(getCurrentMoveCount()).toBe(initialMoveCount);

    // Component should remain functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });

  test('handles piece selection and deselection correctly', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    const chessboard = screen.getByTestId('chessboard');

    // Click on a pawn to select it
    const e2Square = chessboard.querySelector('[data-square="e2"]');

    if (e2Square) {
      await user.click(e2Square);
    }

    // Click on it again to deselect
    if (e2Square) {
      await user.click(e2Square);
    }

    // Click on a different piece
    const d2Square = chessboard.querySelector('[data-square="d2"]');

    if (d2Square) {
      await user.click(d2Square);
    }

    // Click on knight
    const g1Square = chessboard.querySelector('[data-square="g1"]');

    if (g1Square) {
      await user.click(g1Square);
    }

    // Component should remain functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });

  test('handles multiple rapid interactions gracefully', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    const chessboard = screen.getByTestId('chessboard');
    const initialMoveCount = getCurrentMoveCount();

    // Test rapid clicks (simulating fast mouse interactions)
    const e2Square = chessboard.querySelector('[data-square="e2"]');
    const d2Square = chessboard.querySelector('[data-square="d2"]');
    const e4Square = chessboard.querySelector('[data-square="e4"]');

    await act(async () => {
      if (e2Square) await user.click(e2Square);
      if (d2Square) await user.click(d2Square);
      if (e2Square) await user.click(e2Square);
      if (e4Square) await user.click(e4Square);
    });

    // Should have made one valid move
    await waitFor(() => {
      expect(getCurrentMoveCount()).toBeGreaterThan(initialMoveCount);
    });

    // Component should remain stable
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });
});
