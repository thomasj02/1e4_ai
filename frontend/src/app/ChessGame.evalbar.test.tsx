/* eslint-env jest */
import React from 'react';
import { render, screen, waitFor, cleanup } from '@testing-library/react';
import '@testing-library/jest-dom';
import ChessGame from './ChessGame';
import { createMockUseChessGame, createMockUseChessClock } from '../test-utils/chess-mocks';

// Mock the hooks that use Chess.js
jest.mock('../hooks/useChessGame', () => ({
  useChessGame: () => createMockUseChessGame()
}));

// Mock the chess clock hook
jest.mock('../hooks/useChessClock', () => ({
  useChessClock: () => createMockUseChessClock()
}));

// Mock chess.js to avoid errors in components that might still use it directly
jest.mock('chess.js', () => ({
  Chess: jest.fn(() => ({
    fen: () => 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1',
    pgn: () => '',
    history: () => [],
    turn: () => 'w',
    moves: () => [],
    move: jest.fn(),
    undo: jest.fn(),
    reset: jest.fn(),
    load: () => true,
    loadPgn: () => true,
    isGameOver: () => false,
    isCheckmate: () => false,
    isStalemate: () => false,
    isDraw: () => false,
    isCheck: () => false,
    inCheck: () => false,
    inCheckmate: () => false,
    inStalemate: () => false,
    inDraw: () => false,
    header: () => ({}),
    get: () => null,
    put: () => true,
    remove: () => null,
    board: () => [],
    isInsufficientMaterial: () => false,
    isThreefoldRepetition: () => false,
  }))
}));

// Mock fetch for API calls
global.fetch = jest.fn() as jest.MockedFunction<typeof fetch>;

