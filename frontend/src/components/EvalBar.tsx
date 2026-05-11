'use client';

import React, { useEffect, useRef } from 'react';
import type { EvalBarProps } from '../types';

const EvalBar: React.FC<EvalBarProps> = ({
  evaluation = 0,
  isLoading = false,
  error = null,
  className = '',
  vertical = false,
  blackProb,
  drawProb,
  whiteProb,
  playerColor = 'white'
}) => {
  const verticalTooltipRef = useRef<HTMLDivElement>(null);
  const horizontalTooltipRef = useRef<HTMLDivElement>(null);
  
  // Format evaluation - round to 2 decimal places
  const formatEval = (value: number): string => {
    return value.toFixed(2);
  };
  
  // Create tooltip text
  const tooltipText = error
    ? error
    : blackProb !== undefined && drawProb !== undefined && whiteProb !== undefined
    ? `Black: ${(blackProb * 100).toFixed(0)}%, Draw: ${(drawProb * 100).toFixed(0)}%, White: ${(whiteProb * 100).toFixed(0)}%`
    : evaluation > 0
    ? `Evaluation: ${evaluation} (White advantage)`
    : evaluation < 0
    ? `Evaluation: ${evaluation} (Black advantage)`
    : `Evaluation: ${evaluation} (Equal position)`;
  
  // Initialize Bootstrap tooltip
  useEffect(() => {
    // Capture refs to avoid stale closure issues
    const verticalEl = verticalTooltipRef.current;
    const horizontalEl = horizontalTooltipRef.current;
    
    // Dynamically import Bootstrap to avoid SSR issues
    const initTooltips = async () => {
      if (typeof window !== 'undefined') {
        const { Tooltip } = await import('bootstrap');
        
        // Initialize vertical tooltip if it exists
        if (verticalEl) {
          new Tooltip(verticalEl, {
            trigger: 'hover focus',
            placement: 'left',
            delay: { show: 100, hide: 100 }
          });
        }
        
        // Initialize horizontal tooltip if it exists
        if (horizontalEl) {
          new Tooltip(horizontalEl, {
            trigger: 'hover focus',
            placement: 'top',
            delay: { show: 100, hide: 100 }
          });
        }
      }
    };
    
    initTooltips();
    
    // Cleanup tooltips on unmount
    return () => {
      if (typeof window !== 'undefined') {
        // Dispose tooltips
        const disposeTooltip = async () => {
          const { Tooltip } = await import('bootstrap');
          if (verticalEl) {
            const tooltip = Tooltip.getInstance(verticalEl);
            if (tooltip) tooltip.dispose();
          }
          if (horizontalEl) {
            const tooltip = Tooltip.getInstance(horizontalEl);
            if (tooltip) tooltip.dispose();
          }
        };
        disposeTooltip();
      }
    };
  }, [vertical, tooltipText]); // Re-initialize if orientation or tooltip text changes
  
  // Calculate bar fill percentage
  // For horizontal: Convert -1 to 1 range to 0% to 100%
  // -1 = 0%, 0 = 50%, 1 = 100%
  const fillPercentage = ((evaluation + 1) / 2) * 100;
  
  // For vertical bar: Calculate height and position relative to center
  // The bar extends from center (50%) either up or down
  const verticalBarHeight = Math.abs(evaluation) * 50; // Max 50% height
  const isPositive = evaluation > 0;
  
  // Determine aria text based on evaluation
  const ariaText = evaluation > 0
    ? `White advantage: ${formatEval(evaluation)}`
    : evaluation < 0
    ? `Black advantage: ${formatEval(evaluation)}`
    : 'Equal position';
  
  // Determine which side is winning for vertical display
  const whiteWinning = evaluation > 0;
  const blackWinning = evaluation < 0;
  const displayValue = formatEval(evaluation);
  
  if (vertical) {
    return (
      <div className={`d-flex flex-column align-items-center h-100 me-2 gap-0 ${className}`} style={{ width: '40px' }}>
        <div className="bg-dark text-white px-1 py-1 rounded-top text-sm fw-bold text-center shadow border border-2 border-secondary border-bottom-0 d-flex align-items-center justify-content-center flex-shrink-0" style={{ width: '40px', height: '32px' }}>
          {isLoading ? (
            <span className="text-secondary">...</span>
          ) : error ? (
            <span className="text-danger">?</span>
          ) : (
            <span className={whiteWinning ? 'text-white' : blackWinning ? 'text-dark bg-white border border-secondary rounded px-1 fw-bold' : 'text-muted'}>
              {displayValue}
            </span>
          )}
        </div>
        <div 
          ref={verticalTooltipRef}
          className="position-relative flex-fill bg-secondary bg-opacity-25 border border-2 border-secondary border-top-0 rounded-bottom overflow-hidden shadow-lg"
          style={{ width: '40px', cursor: 'help' }}
          role="meter"
          aria-label="Position evaluation"
          aria-valuenow={evaluation}
          aria-valuemin={-1}
          aria-valuemax={1}
          aria-valuetext={ariaText}
          data-bs-toggle="tooltip"
          data-bs-placement="left"
          data-bs-title={tooltipText}
        >
          <div className="position-absolute bottom-0 start-0 end-0 top-0 bg-secondary bg-opacity-50">
            {!error && blackProb !== undefined && drawProb !== undefined && whiteProb !== undefined ? (
              <>
                {/* Probability regions - stacked from bottom to top */}
                {/* If player is white, white is at bottom. If player is black, black is at bottom */}
                <div 
                  data-testid={playerColor === 'white' ? "white-prob-region" : "black-prob-region"}
                  className={`position-absolute bottom-0 start-0 end-0 ${playerColor === 'white' ? 'bg-white border-top border-secondary' : 'bg-black'}`}
                  style={{ height: `${(playerColor === 'white' ? whiteProb : blackProb) * 100}%` }}
                />
                <div 
                  data-testid="draw-prob-region"
                  className="position-absolute start-0 end-0 bg-secondary"
                  style={{ 
                    bottom: `${(playerColor === 'white' ? whiteProb : blackProb) * 100}%`,
                    height: `${drawProb * 100}%` 
                  }}
                />
                <div 
                  data-testid={playerColor === 'white' ? "black-prob-region" : "white-prob-region"}
                  className={`position-absolute top-0 start-0 end-0 ${playerColor === 'white' ? 'bg-black' : 'bg-white border-bottom border-secondary'}`}
                  style={{ height: `${(playerColor === 'white' ? blackProb : whiteProb) * 100}%` }}
                />
              </>
            ) : (
              <>
                {evaluation !== 0 && (
                  <div 
                    className={`position-absolute start-0 end-0 ${
                      // If player is white: positive eval (white advantage) = white bar from center down
                      // If player is black: positive eval (white advantage) = white bar from center up
                      (isPositive && playerColor === 'white') || (!isPositive && playerColor === 'black')
                        ? 'bg-white border-top border-light' 
                        : 'bg-black border-bottom border-dark'
                    }`}
                    style={{ 
                      height: `${verticalBarHeight}%`,
                      // If player is white: positive = bottom, negative = top
                      // If player is black: positive = top, negative = bottom
                      [(isPositive && playerColor === 'white') || (!isPositive && playerColor === 'black') ? 'bottom' : 'top']: '50%'
                    }}
                  />
                )}
                <div className="eval-bar-vertical-center-line position-absolute start-0 end-0 bg-secondary" style={{ height: '2px', zIndex: 10 }} />
              </>
            )}
          </div>
        </div>
      </div>
    );
  }
  
  // Horizontal bar (original implementation)
  return (
    <div className={`w-100 p-2 ${className}`}>
      <div 
        ref={horizontalTooltipRef}
        className="position-relative w-100 bg-light border border-secondary rounded overflow-hidden d-flex align-items-center"
        style={{ height: '40px', cursor: 'help' }}
        role="meter"
        aria-label="Position evaluation"
        aria-valuenow={evaluation}
        aria-valuemin={-1}
        aria-valuemax={1}
        aria-valuetext={ariaText}
        data-bs-toggle="tooltip"
        data-bs-placement="top"
        data-bs-title={tooltipText}
      >
        <div className="position-absolute top-0 start-0 end-0 bottom-0 bg-dark">
          {!error && blackProb !== undefined && drawProb !== undefined && whiteProb !== undefined ? (
            <>
              {/* Probability regions - stacked from left to right */}
              <div 
                data-testid="black-prob-region"
                className="position-absolute start-0 top-0 bottom-0 bg-black"
                style={{ width: `${blackProb * 100}%` }}
              />
              <div 
                data-testid="draw-prob-region"
                className="position-absolute top-0 bottom-0 bg-secondary"
                style={{ 
                  left: `${blackProb * 100}%`,
                  width: `${drawProb * 100}%` 
                }}
              />
              <div 
                data-testid="white-prob-region"
                className="position-absolute end-0 top-0 bottom-0 bg-white"
                style={{ width: `${whiteProb * 100}%` }}
              />
            </>
          ) : (
            <div 
              className="position-absolute top-0 start-0 bottom-0"
              style={{ 
                backgroundImage: 'linear-gradient(to right, black, #6c757d, white)',
                width: `${fillPercentage}%`
              }}
            />
          )}
        </div>
        <div className="position-relative w-100 text-center fw-bold text-white" style={{ zIndex: 10, fontSize: '1rem' }}>
          {isLoading ? (
            <span className="d-inline-block" style={{ lineHeight: '40px' }}>...</span>
          ) : error ? (
            <span className="d-inline-block" style={{ lineHeight: '40px' }}>?</span>
          ) : (
            <span className="d-inline-block" style={{ lineHeight: '40px', textShadow: '0 1px 2px rgba(0,0,0,0.8)' }}>{formatEval(evaluation)}</span>
          )}
        </div>
      </div>
    </div>
  );
};

export default EvalBar;