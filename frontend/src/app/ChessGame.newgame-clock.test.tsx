/* eslint-env jest */
import React from 'react';
import { render, screen, waitFor, act } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
// UserEvent type is used in this file
import ChessGame from './ChessGame';
import {
  getCurrentMoveCount,
  setupFetchMocks,
  waitForChessGameReady
} from './ChessGame.test.utils';

describe('New Game Dialog - Clock Reset', () => {
  afterEach(() => {
    jest.clearAllMocks();
    jest.restoreAllMocks();
    jest.useRealTimers();
    if ((global as unknown as { performance?: { now?: { mockReturnValue?: unknown } } }).performance?.now?.mockReturnValue) {
      delete (global as unknown as { performance?: unknown }).performance;
    }
  });

  test('should reset clocks to selected time when starting new game after finished game', async () => {
    jest.useFakeTimers();
    const user = userEvent.setup({ advanceTimers: jest.advanceTimersByTime, delay: null });

    // Mock performance.now for time tracking
    global.performance = {
      now: jest.fn().mockReturnValue(0)
    } as unknown as Performance;

    // Setup fetch mocks
    setupFetchMocks({
      botMove: 'e5',
      thinkingTime: 1.0
    });
    render(<ChessGame />);

    await waitForChessGameReady();

    // Step 1: Play some moves to consume time
    // Make a move as white using board squares
    const chessboard = screen.getByTestId('chessboard');
    const e2Square = chessboard.querySelector('[data-square="e2"]');
    const e4Square = chessboard.querySelector('[data-square="e4"]');

    if (e2Square && e4Square) {
      await user.click(e2Square);
      await user.click(e4Square);
    }

    // Advance time by 10 seconds
    act(() => {
      ((global.performance.now as unknown) as jest.Mock).mockReturnValue(10000);
      jest.advanceTimersByTime(10000);
    });

    // Wait for bot to respond
    await waitFor(() => {
      expect(getCurrentMoveCount()).toBe(2);
    }, { timeout: 5000 });

    // Verify clocks have consumed time (5:00 - time used + increment)
    const whiteClockBefore = screen.getByTestId('white-time');
    const blackClockBefore = screen.getByTestId('black-time');

    // Store the current times to verify they change after new game
    const whiteTimeBefore = whiteClockBefore.textContent;
    const blackTimeBefore = blackClockBefore.textContent;

    // Times should not be the initial 05:00 anymore (format is mm:ss with leading zeros)
    expect(whiteTimeBefore).not.toBe('05:00');
    expect(blackTimeBefore).not.toBe('05:00');

    // Step 2: Open New Game dialog - be specific about which button
    const newGameButtons = screen.getAllByText('New Game');
    const newGameButton = newGameButtons.find(button =>
      button.classList.contains('btn-primary')
    ) || newGameButtons[0];
    await user.click(newGameButton);

    // Wait for dialog to appear
    await waitFor(() => {
      expect(screen.getByText('Start Game')).toBeInTheDocument();
      expect(screen.getByText('Time Control')).toBeInTheDocument();
    });

    // Step 3: Select different time control (3+0)
    const threeMinButton = screen.getByText('3+0').closest('button');
    await user.click(threeMinButton!);

    // Step 4: Start the new game
    const startButton = screen.getByText('Start Game');
    await user.click(startButton);

    // Step 5: Verify clocks are reset to 3:00 (format is mm:ss with leading zeros)
    await waitFor(() => {
      const whiteClockAfter = screen.getByTestId('white-time');
      const blackClockAfter = screen.getByTestId('black-time');

      expect(whiteClockAfter.textContent).toBe('03:00');
      expect(blackClockAfter.textContent).toBe('03:00');
    });

    // Step 6: Verify the game is actually reset (move count is 0)
    expect(getCurrentMoveCount()).toBe(0);
  });

  test('should stop running clocks when starting new game', async () => {
    jest.useFakeTimers();
    const user = userEvent.setup({ advanceTimers: jest.advanceTimersByTime, delay: null });

    // Mock performance.now for time tracking
    global.performance = {
      now: jest.fn().mockReturnValue(0)
    } as unknown as Performance;

    // Setup fetch mocks
    setupFetchMocks({
      botMove: 'e5',
      thinkingTime: 1.0
    });

    render(<ChessGame />);

    await waitForChessGameReady();

    // Make a move to start the clocks using board squares
    const chessboard = screen.getByTestId('chessboard');
    const e2Square = chessboard.querySelector('[data-square="e2"]');
    const e4Square = chessboard.querySelector('[data-square="e4"]');

    if (e2Square && e4Square) {
      await user.click(e2Square);
      await user.click(e4Square);
    }

    // Mock continuous time advancement
    let currentTime = 0;
    (global.performance.now as jest.Mock).mockImplementation(() => {
      currentTime += 100;
      return currentTime;
    });

    // Advance time to let clock tick
    act(() => {
      jest.advanceTimersByTime(2000);
    });

    // Open New Game dialog while clock is running - be specific about which button
    const newGameButtons = screen.getAllByText('New Game');
    const newGameButton = newGameButtons.find(button =>
      button.classList.contains('btn-primary')
    ) || newGameButtons[0];
    await user.click(newGameButton);

    await waitFor(() => {
      // Check that dialog is open by looking for the Start Game button
      expect(screen.getByText('Start Game')).toBeInTheDocument();
    });

    // Record clock values when dialog opens
    const whiteClockWhenDialogOpens = screen.getByTestId('white-time').textContent;

    // Advance time more while dialog is open
    act(() => {
      jest.advanceTimersByTime(5000);
    });

    // Clock should not have changed while dialog is open
    expect(screen.getByTestId('white-time').textContent).toBe(whiteClockWhenDialogOpens);

    // Start new game
    const startButton = screen.getByText('Start Game');
    await user.click(startButton);

    // Verify clocks are reset and not running (format is mm:ss with leading zeros)
    await waitFor(() => {
      const whiteClock = screen.getByTestId('white-time');
      expect(whiteClock.textContent).toBe('05:00');
    });

    // Advance time and verify clocks don't change until a move is made
    act(() => {
      jest.advanceTimersByTime(3000);
    });

    expect(screen.getByTestId('white-time').textContent).toBe('05:00');
    expect(screen.getByTestId('black-time').textContent).toBe('05:00');
  });
});
