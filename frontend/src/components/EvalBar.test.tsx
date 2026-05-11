import React from 'react';
import { render, screen } from '@testing-library/react';
import '@testing-library/jest-dom';
import EvalBar from './EvalBar';

describe('EvalBar', () => {
  it('renders with evaluation value', () => {
    render(
      <EvalBar 
        evaluation={0.5}
        isWhiteToMove={true}
      />
    );
    
    // Should show 0.5 for white advantage
    expect(screen.getByText('0.50')).toBeInTheDocument();
  });

  it('shows correct evaluation for black advantage', () => {
    render(
      <EvalBar 
        evaluation={-0.3}
        isWhiteToMove={false}
      />
    );
    
    // Should show -0.3 for black advantage
    expect(screen.getByText('-0.30')).toBeInTheDocument();
  });

  it('renders bar visualization correctly', () => {
    render(
      <EvalBar 
        evaluation={0.5}
        isWhiteToMove={true}
      />
    );
    
    const evalBar = screen.getByRole('meter');
    
    expect(evalBar).toBeInTheDocument();
    
    // Check that the bar has the correct width style
    const barFill = evalBar.querySelector('[style*="width"]');
    expect(barFill).toBeInTheDocument();
    // For +0.5 evaluation, bar should be 75% filled (50% + 25%)
    expect(barFill).toHaveStyle({ width: '75%' });
  });

  it('updates when evaluation changes', () => {
    const { rerender } = render(
      <EvalBar 
        evaluation={0.0}
        isWhiteToMove={true}
      />
    );
    
    expect(screen.getByText('0.00')).toBeInTheDocument();
    
    // Update props to simulate position change
    rerender(
      <EvalBar 
        evaluation={0.8}
        isWhiteToMove={true}
      />
    );
    
    expect(screen.getByText('0.80')).toBeInTheDocument();
  });

  it('handles loading state', () => {
    render(
      <EvalBar 
        isLoading={true}
        isWhiteToMove={true}
      />
    );
    
    expect(screen.getByText('...')).toBeInTheDocument();
  });

  it('handles error state', () => {
    render(
      <EvalBar 
        error="Failed to load evaluation"
        isWhiteToMove={true}
      />
    );
    
    expect(screen.getByText('?')).toBeInTheDocument();
  });

  it('has proper accessibility attributes', () => {
    render(
      <EvalBar 
        evaluation={0.25}
        isWhiteToMove={true}
      />
    );
    
    const evalBar = screen.getByRole('meter');
    expect(evalBar).toHaveAttribute('aria-label', 'Position evaluation');
    expect(evalBar).toHaveAttribute('aria-valuenow', '0.25');
    expect(evalBar).toHaveAttribute('aria-valuemin', '-1');
    expect(evalBar).toHaveAttribute('aria-valuemax', '1');
    expect(evalBar).toHaveAttribute('aria-valuetext', 'White advantage: 0.25');
  });

  it('shows tooltip with evaluation', () => {
    render(
      <EvalBar
        evaluation={-0.45}
        isWhiteToMove={true}
      />
    );

    const evalBar = screen.getByRole('meter');
    expect(evalBar).toHaveAttribute('data-bs-title', 'Evaluation: -0.45 (Black advantage)');
  });

  it('handles edge case values', () => {
    const { rerender } = render(
      <EvalBar 
        evaluation={1.0}
        isWhiteToMove={true}
      />
    );
    
    expect(screen.getByText('1.00')).toBeInTheDocument();
    
    rerender(
      <EvalBar 
        evaluation={-1.0}
        isWhiteToMove={false}
      />
    );
    
    expect(screen.getByText('-1.00')).toBeInTheDocument();
  });

  it('shows equal position correctly', () => {
    render(
      <EvalBar 
        evaluation={0.0}
        isWhiteToMove={true}
      />
    );
    
    expect(screen.getByText('0.00')).toBeInTheDocument();
    
    const { container: newContainer } = render(
      <EvalBar 
        evaluation={0.0}
        isWhiteToMove={false}
      />
    );
    
    const barFill = newContainer.querySelector('[style*="width"]');
    // For 0.0 evaluation, bar should be exactly 50% filled
    expect(barFill).toHaveStyle({ width: '50%' });
  });

  it('applies custom className if provided', () => {
    const { container } = render(
      <EvalBar 
        evaluation={0.1}
        isWhiteToMove={true}
        className="custom-eval-bar"
      />
    );
    
    const evalBarContainer = container.querySelector('.custom-eval-bar');
    expect(evalBarContainer).toBeInTheDocument();
  });

  it('renders vertical eval bar when vertical prop is true', () => {
    const { container } = render(
      <EvalBar
        evaluation={0.3}
        isWhiteToMove={true}
        vertical={true}
      />
    );

    const verticalBar = screen.getByRole('meter');
    // Check for vertical layout by looking for flex-column container
    const verticalContainer = container.querySelector('.flex-column');

    expect(verticalBar).toBeInTheDocument();
    expect(verticalContainer).toBeInTheDocument();
    expect(screen.getByText('0.30')).toBeInTheDocument(); // Shows formatted value
  });

  it('shows correct styles for winning side in vertical mode', () => {
    const { rerender } = render(
      <EvalBar
        evaluation={0.5}
        isWhiteToMove={true}
        vertical={true}
      />
    );

    let valueSpan = screen.getByText('0.50');
    expect(valueSpan).toHaveClass('text-white');

    rerender(
      <EvalBar
        evaluation={-0.4}
        isWhiteToMove={false}
        vertical={true}
      />
    );

    valueSpan = screen.getByText('-0.40');
    expect(valueSpan).toHaveClass('text-dark', 'bg-white');
  });

  it('vertical bar fill corresponds to evaluation', () => {
    const { container } = render(
      <EvalBar 
        evaluation={0.6}
        isWhiteToMove={true}
        vertical={true}
      />
    );
    
    const barFill = container.querySelector('[style*="height: 30%"]');
    // 0.6 evaluation = 0.6 * 50 = 30% height from center
    expect(barFill).toBeInTheDocument();
    // For positive evaluation, bar extends upward from 50%
    expect(barFill).toHaveStyle({ bottom: '50%' });
  });

  describe('Probability Regions', () => {
    it('renders three probability regions when probabilities are provided', () => {
      render(
        <EvalBar 
          evaluation={0.2}
          blackProb={0.3}
          drawProb={0.5}
          whiteProb={0.2}
          vertical={true}
        />
      );
      
      const blackRegion = screen.getByTestId('black-prob-region');
      const drawRegion = screen.getByTestId('draw-prob-region');
      const whiteRegion = screen.getByTestId('white-prob-region');
      
      expect(blackRegion).toBeInTheDocument();
      expect(drawRegion).toBeInTheDocument();
      expect(whiteRegion).toBeInTheDocument();
    });

    it('probability regions have correct heights in vertical mode', () => {
      render(
        <EvalBar 
          evaluation={0.2}
          blackProb={0.3}
          drawProb={0.5}
          whiteProb={0.2}
          vertical={true}
        />
      );
      
      const blackRegion = screen.getByTestId('black-prob-region');
      const drawRegion = screen.getByTestId('draw-prob-region');
      const whiteRegion = screen.getByTestId('white-prob-region');
      
      expect(blackRegion).toHaveStyle({ height: '30%' });
      expect(drawRegion).toHaveStyle({ height: '50%' });
      expect(whiteRegion).toHaveStyle({ height: '20%' });
    });

    it('probability regions have correct widths in horizontal mode', () => {
      render(
        <EvalBar 
          evaluation={0.2}
          blackProb={0.3}
          drawProb={0.5}
          whiteProb={0.2}
          vertical={false}
        />
      );
      
      const blackRegion = screen.getByTestId('black-prob-region');
      const drawRegion = screen.getByTestId('draw-prob-region');
      const whiteRegion = screen.getByTestId('white-prob-region');
      
      expect(blackRegion).toHaveStyle({ width: '30%' });
      expect(drawRegion).toHaveStyle({ width: '50%' });
      expect(whiteRegion).toHaveStyle({ width: '20%' });
    });

    it('probability regions sum to 100% height/width', () => {
      const testCases = [
        { black: 0.33, draw: 0.34, white: 0.33 },
        { black: 0.1, draw: 0.8, white: 0.1 },
        { black: 0.7, draw: 0.2, white: 0.1 },
        { black: 0.0, draw: 0.0, white: 1.0 },
      ];

      testCases.forEach(probs => {
        const { unmount } = render(
          <EvalBar 
            evaluation={probs.black * -1 + probs.white + probs.draw * 0.5}
            blackProb={probs.black}
            drawProb={probs.draw}
            whiteProb={probs.white}
            vertical={true}
          />
        );

        const blackRegion = screen.getByTestId('black-prob-region');
        const drawRegion = screen.getByTestId('draw-prob-region');
        const whiteRegion = screen.getByTestId('white-prob-region');

        // Extract height percentages
        const blackHeight = parseFloat(blackRegion.style.height);
        const drawHeight = parseFloat(drawRegion.style.height);
        const whiteHeight = parseFloat(whiteRegion.style.height);

        expect(blackHeight + drawHeight + whiteHeight).toBeCloseTo(100, 1);
        
        // Clean up to avoid multiple elements with same test ID
        unmount();
      });
    });

    it('tooltip shows all three probabilities', () => {
      render(
        <EvalBar
          evaluation={0.2}
          blackProb={0.3}
          drawProb={0.5}
          whiteProb={0.2}
        />
      );

      const evalBar = screen.getByRole('meter');
      const tooltip = evalBar.getAttribute('data-bs-title');

      expect(tooltip).toContain('Black: 30%');
      expect(tooltip).toContain('Draw: 50%');
      expect(tooltip).toContain('White: 20%');
    });

    it('handles backward compatibility when probabilities are not provided', () => {
      const { container } = render(
        <EvalBar
          evaluation={0.5}
          isWhiteToMove={true}
        />
      );

      // Should render the old style bar, not probability regions
      const blackRegion = screen.queryByTestId('black-prob-region');
      const drawRegion = screen.queryByTestId('draw-prob-region');
      const whiteRegion = screen.queryByTestId('white-prob-region');

      expect(blackRegion).not.toBeInTheDocument();
      expect(drawRegion).not.toBeInTheDocument();
      expect(whiteRegion).not.toBeInTheDocument();

      // Should have the traditional gradient bar (element with background-image style)
      const gradientBar = container.querySelector('[style*="linear-gradient"]');
      expect(gradientBar).toBeInTheDocument();
    });

    it('applies correct colors to probability regions', () => {
      render(
        <EvalBar
          evaluation={0.2}
          blackProb={0.3}
          drawProb={0.5}
          whiteProb={0.2}
          vertical={true}
        />
      );

      const blackRegion = screen.getByTestId('black-prob-region');
      const drawRegion = screen.getByTestId('draw-prob-region');
      const whiteRegion = screen.getByTestId('white-prob-region');

      expect(blackRegion).toHaveClass('bg-black');
      expect(drawRegion).toHaveClass('bg-secondary');
      expect(whiteRegion).toHaveClass('bg-white');
    });

    it('handles edge cases with 0% probability regions', () => {
      render(
        <EvalBar 
          evaluation={1.0}
          blackProb={0.0}
          drawProb={0.0}
          whiteProb={1.0}
          vertical={true}
        />
      );
      
      const blackRegion = screen.getByTestId('black-prob-region');
      const drawRegion = screen.getByTestId('draw-prob-region');
      const whiteRegion = screen.getByTestId('white-prob-region');
      
      expect(blackRegion).toHaveStyle({ height: '0%' });
      expect(drawRegion).toHaveStyle({ height: '0%' });
      expect(whiteRegion).toHaveStyle({ height: '100%' });
    });

    it('updates correctly when probabilities change', () => {
      const { rerender } = render(
        <EvalBar
          evaluation={0.0}
          blackProb={0.33}
          drawProb={0.34}
          whiteProb={0.33}
          vertical={true}
        />
      );

      let blackRegion = screen.getByTestId('black-prob-region');
      expect(blackRegion).toHaveStyle({ height: '33%' });

      // Update probabilities
      rerender(
        <EvalBar
          evaluation={0.5}
          blackProb={0.1}
          drawProb={0.3}
          whiteProb={0.6}
          vertical={true}
        />
      );

      blackRegion = screen.getByTestId('black-prob-region');
      expect(blackRegion).toHaveStyle({ height: '10%' });
    });
  });
});