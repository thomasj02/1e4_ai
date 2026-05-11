import { test, expect } from '@playwright/test';

test.describe('Game Status Display', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/');
    await page.waitForLoadState('networkidle');
    await page.waitForSelector('[data-piece]', { timeout: 10000 });
  });

  test('should show check without checkmate', async ({ page }) => {
    // Load a position with check but not checkmate
    const fenInput = page.locator('input[placeholder*="FEN"]');
    const loadButton = page.locator('button:has-text("Load FEN")');
    
    // White king in check from black queen, but can escape
    const checkFen = '3qk3/8/8/8/8/8/8/3K4 w - - 0 1';
    await fenInput.fill(checkFen);
    await loadButton.click();
    await page.waitForTimeout(500);
    
    // Should show Check! but not Checkmate
    await expect(page.locator('text=Check!')).toBeVisible();
    await expect(page.locator('text=Checkmate!')).not.toBeVisible();
  });

  test('should show checkmate with winner - White wins', async ({ page }) => {
    // Load a position where white has checkmated black
    const fenInput = page.locator('input[placeholder*="FEN"]');
    const loadButton = page.locator('button:has-text("Load FEN")');
    
    // Simple back rank mate - black is checkmated by rook on back rank
    const whiteWinsFen = 'R5k1/5ppp/8/8/8/8/8/7K b - - 0 1';
    await fenInput.fill(whiteWinsFen);
    await loadButton.click();
    await page.waitForTimeout(500);
    
    // Should show Game Over and Checkmate with White wins
    await expect(page.locator('text=Game Over!')).toBeVisible();
    await expect(page.locator('text=Checkmate! White wins!')).toBeVisible();
    // Should NOT show Check! separately
    await expect(page.locator('text=/^Check!$/i')).not.toBeVisible();
  });

  test('should show checkmate with winner - Black wins', async ({ page }) => {
    // Load Fool's mate position where black has checkmated white
    const fenInput = page.locator('input[placeholder*="FEN"]');
    const loadButton = page.locator('button:has-text("Load FEN")');
    
    const blackWinsFen = 'rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3';
    await fenInput.fill(blackWinsFen);
    await loadButton.click();
    await page.waitForTimeout(500);
    
    // Should show Game Over and Checkmate with Black wins
    await expect(page.locator('text=Game Over!')).toBeVisible();
    await expect(page.locator('text=Checkmate! Black wins!')).toBeVisible();
    // Should NOT show Check! separately
    await expect(page.locator('text=/^Check!$/i')).not.toBeVisible();
  });

  test('should show draw for stalemate', async ({ page }) => {
    // Load a stalemate position
    const fenInput = page.locator('input[placeholder*="FEN"]');
    const loadButton = page.locator('button:has-text("Load FEN")');
    
    // King vs King - automatic draw
    const drawFen = '4k3/8/8/8/8/8/8/4K3 w - - 0 1';
    await fenInput.fill(drawFen);
    await loadButton.click();
    await page.waitForTimeout(500);
    
    // Should show Game Over and Draw
    await expect(page.locator('text=Game Over!')).toBeVisible();
    await expect(page.locator('text=Draw!')).toBeVisible();
    // Should not show Check or Checkmate
    await expect(page.locator('text=Check!')).not.toBeVisible();
    await expect(page.locator('text=Checkmate!')).not.toBeVisible();
  });

  test('should clear status on reset', async ({ page }) => {
    // Load a checkmate position
    const fenInput = page.locator('input[placeholder*="FEN"]');
    const loadButton = page.locator('button:has-text("Load FEN")');
    
    // Fool's mate
    const checkmateFen = 'rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3';
    await fenInput.fill(checkmateFen);
    await loadButton.click();
    await page.waitForTimeout(500);
    
    // Should show checkmate
    await expect(page.locator('text=Game Over!')).toBeVisible();
    await expect(page.locator('text=Checkmate! Black wins!')).toBeVisible();
    
    // Reset the game
    const resetButton = page.locator('button:has-text("New Game")');
    await resetButton.click();
    
    // Wait for dialog to be visible
    await page.waitForSelector('text="Start Game"', { state: 'visible' });
    
    // Click Start Game in the dialog with force if needed
    const startGameButton = page.locator('button:has-text("Start Game")');
    await startGameButton.click({ force: true });
    await page.waitForTimeout(500);
    
    // Status should be cleared
    await expect(page.locator('text=Game Over!')).not.toBeVisible();
    await expect(page.locator('text=Checkmate!')).not.toBeVisible();
    await expect(page.locator('text=Check!')).not.toBeVisible();
  });
});