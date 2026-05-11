'use client';

import React, { useRef, useEffect, useState } from 'react';
import { Chessboard } from 'react-chessboard';
import EvalBar from './EvalBar';
import type { ChessboardAreaProps } from '../types';

const ChessboardArea: React.FC<ChessboardAreaProps> = ({
  displayedFen,
  evaluation,
  evalLoading,
  evalError,
  evalBlackProb,
  evalDrawProb,
  evalWhiteProb,
  isWhiteToMove,
  onPieceDrop,
  onPromotionCheck,
  onPromotionPieceSelect,
  showLegalMoves,
  onDragEnd,
  onMouseDown,
  onSquareClick,
  squareStyles,
  playerColor,
  isViewingLatest,
  onBoardHeightChange,
  className = '',
  showHistoryOverlay
}) => {
  // Default: show overlay when not viewing latest, unless explicitly set
  const shouldShowOverlay = showHistoryOverlay ?? !isViewingLatest;
  const chessBoardContainerRef = useRef<HTMLDivElement>(null);
  const parentContainerRef = useRef<HTMLDivElement>(null);
  const [, setBoardHeight] = useState<number | null>(null);
  const [containerSize, setContainerSize] = useState<{ width: number; height: number } | null>(null);

  // Resize observer to track board height
  useEffect(() => {
    // Check if ResizeObserver is available (not in some test environments)
    if (typeof ResizeObserver === 'undefined') {
      // Fallback for test environments
      const updateHeight = () => {
        if (chessBoardContainerRef.current) {
          const height = chessBoardContainerRef.current.offsetHeight;
          setBoardHeight(height);
          if (onBoardHeightChange) {
            onBoardHeightChange(height);
          }
        }
      };
      
      updateHeight();
      window.addEventListener('resize', updateHeight);
      const timer = setTimeout(updateHeight, 100);
      
      return () => {
        window.removeEventListener('resize', updateHeight);
        clearTimeout(timer);
      };
    }
    
    const resizeObserver = new ResizeObserver(entries => {
      for (const entry of entries) {
        const height = entry.contentRect.height;
        setBoardHeight(height);
        if (onBoardHeightChange) {
          onBoardHeightChange(height);
        }
      }
    });

    const currentRef = chessBoardContainerRef.current;
    if (currentRef) {
      resizeObserver.observe(currentRef);
    }

    return () => {
      if (currentRef) {
        resizeObserver.unobserve(currentRef);
      }
    };
  }, [onBoardHeightChange]);

  // Resize observer to track parent container size
  useEffect(() => {
    let timeoutId: NodeJS.Timeout;
    
    const updateContainerSize = () => {
      if (parentContainerRef.current) {
        const rect = parentContainerRef.current.getBoundingClientRect();
        setContainerSize({
          width: rect.width,
          height: rect.height
        });
      }
    };

    const debouncedUpdate = () => {
      clearTimeout(timeoutId);
      timeoutId = setTimeout(updateContainerSize, 10);
    };

    // Initial size
    updateContainerSize();

    // ResizeObserver for container changes
    if (typeof ResizeObserver !== 'undefined') {
      const resizeObserver = new ResizeObserver(updateContainerSize);
      
      const currentRef = parentContainerRef.current;
      if (currentRef) {
        resizeObserver.observe(currentRef);
      }

      // Also listen to window resize
      window.addEventListener('resize', debouncedUpdate);

      return () => {
        if (currentRef) {
          resizeObserver.unobserve(currentRef);
        }
        window.removeEventListener('resize', debouncedUpdate);
        clearTimeout(timeoutId);
      };
    } else {
      // Fallback for environments without ResizeObserver
      window.addEventListener('resize', debouncedUpdate);
      return () => {
        window.removeEventListener('resize', debouncedUpdate);
        clearTimeout(timeoutId);
      };
    }
  }, []);

  return (
    <div className={`d-flex align-items-center justify-content-center gap-0 w-100 h-100 position-relative ${className}`}>
      {/* Dynamic height to match chessboard - legitimate inline style for runtime calculation */}
      <div className="flex-shrink-0 d-flex" style={{ 
        height: containerSize ? `${Math.min(containerSize.width - 16, containerSize.height - 16)}px` : '100%'
      }}>
        <EvalBar 
          evaluation={evaluation}
          isWhiteToMove={isWhiteToMove}
          isLoading={evalLoading}
          error={evalError}
          blackProb={evalBlackProb}
          drawProb={evalDrawProb}
          whiteProb={evalWhiteProb}
          vertical={true}
          playerColor={playerColor}
        />
      </div>
      <div 
        ref={parentContainerRef}
        className="position-relative d-flex align-items-center justify-content-center flex-grow-1 p-2"
        style={{ 
          minWidth: 0, 
          minHeight: 0,
          width: '100%',
          height: '100%'
        }}
      >
        <div 
          ref={chessBoardContainerRef} 
          className="position-relative" 
          style={{ 
            aspectRatio: '1',
            width: '100%',
            height: '100%',
            maxWidth: containerSize ? `${Math.min(containerSize.width - 16, containerSize.height - 16)}px` : '100%',
            maxHeight: containerSize ? `${Math.min(containerSize.width - 16, containerSize.height - 16)}px` : '100%'
          }}>
          <Chessboard
          data-testid="chessboard"
          position={displayedFen}
          onPieceDrop={onPieceDrop}
          onPromotionCheck={onPromotionCheck}
          onPromotionPieceSelect={onPromotionPieceSelect}
          onPieceDragBegin={(piece, square) => {
            return showLegalMoves(piece, square);
          }} // Show legal moves when drag begins
          onPieceDragEnd={onDragEnd}        // Clear legal moves when drag ends
          onPieceClick={onMouseDown}        // Toggle legal moves on click
          onSquareClick={onSquareClick}     // Handle click on empty squares for move completion
          customSquareStyles={squareStyles}
          boardOrientation={playerColor}
          arePiecesDraggable={isViewingLatest}
          arePremovesAllowed={true}
        />
        {shouldShowOverlay && (
          <div className="position-absolute top-0 start-0 w-100 h-100 bg-dark bg-opacity-50 d-flex justify-content-center align-items-center text-white fs-4 text-center pe-none">
            Browse History<br/>(Use arrows or click moves to navigate)
          </div>
        )}
        </div>
      </div>
    </div>
  );
};

export default ChessboardArea;