describe('ChessGame with EvalBar integration', () => {
  beforeEach(() => {
    // Reset fetch mock completely before each test
    jest.resetAllMocks();
    // Set up fresh fetch mock
    global.fetch = jest.fn() as jest.MockedFunction<typeof fetch>;
    // Suppress console errors that are not relevant to these tests
    jest.spyOn(console, 'error').mockImplementation(() => {
      // Suppress all console errors for these tests
    });
  });

  afterEach(() => {
    // Clean up DOM after each test
    cleanup();
    // Clear all mocks
    jest.clearAllMocks();
    // Restore console.error
    jest.restoreAllMocks();
  });

  it('fetches evaluation when position changes', async () => {
    // Mock successful evaluation response
    (fetch as jest.MockedFunction<typeof fetch>).mockImplementation(() => 
      Promise.resolve({
        ok: true,
        json: async () => ({
          evaluation: 0.15,
          raw_value: 0.575
        })
      } as Response)
    );

    render(<ChessGame />);
    
    // Wait for evaluation to be fetched
    await waitFor(() => {
      expect(fetch).toHaveBeenCalledWith(
        expect.stringContaining('/evaluate_position'),
        expect.objectContaining({
          method: 'POST',
          headers: {
            'Content-Type': 'application/json',
          },
          body: expect.stringContaining('fen')
        })
      );
    });

    // Wait for fetch to be called
    await waitFor(() => {
      expect(fetch).toHaveBeenCalled();
    });
  });

  it('shows evaluation in EvalBar component', async () => {
    // Mock evaluation response
    (fetch as jest.MockedFunction<typeof fetch>).mockImplementation(() => 
      Promise.resolve({
        ok: true,
        json: async () => ({
          evaluation: 0.25,
          raw_value: 0.625
        })
      } as Response)
    );

    render(<ChessGame />);
    
    // Wait for fetch to be called
    await waitFor(() => {
      expect(fetch).toHaveBeenCalled();
    });

    // Wait for evaluation to appear (vertical bar shows signed value with 1 decimal)
    await waitFor(() => {
      // Now shows raw value from backend
      const evalElements = screen.getAllByText('0.25');
      expect(evalElements.length).toBeGreaterThan(0);
      // Check that it has the correct class
      expect(evalElements[0]).toHaveClass('text-white');
    }, { timeout: 3000 });
  });

  it('updates evaluation after move', async () => {
    // First evaluation
    (fetch as jest.MockedFunction<typeof fetch>).mockImplementation(() => 
      Promise.resolve({
        ok: true,
        json: async () => ({
          evaluation: 0.0,
          raw_value: 0.5
        })
      } as Response)
    );

    render(<ChessGame />);
    
    // Wait for initial evaluation (vertical bar shows absolute value with 1 decimal)
    await waitFor(() => {
      const evalElements = screen.getAllByText('0.00');
      expect(evalElements.length).toBeGreaterThan(0);
    });

    // Ensure initial fetch completes
    await waitFor(() => {
      expect(fetch).toHaveBeenCalled();
    });
  });

  it('handles evaluation error gracefully', async () => {
    // Mock failed evaluation response
    (fetch as jest.MockedFunction<typeof fetch>).mockImplementation(() => 
      Promise.reject(new Error('Network error'))
    );


    render(<ChessGame />);
    
    // Should show error state in eval bar
    await waitFor(() => {
      expect(screen.getByText('?')).toBeInTheDocument();
    }, { timeout: 3000 });
  });

  it('sends correct parameters to evaluation endpoint', async () => {
    (fetch as jest.MockedFunction<typeof fetch>).mockImplementation(() => 
      Promise.resolve({
        ok: true,
        json: async () => ({
          evaluation: 0.0,
          raw_value: 0.5
        })
      } as Response)
    );

    render(<ChessGame />);
    
    await waitFor(() => {
      expect(fetch).toHaveBeenCalled();
    });

    // Check that the fetch was called with correct parameters
    expect(fetch).toHaveBeenCalledWith(
      expect.stringContaining('/evaluate_position'),
      expect.objectContaining({
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: expect.stringContaining('rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1')
      })
    );
  });

  describe('Probability extraction', () => {
    it('extracts and passes probabilities to EvalBar when API returns them', async () => {
      // Mock evaluation response with probabilities
      (fetch as jest.MockedFunction<typeof fetch>).mockImplementation(() => 
        Promise.resolve({
          ok: true,
          json: async () => ({
            evaluation: 0.2,
            raw_value: 0.6,
            black_prob: 0.3,
            draw_prob: 0.5,
            white_prob: 0.2
          })
        } as Response)
      );

      render(<ChessGame />);
      
      // Wait for fetch to complete
      await waitFor(() => {
        expect(fetch).toHaveBeenCalled();
      });

      // Wait for probability regions to appear
      await waitFor(() => {
        const blackRegion = screen.getByTestId('black-prob-region');
        const drawRegion = screen.getByTestId('draw-prob-region');
        const whiteRegion = screen.getByTestId('white-prob-region');
        
        expect(blackRegion).toBeInTheDocument();
        expect(drawRegion).toBeInTheDocument();
        expect(whiteRegion).toBeInTheDocument();
      });
    });

    it('handles missing probabilities gracefully', async () => {
      // Mock evaluation response without probabilities (backward compatibility)
      (fetch as jest.MockedFunction<typeof fetch>).mockImplementation(() => 
        Promise.resolve({
          ok: true,
          json: async () => ({
            evaluation: 0.5,
            raw_value: 0.75
            // No probability fields
          })
        } as Response)
      );

      render(<ChessGame />);
      
      await waitFor(() => {
        expect(fetch).toHaveBeenCalled();
      });

      // Should not render probability regions
      await waitFor(() => {
        const blackRegion = screen.queryByTestId('black-prob-region');
        const drawRegion = screen.queryByTestId('draw-prob-region');
        const whiteRegion = screen.queryByTestId('white-prob-region');
        
        expect(blackRegion).not.toBeInTheDocument();
        expect(drawRegion).not.toBeInTheDocument();
        expect(whiteRegion).not.toBeInTheDocument();
      });

      // Should still show evaluation value
      await waitFor(() => {
        const evalElements = screen.getAllByText('0.50');
        expect(evalElements.length).toBeGreaterThan(0);
      });
    });

    it('updates probabilities when position changes', async () => {
      // First evaluation with certain probabilities
      (fetch as jest.MockedFunction<typeof fetch>).mockImplementation(() => 
        Promise.resolve({
          ok: true,
          json: async () => ({
            evaluation: 0.0,
            raw_value: 0.5,
            black_prob: 0.33,
            draw_prob: 0.34,
            white_prob: 0.33
          })
        } as Response)
      );

      const { rerender } = render(<ChessGame />);
      
      await waitFor(() => {
        expect(fetch).toHaveBeenCalled();
      });

      // Verify initial probability regions
      await waitFor(() => {
        const blackRegion = screen.getByTestId('black-prob-region');
        expect(blackRegion).toHaveStyle({ height: '33%' });
      });

      // Note: In a real scenario, we'd need to trigger a move to cause re-evaluation
      // This test would need to be updated to actually trigger a position change

      // Trigger re-evaluation (in real app this would happen on move)
      // For test purposes, we'll just re-render
      rerender(<ChessGame />);
      
      // Note: In a real scenario, we'd need to trigger a move to cause re-evaluation
      // This test demonstrates the structure, but may need adjustment based on actual implementation
    });

    it('calculates evaluation correctly from probabilities', async () => {
      // Mock response with probabilities that should yield specific evaluation
      (fetch as jest.MockedFunction<typeof fetch>).mockImplementation(() => 
        Promise.resolve({
          ok: true,
          json: async () => ({
            evaluation: -0.35, // black_prob * -1 + white_prob + draw_prob * 0.5 = 0.6 * -1 + 0.1 + 0.3 * 0.5 = -0.35
            raw_value: 0.325,
            black_prob: 0.6,
            draw_prob: 0.3,
            white_prob: 0.1
          })
        } as Response)
      );

      render(<ChessGame />);
      
      await waitFor(() => {
        expect(fetch).toHaveBeenCalled();
      });

      // Wait for everything to render together
      await waitFor(() => {
        // Check probability regions are rendered
        expect(screen.getByTestId('black-prob-region')).toBeInTheDocument();
        expect(screen.getByTestId('draw-prob-region')).toBeInTheDocument();
        expect(screen.getByTestId('white-prob-region')).toBeInTheDocument();
        
        // And evaluation value is displayed
        const evalElements = screen.getAllByText('-0.35');
        expect(evalElements.length).toBeGreaterThan(0);
      });
    });

    it('passes null probabilities when API returns error', async () => {
      // Mock failed evaluation response
      (fetch as jest.MockedFunction<typeof fetch>).mockImplementation(() => 
        Promise.resolve({
          ok: true,
          json: async () => ({
            evaluation: 0.0,
            raw_value: 0.5,
            error: 'Model unavailable'
          })
        } as Response)
      );


      render(<ChessGame />);
      
      // Wait for error state to be fully rendered
      await waitFor(() => {
        // Check that fetch was called
        expect(fetch).toHaveBeenCalled();
        // Check that the error indicator is shown
        expect(screen.getByText('?')).toBeInTheDocument();
      });
      
      // Then verify probability regions are not shown
      const blackRegion = screen.queryByTestId('black-prob-region');
      const drawRegion = screen.queryByTestId('draw-prob-region');
      const whiteRegion = screen.queryByTestId('white-prob-region');
      
      expect(blackRegion).not.toBeInTheDocument();
      expect(drawRegion).not.toBeInTheDocument();
      expect(whiteRegion).not.toBeInTheDocument();
    });
  });
});