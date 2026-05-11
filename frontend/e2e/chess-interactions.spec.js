import { test, expect } from '@playwright/test';

test.describe('Chess Game Interactions', () => {
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
    
    await page.goto('/');
    await page.waitForLoadState('networkidle');
  });

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
    await page.goto('/');
    // Wait for the app to load
    await page.waitForLoadState('networkidle');
  });

  test('should display legal moves when clicking on chess pieces', async ({ page }) => {
    // Look for chess pieces (react-chessboard uses specific data attributes)
    const pieces = page.locator('[data-piece]');

    // Check if pieces are rendered (should have exactly 32 pieces at start)
    await expect(pieces.first()).toBeVisible({ timeout: 10000 });
    const pieceCount = await pieces.count();
    expect(pieceCount).toBe(32);
    
    // Find a white pawn in the initial position (e2 pawn is a good choice)
    // Note: react-chessboard uses "wP" not "wp" for white pawn
    const e2Pawn = page.locator('[data-square="e2"] [data-piece="wP"]');
    
    // Listen for console messages to verify legal moves are calculated
    const consoleLogs = [];
    page.on('console', msg => {
      if (msg.text().includes('Legal moves from')) {
        consoleLogs.push(msg.text());
      }
    });
    
    // Click on the e2 pawn
    await e2Pawn.click();
    
    // Wait a bit for the click to be processed
    await page.waitForTimeout(100);
    
    // Verify that legal moves were calculated
    const legalMovesLog = consoleLogs.find(log => log.includes('Legal moves from e2:'));
    expect(legalMovesLog).toBeTruthy();
    // Firefox logs "Array" instead of "[", so check for either
    expect(legalMovesLog).toMatch(/\[|Array/); // Should contain an array
    
    // Check that legal move indicators are displayed
    // In the initial position, e2 pawn can move to e3 and e4
    const e3Square = page.locator('[data-square="e3"]');
    const e4Square = page.locator('[data-square="e4"]');
    
    // Wait for styles to be applied
    await page.waitForTimeout(500);
    
    // Check if legal move squares have the radial-gradient background
    // React-chessboard might apply styles to the square div or its children
    // Also check for any child elements that might have the style
    const e3HasGradient = await e3Square.evaluate(el => {
      const checkElement = (element) => {
        if (!element) return false;
        const style = window.getComputedStyle(element);
        const hasGradient = style.background.includes('radial-gradient') || 
                          style.backgroundImage.includes('radial-gradient') ||
                          element.style.background?.includes('radial-gradient') ||
                          element.style.backgroundImage?.includes('radial-gradient');
        if (hasGradient) return true;
        // Check children
        for (const child of element.children) {
          if (checkElement(child)) return true;
        }
        return false;
      };
      return checkElement(el);
    });
    const e4HasGradient = await e4Square.evaluate(el => {
      const checkElement = (element) => {
        if (!element) return false;
        const style = window.getComputedStyle(element);
        const hasGradient = style.background.includes('radial-gradient') || 
                          style.backgroundImage.includes('radial-gradient') ||
                          element.style.background?.includes('radial-gradient') ||
                          element.style.backgroundImage?.includes('radial-gradient');
        if (hasGradient) return true;
        // Check children
        for (const child of element.children) {
          if (checkElement(child)) return true;
        }
        return false;
      };
      return checkElement(el);
    });
    
    expect(e3HasGradient).toBe(true);
    expect(e4HasGradient).toBe(true);
    
    // The source square (e2) should be highlighted
    const e2Square = page.locator('[data-square="e2"]');
    const e2HasHighlight = await e2Square.evaluate(el => {
      const checkElement = (element) => {
        if (!element) return false;
        const style = window.getComputedStyle(element);
        const inlineStyle = element.style;
        
        // Check for any indication of highlighting:
        // - backgroundColor with rgba
        // - backgroundColor with rgb
        // - any background style that's not empty
        const hasBackgroundColor = 
          (style.backgroundColor && style.backgroundColor !== 'rgba(0, 0, 0, 0)' && style.backgroundColor !== 'transparent') ||
          (inlineStyle.backgroundColor && inlineStyle.backgroundColor !== '');
          
        if (hasBackgroundColor) return true;
        
        // Check children
        for (const child of element.children) {
          if (checkElement(child)) return true;
        }
        return false;
      };
      return checkElement(el);
    });
    expect(e2HasHighlight).toBe(true);
  });

  test('should toggle legal moves on and off when clicking the same piece', async ({ page }) => {
    // Find the e2 pawn (using correct capitalization)
    const e2Pawn = page.locator('[data-square="e2"] [data-piece="wP"]');
    
    // First click - show legal moves
    await e2Pawn.click();
    await page.waitForTimeout(100);
    
    // Check that e3 has legal move indicator
    const e3Square = page.locator('[data-square="e3"]');
    let e3HasGradient = await e3Square.evaluate(el => {
      const checkElement = (element) => {
        if (!element) return false;
        const style = window.getComputedStyle(element);
        const hasGradient = style.background.includes('radial-gradient') || 
                          style.backgroundImage.includes('radial-gradient') ||
                          element.style.background?.includes('radial-gradient') ||
                          element.style.backgroundImage?.includes('radial-gradient');
        if (hasGradient) return true;
        for (const child of element.children) {
          if (checkElement(child)) return true;
        }
        return false;
      };
      return checkElement(el);
    });
    expect(e3HasGradient).toBe(true);
    
    // Second click on same piece - hide legal moves
    await e2Pawn.click();
    await page.waitForTimeout(100);
    
    // Legal move indicators should be gone
    e3HasGradient = await e3Square.evaluate(el => {
      const checkElement = (element) => {
        if (!element) return false;
        const style = window.getComputedStyle(element);
        const hasGradient = style.background.includes('radial-gradient') || 
                          style.backgroundImage.includes('radial-gradient') ||
                          element.style.background?.includes('radial-gradient') ||
                          element.style.backgroundImage?.includes('radial-gradient');
        if (hasGradient) return true;
        for (const child of element.children) {
          if (checkElement(child)) return true;
        }
        return false;
      };
      return checkElement(el);
    });
    expect(e3HasGradient).toBe(false);
  });

  test('should make a valid move by dragging', async ({ page, browserName }) => {
    // First ensure we're at the starting position
    // Check for white pawn on e2
    const e2Pawn = await page.locator('[data-square="e2"] [data-piece="wP"]').count();
    if (e2Pawn === 0) {
      // Board is not in initial position, click reset
      const resetButton = page.locator('button:has-text("New Game")');
      await resetButton.click();
      
      // Wait for dialog and click Start Game if it appears
      const dialog = page.locator('dialog.modal');
      if (await dialog.isVisible()) {
        const startGameButton = page.locator('dialog.modal button:has-text("Start Game")');
        await startGameButton.click();
        await expect(dialog).not.toBeVisible({ timeout: 5000 });
      }
      
      // Wait for board to reset
      await expect(page.locator('[data-square="e2"] [data-piece="wP"]')).toBeVisible({ timeout: 5000 });
    }
    
    // Find the e2 pawn (using correct capitalization)
    const e2Square = page.locator('[data-square="e2"]');
    const e4Square = page.locator('[data-square="e4"]');
    
    // Make the move
    await makeMove(page, 'e2', 'e4', browserName);
    
    // Wait for the piece to move - use proper waiting instead of timeout
    await expect(e4Square.locator('[data-piece="wP"]')).toBeVisible({ timeout: 5000 });
    
    // Verify e2 is now empty
    await expect(e2Square.locator('[data-piece]')).toHaveCount(0);
    
    // Wait for move to appear in move list
    await expect(page.locator('[data-testid="move-list"] span:has-text("e4")')).toBeVisible({ timeout: 5000 });
    
    // Wait for AI response - the mock always plays e5 or Nf6
    await expect(async () => {
      const moveTexts = await page.locator('[data-testid="move-list"] span').allTextContents();
      const chessMoves = moveTexts.filter(text => {
        const trimmed = text.trim();
        return /^[a-h][1-8]$|^[NBRQK][a-h]?[1-8]?x?[a-h][1-8]$|^O-O-O$|^O-O$/.test(trimmed);
      });
      expect(chessMoves.length).toBeGreaterThanOrEqual(2);
    }).toPass({ timeout: 5000, intervals: [500, 1000, 1500] });
  });

  test('should display move history', async ({ page, browserName }) => {
    // First ensure we're at the starting position
    const e2Pawn = await page.locator('[data-square="e2"] [data-piece="wP"]').count();
    if (e2Pawn === 0) {
      const resetButton = page.locator('button:has-text("New Game")');
      await resetButton.click();
      await page.waitForTimeout(1000);
    }
    
    // Look for move list component with data-testid
    const moveList = page.locator('[data-testid="move-list"]');

    // Move list container should be visible
    await expect(moveList).toBeVisible();
    
    // Make a move and check the history updates
    await makeMove(page, 'e2', 'e4', browserName);
    
    // Wait for the move to appear in the move list with a more specific locator
    // The move list contains spans with chess notation (e4, Nf6, etc)
    await expect(page.locator('[data-testid="move-list"] span:has-text("e4")')).toBeVisible({ timeout: 5000 });
    
    // Wait for AI to make a move - AI always plays e5 or Nf6 based on the mock
    await expect(async () => {
      const moveTexts = await page.locator('[data-testid="move-list"] span').allTextContents();
      const chessMoves = moveTexts.filter(text => {
        const trimmed = text.trim();
        // Only match actual chess moves, not move numbers or other UI elements
        return /^[a-h][1-8]$|^[NBRQK][a-h]?[1-8]?x?[a-h][1-8]$|^O-O-O$|^O-O$/.test(trimmed);
      });
      expect(chessMoves.length).toBeGreaterThanOrEqual(2);
    }).toPass({ timeout: 5000, intervals: [500, 1000, 1500] });
    
    // Verify specific moves are present
    const moveTexts = await page.locator('[data-testid="move-list"] span').allTextContents();
    const chessMoves = moveTexts.filter(text => {
      const trimmed = text.trim();
      return /^[a-h][1-8]$|^[NBRQK][a-h]?[1-8]?x?[a-h][1-8]$|^O-O-O$|^O-O$/.test(trimmed);
    });
    
    // Should have at least player's e4 and AI's response (e5 or Nf6)
    expect(chessMoves).toContain('e4');
    expect(chessMoves.length).toBeGreaterThanOrEqual(2);
  });

  test('should handle game controls', async ({ page }) => {
    // Look for the specific buttons in the app
    const buttons = page.locator('button');
    
    // Check for Load FEN button
    const loadFenButton = buttons.filter({ hasText: /load fen/i });
    await expect(loadFenButton.first()).toBeVisible();
    await expect(loadFenButton.first()).toBeEnabled();
    
    // Check for New Game button
    const resetButton = buttons.filter({ hasText: /new game/i });
    await expect(resetButton.first()).toBeVisible();
    await expect(resetButton.first()).toBeEnabled();
    
    // Check for FEN input field
    const fenInput = page.locator('input[placeholder*="FEN"]');
    await expect(fenInput).toBeVisible();
  });

  test('should show legal moves for different piece types', async ({ page }) => {
    // Test knight moves (they have a unique pattern)
    const b1Knight = page.locator('[data-square="b1"] [data-piece="wN"]');
    
    await b1Knight.click();
    await page.waitForTimeout(100);
    
    // Knight from b1 can move to a3 and c3 in starting position
    const a3Square = page.locator('[data-square="a3"]');
    const c3Square = page.locator('[data-square="c3"]');
    
    const a3HasGradient = await a3Square.evaluate(el => {
      const checkElement = (element) => {
        if (!element) return false;
        const style = window.getComputedStyle(element);
        const hasGradient = style.background.includes('radial-gradient') || 
                          style.backgroundImage.includes('radial-gradient') ||
                          element.style.background?.includes('radial-gradient') ||
                          element.style.backgroundImage?.includes('radial-gradient');
        if (hasGradient) return true;
        for (const child of element.children) {
          if (checkElement(child)) return true;
        }
        return false;
      };
      return checkElement(el);
    });
    const c3HasGradient = await c3Square.evaluate(el => {
      const checkElement = (element) => {
        if (!element) return false;
        const style = window.getComputedStyle(element);
        const hasGradient = style.background.includes('radial-gradient') || 
                          style.backgroundImage.includes('radial-gradient') ||
                          element.style.background?.includes('radial-gradient') ||
                          element.style.backgroundImage?.includes('radial-gradient');
        if (hasGradient) return true;
        for (const child of element.children) {
          if (checkElement(child)) return true;
        }
        return false;
      };
      return checkElement(el);
    });
    
    expect(a3HasGradient).toBe(true);
    expect(c3HasGradient).toBe(true);
    
    // Should NOT be able to move to d3 (blocked by pawn)
    const d3Square = page.locator('[data-square="d3"]');
    const d3HasGradient = await d3Square.evaluate(el => {
      const checkElement = (element) => {
        if (!element) return false;
        const style = window.getComputedStyle(element);
        const hasGradient = style.background.includes('radial-gradient') || 
                          style.backgroundImage.includes('radial-gradient') ||
                          element.style.background?.includes('radial-gradient') ||
                          element.style.backgroundImage?.includes('radial-gradient');
        if (hasGradient) return true;
        for (const child of element.children) {
          if (checkElement(child)) return true;
        }
        return false;
      };
      return checkElement(el);
    });
    expect(d3HasGradient).toBe(false);
  });

  test('should not allow moving opponent pieces', async ({ page }) => {
    // Try to click on a black piece (should not show moves)
    const e7Pawn = page.locator('[data-square="e7"] [data-piece="bP"]');
    
    await e7Pawn.click();
    await page.waitForTimeout(100);
    
    // No legal move indicators should be shown for black pieces when it's white's turn
    const e6Square = page.locator('[data-square="e6"]');
    const e5Square = page.locator('[data-square="e5"]');
    
    // Check that no legal move indicators are shown
    const e6HasGradient = await e6Square.evaluate(el => {
      const checkElement = (element) => {
        if (!element) return false;
        const style = window.getComputedStyle(element);
        const hasGradient = style.background.includes('radial-gradient') || 
                          style.backgroundImage.includes('radial-gradient') ||
                          element.style.background?.includes('radial-gradient') ||
                          element.style.backgroundImage?.includes('radial-gradient');
        if (hasGradient) return true;
        for (const child of element.children) {
          if (checkElement(child)) return true;
        }
        return false;
      };
      return checkElement(el);
    });
    
    const e5HasGradient = await e5Square.evaluate(el => {
      const checkElement = (element) => {
        if (!element) return false;
        const style = window.getComputedStyle(element);
        const hasGradient = style.background.includes('radial-gradient') || 
                          style.backgroundImage.includes('radial-gradient') ||
                          element.style.background?.includes('radial-gradient') ||
                          element.style.backgroundImage?.includes('radial-gradient');
        if (hasGradient) return true;
        for (const child of element.children) {
          if (checkElement(child)) return true;
        }
        return false;
      };
      return checkElement(el);
    });
    
    // Neither square should have gradient indicating legal moves
    expect(e6HasGradient).toBe(false);
    expect(e5HasGradient).toBe(false);
  });

  test('should handle FEN loading and show correct legal moves', async ({ page, browserName }) => {
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
    
    // Test loading a custom position
    const fenInput = page.locator('input[placeholder*="FEN"]');
    const loadButton = page.locator('button:has-text("Load FEN")');
    
    // Load a position after 1.e4 e5 (white's turn)
    const testFen = 'rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2';
    await fenInput.fill(testFen);
    await loadButton.click();
    
    // Wait for the board to update - look for the black pawn on e5
    await page.waitForFunction(
      () => document.querySelector('[data-square="e5"] [data-piece="bP"]') !== null,
      { timeout: 5000 }
    ).catch(async () => {
      // If it fails on webkit, try clicking load again
      if (browserName === 'webkit') {
        await loadButton.click();
        await page.waitForTimeout(1000);
      }
    });
    
    // Check that the position changed
    const e4Pawn = await page.locator('[data-square="e4"] [data-piece="wP"]').count();
    expect(e4Pawn).toBe(1); // White pawn should be on e4
    
    const e5Pawn = await page.locator('[data-square="e5"] [data-piece="bP"]').count();
    expect(e5Pawn).toBe(1); // Black pawn should be on e5
    
    // Now test that white pieces can be clicked (it's white's turn and player plays white)
    const d2Pawn = page.locator('[data-square="d2"] [data-piece="wP"]');
    
    // First verify the pawn exists
    await expect(d2Pawn).toBeVisible();
    
    // Click on the pawn
    await d2Pawn.click();
    
    // Wait for legal moves to be displayed
    await page.waitForTimeout(300);
    
    // Should show legal moves for white pawn (d3 and d4)
    const d3Square = page.locator('[data-square="d3"]');
    const d4Square = page.locator('[data-square="d4"]');
    
    // Check if legal move indicators are shown (radial gradient)
    const d3HasIndicator = await d3Square.evaluate(el => {
      const checkElement = (element) => {
        if (!element) return false;
        const style = window.getComputedStyle(element);
        const hasGradient = style.background.includes('radial-gradient') || 
                          style.backgroundImage.includes('radial-gradient') ||
                          element.style.background?.includes('radial-gradient') ||
                          element.style.backgroundImage?.includes('radial-gradient');
        if (hasGradient) return true;
        for (const child of element.children) {
          if (checkElement(child)) return true;
        }
        return false;
      };
      return checkElement(el);
    });
    
    const d4HasIndicator = await d4Square.evaluate(el => {
      const checkElement = (element) => {
        if (!element) return false;
        const style = window.getComputedStyle(element);
        const hasGradient = style.background.includes('radial-gradient') || 
                          style.backgroundImage.includes('radial-gradient') ||
                          element.style.background?.includes('radial-gradient') ||
                          element.style.backgroundImage?.includes('radial-gradient');
        if (hasGradient) return true;
        for (const child of element.children) {
          if (checkElement(child)) return true;
        }
        return false;
      };
      return checkElement(el);
    });
    
    // Both legal move squares should show indicators
    expect(d3HasIndicator).toBe(true);
    expect(d4HasIndicator).toBe(true);
  });

  test('should handle promotion moves', async ({ page, browserName }) => {
    // Load a position where promotion is possible
    const fenInput = page.locator('input[placeholder*="FEN"]');
    const loadFenButton = page.locator('button:has-text("Load FEN")');
    
    // Position with white pawn on 7th rank
    await fenInput.fill('4k3/P7/8/8/8/8/8/4K3 w - - 0 1');
    await loadFenButton.click();
    await page.waitForTimeout(100);
    
    // Move the pawn from a7 to a8 (promotion)
    await makeMove(page, 'a7', 'a8', browserName);
    
    // Wait for promotion dialog
    await page.waitForTimeout(200);
    
    // Select queen for promotion
    const queenButton = page.locator('[data-testid*="queen"], [role="button"]:has-text("Q"), .promotion-piece:has([data-piece="wq"])');
    
    // Click on the queen if it exists
    if (await queenButton.count() > 0) {
      await queenButton.first().click();
      await page.waitForTimeout(200);
      
      // Check that a queen is now on a8
      const a8Queen = page.locator('[data-square="a8"] [data-piece="wQ"]');
      await expect(a8Queen).toBeVisible();
    }
  });

  test('should reset game properly', async ({ page, browserName }) => {
    // First ensure we're at the starting position
    const e2Pawn = await page.locator('[data-square="e2"] [data-piece="wP"]').count();
    if (e2Pawn === 0) {
      const resetButton = page.locator('button:has-text("New Game")');
      await resetButton.click();
      await page.waitForTimeout(1000);
    }
    
    // Make some moves first
    await makeMove(page, 'e2', 'e4', browserName);
    
    // Wait for move to appear in move list
    await expect(page.locator('[data-testid="move-list"] span:has-text("e4")')).toBeVisible({ timeout: 5000 });
    
    // Click reset
    const resetButton = page.locator('button:has-text("New Game")');
    await resetButton.click();
    
    // Wait for dialog to appear using the dialog element directly
    const dialog = page.locator('dialog.modal');
    await expect(dialog).toBeVisible({ timeout: 5000 });
    
    // Wait for Start Game button to be visible and clickable
    const startGameButton = page.locator('dialog.modal button:has-text("Start Game")');
    await expect(startGameButton).toBeVisible({ timeout: 5000 });
    await startGameButton.click();
    
    // Wait for dialog to close
    await expect(dialog).not.toBeVisible({ timeout: 5000 });
    
    // Check that board is back to starting position
    const e2PawnAfterReset = page.locator('[data-square="e2"] [data-piece="wP"]');
    await expect(e2PawnAfterReset).toBeVisible({ timeout: 5000 });
    
    // Move list should be empty or only show "Start" - wait for it to clear
    await expect(async () => {
      const moveTexts = await page.locator('[data-testid="move-list"] span').allTextContents();
      const chessMoves = moveTexts.filter(text => {
        const trimmed = text.trim();
        return /^[a-h][1-8]$|^[NBRQK][a-h]?[1-8]?x?[a-h][1-8]$|^O-O-O$|^O-O$/.test(trimmed);
      });
      expect(chessMoves.length).toBe(0);
    }).toPass({ timeout: 5000 });
  });

  test('should navigate through move history', async ({ page, browserName }) => {
    // First ensure we're at the starting position
    const e2Pawn = await page.locator('[data-square="e2"] [data-piece="wP"]').count();
    if (e2Pawn === 0) {
      const resetButton = page.locator('button:has-text("New Game")');
      await resetButton.click();
      
      // Wait for dialog to be visible
      await page.waitForSelector('text="Start Game"', { state: 'visible' });
      
      // Click Start Game in the dialog with force if needed
      const startGameButton = page.locator('button:has-text("Start Game")');
      await startGameButton.click({ force: true });
      await page.waitForTimeout(500);
    }
    
    // Make a few moves
    await makeMove(page, 'e2', 'e4', browserName);
    await page.waitForTimeout(1500); // Increased wait for move processing
    
    await makeMove(page, 'd2', 'd4', browserName);
    await page.waitForTimeout(1500); // Increased wait for move processing
    
    // Wait for move list to be populated
    const moveList = page.locator('[data-testid="move-list"]');
    await expect(moveList).toBeVisible({ timeout: 10000 });
    
    // Wait for e4 move to appear in the list
    await page.waitForFunction(
      () => {
        const moveListEl = document.querySelector('[data-testid="move-list"]');
        return moveListEl && moveListEl.textContent?.includes('e4');
      },
      { timeout: 10000 }
    );
    
    // Additional wait for Webkit to ensure move list is fully rendered
    if (browserName === 'webkit') {
      await page.waitForTimeout(500);
    }
    
    // Click on the first move in history - look for e4 specifically
    const e4Move = moveList.locator('span:has-text("e4")').first();
    await expect(e4Move).toBeVisible({ timeout: 5000 });
    await e4Move.click();
    
    // Wait a bit after clicking
    await page.waitForTimeout(browserName === 'webkit' ? 500 : 200);
    
    // Should show "Browse History" overlay
    const browseOverlay = page.locator('text=Browse History');
    await expect(browseOverlay).toBeVisible({ timeout: 5000 });
    
    // Use arrow key to navigate forward
    await page.keyboard.press('ArrowRight');
    await page.waitForTimeout(browserName === 'webkit' ? 300 : 100);
    
    // Use arrow key to navigate back
    await page.keyboard.press('ArrowLeft');
    await page.waitForTimeout(browserName === 'webkit' ? 300 : 100);
    
    // The overlay should still be visible when not at latest move
    await expect(browseOverlay).toBeVisible({ timeout: 5000 });
  });
});