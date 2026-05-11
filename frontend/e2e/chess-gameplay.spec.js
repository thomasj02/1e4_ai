import { test, expect } from '@playwright/test';
import { navigateToApp, waitForChessboard } from './helpers.js';

test.describe('Chess Gameplay Tests', () => {
  // Helper function to perform touch-based drag
  async function touchDrag(page, fromElement, toElement) {
    const fromBox = await fromElement.boundingBox();
    const toBox = await toElement.boundingBox();
    
    if (!fromBox || !toBox) {
      throw new Error('Could not get bounding boxes for elements');
    }
    
    const fromX = fromBox.x + fromBox.width / 2;
    const fromY = fromBox.y + fromBox.height / 2;
    const toX = toBox.x + toBox.width / 2;
    const toY = toBox.y + toBox.height / 2;
    
    // Dispatch touch events
    await fromElement.dispatchEvent('touchstart', {
      touches: [{ clientX: fromX, clientY: fromY, pageX: fromX, pageY: fromY, screenX: fromX, screenY: fromY, identifier: 0 }],
      targetTouches: [{ clientX: fromX, clientY: fromY, pageX: fromX, pageY: fromY, screenX: fromX, screenY: fromY, identifier: 0 }],
      changedTouches: [{ clientX: fromX, clientY: fromY, pageX: fromX, pageY: fromY, screenX: fromX, screenY: fromY, identifier: 0 }]
    });
    
    // Simulate intermediate touchmove events
    const steps = 10;
    for (let i = 1; i <= steps; i++) {
      const x = fromX + (toX - fromX) * (i / steps);
      const y = fromY + (toY - fromY) * (i / steps);
      
      await page.dispatchEvent('body', 'touchmove', {
        touches: [{ clientX: x, clientY: y, pageX: x, pageY: y, screenX: x, screenY: y, identifier: 0 }],
        targetTouches: [{ clientX: x, clientY: y, pageX: x, pageY: y, screenX: x, screenY: y, identifier: 0 }],
        changedTouches: [{ clientX: x, clientY: y, pageX: x, pageY: y, screenX: x, screenY: y, identifier: 0 }]
      });
      
      await page.waitForTimeout(20);
    }
    
    await toElement.dispatchEvent('touchend', {
      touches: [],
      targetTouches: [],
      changedTouches: [{ clientX: toX, clientY: toY, pageX: toX, pageY: toY, screenX: toX, screenY: toY, identifier: 0 }]
    });
  }

  // Helper function to make moves using appropriate method
  async function makeMove(page, fromSquare, toSquare, browserName) {
    const piece = page.locator(`[data-square="${fromSquare}"] [data-piece]`).first();
    const target = page.locator(`[data-square="${toSquare}"]`);
    
    const isMobile = page.context()._options.isMobile === true;
    const isWebkit = browserName === 'webkit';
    
    if (isMobile && browserName === 'webkit') {
      // For mobile Safari, use click-to-move as touch drag can be unreliable
      await piece.click();
      await page.waitForTimeout(300);
      await target.click();
    } else if (isMobile) {
      // Use touch-based drag for other mobile browsers
      await touchDrag(page, piece, target);
    } else if (isWebkit) {
      // For webkit desktop, use click-to-move as drag is unreliable
      await piece.click();
      await page.waitForTimeout(200);
      await target.click();
    } else {
      // Use regular drag for desktop Chrome/Firefox
      await piece.dragTo(target);
    }
    
    await page.waitForTimeout(500);
  }
  test.beforeEach(async ({ page }) => {
    // Mock API endpoints
    await page.route('**/get_move', async (route) => {
      const postData = await route.request().postDataJSON();
      
      // Simple mock response - always play e5 as black's first move
      const mockResponse = {
        move: postData.fen.includes('b KQkq') ? 'e5' : 'Nf6',
        thinking_time: 0.5
      };
      
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify(mockResponse)
      });
    });
    
    // Mock evaluation endpoint for the EvalBar
    await page.route('**/evaluate_position', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          evaluation: 0.1,
          raw_value: 0.55
        })
      });
    });
    
    await navigateToApp(page);
    await waitForChessboard(page);
  });

  test('should display initial chess position correctly', async ({ page }) => {
    // Check that we have 32 pieces
    const pieceCount = await page.locator('[data-piece]').count();
    expect(pieceCount).toBe(32);
    
    // Check specific pieces are in correct positions
    const whiteKing = await page.locator('[data-square="e1"] [data-piece="wK"]').count();
    expect(whiteKing).toBe(1);
    
    const blackKing = await page.locator('[data-square="e8"] [data-piece="bK"]').count();
    expect(blackKing).toBe(1);
    
    // Check pawns
    for (let file of ['a', 'b', 'c', 'd', 'e', 'f', 'g', 'h']) {
      const whitePawn = await page.locator(`[data-square="${file}2"] [data-piece="wP"]`).count();
      expect(whitePawn).toBe(1);
      
      const blackPawn = await page.locator(`[data-square="${file}7"] [data-piece="bP"]`).count();
      expect(blackPawn).toBe(1);
    }
  });

  test('should allow valid pawn moves', async ({ page, browserName }) => {
    // First ensure we're at the starting position
    const e2Pawn = await page.locator('[data-square="e2"] [data-piece="wP"]').count();
    if (e2Pawn === 0) {
      const resetButton = page.locator('button:has-text("New Game")');
      await resetButton.click();
      await page.waitForTimeout(1000);
    }
    
    // Move e2 to e4
    await makeMove(page, 'e2', 'e4', browserName);
    await page.waitForTimeout(1000);
    
    // Verify the move happened
    const e2Empty = await page.locator('[data-square="e2"] [data-piece]').count();
    expect(e2Empty).toBe(0);
    
    const e4Pawn = await page.locator('[data-square="e4"] [data-piece="wP"]').count();
    expect(e4Pawn).toBe(1);
    
    // Move list should show the move (uses spans not buttons)
    const moves = await page.locator('[data-testid="move-list"] span').filter({ hasText: /^[a-h][1-8]/ }).count();
    expect(moves).toBeGreaterThanOrEqual(1);
  });

  test('should handle game reset', async ({ page, browserName }) => {
    // First ensure we're at the starting position
    const e2Pawn = await page.locator('[data-square="e2"] [data-piece="wP"]').count();
    if (e2Pawn === 0) {
      const resetButton = page.locator('button:has-text("New Game")');
      await resetButton.click();
      await page.waitForTimeout(1000);
    }
    
    // Make a move first
    await makeMove(page, 'e2', 'e4', browserName);
    await page.waitForTimeout(1000);
    
    // Verify move was made
    const e4PawnBefore = await page.locator('[data-square="e4"] [data-piece="wP"]').count();
    expect(e4PawnBefore).toBe(1);
    
    // Click reset
    const resetButton = page.locator('button:has-text("New Game")');
    await resetButton.click();
    
    // Wait for dialog to be visible
    await page.waitForSelector('text="Start Game"', { state: 'visible' });
    
    // Click Start Game in the dialog with force if needed
    const startGameButton = page.locator('button:has-text("Start Game")');
    await startGameButton.click({ force: true });
    await page.waitForTimeout(500);
    
    // Verify board is reset
    const e2PawnAfter = await page.locator('[data-square="e2"] [data-piece="wP"]').count();
    expect(e2PawnAfter).toBe(1);
    
    const e4Empty = await page.locator('[data-square="e4"] [data-piece]').count();
    expect(e4Empty).toBe(0);
    
    // Move list should be empty (only "Start Position" should remain)
    const movesAfter = await page.locator('[data-testid="move-list"] span').filter({ hasText: /^[a-h][1-8]/ }).count();
    expect(movesAfter).toBe(0);
  });

  test('should load and display custom FEN positions', async ({ page, browserName }) => {
    // For webkit, ensure clean state by resetting first
    if (browserName === 'webkit') {
      const resetButton = page.locator('button:has-text("New Game")');
      await resetButton.click();
      
      // Wait for dialog to be visible
      await page.waitForSelector('text="Start Game"', { state: 'visible' });
      
      // Click Start Game in the dialog with force if needed
      const startGameButton = page.locator('button:has-text("Start Game")');
      await startGameButton.click({ force: true });
      await page.waitForTimeout(500);
    }
    
    const fenInput = page.locator('input[placeholder*="FEN"]');
    const loadButton = page.locator('button:has-text("Load FEN")');
    
    // First ensure we're at the starting position
    const initialPieces = await page.locator('[data-piece]').count();
    expect(initialPieces).toBe(32);
    
    // Load endgame position
    const endgameFen = 'k7/8/8/8/8/8/8/K6R w - - 0 1';
    await fenInput.fill(endgameFen);
    
    await loadButton.click();
    
    // For webkit, we need extra time for the board to update
    if (browserName === 'webkit') {
      await page.waitForTimeout(1500);
    } else {
      await page.waitForTimeout(500);
    }
    
    // Wait for exact piece count with retry
    let retries = 0;
    let totalPieces = 0;
    while (retries < 15) {
      totalPieces = await page.locator('[data-piece]').count();
      if (totalPieces === 3) break;
      await page.waitForTimeout(200);
      retries++;
    }
    
    
    // Verify position
    expect(totalPieces).toBe(3); // Only 3 pieces
    
    const whiteKing = await page.locator('[data-square="a1"] [data-piece="wK"]').count();
    expect(whiteKing).toBe(1);
    
    const whiteRook = await page.locator('[data-square="h1"] [data-piece="wR"]').count();
    expect(whiteRook).toBe(1);
    
    const blackKing = await page.locator('[data-square="a8"] [data-piece="bK"]').count();
    expect(blackKing).toBe(1);
  });

  test('should display game state indicators', async ({ page }) => {
    // Check initial FEN display
    const fenDisplay = page.locator('text=/Current FEN:/');
    await expect(fenDisplay).toBeVisible();
    
    // Check move count display
    const moveCount = page.locator('text=/Move Count:/');
    await expect(moveCount).toBeVisible();
    
    // Load a checkmate position
    const fenInput = page.locator('input[placeholder*="FEN"]');
    const loadButton = page.locator('button:has-text("Load FEN")');
    
    // Fool's mate position
    const checkmateFen = 'rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3';
    await fenInput.fill(checkmateFen);
    await loadButton.click();
    await page.waitForTimeout(500);
    
    // Should show game over indicators
    const gameOver = page.locator('text=/Game Over/i');
    await expect(gameOver).toBeVisible();
    
    const checkmate = page.locator('text=/Checkmate/i');
    await expect(checkmate).toBeVisible();
  });

  test('should allow clicking on pieces', async ({ page }) => {
    // Listen for any console activity when clicking
    let clickDetected = false;
    page.on('console', msg => {
      if (msg.text().includes('Legal moves from')) {
        clickDetected = true;
      }
    });
    
    // Click on various pieces
    const e2Pawn = page.locator('[data-square="e2"] [data-piece="wP"]');
    await e2Pawn.click();
    await page.waitForTimeout(300);
    
    const b1Knight = page.locator('[data-square="b1"] [data-piece="wN"]');
    await b1Knight.click();
    await page.waitForTimeout(300);
    
    // Verify that clicking was detected
    expect(clickDetected).toBe(true);
  });
});