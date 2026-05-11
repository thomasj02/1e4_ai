# Chess Clock Implementation Plan

## Overview
Add a chess clock to the ChessMimic frontend with timeseal-like lag compensation, where users aren't penalized for network latency between their browser and the server.

## Requirements
1. **Timeseal-like behavior**: Clock stops immediately when user makes a move (no lag penalty)
2. **Bot thinking simulation**: Backend calculates random think time (0-3 seconds), frontend simulates it
3. **Auto-flag**: Automatically end game when a player runs out of time
4. **Default time control**: 5 minutes initial time, 3 second increment per move
5. **Future configurability**: Design to easily add time control selection later

## Architecture

### Frontend Changes

#### New Components
1. **ChessClock.jsx**
   - Displays time for both players (white and black)
   - Visual indication of active clock
   - Formats time display (MM:SS or HH:MM:SS)
   - Low time warning (e.g., red text under 30 seconds)

#### State Management (in App.jsx)
```javascript
// New state variables:
- whiteTime: number (milliseconds remaining)
- blackTime: number (milliseconds remaining)
- activeColor: 'white' | 'black' | null
- clockRunning: boolean
- lastMoveTimestamp: number
- timeControl: { initial: 300000, increment: 3000 } // 5 min + 3 sec
- botThinkingTime: number | null
```

#### Time Tracking Logic
1. **User Move**:
   - Stop clock immediately on valid move (timeseal principle)
   - Add increment to user's time
   - Start opponent's clock
   - Send move to backend

2. **Bot Move**:
   - Backend returns move + thinking time
   - Simulate thinking time in frontend
   - After simulation, make move and add increment
   - Start user's clock

3. **Auto-flag**:
   - Check time after each clock tick
   - End game with time forfeit when time reaches 0

### Backend Changes

#### API Modifications ✅ COMPLETE
1. **Update `/get_move` response** ✅:
   ```python
   {
     "move": "e4",
     "thinking_time": 1.5  # Random float between 0 and 3
   }
   ```

2. **Thinking time calculation** ✅:
   - Generate random time between 0-3 seconds using `random.uniform(0, 3)`
   - Included in all responses (even when move is null for game over)
   - Could later make this depend on position complexity or move difficulty

## Test Plan

### Frontend Unit Tests (App.test.jsx additions)

#### Clock Display Tests
1. **Initial state**
   - Both clocks show 5:00
   - Neither clock is running
   - No active color indicated

2. **Clock formatting**
   - Correctly formats MM:SS for times under 1 hour
   - Correctly formats HH:MM:SS for times over 1 hour
   - Shows tenths of seconds when under 10 seconds

#### Time Management Tests
1. **Game start**
   - White's clock starts when first move is made
   - Black's clock remains at 5:00

2. **Move transitions**
   - User's clock stops immediately on move
   - Increment is added to user's time
   - Opponent's clock starts

3. **Timeseal behavior**
   - Mock delayed API response
   - Verify user's clock doesn't lose time during network delay
   - Only opponent's time decreases during delay

4. **Bot thinking simulation**
   - Bot clock decreases during thinking time
   - User clock doesn't run during bot thinking
   - Bot gets increment after move

5. **Auto-flag**
   - Game ends when time reaches 0
   - Correct winner is declared
   - Clock stops running after flag

6. **Edge cases**
   - Clock behavior during checkmate/stalemate
   - Clock stops when game ends normally
   - Time doesn't go negative

### Frontend Integration Tests (App.test.jsx)

1. **Full game with clock**
   - Play several moves
   - Verify time decreases appropriately
   - Verify increments are added
   - Test auto-flag scenario

2. **Network latency simulation**
   - Add artificial delay to API calls
   - Verify timeseal protection works
   - User time unaffected by network delay

### Backend Unit Tests (test_main.py additions) ✅ COMPLETE

1. **Thinking time in response** ✅
   - `/get_move` returns thinking_time field ✅
   - Thinking time is between 0 and 3 ✅
   - Thinking time is a float ✅

2. **Thinking time randomness** ✅
   - Multiple calls produce different values ✅
   - Values are properly distributed ✅

### E2E Tests (Playwright)

1. **Clock visibility and updates**
   - Clocks visible on page load
   - Time updates every second
   - Active clock highlighted

2. **Complete timed game**
   - Play game with clock running
   - Verify time management
   - Test auto-flag scenario

