/* eslint-env jest */
import { screen, waitFor } from '@testing-library/react';
import { Chess } from 'chess.js';
import type { Square, Move } from 'chess.js';
import type { UserEvent } from '@testing-library/user-event';

declare global {
  var suppressConsoleErrors: ((patterns: string[]) => void) | undefined;
  var restoreConsole: (() => void) | undefined;
}

interface ResetGameOptions {
  timeControl?: string;
  humanRating?: number;
  botRating?: number;
}

interface SetupFetchMocksOptions {
  botMove?: string;
  thinkingTime?: number;
  evaluation?: number;
  bestMove?: string;
  principalVariation?: string[];
  depth?: number;
  botMoves?: string[] | null;
  disableBotMoves?: boolean;
}

/**
 * Helper to wait for the ChessGame component to be fully rendered
 * Use this instead of waiting for "Chessmimic MVP" which only exists in the loading state
 */
export async function waitForChessGameReady(): Promise<void> {
  await waitFor(() => {
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });
}

/**
 * Helper to make a move by clicking on board squares
 * Parses SAN notation and converts to square clicks
 * @param {string} move - The move in algebraic notation (e.g., 'e4', 'Nf3')
 * @param {object} user - The userEvent instance
 * @param {string} fen - Optional current FEN position (defaults to starting position)
 */
export async function makeMove(move: string, user: UserEvent, fen: string = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1'): Promise<void> {
  // Use chess.js to parse the move and get the from/to squares
  const chess = new Chess(fen);
  const parsedMove = chess.move(move);

  if (!parsedMove) {
    throw new Error(`Invalid move: ${move} in position ${fen}`);
  }

  // Use makeMoveBySquares to click on the from and to squares
  await makeMoveBySquares(parsedMove.from, parsedMove.to, user);
}

/**
 * Helper to make a move by clicking on squares (alternative method)
 * Note: This method is less reliable and depends on board implementation
 * @param {string} from - Source square (e.g., 'e2')
 * @param {string} to - Destination square (e.g., 'e4')
 * @param {object} user - The userEvent instance
 */
export async function makeMoveBySquares(from: string, to: string, user: UserEvent): Promise<void> {
  const board = screen.getByTestId('chessboard');
  
  // Find the source square and click it
  const fromSquare = board.querySelector(`[data-square="${from}"]`);
  if (!fromSquare) {
    throw new Error(`Square ${from} not found`);
  }
  
  await user.click(fromSquare);
  
  // Find the destination square and click it
  const toSquare = board.querySelector(`[data-square="${to}"]`);
  if (!toSquare) {
    throw new Error(`Square ${to} not found`);
  }
  
  await user.click(toSquare);
}

/**
 * Helper to get current move count from the game state
 * Uses window.__chessAppState exposed by ChessGame component
 * @returns {number} The current move count
 */
export function getCurrentMoveCount(): number {
  // Try to get from window.__chessAppState first (exposed by ChessGame)
  if (typeof window !== 'undefined' && window.__chessAppState) {
    return window.__chessAppState.moveHistory?.length || 0;
  }
  // Fallback: count moves in the move list
  const moveList = screen.queryByTestId('move-list');
  if (moveList) {
    // Count all move notation spans (excluding move numbers)
    const moves = moveList.querySelectorAll('[class*="bg-info"], [class*="bg-secondary"]');
    // Filter out the "Start Position" button
    const actualMoves = Array.from(moves).filter(el => !el.textContent?.includes('Start'));
    return actualMoves.length;
  }
  return 0;
}

/**
 * Helper to get current view index from the game state
 * Uses window.__chessAppState exposed by ChessGame component
 * @returns {number|null} The current view index (-1 for start position)
 */
