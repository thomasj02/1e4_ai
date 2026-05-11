import React from 'react';
import { render, screen } from '@testing-library/react';
import ChessClock from './ChessClock';

describe('ChessClock', () => {
  describe('Initial Display', () => {
    it('should render both player clocks', () => {
      render(<ChessClock whiteTime={300000} blackTime={300000} activeColor={null} clockRunning={false} />);
      
      // Should show time for both players
      expect(screen.getByTestId('white-clock')).toBeInTheDocument();
      expect(screen.getByTestId('black-clock')).toBeInTheDocument();
    });

    it('should display initial time correctly', () => {
      render(<ChessClock whiteTime={300000} blackTime={300000} activeColor={null} clockRunning={false} />);
      
      // 5 minutes = 300000ms should display as 5:00
      expect(screen.getByTestId('white-time')).toHaveTextContent('5:00');
      expect(screen.getByTestId('black-time')).toHaveTextContent('5:00');
    });

    it('should not highlight any clock when no active color', () => {
      render(<ChessClock whiteTime={300000} blackTime={300000} activeColor={null} clockRunning={false} />);

      const whiteClock = screen.getByTestId('white-clock');
      const blackClock = screen.getByTestId('black-clock');

      expect(whiteClock).not.toHaveClass('border-primary');
      expect(blackClock).not.toHaveClass('border-primary');
    });
  });

  describe('Time Formatting', () => {
    it('should format time under 1 hour as MM:SS', () => {
      const testCases = [
        { ms: 0, expected: '0:00' },
        { ms: 1000, expected: '0:01' },
        { ms: 59000, expected: '0:59' },
        { ms: 60000, expected: '1:00' },
        { ms: 90000, expected: '1:30' },
        { ms: 599000, expected: '9:59' },
        { ms: 3599000, expected: '59:59' }
      ];

      const { rerender } = render(<ChessClock whiteTime={0} blackTime={300000} activeColor={null} clockRunning={false} />);
      
      testCases.forEach(({ ms, expected }) => {
        rerender(<ChessClock whiteTime={ms} blackTime={300000} activeColor={null} clockRunning={false} />);
        expect(screen.getByTestId('white-time')).toHaveTextContent(expected);
      });
    });

    it('should format time over 1 hour as H:MM:SS', () => {
      const testCases = [
        { ms: 3600000, expected: '1:00:00' },
        { ms: 3661000, expected: '1:01:01' },
        { ms: 7200000, expected: '2:00:00' },
        { ms: 36000000, expected: '10:00:00' }
      ];

      const { rerender } = render(<ChessClock whiteTime={0} blackTime={300000} activeColor={null} clockRunning={false} />);
      
      testCases.forEach(({ ms, expected }) => {
        rerender(<ChessClock whiteTime={ms} blackTime={300000} activeColor={null} clockRunning={false} />);
        expect(screen.getByTestId('white-time')).toHaveTextContent(expected);
      });
    });

    it('should show tenths of seconds when under 10 seconds', () => {
      const testCases = [
        { ms: 9900, expected: '0:09.9' },
        { ms: 9500, expected: '0:09.5' },
        { ms: 5200, expected: '0:05.2' },
        { ms: 1100, expected: '0:01.1' },
        { ms: 100, expected: '0:00.1' }
      ];

      const { rerender } = render(<ChessClock whiteTime={0} blackTime={300000} activeColor={null} clockRunning={false} />);
      
      testCases.forEach(({ ms, expected }) => {
        rerender(<ChessClock whiteTime={ms} blackTime={300000} activeColor={null} clockRunning={false} />);
        expect(screen.getByTestId('white-time')).toHaveTextContent(expected);
      });
    });

    it('should handle zero and negative time', () => {
      render(<ChessClock whiteTime={0} blackTime={-1000} activeColor={null} clockRunning={false} />);
      
      expect(screen.getByTestId('white-time')).toHaveTextContent('0:00.0');
      expect(screen.getByTestId('black-time')).toHaveTextContent('0:00.0');
    });
  });

  describe('Active Clock Indication', () => {
    it('should highlight white clock when white is active', () => {
      render(<ChessClock whiteTime={300000} blackTime={300000} activeColor="white" clockRunning={true} />);

      const whiteClock = screen.getByTestId('white-clock');
      const blackClock = screen.getByTestId('black-clock');

      expect(whiteClock).toHaveClass('active', 'border', 'border-primary', 'border-2');
      expect(blackClock).not.toHaveClass('border-primary');
    });

    it('should highlight black clock when black is active', () => {
      render(<ChessClock whiteTime={300000} blackTime={300000} activeColor="black" clockRunning={true} />);

      const whiteClock = screen.getByTestId('white-clock');
      const blackClock = screen.getByTestId('black-clock');

      expect(whiteClock).not.toHaveClass('border-primary');
      expect(blackClock).toHaveClass('active', 'border', 'border-primary', 'border-2');
    });
  });

  describe('Low Time Warning', () => {
    it('should add low-time class when time is under 30 seconds', () => {
      render(<ChessClock whiteTime={29000} blackTime={300000} activeColor={null} clockRunning={false} />);
      
      const whiteTime = screen.getByTestId('white-time');
      const blackTime = screen.getByTestId('black-time');
      
      expect(whiteTime).toHaveClass('text-warning');
      expect(blackTime).not.toHaveClass('text-warning');
    });

    it('should add low-time class for both players when both under 30 seconds', () => {
      render(<ChessClock whiteTime={15000} blackTime={20000} activeColor={null} clockRunning={false} />);
      
      expect(screen.getByTestId('white-time')).toHaveClass('text-warning');
      expect(screen.getByTestId('black-time')).toHaveClass('text-warning');
    });

    it('should not have low-time class when time is 30 seconds or more', () => {
      render(<ChessClock whiteTime={30000} blackTime={31000} activeColor={null} clockRunning={false} />);

      // 30-60 seconds is caution time, so it has text-warning with opacity-75
      expect(screen.getByTestId('white-time')).toHaveClass('text-warning', 'opacity-75');
      expect(screen.getByTestId('black-time')).toHaveClass('text-warning', 'opacity-75');
      // But it should not have the danger class for critical time
      expect(screen.getByTestId('white-time')).not.toHaveClass('text-danger');
      expect(screen.getByTestId('black-time')).not.toHaveClass('text-danger');
    });
  });

  describe('Phase 7: Enhanced Time Warnings', () => {
    describe('Multiple Warning Levels', () => {
      it('should add critical-time class when time is under 10 seconds', () => {
        render(<ChessClock whiteTime={9000} blackTime={300000} activeColor={null} clockRunning={false} />);

        const whiteTime = screen.getByTestId('white-time');
        const blackTime = screen.getByTestId('black-time');

        expect(whiteTime).toHaveClass('text-danger');
        expect(blackTime).not.toHaveClass('text-danger');
      });

      it('should add caution-time class when time is under 60 seconds but over 30 seconds', () => {
        render(<ChessClock whiteTime={45000} blackTime={300000} activeColor={null} clockRunning={false} />);

        const whiteTime = screen.getByTestId('white-time');
        const blackTime = screen.getByTestId('black-time');

        expect(whiteTime).toHaveClass('text-warning', 'opacity-75');
        expect(blackTime).not.toHaveClass('text-warning');
        expect(whiteTime).not.toHaveClass('text-danger');
      });

      it('should prioritize critical-time over low-time when under 10 seconds', () => {
        render(<ChessClock whiteTime={5000} blackTime={300000} activeColor={null} clockRunning={false} />);

        const whiteTime = screen.getByTestId('white-time');

        expect(whiteTime).toHaveClass('text-danger');
        expect(whiteTime).not.toHaveClass('opacity-75');
      });

      it('should show warning levels correctly for different time ranges', () => {
        const testCases = [
          { time: 5000, expectedClass: 'text-danger', description: 'critical for 5 seconds' },
          { time: 15000, expectedClass: 'text-warning', description: 'low for 15 seconds' },
          { time: 45000, expectedClass: 'text-warning', description: 'caution for 45 seconds' },
          { time: 70000, expectedClass: null, description: 'no warning for 70 seconds' }
        ];

        testCases.forEach(({ time, expectedClass }) => {
          const { unmount } = render(<ChessClock whiteTime={time} blackTime={300000} activeColor={null} clockRunning={false} />);

          const whiteTime = screen.getByTestId('white-time');

          if (expectedClass) {
            expect(whiteTime).toHaveClass(expectedClass);
          } else {
            expect(whiteTime).not.toHaveClass('text-danger');
            expect(whiteTime).not.toHaveClass('text-warning');
            expect(whiteTime).not.toHaveClass('opacity-75');
          }

          // Clean up this render
          unmount();
        });
      });
    });

    describe('Enhanced Visual Feedback', () => {
      it('should add urgent styling to critical time warning', () => {
        render(<ChessClock whiteTime={8000} blackTime={300000} clockRunning={true} activeColor="white" />);

        const whiteClock = screen.getByTestId('white-clock');

        expect(whiteClock).toHaveClass('border-danger', 'border-2');
      });

      it('should not add urgent styling when clock is not running', () => {
        render(<ChessClock whiteTime={8000} blackTime={300000} clockRunning={false} activeColor="white" />);

        const whiteClock = screen.getByTestId('white-clock');

        expect(whiteClock).not.toHaveClass('border-danger');
      });

      it('should not add urgent styling when not critical time', () => {
        render(<ChessClock whiteTime={15000} blackTime={300000} clockRunning={true} activeColor="white" />);

        const whiteClock = screen.getByTestId('white-clock');

        expect(whiteClock).not.toHaveClass('border-danger');
      });
    });

    describe('Sound Effects (Optional)', () => {
      beforeEach(() => {
        // Mock Audio constructor
        global.Audio = jest.fn().mockImplementation(() => ({
          play: jest.fn().mockResolvedValue(undefined),
          pause: jest.fn(),
          currentTime: 0,
          volume: 1
        }));
      });

      afterEach(() => {
        jest.resetAllMocks();
      });

      it('should call onTimeWarning callback when entering critical time', () => {
        const mockOnTimeWarning = jest.fn();
        
        const { rerender } = render(
          <ChessClock 
            whiteTime={15000} 
            blackTime={300000} 
            activeColor="white" 
            clockRunning={true}
            onTimeWarning={mockOnTimeWarning}
          />
        );
        
        // Change to critical time
        rerender(
          <ChessClock 
            whiteTime={9000} 
            blackTime={300000} 
            activeColor="white" 
            clockRunning={true}
            onTimeWarning={mockOnTimeWarning}
          />
        );
        
        expect(mockOnTimeWarning).toHaveBeenCalledWith('white', 'critical');
      });

      it('should call onTimeWarning callback when entering low time', () => {
        const mockOnTimeWarning = jest.fn();
        
        const { rerender } = render(
          <ChessClock 
            whiteTime={45000} 
            blackTime={300000} 
            activeColor="white" 
            clockRunning={true}
            onTimeWarning={mockOnTimeWarning}
          />
        );
        
        // Change to low time
        rerender(
          <ChessClock 
            whiteTime={25000} 
            blackTime={300000} 
            activeColor="white" 
            clockRunning={true}
            onTimeWarning={mockOnTimeWarning}
          />
        );
        
        expect(mockOnTimeWarning).toHaveBeenCalledWith('white', 'low');
      });

      it('should call onTimeWarning callback when entering caution time', () => {
        const mockOnTimeWarning = jest.fn();
        
        const { rerender } = render(
          <ChessClock 
            whiteTime={70000} 
            blackTime={300000} 
            activeColor="white" 
            clockRunning={true}
            onTimeWarning={mockOnTimeWarning}
          />
        );
        
        // Change to caution time
        rerender(
          <ChessClock 
            whiteTime={55000} 
            blackTime={300000} 
            activeColor="white" 
            clockRunning={true}
            onTimeWarning={mockOnTimeWarning}
          />
        );
        
        expect(mockOnTimeWarning).toHaveBeenCalledWith('white', 'caution');
      });

      it('should not call onTimeWarning when callback is not provided', () => {
        const { rerender } = render(
          <ChessClock 
            whiteTime={15000} 
            blackTime={300000} 
            activeColor="white" 
            clockRunning={true}
          />
        );
        
        // Should not throw error when changing to critical time without callback
        expect(() => {
          rerender(
            <ChessClock 
              whiteTime={9000} 
              blackTime={300000} 
              activeColor="white" 
              clockRunning={true}
            />
          );
        }).not.toThrow();
      });

      it('should only call onTimeWarning once per warning level threshold', () => {
        const mockOnTimeWarning = jest.fn();
        
        // Start with a safe time that doesn't trigger any warnings
        const { rerender } = render(
          <ChessClock 
            whiteTime={120000} 
            blackTime={300000} 
            activeColor="white" 
            clockRunning={true}
            onTimeWarning={mockOnTimeWarning}
          />
        );
        
        // Move to critical time - should trigger callback once
        rerender(
          <ChessClock 
            whiteTime={9000} 
            blackTime={300000} 
            activeColor="white" 
            clockRunning={true}
            onTimeWarning={mockOnTimeWarning}
          />
        );
        
        // Stay in critical time - should not trigger callback again
        rerender(
          <ChessClock 
            whiteTime={8000} 
            blackTime={300000} 
            activeColor="white" 
            clockRunning={true}
            onTimeWarning={mockOnTimeWarning}
          />
        );
        
        // Should only be called once for entering critical threshold
        expect(mockOnTimeWarning).toHaveBeenCalledTimes(1);
        expect(mockOnTimeWarning).toHaveBeenCalledWith('white', 'critical');
      });
    });
  });

  describe('Clock Running State', () => {
    it('should show running indicator when clock is running', () => {
      render(<ChessClock whiteTime={300000} blackTime={300000} clockRunning={true} activeColor="white" />);

      const whiteClock = screen.getByTestId('white-clock');
      expect(whiteClock).toHaveClass('border', 'border-2');
    });

    it('should not show running indicator when clock is not running', () => {
      render(<ChessClock whiteTime={300000} blackTime={300000} clockRunning={false} activeColor="white" />);

      const whiteClock = screen.getByTestId('white-clock');
      expect(whiteClock).not.toHaveClass('border-2');
    });
  });
});