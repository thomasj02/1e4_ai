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
  loadFEN,
  cleanupTimers,
  waitForChessGameReady
} from './ChessGame.test.utils';

// Increase timeout for error handling tests
jest.setTimeout(10000);

describe('ChessGame Component - Error Handling Tests', () => {
  let user: UserEvent;
  let consoleErrorSpy: jest.SpyInstance;
  let originalFetch: typeof global.fetch;

  beforeEach(() => {
    user = userEvent.setup();
    suppressExpectedErrors();

    // Set up zero-delay fetch mocks to prevent timeouts
    setupFetchMocks({
      thinkingTime: 0  // Eliminate setTimeout delays
    });

    // Save the fetch mock from setupFetchMocks for restoration
    originalFetch = global.fetch;

    // Spy on console methods to verify error handling
    consoleErrorSpy = jest.spyOn(console, 'error').mockImplementation();
  });

  afterEach(() => {
    // Restore the original fetch mock from setupFetchMocks
    global.fetch = originalFetch;

    // Clean up timers properly
    cleanupTimers();

    jest.clearAllMocks();
    jest.restoreAllMocks();
    if (global.restoreConsole) {
      global.restoreConsole();
    }
  });

  describe('Network Error Handling', () => {
    test('should handle bot move API failure gracefully', async () => {
      // Mock Math.random to return 0 to prevent setTimeout delays in fallback
      const originalMathRandom = Math.random;
      Math.random = jest.fn(() => 0);

      // Setup fetch to fail for bot moves
      global.fetch = jest.fn().mockImplementation((url: string | URL | Request) => {
        if (typeof url === 'string' && url.includes('/get_move')) {
          return Promise.reject(new Error('Network error'));
        }
        if (typeof url === 'string' && url.includes('/evaluate_position')) {
          return Promise.resolve({
            ok: true,
            json: () => Promise.resolve({ evaluation: 0 })
          } as Response);
        }
        return Promise.reject(new Error('Unknown endpoint'));
      });

      render(<ChessGame />);

      await waitForChessGameReady();

      // Make a move to trigger bot response
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await user.click(e2Square);
        await user.click(e4Square);
      }

      // Wait for the error to be handled
      await waitFor(() => {
        expect(consoleErrorSpy).toHaveBeenCalledWith(
          'Error fetching move from backend:',
          expect.any(Error)
        );
      }, { timeout: 5000 });

      // Game should still be playable - bot should make fallback move
      await waitFor(() => {
        expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(1);
      }, { timeout: 5000 });

      // Restore Math.random
      Math.random = originalMathRandom;
    });

    test('should handle evaluation API failure', async () => {
      // Setup fetch to fail for evaluation
      global.fetch = jest.fn().mockImplementation((url: string | URL | Request) => {
        if (typeof url === 'string' && url.includes('/evaluate_position')) {
          return Promise.reject(new Error('Evaluation service unavailable'));
        }
        if (typeof url === 'string' && url.includes('/get_move')) {
          return Promise.resolve({
            ok: true,
            json: () => Promise.resolve({ move: 'e5', thinking_time: 0 })
          } as Response);
        }
        return Promise.reject(new Error('Unknown endpoint'));
      });

      render(<ChessGame />);

      await waitForChessGameReady();

      // Make a move to trigger evaluation
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await user.click(e2Square);
        await user.click(e4Square);
      }

      // Wait for the error to be handled
      await waitFor(() => {
        expect(consoleErrorSpy).toHaveBeenCalledWith(
          'Error fetching evaluation:',
          expect.any(Error)
        );
      }, { timeout: 5000 });

      // App should continue functioning
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();
    });

    test('should handle malformed API response', async () => {
      // Setup fetch to return invalid data
      global.fetch = jest.fn().mockImplementation((url: string | URL | Request) => {
        if (typeof url === 'string' && url.includes('/get_move')) {
          return Promise.resolve({
            ok: true,
            json: () => Promise.resolve({ /* missing move field */ thinking_time: 0 })
          } as Response);
        }
        if (typeof url === 'string' && url.includes('/evaluate_position')) {
          return Promise.resolve({
            ok: true,
            json: () => Promise.resolve({ evaluation: 0 })
          } as Response);
        }
        return Promise.reject(new Error('Unknown endpoint'));
      });

      render(<ChessGame />);

      await waitForChessGameReady();

      // Make a move to trigger bot response
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await user.click(e2Square);
        await user.click(e4Square);
      }

      // Wait for move count to confirm move was made
      await waitFor(() => {
        expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(1);
      }, { timeout: 5000 });

      // Game should handle missing move gracefully
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();
    });

    test('should handle API timeout', async () => {
      // Setup fetch to timeout
      global.fetch = jest.fn().mockImplementation((url: string | URL | Request) => {
        if (typeof url === 'string' && url.includes('/get_move')) {
          return new Promise((_, reject) => {
            setTimeout(() => reject(new Error('Request timeout')), 100);
          });
        }
        if (typeof url === 'string' && url.includes('/evaluate_position')) {
          return Promise.resolve({
            ok: true,
            json: () => Promise.resolve({ evaluation: 0 })
          } as Response);
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

      // Wait for timeout error
      await waitFor(() => {
        expect(consoleErrorSpy).toHaveBeenCalledWith(
          'Error fetching move from backend:',
          expect.any(Error)
        );
      }, { timeout: 3000 });
    });
  });

  describe('Game State Error Handling', () => {
    test('should handle invalid move attempts', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      const initialMoveCount = getCurrentMoveCount();

      // Try to move a pawn backwards (invalid)
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e1Square = chessboard.querySelector('[data-square="e1"]');

      if (e2Square && e1Square) {
        await user.click(e2Square);
        await user.click(e1Square); // Invalid - pawn can't move backwards
      }

      // Move count should not change
      expect(getCurrentMoveCount()).toBe(initialMoveCount);

      // App should still be functional
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();
    });

    test('should handle invalid AI move', async () => {
      // Setup fetch to return an invalid move
      global.fetch = jest.fn().mockImplementation((url: string | URL | Request) => {
        if (typeof url === 'string' && url.includes('/get_move')) {
          return Promise.resolve({
            ok: true,
            json: () => Promise.resolve({ move: 'Ke9', thinking_time: 0 }) // Invalid square
          });
        }
        if (typeof url === 'string' && url.includes('/evaluate_position')) {
          return Promise.resolve({
            ok: true,
            json: () => Promise.resolve({ evaluation: 0 })
          } as Response);
        }
        return Promise.reject(new Error('Unknown endpoint'));
      });

      render(<ChessGame />);

      await waitForChessGameReady();

      // Make a move to trigger bot response
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await user.click(e2Square);
        await user.click(e4Square);
      }

      // Wait for error handling
      await waitFor(() => {
        expect(consoleErrorSpy).toHaveBeenCalledWith(
          'Error applying AI move:',
          expect.any(Error)
        );
      }, { timeout: 5000 });
    });

    test('should handle corrupted FEN strings', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Get initial position
      const chessboard = screen.getByTestId('chessboard');
      const initialPosition = chessboard.getAttribute('data-position');

      // Open FEN dialog
      const fenButton = screen.getByRole('button', { name: /^FEN$/i });
      await user.click(fenButton);

      await waitFor(() => {
        expect(screen.getByPlaceholderText('Paste FEN string here')).toBeInTheDocument();
      });

      // Try to load invalid FEN
      const fenInput = screen.getByPlaceholderText('Paste FEN string here');
      const loadButton = screen.getByRole('button', { name: /Load Position/i });

      await user.clear(fenInput);
      await user.type(fenInput, 'invalid-fen-string');
      await user.click(loadButton);

      // Dialog should close after clicking load
      await waitFor(() => {
        expect(screen.queryByPlaceholderText('Paste FEN string here')).not.toBeInTheDocument();
      }, { timeout: 5000 });

      // Should still be functional with original position (invalid FEN was silently rejected)
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();

      // Position should remain unchanged (starting position)
      const finalPosition = chessboard.getAttribute('data-position');
      expect(finalPosition).toBe(initialPosition);
    });

    test('should handle bot making invalid moves', async () => {
      // Setup fetch to return an invalid move
      global.fetch = jest.fn().mockImplementation((url: string | URL | Request) => {
        if (typeof url === 'string' && url.includes('/get_move')) {
          return Promise.resolve({
            ok: true,
            json: () => Promise.resolve({ move: 'InvalidMove', thinking_time: 0 })
          } as Response);
        }
        if (typeof url === 'string' && url.includes('/evaluate_position')) {
          return Promise.resolve({
            ok: true,
            json: () => Promise.resolve({ evaluation: 0 })
          } as Response);
        }
        return Promise.reject(new Error('Unknown endpoint'));
      });

      render(<ChessGame />);

      await waitForChessGameReady();

      // Make a move to trigger bot response
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await user.click(e2Square);
        await user.click(e4Square);
      }

      // Wait for error handling
      await waitFor(() => {
        expect(consoleErrorSpy).toHaveBeenCalledWith(
          'Error applying AI move:',
          expect.any(Error)
        );
      }, { timeout: 5000 });

      // Game should still show the user's move
      expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(1);
    });
  });

  describe('Clock Error Handling', () => {
    test.skip('should handle clock running out gracefully', async () => {
      // FIXME: This test needs to properly test clock expiration
      // It should:
      // 1. Set up a game with very low clock time
      // 2. Make a move to start the clock
      // 3. Wait for the clock to expire
      // 4. Verify the game ends with proper time forfeit message
      // 5. Verify no errors are thrown during the process
      //
      // Current challenges:
      // - React 18's concurrent features make timer testing complex
      // - Need to mock or control the clock interval updates
    });

    test('should handle clock cleanup on unmount', async () => {
      const { unmount } = render(<ChessGame />);

      await waitForChessGameReady();

      // Unmount should clean up intervals without errors
      await act(async () => {
        unmount();
      });

      // Check that no errors were thrown other than the expected React 18 act warning
      const errorCalls = consoleErrorSpy.mock.calls;
      const nonActErrors = errorCalls.filter((call: unknown[]) => {
        const errorMsg = call[0]?.toString() || '';
        return !errorMsg.includes('act()');
      });

      // Log any non-act errors for debugging
      if (nonActErrors.length > 0) {
        console.log('Non-act errors found:', nonActErrors);
      }

      // Should have no errors besides potential act warnings
      expect(nonActErrors.length).toBe(0);
    });
  });

  describe('Edge Case Error Handling', () => {
    test('should handle empty bot response', async () => {
      // Setup fetch to return empty response
      global.fetch = jest.fn().mockImplementation((url: string | URL | Request) => {
        if (typeof url === 'string' && url.includes('/get_move')) {
          return Promise.resolve({
            ok: true,
            json: () => Promise.resolve({ thinking_time: 0 })  // Include thinking_time to prevent delays
          });
        }
        if (typeof url === 'string' && url.includes('/evaluate_position')) {
          return Promise.resolve({
            ok: true,
            json: () => Promise.resolve({ evaluation: 0 })
          } as Response);
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

      // Should handle empty response gracefully - no bot move since no 'move' field
      await waitFor(() => {
        expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(1);
      });
    });

    test('should handle game over during bot thinking', async () => {
      // Setup immediate bot response (removed setTimeout delay)
      global.fetch = jest.fn().mockImplementation((url: string | URL | Request) => {
        if (typeof url === 'string' && url.includes('/get_move')) {
          return Promise.resolve({
            ok: true,
            json: () => Promise.resolve({ move: 'e5', thinking_time: 0 })
          } as Response);
        }
        if (typeof url === 'string' && url.includes('/evaluate_position')) {
          return Promise.resolve({
            ok: true,
            json: () => Promise.resolve({ evaluation: 0 })
          } as Response);
        }
        return Promise.reject(new Error('Unknown endpoint'));
      });

      render(<ChessGame />);

      await waitForChessGameReady();

      // Load a position near checkmate using loadFEN helper
      const nearCheckmateFEN = 'rnb1kbnr/pppp1ppp/8/4p3/5PPq/8/PPPPP2P/RNBQKBNR w KQkq - 1 3';

      await act(async () => {
        await loadFEN(nearCheckmateFEN, user);
      });

      // Game should detect checkmate
      await waitFor(() => {
        expect(screen.getByText(/Checkmate!.*wins!/)).toBeInTheDocument();
      });
    });

    test('should handle missing getComputedStyle gracefully', async () => {
      // Disable bot moves for this test
      setupFetchMocks({ disableBotMoves: true });

      // Save original getComputedStyle
      const originalGetComputedStyle = window.getComputedStyle;

      // Make getComputedStyle return empty object (simulating partial failure)
      window.getComputedStyle = jest.fn(() => ({
        getPropertyValue: jest.fn(() => '')
      })) as unknown as typeof window.getComputedStyle;

      render(<ChessGame />);

      await waitForChessGameReady();

      // Should use CSS fallback values
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

      // Restore
      window.getComputedStyle = originalGetComputedStyle;
    });
  });
});
