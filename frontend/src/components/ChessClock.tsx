'use client';

import React, { useEffect, useRef } from 'react';
import { Card, ListGroup } from 'react-bootstrap';
import type { ChessClockProps, ChessColor, TimeWarningLevel } from '../types';

const ChessClock: React.FC<ChessClockProps> = ({ whiteTime, blackTime, activeColor, clockRunning, onTimeWarning }) => {
  // Format time for display
  const formatTime = (milliseconds: number): string => {
    // Ensure non-negative time
    const ms = Math.max(0, milliseconds);
    
    const hours = Math.floor(ms / 3600000);
    const minutes = Math.floor((ms % 3600000) / 60000);
    const seconds = Math.floor((ms % 60000) / 1000);
    const tenths = Math.floor((ms % 1000) / 100);
    
    // Show tenths of seconds when under 10 seconds
    if (ms < 10000) {
      return `${minutes}:${seconds.toString().padStart(2, '0')}.${tenths}`;
    }
    
    // Show HH:MM:SS for times over 1 hour
    if (hours > 0) {
      return `${hours}:${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}`;
    }
    
    // Show MM:SS for times under 1 hour
    return `${minutes}:${seconds.toString().padStart(2, '0')}`;
  };

  // Phase 7: Enhanced time warning levels
  const isCriticalTime = (time: number): boolean => time < 10000; // Under 10 seconds
  const isLowTime = (time: number): boolean => time < 30000; // Under 30 seconds
  const isCautionTime = (time: number): boolean => time < 60000; // Under 60 seconds

  // Track previous warning states to avoid duplicate callbacks
  const prevWarningLevels = useRef<{ white: TimeWarningLevel; black: TimeWarningLevel }>({ white: 'none', black: 'none' });

  // Phase 7: Sound effects - detect warning level changes
  useEffect(() => {
    if (!onTimeWarning) return;

    // Helper function to get warning level for a given time
    const getWarningLevel = (time: number): TimeWarningLevel => {
      if (isCriticalTime(time)) return 'critical';
      if (isLowTime(time)) return 'low';
      if (isCautionTime(time)) return 'caution';
      return 'none';
    };

    const whiteLevel = getWarningLevel(whiteTime);
    const blackLevel = getWarningLevel(blackTime);

    // Helper to check if we've entered a new warning level (not just changed)
    const hasEnteredNewWarning = (currentLevel: TimeWarningLevel, prevLevel: TimeWarningLevel): boolean => {
      const levels: TimeWarningLevel[] = ['none', 'caution', 'low', 'critical'];
      const currentIndex = levels.indexOf(currentLevel);
      const prevIndex = levels.indexOf(prevLevel);
      
      // Only trigger if we've moved to a more severe warning level
      return currentIndex > prevIndex && currentLevel !== 'none';
    };

    // Check if white entered a new warning level
    if (hasEnteredNewWarning(whiteLevel, prevWarningLevels.current.white)) {
      onTimeWarning('white', whiteLevel);
    }

    // Check if black entered a new warning level
    if (hasEnteredNewWarning(blackLevel, prevWarningLevels.current.black)) {
      onTimeWarning('black', blackLevel);
    }

    // Update previous warning levels
    prevWarningLevels.current = { white: whiteLevel, black: blackLevel };
  }, [whiteTime, blackTime, onTimeWarning]);

  const getItemClasses = (color: ChessColor, time: number): string => {
    let classes = 'd-flex flex-column align-items-center p-3';
    
    if (activeColor === color) {
      classes += ' active';
      if (clockRunning) {
        // Phase 7: Add urgent styling for critical time
        if (isCriticalTime(time)) {
          classes += ' border border-danger border-2';
        } else {
          classes += ' border border-primary border-2';
        }
      }
    }
    
    return classes;
  };

  const getTimeClasses = (time: number): string => {
    let classes = 'font-monospace fs-3';
    
    // Phase 7: Prioritized warning levels
    if (isCriticalTime(time)) {
      classes += ' text-danger';
      // Note: Bootstrap doesn't have animate-pulse, would need custom CSS
    } else if (isLowTime(time)) {
      classes += ' text-warning';
    } else if (isCautionTime(time)) {
      classes += ' text-warning opacity-75';
    }
    
    return classes;
  };

  return (
    <Card className="shadow w-100 mb-3">
      <ListGroup horizontal className="w-100">
        <ListGroup.Item 
          data-testid="black-clock" 
          className={getItemClasses('black', blackTime)}
          style={{ flex: 1 }}
        >
          <div className="text-muted">Black</div>
          <div data-testid="black-time" className={getTimeClasses(blackTime)}>
            {formatTime(blackTime)}
          </div>
        </ListGroup.Item>
        
        <ListGroup.Item 
          data-testid="white-clock" 
          className={getItemClasses('white', whiteTime)}
          style={{ flex: 1 }}
        >
          <div className="text-muted">White</div>
          <div data-testid="white-time" className={getTimeClasses(whiteTime)}>
            {formatTime(whiteTime)}
          </div>
        </ListGroup.Item>
      </ListGroup>
    </Card>
  );
};

export default ChessClock;