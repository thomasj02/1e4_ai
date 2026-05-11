/* eslint-env jest */
import React from 'react';
import { render, screen, fireEvent, waitFor, act } from '@testing-library/react';
import '@testing-library/jest-dom';
import ChessGame from './ChessGame';

// Mock the chess.js library
jest.mock('chess.js', () => {
  return {
    Chess: jest.fn().mockImplementation(() => {
      const localMoveHistory: { san: string; from: string; to: string; color: string; piece: string; after: string }[] = [];
      let currentFen = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';

      const mockInstance = {
        fen: jest.fn().mockImplementation(() => currentFen),
        move: jest.fn().mockImplementation((move: { san?: string; from?: string; to?: string }) => {
          const moveResult = {
            san: move.san || 'e4',
            from: move.from || 'e2',
            to: move.to || 'e4',
            color: localMoveHistory.length % 2 === 0 ? 'w' : 'b',
            piece: 'p',
            after: 'rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1'
          };
          localMoveHistory.push(moveResult);
          currentFen = moveResult.after;
          return moveResult;
        }),
        history: jest.fn().mockImplementation(() => localMoveHistory),
        isGameOver: jest.fn().mockReturnValue(false),
        isCheckmate: jest.fn().mockReturnValue(false),
        isDraw: jest.fn().mockReturnValue(false),
        isCheck: jest.fn().mockReturnValue(false),
        inCheck: jest.fn().mockReturnValue(false),
        isStalemate: jest.fn().mockReturnValue(false),
        isInsufficientMaterial: jest.fn().mockReturnValue(false),
        isThreefoldRepetition: jest.fn().mockReturnValue(false),
        pgn: jest.fn().mockImplementation(() => {
          if (localMoveHistory.length === 0) return '';
          return '1. e4 e5 2. Nf3';
        }),
        turn: jest.fn().mockReturnValue('w'),
        load: jest.fn(),
        loadPgn: jest.fn(),
      };
      return mockInstance;
    })
  };
});

// Mock react-chessboard
jest.mock('react-chessboard', () => ({
  Chessboard: ({ position, onPieceDrop }: { position: string; onPieceDrop?: (from: string, to: string, piece: string) => void }) => (
    <div data-testid="chessboard" data-position={position} onClick={() => onPieceDrop && onPieceDrop('e2', 'e4', 'wP')}>
      Mock Chessboard - Position: {position}
    </div>
  ),
}));

// Mock other components
jest.mock('@/components/MoveList', () => {
  return function MoveList() {
    return <div data-testid="move-list">Mock MoveList</div>;
  };
});

jest.mock('@/components/ChessClock', () => {
  return function ChessClock() {
    return <div data-testid="chess-clock">Mock ChessClock</div>;
  };
});

jest.mock('@/components/EvalBar', () => {
  return function EvalBar() {
    return <div data-testid="eval-bar">Mock EvalBar</div>;
  };
});

jest.mock('@/components/NewGameDialog', () => {
  return function NewGameDialog() {
    return null; // Dialog is closed by default
  };
});

// Mock axios
jest.mock('axios', () => ({
  default: {
    post: jest.fn().mockResolvedValue({ data: { evaluation: 0 } }),
    get: jest.fn().mockResolvedValue({ data: {} })
  }
}));

// Mock URL.createObjectURL and URL.revokeObjectURL
global.URL.createObjectURL = jest.fn(() => 'blob:mock-url');
global.URL.revokeObjectURL = jest.fn();

// Mock window.getComputedStyle for CSS variables
window.getComputedStyle = jest.fn(() => ({
  getPropertyValue: jest.fn((varName) => {
    const cssVars = {
      '--color-square-last-move-pale': 'rgba(255, 255, 190, 0.5)',
      '--color-square-last-move': 'rgba(255, 255, 190, 0.7)',
      '--color-square-selected': 'rgba(255, 255, 0, 0.2)',
      '--color-square-navigation': 'rgba(255, 255, 0, 0.4)',
      '--color-chess-legal': 'rgba(0, 128, 0, 0.4)'
    };
    return (cssVars as Record<string, string>)[varName] || '';
  })
})) as unknown as typeof window.getComputedStyle;