export function getCurrentViewIndex(): number | null {
  // Try to get from window.__chessAppState first (exposed by ChessGame)
  if (typeof window !== 'undefined' && window.__chessAppState) {
    return window.__chessAppState.viewedPlyIndex ?? null;
  }
  // Fallback: find the highlighted element in the move list
  const moveList = screen.queryByTestId('move-list');
  if (moveList) {
    // Check if "Start Position" or "Start" is highlighted (fw-bold + bg-primary indicates start position selected)
    const startPositionHighlighted = moveList.querySelector('.fw-bold.bg-primary');
    if (startPositionHighlighted && (startPositionHighlighted.textContent?.includes('Start') || startPositionHighlighted.textContent?.includes('Position'))) {
      return -1;
    }

    // Otherwise find the highlighted move
    const highlightedMove = moveList.querySelector('.fw-bold.bg-info');
    if (highlightedMove) {
      // Count moves before the highlighted one
      const allMoves = moveList.querySelectorAll('[class*="bg-info"], [class*="bg-secondary"]');
      const movesArray = Array.from(allMoves).filter(el => !el.textContent?.includes('Start'));
      const index = movesArray.indexOf(highlightedMove as Element);
      return index >= 0 ? index : null;
    }
  }
  return null;
}

/**
 * Helper to parse time string to seconds
 * @param {string | null} timeStr - Time string in format "MM:SS" or null
 * @returns {number} Time in seconds
 */
export function parseTimeToSeconds(timeStr: string | null): number {
  if (!timeStr) return 0;
  const [minutes, seconds] = timeStr.split(':').map(Number);
  return minutes * 60 + seconds;
}

/**
 * Helper to format seconds to time string
 * @param {number} seconds - Time in seconds
 * @returns {string} Time string in format "MM:SS"
 */
export function formatTime(seconds: number): string {
  const minutes = Math.floor(seconds / 60);
  const secs = seconds % 60;
  return `${minutes}:${secs.toString().padStart(2, '0')}`;
}

/**
 * Helper to get time from clock display
 * @param {string} color - 'white' or 'black'
 * @returns {number} Time in seconds
 */
export function getClockTime(color: string): number {
  const clockElement = screen.getByTestId(`${color}-clock`);
  const timeSpan = clockElement.querySelector('span');
  return parseTimeToSeconds(timeSpan?.textContent || '0:00');
}

/**
 * Helper to wait for a specific move count
 * @param {number} expectedCount - The expected move count
 * @param {number} timeout - Maximum wait time in ms (default: 5000)
 */
export async function waitForMoveCount(expectedCount: number, timeout: number = 5000): Promise<void> {
  await waitFor(() => {
    expect(getCurrentMoveCount()).toBe(expectedCount);
  }, { timeout });
}

/**
 * Helper to navigate to a specific move in history
 * @param {number} moveIndex - The move index to navigate to
 * @param {object} user - The userEvent instance
 */
export async function navigateToMove(moveIndex: number, user: UserEvent): Promise<void> {
  const moveElements = screen.getAllByText(/^\d+\./);
  if (moveIndex >= 0 && moveIndex < moveElements.length) {
    const parentEl = moveElements[moveIndex].parentElement;
    if (parentEl) {
      const moves = parentEl.querySelectorAll('button');
      if (moves.length > 0) {
        await user.click(moves[0]);
      }
    }
  }
}

/**
 * Helper to get highlighted squares (for legal moves)
 * @returns {string[]} Array of square names that are highlighted
 */
export function getHighlightedSquares(): string[] {
  const board = screen.getByTestId('chessboard');
  const highlightedElements = board.querySelectorAll('[data-highlighted="true"], .highlight-legal-move');
  return Array.from(highlightedElements)
    .map(el => el.getAttribute('data-square'))
    .filter((square): square is string => square !== null);
}

/**
 * Helper to simulate drag and drop (using mouse events since DragEvent is not available in JSDOM)
 * @param {HTMLElement} source - Source element
 * @param {HTMLElement} target - Target element
 */
