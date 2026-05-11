'use client';

import React from 'react';
import MoveList from './MoveList';
import type { MoveHistoryProps } from '../types';

const MoveHistory: React.FC<MoveHistoryProps> = ({ 
  moveHistory, 
  viewedPlyIndex, 
  onGoToPly, 
  boardHeight,
  isMobileLayout 
}) => {
  return (
    <div 
      className={isMobileLayout ? "h-100 d-flex align-items-center px-2" : "card bg-dark text-light border-secondary shadow-sm h-100 overflow-auto"}
      style={boardHeight ? { height: `${boardHeight}px` } : undefined}
    >
      <MoveList
        history={moveHistory}
        viewedPlyIndex={viewedPlyIndex}
        onGoToPly={onGoToPly}
        isMobileLayout={isMobileLayout}
      />
    </div>
  );
};

export default MoveHistory;