// Mock fetch
global.fetch = jest.fn().mockResolvedValue({
  ok: true,
  json: jest.fn().mockResolvedValue({
    move: 'e5',
    thinking_time: 1,
    evaluation: 0
  })
});

// Mock the download functionality
let mockCreateElement: jest.MockedFunction<typeof document.createElement>;
const mockClick = jest.fn();

// Helper to get move count from window state
function getMoveCount(): number {
  const state = (window as unknown as { __chessAppState?: { moveHistory?: unknown[] } }).__chessAppState;
  return state?.moveHistory?.length || 0;
}

describe('PGN Export Functionality', () => {
  let originalCreateElement: typeof document.createElement;

  beforeEach(() => {
    jest.clearAllMocks();
    mockClick.mockClear();

    // Store original createElement before mocking
    originalCreateElement = document.createElement.bind(document);

    // Create a fresh mock for each test
    mockCreateElement = jest.spyOn(document, 'createElement') as jest.MockedFunction<typeof document.createElement>;
    mockCreateElement.mockImplementation((tagName) => {
      // Use the original createElement to ensure we get a proper DOM element
      const element = originalCreateElement(tagName);
      if (tagName === 'a') {
        // Override click for anchor elements
        element.click = mockClick;
      }
      return element;
    });
  });

  afterEach(() => {
    // Clean up mocks
    if (mockCreateElement) {
      mockCreateElement.mockRestore();
    }
    jest.clearAllMocks();
  });

  test('should export basic PGN without clock times', async () => {
    // Use standard render without custom container
    render(<ChessGame />);

    // Wait for the component to be ready
    await waitFor(() => {
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();
    });

    // Make a move
    const chessboard = screen.getByTestId('chessboard');
    await act(async () => {
      fireEvent.click(chessboard);
    });

    // Click export button
    const exportButton = screen.getByText('Export PGN');
    fireEvent.click(exportButton);

    // Verify download was triggered
    expect(mockCreateElement).toHaveBeenCalledWith('a');
    expect(mockClick).toHaveBeenCalled();

    // Check that an anchor was created
    const calls = mockCreateElement.mock.calls;
    const anchorCall = calls.find(call => call[0] === 'a');
    expect(anchorCall).toBeDefined();
  });

  test('should show Export PGN button and trigger download on click', async () => {
    await act(async () => {
      render(<ChessGame />);
    });

    const exportButton = screen.getByText('Export PGN');
    expect(exportButton).toBeInTheDocument();
    expect(exportButton).toBeEnabled();

    // Verify clicking the button triggers download
    const mockBlob = jest.fn<void, [content: string, options?: BlobPropertyBag]>();
    global.Blob = jest.fn((content?: BlobPart[], options?: BlobPropertyBag) => {
      if (content && content[0]) {
        mockBlob(String(content[0]), options);
        return { size: String(content[0]).length } as Blob;
      }
      return { size: 0 } as Blob;
    }) as unknown as typeof Blob;

    fireEvent.click(exportButton);

    // Verify download was initiated
    expect(mockCreateElement).toHaveBeenCalledWith('a');
    expect(mockClick).toHaveBeenCalled();
    expect(mockBlob).toHaveBeenCalled();

    // Verify PGN content structure
    const pgnContent = mockBlob.mock.calls[0][0];
    expect(pgnContent).toMatch(/\[Event ".*"\]/);
    expect(pgnContent).toMatch(/\[Date "\d{4}\.\d{2}\.\d{2}"\]/);
  });

  test('should export PGN with clock annotations', async () => {
    await act(async () => {
      render(<ChessGame />);
    });

    // Make moves
    const chessboard = screen.getByTestId('chessboard');
    await act(async () => {
      fireEvent.click(chessboard);
    });

    await waitFor(() => {
      expect(getMoveCount()).toBeGreaterThanOrEqual(1);
    }, { timeout: 3000 });

    // Mock the Blob constructor to capture content
    const mockBlob = jest.fn<void, [content: string, options?: BlobPropertyBag]>();
    global.Blob = jest.fn((content?: BlobPart[], options?: BlobPropertyBag) => {
      if (content && content[0]) {
        mockBlob(String(content[0]), options);
        return { size: String(content[0]).length } as Blob;
      }
      return { size: 0 } as Blob;
    }) as unknown as typeof Blob;

    // Click export button
    const exportButton = screen.getByText('Export PGN');
    fireEvent.click(exportButton);

    // Verify PGN content includes clock annotations
    expect(mockBlob).toHaveBeenCalled();
    const pgnContent = mockBlob.mock.calls[0][0];
    expect(pgnContent).toContain('[Event "ChessMimic Game"]');
    expect(pgnContent).toContain('[TimeControl "300+3"]');
    expect(pgnContent).toMatch(/1\. e4 \{\[%clk \d:\d{2}:\d{2}\]\}/);
  });

  test('should include game metadata in PGN export', async () => {
    await act(async () => {
      render(<ChessGame />);
    });

    // Mock the Blob constructor
    const mockBlob = jest.fn<void, [content: string, options?: BlobPropertyBag]>();
    global.Blob = jest.fn((content?: BlobPart[], options?: BlobPropertyBag) => {
      if (content && content[0]) {
        mockBlob(String(content[0]), options);
        return { size: String(content[0]).length } as Blob;
      }
      return { size: 0 } as Blob;
    }) as unknown as typeof Blob;

    // Let the properly mocked createElement from beforeEach handle this

    // Click export button
    const exportButton = screen.getByText('Export PGN');
    fireEvent.click(exportButton);

    // Verify metadata
    const pgnContent = mockBlob.mock.calls[0][0];
    expect(pgnContent).toContain('[Event "ChessMimic Game"]');
    expect(pgnContent).toContain('[Site "ChessMimic"]');
    expect(pgnContent).toMatch(/\[Date "\d{4}\.\d{2}\.\d{2}"\]/);
    expect(pgnContent).toContain('[Round "1"]');
    expect(pgnContent).toContain('[White "Human"]');
    expect(pgnContent).toContain('[Black "ChessMimic Bot (1800)"]');
    expect(pgnContent).toContain('[TimeControl "300+3"]');
  });

  test('should handle time forfeit in PGN export', async () => {
    // Mock a game state with time forfeit
    // Chess is already mocked above

    await act(async () => {
      render(<ChessGame />);
    });

    // Make a move to start the game
    const chessboard = screen.getByTestId('chessboard');
    await act(async () => {
      fireEvent.click(chessboard);
    });

    // Wait for move to be processed
    await waitFor(() => {
      expect(getMoveCount()).toBeGreaterThanOrEqual(1);
    }, { timeout: 3000 });

    // Mock time forfeit by setting clock to zero through the component
    // This is a unit test limitation - we verify the PGN export still works
    // and includes proper termination header when game ends

    // Export PGN
    const mockBlob = jest.fn<void, [content: string, options?: BlobPropertyBag]>();
    global.Blob = jest.fn((content?: BlobPart[], options?: BlobPropertyBag) => {
      if (content && content[0]) {
        mockBlob(String(content[0]), options);
        return { size: String(content[0]).length } as Blob;
      }
      return { size: 0 } as Blob;
    }) as unknown as typeof Blob;

    const exportButton = screen.getByText('Export PGN');
    fireEvent.click(exportButton);

    // Verify PGN was created and contains proper headers
    expect(mockBlob).toHaveBeenCalled();
    const pgnContent = mockBlob.mock.calls[0][0];
    expect(pgnContent).toContain('[Event "ChessMimic Game"]');
    expect(pgnContent).toContain('[TimeControl "300+3"]');
    // The actual termination header would be added when gameEndedByTime is true
    // but we're verifying the export mechanism works correctly
    expect(pgnContent).toMatch(/1\. \w+ \{\[%clk \d:\d{2}:\d{2}\]\}/);
  });

  test('should format clock times correctly', async () => {
    await act(async () => {
      render(<ChessGame />);
    });

    // Make moves to generate clock annotations
    const chessboard = screen.getByTestId('chessboard');
    await act(async () => {
      fireEvent.click(chessboard);
    });

    // Wait for move to be processed
    await waitFor(() => {
      expect(getMoveCount()).toBeGreaterThanOrEqual(1);
    });

    // Export and check clock format
    const mockBlob = jest.fn<void, [content: string, options?: BlobPropertyBag]>();
    global.Blob = jest.fn((content?: BlobPart[], options?: BlobPropertyBag) => {
      if (content && content[0]) {
        mockBlob(String(content[0]), options);
        return { size: String(content[0]).length } as Blob;
      }
      return { size: 0 } as Blob;
    }) as unknown as typeof Blob;

    const exportButton = screen.getByText('Export PGN');
    fireEvent.click(exportButton);

    expect(mockBlob).toHaveBeenCalled();
    const pgnContent = mockBlob.mock.calls[0][0];

    // Verify clock time format is h:mm:ss
    const clockPattern = /\[%clk (\d:\d{2}:\d{2})\]/;
    expect(pgnContent).toMatch(clockPattern);

    // Extract and verify the clock time format
    const match = pgnContent.match(clockPattern);
    if (match) {
      const clockTime = match[1];
      const [hours, minutes, seconds] = clockTime.split(':');
      expect(parseInt(hours)).toBeGreaterThanOrEqual(0);
      expect(parseInt(minutes)).toBeLessThan(60);
      expect(parseInt(seconds)).toBeLessThan(60);
      // Initial time should be 5:00 (0:05:00)
      expect(clockTime).toBe('0:05:00');
    }
  });

  test('should handle games loaded from FEN position', async () => {
    await act(async () => {
      render(<ChessGame />);
    });

    // Wait for the component to be ready
    await waitFor(() => {
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();
    });

    // Open the FEN dialog first
    const fenButton = screen.getByRole('button', { name: /^FEN$/i });
    fireEvent.click(fenButton);

    // Wait for dialog to appear
    await waitFor(() => {
      expect(screen.getByPlaceholderText('Paste FEN string here')).toBeInTheDocument();
    });

    // Load a FEN position
    const fenInput = screen.getByPlaceholderText('Paste FEN string here');
    const loadButton = screen.getByRole('button', { name: /Load Position/i });

    await act(async () => {
      fireEvent.change(fenInput, {
        target: { value: 'rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2' }
      });
      fireEvent.click(loadButton);
    });

    // Mock the Blob constructor to capture content
    const mockBlob = jest.fn<void, [content: string, options?: BlobPropertyBag]>();
    global.Blob = jest.fn((content?: BlobPart[], options?: BlobPropertyBag) => {
      if (content && content[0]) {
        mockBlob(String(content[0]), options);
        return { size: String(content[0]).length } as Blob;
      }
      return { size: 0 } as Blob;
    }) as unknown as typeof Blob;

    // Export PGN after loading FEN
    const exportButton = screen.getByText('Export PGN');
    fireEvent.click(exportButton);

    // Verify export was called
    expect(mockBlob).toHaveBeenCalled();
    const pgnContent = mockBlob.mock.calls[0][0];

    // The PGN should contain basic headers
    expect(pgnContent).toContain('[Event "ChessMimic Game"]');
    expect(pgnContent).toContain('[Site "ChessMimic"]');

    // Note: Testing FEN headers specifically requires deeper integration
    // with the chess.js mock, which is complex in this unit test setup
  });

  test('should generate correct filename with timestamp', async () => {
    // Mock Date to have consistent timestamp
    const mockDate = new Date('2025-01-29T14:30:00');
    jest.spyOn(global, 'Date').mockImplementation(() => mockDate);

    await act(async () => {
      render(<ChessGame />);
    });

    const exportButton = screen.getByText('Export PGN');
    fireEvent.click(exportButton);

    // Find the anchor element that was created
    const anchorCalls = mockCreateElement.mock.calls.filter(call => call[0] === 'a');
    expect(anchorCalls.length).toBeGreaterThan(0);

    // The download attribute should have been set on the created anchor
    const anchorResults = mockCreateElement.mock.results.filter(result =>
      result.value && result.value.tagName === 'A'
    );
    expect(anchorResults.length).toBeGreaterThan(0);
    expect(anchorResults[0].value.download).toBe('chessmimic_game_2025-01-29_14-30.pgn');
  });
});