export function simulateDragAndDrop(source: HTMLElement, target: HTMLElement): void {
  // Since DragEvent is not available in JSDOM, we'll use mouse events instead
  const mouseDownEvent = new MouseEvent('mousedown', { 
    bubbles: true,
    clientX: 100,
    clientY: 100
  });
  
  const mouseMoveEvent = new MouseEvent('mousemove', {
    bubbles: true,
    clientX: 200,
    clientY: 200
  });
  
  const mouseUpEvent = new MouseEvent('mouseup', {
    bubbles: true,
    clientX: 200,
    clientY: 200
  });
  
  source.dispatchEvent(mouseDownEvent);
  target.dispatchEvent(mouseMoveEvent);
  target.dispatchEvent(mouseUpEvent);
}

/**
 * Helper to check if a position is in check
 * @param {string} fen - The FEN string to check
 * @returns {boolean} True if the position is in check
 */
export function isPositionInCheck(fen: string): boolean {
  const chess = new Chess(fen);
  return chess.isCheck();
}

/**
 * Helper to check if a position is checkmate
 * @param {string} fen - The FEN string to check
 * @returns {boolean} True if the position is checkmate
 */
export function isPositionCheckmate(fen: string): boolean {
  const chess = new Chess(fen);
  return chess.isCheckmate();
}

/**
 * Helper to check if a position is stalemate
 * @param {string} fen - The FEN string to check
 * @returns {boolean} True if the position is stalemate
 */
export function isPositionStalemate(fen: string): boolean {
  const chess = new Chess(fen);
  return chess.isStalemate();
}

/**
 * Helper to get legal moves for a square
 * @param {string} square - The square to check (e.g., 'e2')
 * @param {string} fen - The current FEN position
 * @returns {string[]} Array of legal destination squares
 */
export function getLegalMovesForSquare(square: string, fen: string): string[] {
  const chess = new Chess(fen);
  const moves = chess.moves({ square: square as Square, verbose: true });
  return moves.map((move: Move) => move.to);
}

/**
 * Helper to reset the game via New Game dialog
 * @param {object} user - The userEvent instance
 * @param {object} options - Optional game settings
 */
export async function resetGame(user: UserEvent, options: ResetGameOptions = {}): Promise<void> {
  // Click New Game button
  const newGameButton = screen.getByText('New Game');
  await user.click(newGameButton);
  
  // Wait for dialog
  await waitFor(() => {
    expect(screen.getByText('Start Game')).toBeInTheDocument();
  });
  
  // Apply any custom settings if provided
  if (options.timeControl) {
    const timeSelect = screen.getByLabelText('Time Control');
    await user.selectOptions(timeSelect, options.timeControl);
  }
  
  if (options.humanRating) {
    const humanRatingInput = screen.getByLabelText('Your Rating');
    await user.clear(humanRatingInput);
    await user.type(humanRatingInput, options.humanRating.toString());
  }
  
  if (options.botRating) {
    const botRatingInput = screen.getByLabelText('Bot Rating');
    await user.clear(botRatingInput);
    await user.type(botRatingInput, options.botRating.toString());
  }
  
  // Click Start Game
  const startGameButton = screen.getByText('Start Game');
  await user.click(startGameButton);
  
  // Wait for dialog to close
  await waitFor(() => {
    expect(screen.queryByText('Start Game')).not.toBeInTheDocument();
  });
}

/**
 * Helper to load a FEN position
 * Opens the FEN dialog, enters the FEN, and loads it
 * @param {string} fen - The FEN string to load
 * @param {object} user - The userEvent instance
 */
