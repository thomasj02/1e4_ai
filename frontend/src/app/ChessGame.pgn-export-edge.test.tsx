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
  getCurrentMoveCount
} from './ChessGame.test.utils';

// Increase timeout for these tests as they involve complex async operations
jest.setTimeout(10000);

describe('ChessGame Component - PGN Export Edge Cases', () => {
  let user: UserEvent;
  let mockBlob: jest.Mock<void, [content: string, options?: BlobPropertyBag]>;

  beforeEach(() => {
    user = userEvent.setup();
    setupFetchMocks({
      botMove: 'e5',
      thinkingTime: 0.5
    });
    suppressExpectedErrors();

    // Mock Blob constructor to capture PGN content
    mockBlob = jest.fn<void, [content: string, options?: BlobPropertyBag]>();
    global.Blob = jest.fn((content?: BlobPart[], options?: BlobPropertyBag) => {
      if (content && content[0]) {
        mockBlob(String(content[0]), options);
        return { size: String(content[0]).length } as Blob;
      }
      return { size: 0 } as Blob;
    }) as unknown as typeof Blob;

    // Mock URL methods
    global.URL.createObjectURL = jest.fn(() => 'blob:mock-url');
    global.URL.revokeObjectURL = jest.fn();

    // Mock anchor element click
    HTMLAnchorElement.prototype.click = jest.fn();
  });

  afterEach(() => {
    cleanupTimers();
    jest.clearAllMocks();
    jest.restoreAllMocks();
    if (global.restoreConsole) {
      global.restoreConsole();
    }
  });

  describe('Custom FEN position headers', () => {
    test('should include FEN header when game starts from custom position', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Load a custom FEN position
      const customFEN = 'rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2';

      await act(async () => {
        await loadFEN(customFEN, user);
      });

      // Make a move in the custom position using board squares
      const chessboard = screen.getByTestId('chessboard');
      const g1Square = chessboard.querySelector('[data-square="g1"]');
      const f3Square = chessboard.querySelector('[data-square="f3"]');

      if (g1Square && f3Square) {
        await user.click(g1Square);
        await user.click(f3Square);
      }

      await waitFor(() => {
        expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(1);
      });

      // Export PGN
      const exportButton = screen.getByText('Export PGN');
      await user.click(exportButton);

      // Verify PGN includes FEN header
      expect(mockBlob).toHaveBeenCalled();
      const pgnContent = mockBlob.mock.calls[0][0];
      expect(pgnContent).toContain('[FEN "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2"]');
      expect(pgnContent).toContain('[SetUp "1"]');
    });

    test('should handle moveHistory with customStartFen property', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Load position and make moves to create moveHistory
      const customFEN = 'r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3';

      await act(async () => {
        await loadFEN(customFEN, user);
      });

      // Export PGN
      const exportButton = screen.getByText('Export PGN');
      await user.click(exportButton);

      const pgnContent = mockBlob.mock.calls[0][0];
      // Should detect non-standard starting position
      expect(pgnContent).toMatch(/\[FEN ".*"\]/);
      expect(pgnContent).toContain('[SetUp "1"]');
    });
  });

  describe('Game termination scenarios', () => {
    test.skip('should include time forfeit termination header', async () => {
      // FIXME: This test needs to properly test PGN export for time forfeit
      // It should:
      // 1. Create a game that ends by time forfeit
      // 2. Export the PGN
      // 3. Verify the PGN includes [Termination "Time forfeit"]
      // 4. Verify the correct result (1-0 or 0-1) based on who ran out of time
      //
      // Current challenges:
      // - Need to trigger actual time forfeit condition
      // - React 18's concurrent features make timer testing complex
      // - May need to mock the clock management logic
    });

    test('should include checkmate termination header', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Load a checkmate position
      // Fool's mate position (black wins)
      const checkmateFEN = 'rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3';

      await act(async () => {
        await loadFEN(checkmateFEN, user);
      });

      // Wait for checkmate to be detected (the component should detect it immediately)
      await waitFor(() => {
        // The component shows checkmate in the game state display
        const gameState = screen.getByText((content, element) => {
          return element !== null && content.includes('Checkmate');
        });
        expect(gameState).toBeInTheDocument();
      });

      // Export PGN
      const exportButton = screen.getByText('Export PGN');
      await user.click(exportButton);

      // Verify checkmate headers
      expect(mockBlob).toHaveBeenCalled();
      const pgnContent = mockBlob.mock.calls[0][0];
      expect(pgnContent).toContain('[Termination "Normal"]');
      expect(pgnContent).toContain('[Result "0-1"]'); // Black wins
    });

    test('should handle draw result correctly', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Load a stalemate position
      const stalemateFEN = '7k/5Q2/6K1/8/8/8/8/8 b - - 0 1';

      await act(async () => {
        await loadFEN(stalemateFEN, user);
      });

      // Wait for stalemate to be detected (shows as "Draw!")
      await waitFor(() => {
        expect(screen.getByText('Draw!')).toBeInTheDocument();
      });

      // Export PGN
      const exportButton = screen.getByText('Export PGN');
      await user.click(exportButton);

      // Verify draw result
      expect(mockBlob).toHaveBeenCalled();
      const pgnContent = mockBlob.mock.calls[0][0];
      expect(pgnContent).toContain('[Result "1/2-1/2"]');
    });

    test('should handle unfinished game result', async () => {
      // Disable bot moves for this test
      disableBotMoves();

      render(<ChessGame />);

      await waitForChessGameReady();

      // Make a move but don't finish the game - using board squares
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

      // Export PGN
      const exportButton = screen.getByText('Export PGN');
      await user.click(exportButton);

      // Verify unfinished game result
      expect(mockBlob).toHaveBeenCalled();
      const pgnContent = mockBlob.mock.calls[0][0];
      expect(pgnContent).toContain('[Result "*"]');
    });
  });

  describe('Clock time edge cases', () => {
    test('should handle missing clock times gracefully', async () => {
      // Disable bot moves for this test
      disableBotMoves();

      render(<ChessGame />);

      await waitForChessGameReady();

      // Load a position and immediately export without moves
      const exportButton = screen.getByText('Export PGN');
      await user.click(exportButton);

      // Should export without errors
      expect(mockBlob).toHaveBeenCalled();
      const pgnContent = mockBlob.mock.calls[0][0];
      expect(pgnContent).toContain('[Event "ChessMimic Game"]');
      // Should not contain clock annotations when no moves made
      expect(pgnContent).not.toContain('[%clk');
    });

    test('should handle partial clock data', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Make moves using board squares
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
        expect(getCurrentMoveCount()).toBe(2);
      }, { timeout: 3000 });

      // Make another move
      const g1Square = chessboard.querySelector('[data-square="g1"]');
      const f3Square = chessboard.querySelector('[data-square="f3"]');

      if (g1Square && f3Square) {
        await user.click(g1Square);
        await user.click(f3Square);
      }

      await waitFor(() => {
        expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(3);
      });

      // Export PGN
      const exportButton = screen.getByText('Export PGN');
      await user.click(exportButton);

      // Verify clock annotations exist for moves with clock data
      expect(mockBlob).toHaveBeenCalled();
      const pgnContent = mockBlob.mock.calls[0][0];
      expect(pgnContent).toMatch(/\{\[%clk \d:\d{2}:\d{2}\]\}/);
    });
  });

  describe('Move formatting edge cases', () => {
    test('should handle last move by white correctly', async () => {
      // Disable bot moves for this test
      disableBotMoves();

      render(<ChessGame />);

      await waitForChessGameReady();

      // Make an odd number of moves (white's last move) using board squares
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

      // Export PGN
      const exportButton = screen.getByText('Export PGN');
      await user.click(exportButton);

      // Verify move formatting
      const pgnContent = mockBlob.mock.calls[0][0];
      // Should have proper move numbering and spacing
      expect(pgnContent).toMatch(/1\. e4 \{\[%clk.*?\]\}/);
    });

    test('should handle multiple move pairs correctly', async () => {
      // Disable bot moves for this test
      disableBotMoves();

      render(<ChessGame />);

      await waitForChessGameReady();

      // Load a position where we can make multiple moves
      // Position after 1.e4 e5 (so we can continue with 2.Nf3)
      const midGameFEN = 'rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2';

      await act(async () => {
        await loadFEN(midGameFEN, user);
      });

      // Export PGN
      const exportButton = screen.getByText('Export PGN');
      await user.click(exportButton);

      // Verify PGN includes proper FEN header
      const pgnContent = mockBlob.mock.calls[0][0];
      expect(pgnContent).toContain('[FEN "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2"]');
      expect(pgnContent).toContain('[SetUp "1"]');
    });
  });

  describe('Player color variations', () => {
    test('should correctly identify players in PGN export', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      // Export initial PGN to check default player assignments
      const exportButton = screen.getByText('Export PGN');
      await user.click(exportButton);

      // Verify default player assignments (human as white)
      expect(mockBlob).toHaveBeenCalled();
      const pgnContent = mockBlob.mock.calls[0][0];
      expect(pgnContent).toContain('[White "Human"]');
      expect(pgnContent).toContain('[Black "ChessMimic Bot (1800)"]');
    });
  });

  describe('Error handling in PGN generation', () => {
    test('should handle PGN generation errors gracefully', async () => {
      // Disable bot moves for this test
      disableBotMoves();

      // Mock console.error to verify error handling
      const consoleError = jest.spyOn(console, 'error').mockImplementation();

      render(<ChessGame />);

      await waitForChessGameReady();

      // Export PGN - should work even with potential errors
      const exportButton = screen.getByText('Export PGN');
      await user.click(exportButton);

      // Should create a PGN even if some parts fail
      expect(mockBlob).toHaveBeenCalled();
      const pgnContent = mockBlob.mock.calls[0][0];
      expect(pgnContent).toContain('[Event "ChessMimic Game"]');

      consoleError.mockRestore();
    });

    test('should handle empty move history', async () => {
      // Disable bot moves for this test
      disableBotMoves();

      render(<ChessGame />);

      await waitForChessGameReady();

      // Export without any moves
      const exportButton = screen.getByText('Export PGN');
      await user.click(exportButton);

      // Should still generate valid PGN headers
      expect(mockBlob).toHaveBeenCalled();
      const pgnContent = mockBlob.mock.calls[0][0];
      expect(pgnContent).toContain('[Event "ChessMimic Game"]');
      expect(pgnContent).toContain('[Result "*"]');
      // Should not crash on empty move list
      expect(pgnContent).toBeDefined();
    });
  });

  describe('Complex game scenarios', () => {
    test('should handle game with captures and checks', async () => {
      // Disable bot moves for this test
      disableBotMoves();

      render(<ChessGame />);

      await waitForChessGameReady();

      // Load a position with captures available
      const captureFEN = 'rnbqkb1r/pppp1ppp/5n2/4p3/3PP3/5N2/PPP2PPP/RNBQKB1R w KQkq - 2 4';

      await act(async () => {
        await loadFEN(captureFEN, user);
      });

      // Make a capture move using board squares (d4xe5)
      const chessboard = screen.getByTestId('chessboard');
      const d4Square = chessboard.querySelector('[data-square="d4"]');
      const e5Square = chessboard.querySelector('[data-square="e5"]');

      if (d4Square && e5Square) {
        await user.click(d4Square);
        await user.click(e5Square);
      }

      await waitFor(() => {
        expect(getCurrentMoveCount()).toBe(1);
      });

      // Export PGN
      const exportButton = screen.getByText('Export PGN');
      await user.click(exportButton);

      // Verify capture notation in PGN
      expect(mockBlob).toHaveBeenCalled();
      const pgnContent = mockBlob.mock.calls[0][0];
      expect(pgnContent).toMatch(/dxe5/);
    });

    test('should preserve all metadata in complex scenarios', async () => {
      // Disable bot moves for this test
      disableBotMoves();

      render(<ChessGame />);

      await waitForChessGameReady();

      // Change bot rating - be specific about which button
      const newGameButtons = screen.getAllByText('New Game');
      const primaryNewGameButton = newGameButtons.find(button =>
        button.classList.contains('btn-primary')
      );
      if (!primaryNewGameButton) throw new Error('Could not find primary New Game button');
      await user.click(primaryNewGameButton);

      // Wait for dialog to open
      await waitFor(() => {
        expect(screen.getByText('Start Game')).toBeInTheDocument();
      });

      // Find the rating input - specifically the number input (not the range)
      const ratingInputs = screen.getAllByDisplayValue('1800');
      const numberInput = ratingInputs.find(input => input.getAttribute('type') === 'number');
      if (!numberInput) throw new Error('Could not find number input');
      await user.clear(numberInput);
      await user.type(numberInput, '2200');

      const startButton = screen.getByText('Start Game');
      await user.click(startButton);

      // Give some time for dialog to process
      await new Promise(resolve => setTimeout(resolve, 100));

      // Make a move using board squares
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
      });

      // Export PGN
      const exportButton = screen.getByText('Export PGN');
      await user.click(exportButton);

      // Verify all metadata is preserved
      expect(mockBlob).toHaveBeenCalled();
      const pgnContent = mockBlob.mock.calls[0][0];
      expect(pgnContent).toContain('[White "Human"]');
      expect(pgnContent).toContain('[Black "ChessMimic Bot (2200)"]');
      expect(pgnContent).toContain('[TimeControl "300+3"]');
    });
  });
});
