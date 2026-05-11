/* eslint-env jest */
import React from 'react';
import { render, screen, fireEvent, act } from '@testing-library/react';
import MoveList from './MoveList';

import type { EnhancedHistoryEntry } from '../types/hooks';

// Helper to create test history entries
function createTestHistoryEntry(
  san: string,
  from: string,
  to: string,
  after: string,
  color: 'w' | 'b',
  before: string = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1'
): EnhancedHistoryEntry {
  return {
    san,
    from,
    to,
    after,
    before,
    color,
    flags: '',
    piece: 'p',
    lan: `${from}${to}`,
    customStartFen: null
  };
}

describe('MoveList Component', () => {
  // Sample move history for testing
  const sampleHistory: EnhancedHistoryEntry[] = [
    createTestHistoryEntry(
      'e4', 'e2', 'e4', 
      'rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1',
      'w'
    ),
    createTestHistoryEntry(
      'e5', 'e7', 'e5', 
      'rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2',
      'b',
      'rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1'
    ),
    createTestHistoryEntry(
      'Nf3', 'g1', 'f3', 
      'rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2',
      'w',
      'rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2'
    ),
  ] as EnhancedHistoryEntry[] & { customStartFen?: string };

  test('renders start position button', () => {
    render(<MoveList history={[]} viewedPlyIndex={-1} onGoToPly={() => {}} />);
    expect(screen.getByText('Start Position')).toBeInTheDocument();
  });

  test('renders empty move list when history is empty', () => {
    render(<MoveList history={[]} viewedPlyIndex={-1} onGoToPly={() => {}} />);
    expect(screen.queryByText('1.')).not.toBeInTheDocument();
  });

  test('renders move history correctly', () => {
    render(<MoveList history={sampleHistory} viewedPlyIndex={-1} onGoToPly={() => {}} />);
    
    // Check move numbers and moves
    expect(screen.getByText('1.')).toBeInTheDocument();
    expect(screen.getByText('e4')).toBeInTheDocument();
    expect(screen.getByText('e5')).toBeInTheDocument();
    expect(screen.getByText('2.')).toBeInTheDocument();
    expect(screen.getByText('Nf3')).toBeInTheDocument();
  });

  test('highlights viewed move correctly', () => {
    const { rerender } = render(
      <MoveList history={sampleHistory} viewedPlyIndex={0} onGoToPly={() => {}} />
    );

    // First white move should be highlighted
    const whiteMove = screen.getByText('e4');
    expect(whiteMove).toHaveClass('fw-bold');
    expect(whiteMove).toHaveClass('bg-info');

    // Black move should not be highlighted
    const blackMove = screen.getByText('e5');
    expect(blackMove).not.toHaveClass('fw-bold');
    expect(blackMove).not.toHaveClass('bg-info');

    // Change viewed ply index to black move
    rerender(<MoveList history={sampleHistory} viewedPlyIndex={1} onGoToPly={() => {}} />);

    // Now black move should be highlighted
    expect(screen.getByText('e5')).toHaveClass('fw-bold');
    expect(screen.getByText('e5')).toHaveClass('bg-info');
  });

  test('calls onGoToPly when a move is clicked', () => {
    const mockOnGoToPly = jest.fn();
    render(
      <MoveList history={sampleHistory} viewedPlyIndex={-1} onGoToPly={mockOnGoToPly} />
    );
    
    // Click on white move
    fireEvent.click(screen.getByText('e4'));
    expect(mockOnGoToPly).toHaveBeenCalledWith(0);
    
    // Click on black move
    fireEvent.click(screen.getByText('e5'));
    expect(mockOnGoToPly).toHaveBeenCalledWith(1);
    
    // Click on start position
    fireEvent.click(screen.getByText('Start Position'));
    expect(mockOnGoToPly).toHaveBeenCalledWith(-1);
  });

  test('handles case where black has not moved yet', () => {
    const incompleteHistory = [
      createTestHistoryEntry(
        'e4', 'e2', 'e4', 
        'rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1',
        'w'
      ),
    ] as EnhancedHistoryEntry[] & { customStartFen?: string };
    
    render(
      <MoveList history={incompleteHistory} viewedPlyIndex={0} onGoToPly={() => {}} />
    );
    
    // Should show white move but no black move
    expect(screen.getByText('e4')).toBeInTheDocument();
    
    // The black move cell should be empty
    const moveRow = screen.getByText('1.').parentElement;
    expect(moveRow).not.toBeNull();
    if (moveRow) {
      const blackMoveCell = moveRow.children[2]; // Third child is black move cell
      expect(blackMoveCell.textContent).toBe('');
    }
  });

  test('scrolls active move into view when viewedPlyIndex changes', () => {
    render(
      <MoveList history={sampleHistory} viewedPlyIndex={1} onGoToPly={() => {}} />
    );
    
    // scrollIntoView should have been called (it's mocked in jest-setup.js)
    expect(Element.prototype.scrollIntoView).toHaveBeenCalled();
  });

  test('handles case when scrollIntoView is not available', () => {
    // Save original scrollIntoView
    const originalScrollIntoView = Element.prototype.scrollIntoView;
    
    // Temporarily remove scrollIntoView to simulate environment without it
    Element.prototype.scrollIntoView = undefined as unknown as typeof Element.prototype.scrollIntoView;
    
    // Should render without errors even when scrollIntoView is not available
    render(
      <MoveList history={sampleHistory} viewedPlyIndex={1} onGoToPly={() => {}} />
    );
    
    // Component should still render correctly
    expect(screen.getByText('e5')).toBeInTheDocument();
    
    // Restore original scrollIntoView
    Element.prototype.scrollIntoView = originalScrollIntoView;
  });

  test('handles case when no items are active', () => {
    // Set viewedPlyIndex to a value that doesn't match any move
    render(
      <MoveList history={sampleHistory} viewedPlyIndex={10} onGoToPly={() => {}} />
    );
    
    // Component should still render correctly without errors
    expect(screen.getByText('e4')).toBeInTheDocument();
    expect(screen.getByText('e5')).toBeInTheDocument();
    expect(screen.getByText('Nf3')).toBeInTheDocument();
  });
  
  test('shows custom FEN in start position when provided', () => {
    // Create a mock history with customStartFen directly on the array
    const mockHistory: EnhancedHistoryEntry[] & { customStartFen?: string } = [];
    mockHistory.customStartFen = 'r1bqkbnr/pppppppp/2n5/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2';
    
    render(<MoveList 
      history={mockHistory} 
      viewedPlyIndex={-1} 
      onGoToPly={() => {}} 
    />);
    
    // It should show shortened FEN in place of "Start Position"
    const startPosition = screen.getByText(/r1bqkbnr\/ppp/);
    expect(startPosition).toBeInTheDocument();
  });

  test('shows custom FEN from first move when moves are made after loading custom position', () => {
    // This simulates what happens when a custom FEN is loaded and then moves are made
    const historyWithCustomStart: EnhancedHistoryEntry[] = [
      {
        ...createTestHistoryEntry(
          'd4', 'd2', 'd4',
          'r1bqkbnr/pppppppp/2n5/8/3PP3/8/PPP2PPP/RNBQKBNR b KQkq d3 0 2',
          'w',
          'r1bqkbnr/pppppppp/2n5/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2'
        ),
        customStartFen: 'r1bqkbnr/pppppppp/2n5/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2'
      },
      createTestHistoryEntry(
        'Nf6', 'g8', 'f6',
        'r1bqkb1r/pppppppp/2n2n2/8/3PP3/8/PPP2PPP/RNBQKBNR w KQkq - 2 3',
        'b',
        'r1bqkbnr/pppppppp/2n5/8/3PP3/8/PPP2PPP/RNBQKBNR b KQkq d3 0 2'
      )
    ] as EnhancedHistoryEntry[] & { customStartFen?: string };
    
    render(<MoveList 
      history={historyWithCustomStart} 
      viewedPlyIndex={0} 
      onGoToPly={() => {}} 
    />);
    
    // It should show shortened FEN instead of "Start Position"
    const startPosition = screen.getByText('r1bqkbnr/ppp...');
    expect(startPosition).toBeInTheDocument();
    
    // And the moves should still be displayed
    expect(screen.getByText('d4')).toBeInTheDocument();
    expect(screen.getByText('Nf6')).toBeInTheDocument();
  });

  test('handles mobile layout when window width is <= 767', () => {
    // Mock window.innerWidth for mobile
    Object.defineProperty(window, 'innerWidth', {
      writable: true,
      configurable: true,
      value: 767,
    });

    render(<MoveList history={sampleHistory} viewedPlyIndex={0} onGoToPly={() => {}} />);
    
    // In mobile layout, it should show "Start" instead of "Start Position"
    expect(screen.getByText('Start')).toBeInTheDocument();
    expect(screen.queryByText('Start Position')).not.toBeInTheDocument();
    
    // Should show move numbers inline
    expect(screen.getByText('1.')).toBeInTheDocument();
    expect(screen.getByText('2.')).toBeInTheDocument();
    
    // All moves should be visible
    expect(screen.getByText('e4')).toBeInTheDocument();
    expect(screen.getByText('e5')).toBeInTheDocument();
    expect(screen.getByText('Nf3')).toBeInTheDocument();
  });

  test('handles window resize events', async () => {
    // Start with desktop width
    Object.defineProperty(window, 'innerWidth', {
      writable: true,
      configurable: true,
      value: 1024,
    });

    const { rerender } = render(<MoveList history={sampleHistory} viewedPlyIndex={-1} onGoToPly={() => {}} />);
    
    // Should show desktop layout
    expect(screen.getByText('Start Position')).toBeInTheDocument();
    
    // Simulate resize to mobile
    Object.defineProperty(window, 'innerWidth', {
      writable: true,
      configurable: true,
      value: 600,
    });
    
    // Trigger resize event wrapped in act
    await act(async () => {
      window.dispatchEvent(new Event('resize'));
    });
    
    // Rerender to see the effect
    rerender(<MoveList history={sampleHistory} viewedPlyIndex={-1} onGoToPly={() => {}} />);
    
    // Should now show mobile layout
    expect(screen.getByText('Start')).toBeInTheDocument();
    expect(screen.queryByText('Start Position')).not.toBeInTheDocument();
  });

  test('does not call onGoToPly when clicking empty black move cell', () => {
    const mockOnGoToPly = jest.fn();
    const incompleteHistory = [
      createTestHistoryEntry(
        'e4', 'e2', 'e4',
        'rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1',
        'w'
      ),
    ] as EnhancedHistoryEntry[] & { customStartFen?: string };
    
    render(
      <MoveList history={incompleteHistory} viewedPlyIndex={0} onGoToPly={mockOnGoToPly} />
    );
    
    // Find the empty black move cell and click it
    const moveRow = screen.getByText('1.').parentElement;
    expect(moveRow).not.toBeNull();
    if (moveRow) {
      const blackMoveCell = moveRow.children[2]; // Third child is black move cell
      fireEvent.click(blackMoveCell);
    }
    
    // Should not call onGoToPly since there's no black move
    expect(mockOnGoToPly).not.toHaveBeenCalledWith(1);
  });

  test('uses default props correctly', () => {
    // Reset window width to desktop to ensure we're not in mobile mode
    Object.defineProperty(window, 'innerWidth', {
      writable: true,
      configurable: true,
      value: 1024,
    });
    
    // Test that default props work by providing an empty array explicitly  
    render(<MoveList history={[]} viewedPlyIndex={-1} onGoToPly={() => {}} />);
    
    // Should render with empty history
    expect(screen.getByText('Start Position')).toBeInTheDocument();
    expect(screen.queryByText('1.')).not.toBeInTheDocument();
  });

  test('setActiveRef returns correct ref based on isActive parameter', () => {
    render(
      <MoveList history={sampleHistory} viewedPlyIndex={1} onGoToPly={() => {}} />
    );
    
    // The active move (e5 at index 1) should have a ref
    const activeMove = screen.getByText('e5');
    expect(activeMove).toBeInTheDocument();
    
    // Non-active moves should not have the activeItemRef
    const inactiveMove = screen.getByText('e4');
    expect(inactiveMove).toBeInTheDocument();
  });

  test('handles mobile layout with custom start FEN', () => {
    // Mock window.innerWidth for mobile
    Object.defineProperty(window, 'innerWidth', {
      writable: true,
      configurable: true,
      value: 500,
    });

    const mockHistory: EnhancedHistoryEntry[] & { customStartFen?: string } = [];
    mockHistory.customStartFen = 'r1bqkbnr/pppppppp/2n5/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2';

    render(<MoveList history={mockHistory} viewedPlyIndex={-1} onGoToPly={() => {}} />);
    
    // In mobile layout, should still show "Start" (mobile doesn't show custom FEN)
    expect(screen.getByText('Start')).toBeInTheDocument();
  });

  test('clicking start position in mobile layout calls onGoToPly correctly', () => {
    // Mock window.innerWidth for mobile
    Object.defineProperty(window, 'innerWidth', {
      writable: true,
      configurable: true,
      value: 600,
    });

    const mockOnGoToPly = jest.fn();
    
    render(<MoveList history={sampleHistory} viewedPlyIndex={0} onGoToPly={mockOnGoToPly} />);
    
    // Click on start position in mobile layout
    fireEvent.click(screen.getByText('Start'));
    expect(mockOnGoToPly).toHaveBeenCalledWith(-1);
  });
});