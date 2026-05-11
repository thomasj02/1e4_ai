// Import testing-library utilities
import '@testing-library/jest-dom';

// Configure React Testing Library for React 18
import { configure } from '@testing-library/react';
configure({ testIdAttribute: 'data-testid' });

// Mock @clerk/nextjs to prevent ClerkProvider requirement in tests
jest.mock('@clerk/nextjs', () => ({
  useAuth: () => ({
    getToken: jest.fn().mockResolvedValue(null),
    isLoaded: true,
    isSignedIn: false,
    userId: null,
    sessionId: null,
    orgId: null,
    orgRole: null,
    orgSlug: null,
  }),
  useUser: () => ({
    isLoaded: true,
    isSignedIn: false,
    user: null,
  }),
  useClerk: () => ({
    session: null,
    user: null,
  }),
  ClerkProvider: ({ children }) => children,
  SignInButton: () => null,
  SignUpButton: () => null,
  SignedIn: ({ children }) => children,
  SignedOut: ({ children }) => children,
  UserButton: () => null,
}));

// Mock react-chessboard to avoid canvas/DOM issues in tests
jest.mock('react-chessboard', () => {
  const React = require('react');
  const { Chess } = require('chess.js');

  // Parse FEN to get piece placement
  function parseFenToPieces(fen) {
    const pieces = {};
    const parts = fen.split(' ');
    const ranks = parts[0].split('/');
    const files = ['a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'];

    ranks.forEach((rank, rankIndex) => {
      let fileIndex = 0;
      for (const char of rank) {
        if (/\d/.test(char)) {
          fileIndex += parseInt(char, 10);
        } else {
          const square = files[fileIndex] + (8 - rankIndex);
          const color = char === char.toUpperCase() ? 'w' : 'b';
          const piece = char.toUpperCase();
          pieces[square] = color + piece;
          fileIndex++;
        }
      }
    });
    return pieces;
  }

  return {
    Chessboard: (props) => {
      const { position, onPieceDrop, onPieceClick, onSquareClick, customSquareStyles, boardOrientation = 'white' } = props;
      const pieces = typeof position === 'string' ? parseFenToPieces(position) : position;
      const files = ['a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'];
      const ranks = ['8', '7', '6', '5', '4', '3', '2', '1'];

      // Create 64 squares
      const squares = [];
      for (const rank of ranks) {
        for (const file of files) {
          const square = file + rank;
          const piece = pieces[square];
          squares.push(
            React.createElement('div', {
              key: square,
              'data-square': square,
              'data-piece': piece || '',
              style: customSquareStyles?.[square] || {},
              onClick: () => {
                if (piece && onPieceClick) {
                  onPieceClick(piece, square);
                } else if (onSquareClick) {
                  onSquareClick(square);
                }
              },
            }, piece || '')
          );
        }
      }

      return React.createElement('div', {
        'data-testid': 'chessboard',
        'data-position': typeof position === 'string' ? position : '',
        style: { display: 'grid', gridTemplateColumns: 'repeat(8, 1fr)' },
      }, squares);
    },
  };
});

// Mock posthog-js/react to avoid analytics in tests
jest.mock('posthog-js/react', () => ({
  usePostHog: () => ({
    capture: jest.fn(),
    identify: jest.fn(),
  }),
}));

// Mock window.matchMedia
Object.defineProperty(window, 'matchMedia', {
  writable: true,
  value: jest.fn().mockImplementation(query => ({
    matches: false,
    media: query,
    onchange: null,
    addListener: jest.fn(),
    removeListener: jest.fn(),
    addEventListener: jest.fn(),
    removeEventListener: jest.fn(),
    dispatchEvent: jest.fn(),
  })),
});

// Mock scrollIntoView
Element.prototype.scrollIntoView = jest.fn();

// Mock HTMLDialogElement methods
if (!HTMLDialogElement.prototype.showModal) {
  HTMLDialogElement.prototype.showModal = jest.fn();
}
if (!HTMLDialogElement.prototype.close) {
  HTMLDialogElement.prototype.close = jest.fn();
}

// Mock performance.now() if not available
if (!global.performance) {
  global.performance = {};
}
if (!global.performance.now) {
  global.performance.now = jest.fn(() => Date.now());
}

// Configure React 18 act() environment for testing
global.IS_REACT_ACT_ENVIRONMENT = true;

// Store original console methods for selective suppression
const originalConsoleError = console.error;
const originalConsoleWarn = console.warn;

// Set up global console suppression utilities
global.suppressConsoleErrors = (patterns) => {
  console.error = (...args) => {
    const message = args.join(' ');
    const shouldSuppress = patterns.some(pattern => 
      typeof pattern === 'string' ? message.includes(pattern) : pattern.test(message)
    );
    if (!shouldSuppress) {
      originalConsoleError(...args);
    }
  };
};

global.restoreConsole = () => {
  console.error = originalConsoleError;
  console.warn = originalConsoleWarn;
};

// Suppress known React 18 act() warnings globally during tests
global.suppressConsoleErrors([
  // React warnings
  'The current testing environment is not configured to support act',
  'ReactDOMTestUtils.act` is deprecated',
  'A component suspended inside an `act` scope',
  'Warning: An invalid form control',
  'Warning: React does not recognize the',
  
  // Expected test errors (these are intentional)
  'Error executing move: Invalid move:',
  'Invalid move:',
  'Invalid FEN string',
  'Error fetching evaluation:',
  'Error fetching move from backend:',
  'Error applying AI move:',
  'Network error',
  'Error generating enhanced history:',
  'Error in evaluation effect:'
]);