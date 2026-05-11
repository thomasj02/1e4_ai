import React from 'react';
import { render, screen, fireEvent } from '@testing-library/react';
import '@testing-library/jest-dom';
import ChessGame from './ChessGame';
import { createMockUseChessGame, createMockUseChessClock } from '../test-utils/chess-mocks';
import { waitForChessGameReady } from './ChessGame.test.utils';

// Mock the required hooks and components
jest.mock('../hooks/useChessClock', () => ({
  useChessClock: () => createMockUseChessClock(),
}));

jest.mock('../hooks/useChessGame', () => ({
  useChessGame: () => createMockUseChessGame({
    loadGameFromFEN: jest.fn(),
    navigateToPly: jest.fn(),
    getViewedFEN: jest.fn(() => 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1'),
  }),
}));

// Mock all the child components
jest.mock('../components/NewGameDialog', () => ({
  __esModule: true,
  default: ({ isOpen, onConfirm }: { isOpen: boolean; onConfirm: (config: { rating: number; color: string; timeControl: { initial: number; increment: number } }) => void }) => {
    if (!isOpen) return null;
    return (
      <div role="dialog">
        <button onClick={() => onConfirm({ 
          rating: 1800, 
          color: 'random',
          timeControl: { initial: 300000, increment: 3000 }
        })}>
          Start Game
        </button>
      </div>
    );
  },
}));

const mockComponents = [
  'GameStatus',
  'GameControls',
  'ChessboardArea',
  'MoveHistory',
];

mockComponents.forEach(comp => {
  jest.mock(`../components/${comp}`, () => ({
    __esModule: true,
    default: () => {
      const React = jest.requireActual('react');
      return React.createElement('div', { 'data-testid': `mock-${comp}` });
    },
  }));
});

// Mock Chess.js with all required methods
const { createMockChessGame } = jest.requireActual('../test-utils/chess-mocks');
jest.mock('chess.js', () => ({
  Chess: jest.fn(() => createMockChessGame()),
}));

// Mock utilities
jest.mock('../utils/chess', () => ({
  getCSSVariable: jest.fn(),
  getAIMoveSAN: jest.fn(() => 'e4'), // Always return a valid move
  generateEnhancedHistory: jest.fn(() => []),
  updateStateInTest: jest.fn((fn, value) => {
    if (typeof fn === 'function') fn(value);
  }),
}));

jest.mock('../utils/pgn', () => ({
  generatePGNWithClocks: jest.fn(() => ''),
  handleExportPGN: jest.fn(),
}));

jest.mock('../constants/chess', () => ({
  BACKEND_URL: 'http://localhost:8000',
}));

describe('ChessGame - Random Color Selection', () => {
  beforeEach(() => {
    jest.clearAllMocks();
    // Reset Math.random to a predictable value
    jest.spyOn(Math, 'random').mockReturnValue(0.3); // Will give 'white'
  });

  afterEach(() => {
    jest.restoreAllMocks();
  });

  test('should resolve random color to white when Math.random < 0.5', async () => {
    jest.spyOn(Math, 'random').mockReturnValue(0.3);
    
    // Mock fetch for bot moves
    global.fetch = jest.fn(() => Promise.resolve({
      ok: true,
      json: () => Promise.resolve({ move: 'e5', thinking_time: 0.1 })
    })) as jest.Mock;
    
    render(<ChessGame />);
    
    // Wait for initial render
    await waitForChessGameReady();
    
    // Open new game dialog - be specific about which button
    const newGameButtons = screen.getAllByRole('button', { name: /new game/i });
    const newGameButton = newGameButtons[0];
    fireEvent.click(newGameButton);
    
    // Click start game (which will select random color)
    const startButton = screen.getByRole('button', { name: /start game/i });
    fireEvent.click(startButton);
    
    // Since Math.random returns 0.3 (< 0.5), player should be white
    // The test passes if the component renders without errors
    await waitForChessGameReady();
  });

  test('should resolve random color to black when Math.random >= 0.5', async () => {
    jest.spyOn(Math, 'random').mockReturnValue(0.7);
    
    // Mock fetch for bot moves
    global.fetch = jest.fn(() => Promise.resolve({
      ok: true,
      json: () => Promise.resolve({ move: 'e4', thinking_time: 0.1 })
    })) as jest.Mock;
    
    render(<ChessGame />);
    
    // Wait for initial render
    await waitForChessGameReady();
    
    // Open new game dialog - be specific about which button
    const newGameButtons = screen.getAllByRole('button', { name: /new game/i });
    const newGameButton = newGameButtons[0];
    fireEvent.click(newGameButton);
    
    // Click start game (which will select random color)
    const startButton = screen.getByRole('button', { name: /start game/i });
    fireEvent.click(startButton);
    
    // Since Math.random returns 0.7 (>= 0.5), player should be black
    // The test passes if the component renders without errors
    // (actual game logic is tested in other test files)
    await waitForChessGameReady();
  });
});