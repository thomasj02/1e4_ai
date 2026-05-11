import { useState, useEffect, useRef, useCallback } from 'react';
import type { ChessColor, MoveTimes, UseChessClockReturn, TimeWarningLevel } from '../types';

/**
 * Custom hook for managing chess clock functionality
 */
export const useChessClock = (
  initialTime: number,
  increment: number,
  onTimeWarning?: (player: ChessColor, warningLevel: TimeWarningLevel) => void
): UseChessClockReturn => {
  const [whiteTime, setWhiteTime] = useState<number>(initialTime);
  const [blackTime, setBlackTime] = useState<number>(initialTime);
  const [activeColor, setActiveColor] = useState<ChessColor | null>(null);
  const [clockRunning, setClockRunning] = useState<boolean>(false);
  const [gameEndedByTime, setGameEndedByTime] = useState<boolean>(false);
  const [timeWinner, setTimeWinner] = useState<ChessColor | null>(null);
  const [moveTimes, setMoveTimes] = useState<MoveTimes>({ white: [], black: [] });
  const [turnStartTime, setTurnStartTime] = useState<number | null>(null);
  const clockIntervalRef = useRef<NodeJS.Timeout | null>(null);

  // Timeseal helper - add increment and switch clocks
  const handleMoveCompletion = useCallback((
    playerWhoJustMoved: ChessColor,
    currentTurn: ChessColor,
    skipIncrement: boolean = false
  ): void => {
    console.log(`handleMoveCompletion: player=${playerWhoJustMoved}, nextTurn=${currentTurn}, skipIncrement=${skipIncrement}`);
    // The interval has already been deducting time, so we just add increment
    // skipIncrement is used for bot moves - the bot gets increment after thinking, not before
    if (!skipIncrement) {
      if (playerWhoJustMoved === 'white') {
        console.log(`Adding increment to white: ${increment}ms`);
        setWhiteTime((prevTime) => prevTime + increment);
      } else {
        console.log(`Adding increment to black: ${increment}ms`);
        setBlackTime((prevTime) => prevTime + increment);
      }
    }
    
    // Reset turn start time for the next player
    setTurnStartTime(typeof performance !== 'undefined' ? performance.now() : Date.now());
    // Set active clock to current player's turn
    setActiveColor(currentTurn);
    // Make sure clock is running
    setClockRunning(true);
    console.log(`Clock switched to: ${currentTurn}`);
  }, [increment]);

  // Clock management effect with timeseal
  useEffect(() => {
    if (!clockRunning || !activeColor) {
      if (clockIntervalRef.current) {
        clearInterval(clockIntervalRef.current);
        clockIntervalRef.current = null;
      }
      return;
    }

    clockIntervalRef.current = setInterval(() => {
      if (activeColor === 'white') {
        setWhiteTime((prevTime) => {
          const newTime = Math.max(0, prevTime - 100);
          if (newTime === 0) {
            // Time forfeit for white - black wins
            setClockRunning(false);
            setGameEndedByTime(true);
            setTimeWinner('black');
          }
          return newTime;
        });
      } else if (activeColor === 'black') {
        setBlackTime((prevTime) => {
          const newTime = Math.max(0, prevTime - 100);
          if (newTime === 0) {
            // Time forfeit for black - white wins
            setClockRunning(false);
            setGameEndedByTime(true);
            setTimeWinner('white');
          }
          return newTime;
        });
      }
    }, 100); // Update every 100ms for smooth display

    return () => {
      if (clockIntervalRef.current) {
        clearInterval(clockIntervalRef.current);
        clockIntervalRef.current = null;
      }
    };
  }, [clockRunning, activeColor]);

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      if (clockIntervalRef.current) {
        clearInterval(clockIntervalRef.current);
        clockIntervalRef.current = null;
      }
    };
  }, []);

  const stopClock = useCallback((): void => {
    setClockRunning(false);
    setActiveColor(null);
  }, []);

  const resetClock = useCallback((newInitialTime: number = initialTime): void => {
    setWhiteTime(newInitialTime);
    setBlackTime(newInitialTime);
    setActiveColor(null);
    setClockRunning(false);
    setGameEndedByTime(false);
    setTimeWinner(null);
    setMoveTimes({ white: [], black: [] });
    setTurnStartTime(null);
    
    if (clockIntervalRef.current) {
      clearInterval(clockIntervalRef.current);
      clockIntervalRef.current = null;
    }
  }, [initialTime]);

  const addMoveTime = useCallback((color: ChessColor, time: number): void => {
    setMoveTimes(prev => ({
      ...prev,
      [color]: [...prev[color], time]
    }));
  }, []);

  useEffect(() => {
    const checkTimeWarning = (player: ChessColor, time: number) => {
      let warningLevel: TimeWarningLevel = 'none';
      if (time < 10000) {
        warningLevel = 'critical';
      } else if (time < 30000) {
        warningLevel = 'low';
      } else if (time < 60000) {
        warningLevel = 'caution';
      }

      if (warningLevel !== 'none' && onTimeWarning) {
        onTimeWarning(player, warningLevel);
      }
    };

    checkTimeWarning('white', whiteTime);
    checkTimeWarning('black', blackTime);
  }, [whiteTime, blackTime, onTimeWarning]);

  return {
    whiteTime,
    blackTime,
    activeColor,
    clockRunning,
    gameEndedByTime,
    timeWinner,
    moveTimes,
    turnStartTime,
    handleMoveCompletion,
    stopClock,
    resetClock,
    addMoveTime,
    setWhiteTime,
    setBlackTime,
    setClockRunning,
    setActiveColor
  };
};