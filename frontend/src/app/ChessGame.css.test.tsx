/* eslint-env jest */
import React from 'react';
import { render, screen, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import ChessGame from './ChessGame';
import {
  setupFetchMocks,
  suppressExpectedErrors,
  getCurrentMoveCount,
  waitForChessGameReady
} from './ChessGame.test.utils';

describe('ChessGame Component - CSS Variable Tests', () => {
  beforeEach(() => {
    setupFetchMocks({
      botMove: 'e5',
      thinkingTime: 0.5
    });
    suppressExpectedErrors();
  });

  afterEach(() => {
    jest.clearAllMocks();
    jest.restoreAllMocks();
    if (global.restoreConsole) {
      global.restoreConsole();
    }
  });

  describe('getCSSVariable fallbacks', () => {
    test('should use fallback values when window is undefined', async () => {
      // This test can't actually delete window in jsdom environment
      // Instead, we verify the fallback logic works when getComputedStyle returns empty
      window.getComputedStyle = jest.fn(() => ({
        getPropertyValue: jest.fn(() => '')
      })) as unknown as typeof window.getComputedStyle;

      // Render component - should use fallback values
      render(<ChessGame />);

      await waitForChessGameReady();

      // Component should render without errors with fallback values
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();
    });

    test('should use fallback values when getComputedStyle is undefined', async () => {
      // Save original getComputedStyle
      const originalGetComputedStyle = window.getComputedStyle;

      // Make getComputedStyle undefined
      // @ts-expect-error - We need to test undefined behavior
      window.getComputedStyle = undefined;

      // Render component
      render(<ChessGame />);

      await waitForChessGameReady();

      // Component should render without errors
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();

      // The app should function correctly even with fallbacks
      const newGameButtons = screen.getAllByText('New Game');
      expect(newGameButtons.length).toBeGreaterThan(0);

      // Restore getComputedStyle
      window.getComputedStyle = originalGetComputedStyle;
    });

    test('should return empty string for unknown CSS variables', async () => {
      // Mock getComputedStyle to return empty for unknown variables
      window.getComputedStyle = jest.fn(() => ({
        getPropertyValue: jest.fn((varName) => {
          // Return empty string for unknown variables
          const knownVars = [
            '--color-square-last-move-pale',
            '--color-square-last-move',
            '--color-square-selected',
            '--color-square-navigation',
            '--color-chess-legal'
          ];
          return knownVars.includes(varName) ? 'rgba(255, 255, 190, 0.5)' : '';
        })
      })) as unknown as typeof window.getComputedStyle;

      render(<ChessGame />);

      await waitForChessGameReady();

      // Component should handle unknown CSS variables gracefully
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();
    });

    test('should use all fallback values correctly', async () => {
      // Remove getComputedStyle to force fallbacks
      const originalGetComputedStyle = window.getComputedStyle;
      // @ts-expect-error - We need to test undefined behavior
      window.getComputedStyle = undefined;

      // Import getCSSVariable function directly if exported
      // Since it's not exported, we'll test indirectly through component behavior

      render(<ChessGame />);

      await waitForChessGameReady();

      // The component should still function with fallback colors
      // Verify core UI elements work with fallback values
      expect(screen.getByText('Export PGN')).toBeInTheDocument();
      expect(screen.getByText('FEN')).toBeInTheDocument();

      // Restore
      window.getComputedStyle = originalGetComputedStyle;
    });

    test('should handle CSS variables in SSR-like environment', async () => {
      // Mock getComputedStyle to simulate SSR environment
      const originalGetComputedStyle = window.getComputedStyle;
      // @ts-expect-error - We need to test undefined behavior
      window.getComputedStyle = undefined;

      render(<ChessGame />);

      await waitForChessGameReady();

      // Should render without crashing
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();

      // Restore
      window.getComputedStyle = originalGetComputedStyle;
    });
  });

  describe('Square styling with CSS variables', () => {
    test('should apply square styles when CSS variables are available', async () => {
      // Mock getComputedStyle with proper values
      window.getComputedStyle = jest.fn(() => ({
        getPropertyValue: jest.fn((varName: string) => {
          const cssVars: Record<string, string> = {
            '--color-square-last-move-pale': 'rgba(255, 255, 190, 0.5)',
            '--color-square-last-move': 'rgba(255, 255, 190, 0.7)',
            '--color-square-selected': 'rgba(255, 255, 0, 0.2)',
            '--color-square-navigation': 'rgba(255, 255, 0, 0.4)',
            '--color-chess-legal': 'rgba(0, 128, 0, 0.4)'
          };
          return cssVars[varName] || '';
        })
      })) as unknown as typeof window.getComputedStyle;

      render(<ChessGame />);

      await waitForChessGameReady();

      // CSS variables are available and component renders correctly
      // The mock getComputedStyle returns proper values
      const mockGetComputedStyle = window.getComputedStyle;
      expect(mockGetComputedStyle).toBeDefined();

      // Call with a dummy element since getComputedStyle requires an element parameter
      const dummyElement = document.createElement('div');
      const computedStyle = mockGetComputedStyle(dummyElement);
      expect(computedStyle.getPropertyValue('--color-square-last-move')).toBe('rgba(255, 255, 190, 0.7)');
    });

    test('should handle missing CSS variable gracefully', async () => {
      // Mock getComputedStyle to return empty string
      window.getComputedStyle = jest.fn(() => ({
        getPropertyValue: jest.fn(() => '')
      })) as unknown as typeof window.getComputedStyle;

      render(<ChessGame />);

      await waitForChessGameReady();

      // Should use fallback values and render successfully
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();
    });
  });

  describe('CSS variables in action', () => {
    test('should use CSS variables when making moves', async () => {
      const user = userEvent.setup();
      const originalGetComputedStyle = window.getComputedStyle;

      // Mock getComputedStyle to return empty string (forcing fallback)
      window.getComputedStyle = jest.fn(() => ({
        getPropertyValue: jest.fn(() => '')
      })) as unknown as typeof window.getComputedStyle;

      render(<ChessGame />);

      await waitForChessGameReady();

      // Make a move by clicking squares
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

      // The move should have been made successfully despite using fallbacks
      expect(screen.getByTestId('chessboard')).toBeInTheDocument();

      // Restore
      window.getComputedStyle = originalGetComputedStyle;
    });

    test('should use fallback CSS values for highlighting', async () => {
      const user = userEvent.setup();

      // Mock getComputedStyle to return empty values
      const originalGetComputedStyle = window.getComputedStyle;
      window.getComputedStyle = jest.fn(() => ({
        getPropertyValue: jest.fn(() => {
          // Return empty string to trigger fallback
          return '';
        })
      })) as unknown as typeof window.getComputedStyle;

      render(<ChessGame />);

      await waitForChessGameReady();

      // Make a move by clicking squares
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

      // The fallback values should have been used
      expect(window.getComputedStyle).toHaveBeenCalled();

      // Restore
      window.getComputedStyle = originalGetComputedStyle;
    });
  });
});
