'use client';

import React, {useEffect, useRef, useState} from 'react';
import type {EnhancedHistoryEntry} from '@/types';

interface MoveListProps {
  history: EnhancedHistoryEntry[] & { customStartFen?: string };
  viewedPlyIndex: number;
  onGoToPly: (plyIndex: number) => void;
  isMobileLayout?: boolean;
}

interface MovePair {
  moveNumber: number;
  white: EnhancedHistoryEntry;
  black: EnhancedHistoryEntry | null;
  whiteIndex: number;
  blackIndex: number;
}

function MoveList({ history, viewedPlyIndex, onGoToPly, isMobileLayout }: MoveListProps) {
  const movesContainerRef = useRef<HTMLDivElement>(null);
  const activeItemRef = useRef<HTMLDivElement>(null);
  const [isMobile, setIsMobile] = useState(isMobileLayout || window.innerWidth <= 767);

  // Detect mobile resize only if not forced by isMobileLayout
  useEffect(() => {
    if (isMobileLayout !== undefined) {
      setIsMobile(isMobileLayout);
    } else {
      const handleResize = () => {
        setIsMobile(window.innerWidth <= 767);
      };
      window.addEventListener('resize', handleResize);
      return () => window.removeEventListener('resize', handleResize);
    }
  }, [isMobileLayout]);

  // Scroll the active move into view whenever viewedPlyIndex changes
  useEffect(() => {
    if (activeItemRef.current && movesContainerRef.current) {
      // Check if scrollIntoView is available (not in test environment)
      if (typeof activeItemRef.current.scrollIntoView === 'function') {
        activeItemRef.current.scrollIntoView({ 
          behavior: 'smooth', 
          block: 'nearest' 
        });
      }
    }
  }, [viewedPlyIndex]);

  // Group history into pairs for display rows
  const movePairs: MovePair[] = [];
  for (let i = 0; i < history.length; i += 2) {
    movePairs.push({
      moveNumber: Math.floor(i / 2) + 1,
      white: history[i],
      black: history[i + 1] || null, // Handle case where black hasn't moved yet
      whiteIndex: i,
      blackIndex: i + 1,
    });
  }


  // Check if history has a custom starting position
  let startPositionLabel = "Start Position";
  
  // Check for customStartFen directly on the history array (from handleLoadFen)
  if (history.customStartFen) {
    startPositionLabel = history.customStartFen.split(' ')[0].substring(0, 12) + '...';
  }
  // Or check on the first move (from generateEnhancedHistory)
  else if (history.length > 0 && history[0]?.customStartFen) {
    // Use a shortened version of custom starting FEN
    startPositionLabel = history[0].customStartFen.split(' ')[0].substring(0, 12) + '...';
  }

  // Mobile layout - horizontal scrolling
  if (isMobile) {
    return (
      <div 
        ref={movesContainerRef}
        data-testid="move-list"
        className="d-flex align-items-center gap-2 p-2 font-monospace small text-nowrap overflow-auto"
        style={{ overflowY: 'hidden', WebkitOverflowScrolling: 'touch' }}
      >
        {/* Start Position */}
        <span
          className={`px-2 py-1 rounded ${
            viewedPlyIndex === -1 
              ? 'fw-bold bg-primary text-white' 
              : 'bg-secondary text-light'
          }`}
          style={{ cursor: 'pointer', minWidth: 'fit-content' }}
          onClick={() => onGoToPly(-1)}
          ref={viewedPlyIndex === -1 ? activeItemRef : null}
        >
          Start
        </span>

        {/* All moves in a single line */}
        {history.map((move, index) => {
          const isActive = index === viewedPlyIndex;
          const moveNumber = Math.floor(index / 2) + 1;
          const isWhite = index % 2 === 0;
          
          return (
            <React.Fragment key={index}>
              {isWhite && (
                <span className="text-muted">
                  {moveNumber}.
                </span>
              )}
              <span
                ref={isActive ? activeItemRef : null}
                className={`px-2 py-1 rounded ${
                  isActive 
                    ? 'fw-bold bg-info text-white' 
                    : 'bg-secondary text-light'
                }`}
                style={{ cursor: 'pointer', minWidth: 'fit-content' }}
                onClick={() => onGoToPly(index)}
              >
                {move.san}
              </span>
            </React.Fragment>
          );
        })}
      </div>
    );
  }

  // Desktop layout - vertical list
  return (
    <div 
      ref={movesContainerRef}
      data-testid="move-list"
      className="px-2 py-1 font-monospace small"
    >
      {/* Start Position Button */}
      <div
        className={`d-inline-block px-1 py-1 mb-2 rounded ${
          viewedPlyIndex === -1 
            ? 'fw-bold bg-primary text-white' 
            : 'bg-secondary text-light'
        }`}
        style={{ cursor: 'pointer' }}
        onClick={() => onGoToPly(-1)}
        ref={viewedPlyIndex === -1 ? activeItemRef : null}
      >
        {startPositionLabel}
      </div>

      {/* Moves Table using Flexbox */}
      {movePairs.map((pair) => {
        const isWhiteActive = pair.whiteIndex === viewedPlyIndex;
        const isBlackActive = pair.black && pair.blackIndex === viewedPlyIndex;

        return (
          <div
            key={pair.moveNumber}
            className="d-flex mb-1 align-items-center"
          >
            {/* Move Number */}
            <span className="text-muted me-2" style={{ width: '30px' }}>
              {pair.moveNumber}.
            </span>

            {/* White Move */}
            <span
              ref={isWhiteActive ? activeItemRef : null}
              className={`flex-fill px-1 py-1 rounded me-2 text-start ${
                isWhiteActive 
                  ? 'fw-bold bg-info text-white' 
                  : 'bg-secondary text-light'
              }`}
              style={{ cursor: 'pointer' }}
              onClick={() => onGoToPly(pair.whiteIndex)}
            >
              {pair.white.san}
            </span>

            {/* Black Move */}
            <span
              ref={isBlackActive ? activeItemRef : null}
              className={`flex-fill px-1 py-1 rounded text-start ${
                isBlackActive 
                  ? 'fw-bold bg-info text-white' 
                  : 'bg-secondary text-light'
              }`}
              style={{ cursor: pair.black ? 'pointer' : 'default', minHeight: '1em' }}
              onClick={() => pair.black && onGoToPly(pair.blackIndex)}
            >
              {pair.black ? pair.black.san : ''}
            </span>
          </div>
        );
      })}
    </div>
  );
}

export default MoveList;