/* eslint-env jest */
import React from 'react';
import { render, screen, waitFor, act } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import type { UserEvent } from '@testing-library/user-event';
import ChessGame from './ChessGame';
import {
  loadFEN,
  setupFetchMocks,
  suppressExpectedErrors,
  waitForChessGameReady
} from './ChessGame.test.utils';

describe('ChessGame Component - FEN/PGN Tests (Real Board)', () => {
  let user: UserEvent;

  beforeEach(() => {
    user = userEvent.setup();
    setupFetchMocks();
    suppressExpectedErrors(); // Suppress expected console errors
  });

  afterEach(() => {
    jest.clearAllMocks();
    jest.restoreAllMocks();
    if (global.restoreConsole) {
      global.restoreConsole(); // Restore console for next test
    }
  });

  test('handles FEN input and loading', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Enter a valid FEN - position after 1.e4 (normalized by chess.js)
    const validFEN = 'rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1';

    await act(async () => {
      await loadFEN(validFEN, user);
    });

    // Verify the FEN was actually loaded by checking the chessboard
    await waitFor(() => {
      const chessboard = screen.getByTestId('chessboard');
      expect(chessboard.getAttribute('data-position')).toBe(validFEN);
    });
  });

  test('loading and resetting custom FEN position', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Enter a custom FEN - King's Indian setup
    const customFEN = 'r1bqkbnr/pppppppp/2n5/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2';

    await act(async () => {
      await loadFEN(customFEN, user);
    });

    // Verify custom FEN was loaded
    await waitFor(() => {
      const chessboard = screen.getByTestId('chessboard');
      expect(chessboard.getAttribute('data-position')).toBe(customFEN);
    });

    // Reset to standard position via New Game
    const newGameButtons = screen.getAllByText('New Game');
    const resetButton = newGameButtons.find(button =>
      button.classList.contains('btn-primary')
    ) || newGameButtons[0];

    await user.click(resetButton);

    // Wait for dialog
    await waitFor(() => {
      expect(screen.getByText('Start Game')).toBeInTheDocument();
    });

    // Click Start Game
    await user.click(screen.getByText('Start Game'));

    // After reset, verify we're back to standard position
    await waitFor(() => {
      const chessboard = screen.getByTestId('chessboard');
      const position = chessboard.getAttribute('data-position');
      expect(position).toContain('rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR');
    });
  });

  test('handles FEN validation', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Test valid FEN loading - position after 1.e4 e5
    const validFEN = 'rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2';

    await act(async () => {
      await loadFEN(validFEN, user);
    });

    // Verify FEN was loaded
    await waitFor(() => {
      const chessboard = screen.getByTestId('chessboard');
      expect(chessboard.getAttribute('data-position')).toBe(validFEN);
    });
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

    // Enter an invalid FEN (too many parts to trigger format error)
    const fenInput = screen.getByPlaceholderText('Paste FEN string here');
    const loadButton = screen.getByRole('button', { name: /Load Position/i });

    await user.clear(fenInput);
    await user.type(fenInput, 'invalid fen string with too many parts here extra');
    await user.click(loadButton);

    // Should show error message
    await waitFor(() => {
      expect(screen.getByText('Invalid FEN format')).toBeInTheDocument();
    });
  });

  test('handles empty FEN input', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Open FEN dialog
    const fenButton = screen.getByRole('button', { name: /^FEN$/i });
    await user.click(fenButton);

    await waitFor(() => {
      expect(screen.getByPlaceholderText('Paste FEN string here')).toBeInTheDocument();
    });

    // Clear input to ensure it's empty and click load
    const fenInput = screen.getByPlaceholderText('Paste FEN string here');
    const loadButton = screen.getByRole('button', { name: /Load Position/i });

    await user.clear(fenInput);
    await user.click(loadButton);

    // Check that an error message is displayed
    await waitFor(() => {
      expect(screen.getByText('Please enter a FEN string')).toBeInTheDocument();
    });
  });

  test('handles loadFen with empty input gracefully', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Open FEN dialog
    const fenButton = screen.getByRole('button', { name: /^FEN$/i });
    await user.click(fenButton);

    await waitFor(() => {
      expect(screen.getByPlaceholderText('Paste FEN string here')).toBeInTheDocument();
    });

    // Ensure input is empty and click load button
    const input = screen.getByPlaceholderText('Paste FEN string here');
    const loadButton = screen.getByRole('button', { name: /Load Position/i });

    await user.clear(input);
    await user.click(loadButton);

    // Verify error message is displayed
    await waitFor(() => {
      expect(screen.getByText('Please enter a FEN string')).toBeInTheDocument();
    });
  });

  test('handles FEN loading and verifies position change', async () => {
    render(<ChessGame />);

    await waitForChessGameReady();

    // Load a specific position - Sicilian Defense setup
    const sicilianFEN = 'rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2';

    await act(async () => {
      await loadFEN(sicilianFEN, user);
    });

    // Verify the position was loaded
    await waitFor(() => {
      const chessboard = screen.getByTestId('chessboard');
      const position = chessboard.getAttribute('data-position');
      expect(position).toContain('pp1ppppp');
      expect(position).toContain('2p5');
    });

    // Verify component remains functional
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });
});