3. **Bot thinking visualization**
   - Bot clock decreases during thinking
   - Move appears after thinking time

## Implementation Order

### Phase 1: Backend (TDD) ✅ COMPLETE
1. ✅ Write tests for thinking_time in API response
   - test_get_move_includes_thinking_time
   - test_thinking_time_range
   - test_thinking_time_randomness
   - test_thinking_time_for_game_over
2. ✅ Implement thinking_time generation
   - Added `random.uniform(0, 3)` for thinking time
3. ✅ Update API response model
   - Added `thinking_time: float` to MoveResponse
   - All endpoints return thinking_time (including game over)

### Phase 2: Basic Clock Display (TDD) ✅ COMPLETE
1. ✅ Write tests for ChessClock component
   - Created comprehensive tests in ChessClock.test.jsx
   - Tests cover: initial display, time formatting, active clock indication, low time warning, running state
2. ✅ Create ChessClock component
   - Created ChessClock.jsx with time formatting logic
   - Handles hours/minutes/seconds display with tenths for low time
3. ✅ Add to App layout
   - Imported and integrated into App.jsx
   - Added clock state variables to App component
   - Positioned above chessboard in grid layout
4. ✅ Style the clock display
   - Created ChessClock.css with modern styling
   - Active clock highlighting with blue background
   - Low time warning with red text and blinking
   - Pulse animation for running clock
   - Responsive design for mobile

### Phase 3: Time Management (TDD) ✅ COMPLETE
1. ✅ Write tests for clock state management
   - Added tests for initial state, clock running, and time updates
2. ✅ Add clock state to App.jsx  
   - Already added in Phase 2
3. ✅ Implement clock start/stop logic
   - Clock starts on first move
   - Switches between players on each move
   - Stops when game ends or viewing history
4. ✅ Add increment logic
   - 3-second increment added after each move
   - Applied before switching to opponent's clock

### Phase 4: Timeseal Implementation (TDD) ✅ COMPLETE
1. ✅ Write tests for lag compensation
   - Added tests for immediate clock stop on move
   - Tests for network delay handling
   - Tests for local time tracking
2. ✅ Implement local time tracking
   - Added `turnStartTime` using performance.now()
   - Track when each turn begins locally
3. ✅ Ensure clock stops immediately on move
   - Clock updates happen locally via interval
   - Increment added on move completion
   - No dependency on server response time
   - User is not penalized for network latency

### Phase 5: Bot Thinking Simulation (TDD) ✅ COMPLETE
1. ✅ Write tests for bot thinking visualization
   - Created tests for thinking time simulation
   - Tests verify clock activation during thinking
   - Tests check that user clock is not affected
2. ✅ Implement thinking time simulation
   - Bot thinking time from backend is used in `handleComputerTurn`
   - Uses setTimeout to delay bot move by thinking_time seconds
   - Bot's clock runs during thinking period
3. ✅ Update move handling logic
   - Bot's clock starts immediately when it's their turn
   - Thinking simulation happens before move execution
   - Increment is added after move completion

**Known Issue**: Bot's clock currently gets increment immediately when turn starts, rather than after thinking completes. This means the bot effectively doesn't lose time during thinking. Fix tracked in TODO #4.

### Phase 6: Auto-flag (TDD)
1. Write tests for time forfeit
2. Implement zero-time detection
3. Add game end logic for flags

### Phase 7: Polish
1. Add low-time warnings
2. Improve visual feedback
3. Add sound effects (optional)

## Future Enhancements
- Time control selector (bullet, blitz, rapid, custom)
- Time odds (different times for each player)
- Other time controls (Fischer, Bronstein delay)
- Move time limits
- Time pressure indication for bot (affects move quality)
- Persistent clock state (resume games)

## Technical Considerations

### Performance
- Use `requestAnimationFrame` or `setInterval` for smooth updates
- Optimize re-renders (React.memo for clock component)
- Consider using a Web Worker for precise timing

### Accuracy
- Use `performance.now()` for precise time measurement
- Account for tab visibility changes
- Handle system clock changes

### User Experience
- Clear visual indication of active clock
- Smooth time transitions
- Audio cues for low time (optional)
- Accessible time announcements

## Success Criteria
1. Clock accurately tracks time for both players
2. No time lost due to network latency
3. Bot thinking time feels natural
4. Auto-flag works reliably
5. All tests pass with good coverage
6. Performance is smooth even on slower devices