export async function loadFEN(fen: string, user: UserEvent): Promise<void> {
  // First click the FEN button to open the dialog
  const fenButton = screen.getByRole('button', { name: /^FEN$/i });
  await user.click(fenButton);

  // Wait for the dialog to appear
  await waitFor(() => {
    expect(screen.getByPlaceholderText('Paste FEN string here')).toBeInTheDocument();
  });

  // Find the input and load button in the dialog
  const fenInput = screen.getByPlaceholderText('Paste FEN string here');
  const loadButton = screen.getByRole('button', { name: /Load Position/i });

  // Enter the FEN and click load
  await user.clear(fenInput);
  await user.type(fenInput, fen);
  await user.click(loadButton);

  // Wait for the dialog to close
  await waitFor(() => {
    expect(screen.queryByPlaceholderText('Paste FEN string here')).not.toBeInTheDocument();
  });
}

/**
 * Helper to trigger keyboard navigation
 * @param {string} key - The key to press (e.g., 'ArrowLeft', 'ArrowRight')
 * @param {object} user - The userEvent instance
 */
export async function navigateWithKeyboard(key: string, user: UserEvent): Promise<void> {
  await user.keyboard(`{${key}}`);
}

/**
 * Helper to get the current FEN from the game state
 * Uses window.__chessAppState exposed by ChessGame component
 * @returns {string} The current FEN string
 */
export function getCurrentFEN(): string {
  // Try to get from window.__chessAppState first (exposed by ChessGame)
  if (typeof window !== 'undefined' && window.__chessAppState) {
    return window.__chessAppState.fen || '';
  }
  // Fallback: get from chessboard data attribute
  const chessboard = screen.queryByTestId('chessboard');
  if (chessboard) {
    return chessboard.getAttribute('data-position') || '';
  }
  return '';
}

/**
 * Mock fetch responses for bot moves and evaluation
 * @param {object} options - Options for the mock
 */
export function setupFetchMocks(options: SetupFetchMocksOptions = {}): { resetMoveCounter: () => void } {
  const {
    botMove = 'e5',
    thinkingTime = 0,  // Changed default from 0.5 to 0 to prevent test delays
    evaluation = 0.0,
    bestMove = 'e2e4',
    principalVariation = ['e2e4', 'e7e5'],
    depth = 10,
    botMoves = null, // Array of moves for sequential responses
    disableBotMoves = false // New option to prevent bot moves entirely
  } = options;
  
  let moveCounter = 0;
  
  global.fetch = jest.fn((url: string) => {
    if (url.includes('/evaluate_position')) {
      return Promise.resolve({
        ok: true,
        json: () => Promise.resolve({ 
          evaluation,
          best_move: bestMove,
          principal_variation: principalVariation,
          depth
        }),
      } as Response);
    }
    
    if (url.includes('/get_move')) {
      // If bot moves are disabled, return a rejected promise
      if (disableBotMoves) {
        return Promise.reject(new Error('Bot moves disabled for testing'));
      }
      
      const currentMove = botMoves ? botMoves[moveCounter % botMoves.length] : botMove;
      moveCounter++;
      
      return Promise.resolve({
        ok: true,
        json: () => Promise.resolve({ 
          move: currentMove,
          thinking_time: thinkingTime
        }),
      } as Response);
    }
    
    return Promise.reject(new Error('Unknown endpoint'));
  }) as jest.MockedFunction<typeof fetch>;
  
  return { resetMoveCounter: () => { moveCounter = 0; } };
}

/**
 * Suppress expected console errors during testing
 */
export function suppressExpectedErrors(): void {
  // Use global suppression utilities from jest-setup.js
  if (global.suppressConsoleErrors) {
    global.suppressConsoleErrors([
      // React 18 act() warnings (already suppressed globally, but included for clarity)
      'The current testing environment is not configured to support act',
      
      // Expected chess move errors (these are intentional test cases)
      'Error executing move: Invalid move:',
      'Invalid move:',
      
      // Expected FEN validation errors (intentional test cases)
      'Invalid FEN string',
      
      // Expected evaluation fetch errors (mock failures)
      'Error fetching evaluation:',
      
      // Expected network/API errors during testing
      'Network error',
      'Unknown endpoint'
    ]);
  }
}

