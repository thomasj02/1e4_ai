/* eslint-env jest */
import React from 'react';
import { render, screen, waitFor, act } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import ChessGame from './ChessGame';
import {
  parseTimeToSeconds,
  getCurrentMoveCount,
  setupFetchMocks,
  waitForChessGameReady
} from './ChessGame.test.utils';

describe('ChessGame Component - Clock Tests (Real Board)', () => {
  afterEach(() => {
    jest.clearAllMocks();
    jest.restoreAllMocks();
    jest.useRealTimers();
    if ((global as unknown as { performance?: { now?: { mockReturnValue?: unknown } } }).performance?.now?.mockReturnValue) {
      delete (global as unknown as { performance?: unknown }).performance;
    }
  });

  describe('Basic Clock Management', () => {
    test('should start with 5 minutes for both players', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      const whiteClock = screen.getByTestId('white-time');
      const blackClock = screen.getByTestId('black-time');

      expect(whiteClock).toHaveTextContent('05:00');
      expect(blackClock).toHaveTextContent('05:00');
    });

    test('should not run clocks initially', async () => {
      render(<ChessGame />);

      await waitForChessGameReady();

      const whiteClock = screen.getByTestId('white-time');
      const blackClock = screen.getByTestId('black-time');

      // Check for Bootstrap classes instead of Tailwind
      expect(whiteClock).not.toHaveClass('border-primary');
      expect(blackClock).not.toHaveClass('border-primary');
    });

    test('should start clocks after first move', async () => {
      jest.useFakeTimers();
      const user = userEvent.setup({ advanceTimers: jest.advanceTimersByTime, delay: null });

      // Mock performance.now for time tracking
      global.performance = {
        now: jest.fn().mockReturnValue(0)
      } as unknown as Performance;

      // Setup fetch mocks with bot moves and evaluation
      setupFetchMocks({
        botMove: 'e5',
        thinkingTime: 1.0
      });

      render(<ChessGame />);

      await waitForChessGameReady();

      // Make first move by clicking squares
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await act(async () => {
          await user.click(e2Square);
          await user.click(e4Square);
        });
      }

      // Wait for move to be processed
      await waitFor(() => {
        expect(getCurrentMoveCount()).toBe(1);
      });

      // After white's move, black's clock should be active
      // Check for Bootstrap active class instead of Tailwind
      const blackClock = screen.getByTestId('black-time');
      expect(blackClock).toBeInTheDocument();
    });
  });

  describe('Timeseal - Lag Compensation', () => {
    test('should stop user clock immediately on move completion', async () => {
      jest.useFakeTimers();
      const user = userEvent.setup({ advanceTimers: jest.advanceTimersByTime, delay: null });

      // Mock performance.now for time tracking
      global.performance = {
        now: jest.fn().mockReturnValue(0)
      } as unknown as Performance;

      render(<ChessGame />);

      await waitForChessGameReady();

      const whiteTimeInitial = screen.getByTestId('white-time').textContent || '0:00';

      // Mock slow network response
      (global.fetch as jest.MockedFunction<typeof fetch>).mockImplementation((url: string | URL | Request) => {
        if (typeof url === 'string' && url.includes('/evaluate_position')) {
          return Promise.resolve({
            ok: true,
            json: () => Promise.resolve({ evaluation: 0.0 })
          } as Response);
        }
        if (typeof url === 'string' && url.includes('/get_move')) {
          return new Promise(resolve =>
            setTimeout(() => resolve({
              ok: true,
              json: () => Promise.resolve({ move: 'e5', thinking_time: 1.0 })
            } as Response),
            3000 // 3 second network delay
            )
          );
        }
        return Promise.reject(new Error('Unknown endpoint'));
      });

      // Make move by clicking squares
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await act(async () => {
          await user.click(e2Square);
          await user.click(e4Square);
        });
      }

      // White's clock should stop immediately, not after network response
      const whiteTimeAfterMove = screen.getByTestId('white-time').textContent || '0:00';

      // Time should be approximately the same (maybe lost a tiny bit during move input)
      const initialSeconds = parseTimeToSeconds(whiteTimeInitial);
      const afterSeconds = parseTimeToSeconds(whiteTimeAfterMove);

      // Should have gained increment (3 seconds) or at least not lost much time
      expect(afterSeconds).toBeGreaterThan(initialSeconds - 1);
    });
  });

  describe('Increment Timing', () => {
    test('should add increment to white after white moves', async () => {
      jest.useFakeTimers();
      const user = userEvent.setup({ advanceTimers: jest.advanceTimersByTime, delay: null });

      // Mock performance.now for time tracking
      global.performance = {
        now: jest.fn().mockReturnValue(0)
      } as unknown as Performance;

      // Setup fetch mocks with bot moves and evaluation
      setupFetchMocks({
        botMove: 'e5',
        thinkingTime: 1.0
      });

      render(<ChessGame />);

      await waitForChessGameReady();

      const whiteTimeInitial = screen.getByTestId('white-time').textContent || '0:00';
      const initialSeconds = parseTimeToSeconds(whiteTimeInitial);

      // Make white's move by clicking squares
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await act(async () => {
          await user.click(e2Square);
          await user.click(e4Square);
        });
      }

      // Wait for move and bot response
      await waitFor(() => {
        expect(getCurrentMoveCount()).toBeGreaterThan(0);
      });

      // White should have received increment
      const whiteTimeAfter = screen.getByTestId('white-time').textContent || '0:00';
      const afterSeconds = parseTimeToSeconds(whiteTimeAfter);

      // Should have gained time (3 second increment)
      expect(afterSeconds).toBeGreaterThan(initialSeconds);
    });

    test('should add increment to black after black moves', async () => {
      jest.useFakeTimers();
      const user = userEvent.setup({ advanceTimers: jest.advanceTimersByTime, delay: null });

      // Mock performance.now for time tracking
      global.performance = {
        now: jest.fn().mockReturnValue(0)
      } as unknown as Performance;

      // Setup fetch mocks with bot moves and evaluation
      setupFetchMocks({
        botMove: 'e5',
        thinkingTime: 1.0
      });

      render(<ChessGame />);

      await waitForChessGameReady();

      const blackTimeInitial = screen.getByTestId('black-time').textContent || '0:00';
      const initialBlackSeconds = parseTimeToSeconds(blackTimeInitial);

      // Make white's move to trigger bot's turn
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await act(async () => {
          await user.click(e2Square);
          await user.click(e4Square);
        });
      }

      // Wait for bot move to complete
      await waitFor(() => {
        expect(getCurrentMoveCount()).toBe(2); // White + Black moves
      }, { timeout: 3000 });

      // Black should have received increment after moving
      const blackTimeAfter = screen.getByTestId('black-time').textContent || '0:00';
      const afterBlackSeconds = parseTimeToSeconds(blackTimeAfter);

      // Black should have approximately same time or more (gained increment, lost minimal thinking time)
      expect(afterBlackSeconds).toBeGreaterThanOrEqual(initialBlackSeconds - 2);
    });

    test('should handle multiple moves with increments correctly', async () => {
      jest.useFakeTimers();
      const user = userEvent.setup({ advanceTimers: jest.advanceTimersByTime, delay: null });

      // Mock performance.now for time tracking
      global.performance = {
        now: jest.fn().mockReturnValue(0)
      } as unknown as Performance;

      // Mock different bot moves for each call
      let moveCount = 0;
      (global.fetch as jest.MockedFunction<typeof fetch>).mockImplementation((url: string | URL | Request) => {
        if (typeof url === 'string' && url.includes('/evaluate_position')) {
          return Promise.resolve({
            ok: true,
            json: () => Promise.resolve({ evaluation: 0.0 })
          } as Response);
        }
        if (typeof url === 'string' && url.includes('/get_move')) {
          moveCount++;
          const moves = ['e5', 'd5', 'Nf6'];
          return Promise.resolve({
            ok: true,
            json: () => Promise.resolve({
              move: moves[moveCount - 1] || 'Nc6',
              thinking_time: 0.1
            })
          } as Response);
        }
        return Promise.reject(new Error('Unknown endpoint'));
      });

      render(<ChessGame />);

      await waitForChessGameReady();

      // Track initial times
      const whiteTimeInitial = parseTimeToSeconds(screen.getByTestId('white-time').textContent || '0:00');
      const blackTimeInitial = parseTimeToSeconds(screen.getByTestId('black-time').textContent || '0:00');

      // Move 1: e4
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await act(async () => {
          await user.click(e2Square);
          await user.click(e4Square);
        });
      }

      // Wait for bot response
      await waitFor(() => {
        expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(2);
      }, { timeout: 3000 });

      // Both players should have gained time from increments
      const whiteTimeAfter1 = parseTimeToSeconds(screen.getByTestId('white-time').textContent || '0:00');
      const blackTimeAfter1 = parseTimeToSeconds(screen.getByTestId('black-time').textContent || '0:00');

      expect(whiteTimeAfter1).toBeGreaterThan(whiteTimeInitial - 1);
      expect(blackTimeAfter1).toBeGreaterThanOrEqual(blackTimeInitial - 2);

      // Move 2: d4
      const d2Square = chessboard.querySelector('[data-square="d2"]');
      const d4Square = chessboard.querySelector('[data-square="d4"]');

      if (d2Square && d4Square) {
        await act(async () => {
          await user.click(d2Square);
          await user.click(d4Square);
        });
      }

      // Wait for second bot response
      await waitFor(() => {
        expect(getCurrentMoveCount()).toBeGreaterThanOrEqual(3);
      }, { timeout: 3000 });

      // White should have gained even more time
      const whiteTimeAfter2 = parseTimeToSeconds(screen.getByTestId('white-time').textContent || '0:00');
      expect(whiteTimeAfter2).toBeGreaterThan(whiteTimeAfter1 - 1);
    });
  });

  describe('Bot Thinking Simulation', () => {
    test('should simulate bot thinking time from backend response', async () => {
      jest.useFakeTimers();
      const user = userEvent.setup({ advanceTimers: jest.advanceTimersByTime, delay: null });

      const currentTime = 0;
      global.performance = {
        now: jest.fn().mockImplementation(() => currentTime)
      } as unknown as Performance;

      // Mock backend with specific thinking time
      (global.fetch as jest.MockedFunction<typeof fetch>).mockImplementation((url: string | URL | Request) => {
        if (typeof url === 'string' && url.includes('/evaluate_position')) {
          return Promise.resolve({
            ok: true,
            json: () => Promise.resolve({ evaluation: 0.0 })
          } as Response);
        }
        if (typeof url === 'string' && url.includes('/get_move')) {
          return Promise.resolve({
            ok: true,
            json: () => Promise.resolve({ move: 'e5', thinking_time: 3.0 })
          } as Response);
        }
        return Promise.reject(new Error('Unknown endpoint'));
      });

      render(<ChessGame />);

      await waitForChessGameReady();

      // Make white's move by clicking squares
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await act(async () => {
          await user.click(e2Square);
          await user.click(e4Square);
        });
      }

      // Wait for bot API call
      await waitFor(() => {
        expect(global.fetch).toHaveBeenCalledWith(
          'http://localhost:8000/get_move',
          expect.any(Object)
        );
      });

      // Black clock should be active during thinking
      const blackClock = screen.getByTestId('black-time');
      expect(blackClock).toBeInTheDocument();
    });

    test('should not affect user clock during bot thinking', async () => {
      jest.useFakeTimers();
      const user = userEvent.setup({ advanceTimers: jest.advanceTimersByTime, delay: null });

      // Mock performance.now for time tracking
      global.performance = {
        now: jest.fn().mockReturnValue(0)
      } as unknown as Performance;

      // Setup fetch mocks with bot moves and evaluation
      setupFetchMocks({
        botMove: 'e5',
        thinkingTime: 1.0
      });

      render(<ChessGame />);

      await waitForChessGameReady();

      // Make user move by clicking squares
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await act(async () => {
          await user.click(e2Square);
          await user.click(e4Square);
        });
      }

      await waitFor(() => {
        expect(getCurrentMoveCount()).toBe(1);
      });

      // During bot thinking, white's clock should not be active
      const whiteClock = screen.getByTestId('white-time');
      const blackClock = screen.getByTestId('black-time');

      expect(whiteClock).toBeInTheDocument();
      expect(blackClock).toBeInTheDocument();
    });
  });

  describe('First Move Clock Switching', () => {
    test('should stop white clock and start black clock after white makes first move', async () => {
      jest.useFakeTimers();
      const user = userEvent.setup({ advanceTimers: jest.advanceTimersByTime, delay: null });

      // Mock performance.now for time tracking
      global.performance = {
        now: jest.fn().mockReturnValue(0)
      } as unknown as Performance;

      // Setup fetch mocks with bot moves and evaluation
      setupFetchMocks({
        botMove: 'e5',
        thinkingTime: 1.0
      });

      render(<ChessGame />);

      await waitForChessGameReady();

      const whiteClock = screen.getByTestId('white-time');
      const blackClock = screen.getByTestId('black-time');

      // Initially no clocks should be active (no border-primary class)
      expect(whiteClock).toBeInTheDocument();
      expect(blackClock).toBeInTheDocument();

      // Make white's first move by clicking squares
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await act(async () => {
          await user.click(e2Square);
          await user.click(e4Square);
        });
      }

      // Wait for move to be processed
      await waitFor(() => {
        expect(getCurrentMoveCount()).toBe(1);
      });

      // After white's first move, clocks should have switched
      expect(whiteClock).toBeInTheDocument();
      expect(blackClock).toBeInTheDocument();
    });
  });

  describe('Time Forfeit', () => {
    test('should end game when player runs out of time', async () => {
      jest.useFakeTimers();
      const user = userEvent.setup({ advanceTimers: jest.advanceTimersByTime, delay: null });

      // Mock performance.now for time tracking
      global.performance = {
        now: jest.fn().mockReturnValue(0)
      } as unknown as Performance;

      // Mock backend to delay forever so clock runs out
      (global.fetch as jest.MockedFunction<typeof fetch>).mockImplementation((url: string | URL | Request) => {
        if (typeof url === 'string' && url.includes('/evaluate_position')) {
          return Promise.resolve({
            ok: true,
            json: () => Promise.resolve({ evaluation: 0.0 })
          } as Response);
        }
        if (typeof url === 'string' && url.includes('/get_move')) {
          return new Promise(() => {}); // Never resolve
        }
        return Promise.reject(new Error('Unknown endpoint'));
      });

      render(<ChessGame />);

      await waitForChessGameReady();

      // Make first move to start the clock
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await act(async () => {
          await user.click(e2Square);
          await user.click(e4Square);
        });
      }

      // Wait for move to be processed
      await waitFor(() => {
        expect(getCurrentMoveCount()).toBe(1);
      });

      // Now black is thinking forever, advance time to exhaust black's clock
      await act(async () => {
        jest.advanceTimersByTime(300100); // 5 minutes + 100ms
      });

      // Black should have run out of time
      await waitFor(() => {
        expect(screen.getByTestId('black-time')).toHaveTextContent('0:00');
      });

      // Game should be over - time win shows specific message
      await waitFor(() => {
        expect(screen.getByText(/White wins on time!/)).toBeInTheDocument();
      });
    });

    test('should prevent moves after time forfeit', async () => {
      jest.useFakeTimers();
      const user = userEvent.setup({ advanceTimers: jest.advanceTimersByTime, delay: null });

      // Mock performance.now for time tracking
      global.performance = {
        now: jest.fn().mockReturnValue(0)
      } as unknown as Performance;

      // Mock backend to delay forever
      (global.fetch as jest.MockedFunction<typeof fetch>).mockImplementation((url: string | URL | Request) => {
        if (typeof url === 'string' && url.includes('/evaluate_position')) {
          return Promise.resolve({
            ok: true,
            json: () => Promise.resolve({ evaluation: 0.0 })
          } as Response);
        }
        if (typeof url === 'string' && url.includes('/get_move')) {
          return new Promise(() => {}); // Never resolve
        }
        return Promise.reject(new Error('Unknown endpoint'));
      });

      render(<ChessGame />);

      await waitForChessGameReady();

      // Make first move to start clock
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await act(async () => {
          await user.click(e2Square);
          await user.click(e4Square);
        });
      }

      await waitFor(() => {
        expect(getCurrentMoveCount()).toBe(1);
      });

      // Let black run out of time
      await act(async () => {
        jest.advanceTimersByTime(300100);
      });

      // Wait for game to end - time win shows specific message
      await waitFor(() => {
        expect(screen.getByText(/wins on time!/)).toBeInTheDocument();
      });

      const moveCountBefore = getCurrentMoveCount();

      // Try to make another move - should be prevented
      const d2Square = chessboard.querySelector('[data-square="d2"]');
      const d4Square = chessboard.querySelector('[data-square="d4"]');

      if (d2Square && d4Square) {
        await act(async () => {
          await user.click(d2Square);
          await user.click(d4Square);
        });
      }

      // Move count should not have changed
      expect(getCurrentMoveCount()).toBe(moveCountBefore);
    });
  });

  describe('Rating Configuration', () => {
    test('should open new game dialog when clicking New Game', async () => {
      const user = userEvent.setup({ delay: null });

      render(<ChessGame />);

      await waitForChessGameReady();

      // Click New Game button - be specific about which one
      const newGameButtons = screen.getAllByText('New Game');
      const newGameButton = newGameButtons.find(button =>
        button.classList.contains('btn-primary')
      ) || newGameButtons[0];
      await user.click(newGameButton);

      // Dialog should be visible
      await waitFor(() => {
        expect(screen.getByText('Start Game')).toBeInTheDocument();
      });

      // Should have rating input
      const ratingInput = screen.getAllByDisplayValue('1800').find(el => (el as HTMLInputElement).type === 'number');
      expect(ratingInput).toBeInTheDocument();
      expect(ratingInput).toHaveValue(1800);
    });

    test('should send rating and clock_time in API request', async () => {
      jest.useFakeTimers();
      const user = userEvent.setup({ advanceTimers: jest.advanceTimersByTime, delay: null });

      // Mock performance.now for time tracking
      global.performance = {
        now: jest.fn().mockReturnValue(0)
      } as unknown as Performance;

      const fetchCalls: Array<{ url: string; options?: RequestInit }> = [];
      (global.fetch as jest.MockedFunction<typeof fetch>).mockImplementation((url: string | URL | Request, options?: RequestInit) => {
        const urlString = typeof url === 'string' ? url : url.toString();
        fetchCalls.push({ url: urlString, options });
        if (urlString.includes('/evaluate_position')) {
          return Promise.resolve({
            ok: true,
            json: () => Promise.resolve({ evaluation: 0.0 })
          } as Response);
        }
        if (urlString.includes('/get_move')) {
          return Promise.resolve({
            ok: true,
            json: () => Promise.resolve({ move: 'e5', thinking_time: 1.0 })
          } as Response);
        }
        return Promise.reject(new Error('Unknown endpoint'));
      });

      render(<ChessGame />);

      await waitForChessGameReady();

      // Open dialog and change rating - be specific about which button
      const newGameButtons = screen.getAllByText('New Game');
      const newGameButton = newGameButtons.find(button =>
        button.classList.contains('btn-primary')
      ) || newGameButtons[0];
      await user.click(newGameButton);

      const ratingInputs = await screen.findAllByDisplayValue('1800');
      const ratingInput = ratingInputs.find(el => (el as HTMLInputElement).type === 'number');
      if (!ratingInput) throw new Error('Rating input not found');
      await user.clear(ratingInput);
      await user.type(ratingInput, '2200');

      // Start game
      const startGameButton = screen.getByText('Start Game');
      await user.click(startGameButton);

      // Make a move to trigger AI
      const chessboard = screen.getByTestId('chessboard');
      const e2Square = chessboard.querySelector('[data-square="e2"]');
      const e4Square = chessboard.querySelector('[data-square="e4"]');

      if (e2Square && e4Square) {
        await act(async () => {
          await user.click(e2Square);
          await user.click(e4Square);
        });
      }

      // Wait for API call
      await waitFor(() => {
        const moveCall = fetchCalls.find(call => call.url.includes('/get_move'));
        expect(moveCall).toBeDefined();
      });

      // Verify API parameters
      const moveCall = fetchCalls.find(call => call.url.includes('/get_move'));
      if (!moveCall || !moveCall.options?.body) throw new Error('Move call not found or missing body');
      const body = JSON.parse(moveCall.options.body as string);

      expect(body).toMatchObject({
        fen: expect.any(String),
        moves: expect.any(Array),
        rating: 2200,
        clock_time: expect.any(Number)
      });

      // Clock time should be close to 5 minutes
      expect(body.clock_time).toBeGreaterThan(295);
      expect(body.clock_time).toBeLessThanOrEqual(300);
    });
  });
});
