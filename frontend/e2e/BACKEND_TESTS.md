# Backend Communication E2E Tests

This document describes the comprehensive e2e tests for backend communication in ChessMimic.

## Test Coverage

### 1. Normal Game Flow ✅
- **FEN & Move History**: Verifies that each move sends the correct board position (FEN) and accumulated move history to the backend
- **Move Accumulation**: Tests that the move history array grows correctly as the game progresses

### 2. Checkmate Handling ✅
- **Fool's Mate**: Tests that checkmate positions are detected and no moves can be made
- **Backend Response**: Verifies that the backend returns `null` for checkmate positions
- **UI Display**: Ensures "Game Over" and "Checkmate" messages appear

### 3. Stalemate Handling ✅
- **Direct Stalemate**: Tests loading a stalemate position directly
- **Created Stalemate**: Tests creating stalemate through moves
- **King vs King**: Tests automatic draw detection for insufficient material
- **Backend Response**: Verifies backend returns `null` for stalemate positions

### 4. Game Reset Scenarios ✅
- **Clean Reset**: Tests that resetting returns board to initial position with empty move history
- **Reset from Checkmate**: Verifies that resetting from checkmate allows new game to start
- **API Calls**: Confirms that post-reset moves send correct initial FEN and single-move history

### 5. Error Handling ✅
- **Backend Failures**: Tests graceful fallback when backend is unavailable
- **Rapid Moves**: Verifies system handles multiple rapid consecutive moves correctly

## API Contract Verification

The tests verify the following API contract:

**Request to `/get_move`:**
```json
{
  "fen": "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
  "moves": ["e4"]
}
```

**Response:**
```json
{
  "move": "e5"  // or null for game over
}
```

## Test Implementation Details

- Uses Playwright's request interception to monitor API calls
- Verifies exact FEN strings sent to backend
- Checks move history accumulation
- Tests both successful and error scenarios
- Validates UI updates based on backend responses

## Running the Tests

```bash
# Run all backend communication tests
npx playwright test e2e/backend-communication.spec.js

# Run specific test groups
npx playwright test e2e/backend-communication.spec.js --grep "Checkmate"
npx playwright test e2e/backend-communication.spec.js --grep "Stalemate"
npx playwright test e2e/backend-communication.spec.js --grep "Normal Game Flow"
```

## Requirements

- Backend must be running at `http://localhost:8000`
- Frontend dev server starts automatically via Playwright config