/**
 * Allow specific error messages to show through (for debugging)
 */
export function allowSpecificErrors(patterns: string[]): void {
  if (global.restoreConsole) {
    global.restoreConsole();
  }
  if (global.suppressConsoleErrors) {
    // Re-suppress everything except the specified patterns
    const defaultPatterns = [
      'The current testing environment is not configured to support act'
    ];
    const filteredPatterns = defaultPatterns.filter(pattern => 
      !patterns.some(allowed => pattern.includes(allowed))
    );
    global.suppressConsoleErrors(filteredPatterns);
  }
}

/**
 * Helper to disable bot moves for a test
 * @returns {object} Mock fetch setup that prevents bot moves
 */
export function disableBotMoves(): { resetMoveCounter: () => void } {
  return setupFetchMocks({ disableBotMoves: true });
}

/**
 * Helper to wait for the game to stabilize (no pending bot moves)
 * @param {number} timeout - Maximum wait time in ms (default: 1000)
 */
export async function waitForGameStable(timeout: number = 1000): Promise<void> {
  // Wait a bit to ensure no pending bot moves
  await waitFor(() => {
    // Check that no loading indicators are present
    const loadingElements = screen.queryAllByText(/thinking|loading|calculating/i);
    expect(loadingElements).toHaveLength(0);
  }, { timeout });
  
  // Additional small delay to ensure stability
  await new Promise(resolve => setTimeout(resolve, 100));
}

/**
 * Enhanced timer cleanup for tests using fake timers
 */
export function cleanupTimers(): void {
  // Run all pending timers
  if (jest.isMockFunction(setTimeout)) {
    jest.runOnlyPendingTimers();
  }
  
  // Clear all timers
  jest.clearAllTimers();
  
  // If using fake timers, restore them
  if (jest.isMockFunction(setTimeout)) {
    jest.useRealTimers();
  }
}

/**
 * Helper to properly advance timers with async operations
 * @param {number} ms - Time to advance in milliseconds
 */
export async function advanceTimersAndFlush(ms: number): Promise<void> {
  // Advance timers
  jest.advanceTimersByTime(ms);
  
  // Flush promises
  await Promise.resolve();
  
  // Run any immediate timers that were scheduled
  jest.runOnlyPendingTimers();
  
  // Flush promises again
  await Promise.resolve();
}

/**
 * Helper to wait for element with better error handling
 * @param {Function} callback - Function that returns the element
 * @param {object} options - waitFor options
 */
export async function waitForElement<T>(callback: () => T, options: Record<string, unknown> = {}): Promise<T> {
  try {
    return await waitFor(callback, { timeout: 5000, ...options });
  } catch (error) {
    // Provide better error context
    const currentHTML = document.body.innerHTML;
    console.error('Element not found. Current DOM:', currentHTML);
    throw error;
  }
}

/**
 * Helper to get move notation from move list
 * @param {number} moveNumber - The move number (1-based)
 * @param {string} color - 'white' or 'black'
 * @returns {string|null} The move notation or null if not found
 */
export function getMoveNotation(moveNumber: number, color: string): string | null {
  const moveList = screen.getByTestId('move-list');
  const moveElements = moveList.querySelectorAll('.move-number, .move-notation');
  
  for (let i = 0; i < moveElements.length; i++) {
    const element = moveElements[i];
    if (element.classList.contains('move-number') && element.textContent?.includes(`${moveNumber}.`)) {
      // Found the move number, now get the notation
      const whiteMove = moveElements[i + 1];
      const blackMove = moveElements[i + 2];
      
      if (color === 'white' && whiteMove && whiteMove.classList.contains('move-notation')) {
        return whiteMove.textContent?.trim() || null;
      } else if (color === 'black' && blackMove && blackMove.classList.contains('move-notation')) {
        return blackMove.textContent?.trim() || null;
      }
    }
  }
  
  return null;
}