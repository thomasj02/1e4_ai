# End-to-End Tests

This directory contains Playwright end-to-end tests for the ChessMimic frontend.

## Running Tests

### Prerequisites
Make sure you have installed the system dependencies for Playwright browsers:
```bash
sudo npx playwright install-deps
```

### Available Commands

```bash
# Run all e2e tests
npm run test:e2e

# Run tests with UI mode (interactive)
npm run test:e2e:ui

# Run tests in debug mode
npm run test:e2e:debug

# Run tests in headed mode (show browser)
npm run test:e2e:headed

# Show test report
npm run test:e2e:report

# SCREENSHOT OPTIONS:
# Run tests with screenshots after every action
npm run test:e2e:screenshots

# Run tests with full tracing (includes screenshots, network, etc.)
npm run test:e2e:trace

# Run with screenshots, traces, and visible browser
npm run test:e2e:full

# Run with maximum visual documentation (slow but comprehensive)
npm run test:e2e:visual
```

## Test Files

- `example.spec.js` - Basic app loading and responsiveness tests
- `chess-interactions.spec.js` - Chess-specific functionality tests with legal move detection
- `accessibility.spec.js` - Accessibility and keyboard navigation tests
- `chess-gameplay.spec.js` - Core gameplay tests (piece movement, reset, FEN loading)
- `chess-valid-moves.spec.js` - Move validation and piece-specific tests
- `backend-communication.spec.js` - Comprehensive backend API tests including:
  - FEN and move history transmission
  - Checkmate and stalemate handling
  - Game reset scenarios
  - Error handling and fallback behavior

## Configuration

The Playwright configuration is in `playwright.config.js` at the root of the frontend directory.

Key settings:
- Tests run against `http://localhost:5173` (Vite dev server)
- Automatic dev server startup for tests
- Cross-browser testing (Chrome, Firefox, Safari)
- Mobile viewport testing
- Screenshots on failure
- Trace collection on retry

## Writing New Tests

When adding new tests:

1. Create `.spec.js` files in this directory
2. Use descriptive test names
3. Group related tests with `test.describe()`
4. Add appropriate `data-testid` attributes to components for reliable selection
5. Consider accessibility in your tests

## Tips

- Use `page.locator()` with multiple fallback selectors for robustness
- Wait for network idle when testing dynamic content
- Test both desktop and mobile viewports
- Include accessibility checks in your test suites

## Important Notes for ChessMimic Tests

1. **Piece Selectors**: React-chessboard uses capitalized piece notation:
   - `wP` for white pawn (not `wp`)
   - `wN` for white knight, `wB` for bishop, `wR` for rook, `wQ` for queen, `wK` for king
   - Same pattern for black pieces with `b` prefix

2. **Data Attributes**:
   - Board squares: `[data-square="e2"]`
   - Chess pieces: `[data-piece="wP"]`
   - Move list: `[data-testid="move-list"]`

3. **Backend Dependency**: Some features require the backend at `http://localhost:8000`:
   - Move history updates
   - AI computer moves
   - Basic chess mechanics work without backend

4. **Legal Move Detection**: The app logs legal moves to console when pieces are clicked.
   Tests can listen for these console messages to verify move calculation.