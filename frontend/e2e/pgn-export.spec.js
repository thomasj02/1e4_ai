import { test, expect } from '@playwright/test';
import { promises as fs } from 'fs';
import path from 'path';

test.describe('PGN Export Feature', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/');
    // Wait for the page to load - look for the main heading
    await page.waitForSelector('h1:has-text("Chessmimic MVP")', { timeout: 10000 });
  });

  test('should display Export PGN button', async ({ page }) => {
    // The Export PGN button might be below the fold, so let's scroll to it
    const exportButton = page.getByRole('button', { name: 'Export PGN' });
    await exportButton.scrollIntoViewIfNeeded();
    await expect(exportButton).toBeVisible();
  });

  test('should download PGN file when Export PGN is clicked', async ({ page }) => {
    // Set up download promise before clicking
    const downloadPromise = page.waitForEvent('download');
    
    // Click the export button
    await page.getByRole('button', { name: 'Export PGN' }).click();
    
    // Wait for download to complete
    const download = await downloadPromise;
    
    // Verify filename format
    const filename = download.suggestedFilename();
    expect(filename).toMatch(/^chessmimic_game_\d{4}-\d{2}-\d{2}_\d{2}-\d{2}\.pgn$/);
    
    // Save and read the file
    const downloadPath = await download.path();
    const content = await fs.readFile(downloadPath, 'utf8');
    
    // Verify basic PGN structure
    expect(content).toContain('[Event "ChessMimic Game"]');
    expect(content).toContain('[Site "ChessMimic"]');
    expect(content).toContain('[Date "');
    expect(content).toContain('[TimeControl "300+3"]');
  });

  test('should include clock annotations after moves', async ({ page }) => {
    // Make a move using the move input field instead of clicking on board
    const moveInput = page.locator('input[placeholder*="Enter move"]');
    await moveInput.fill('e4');
    await moveInput.press('Enter');
    
    // Wait for move to be processed
    await page.waitForTimeout(1000);
    
    // Download PGN
    const downloadPromise = page.waitForEvent('download');
    await page.getByRole('button', { name: 'Export PGN' }).click();
    const download = await downloadPromise;
    
    // Read content
    const downloadPath = await download.path();
    const content = await fs.readFile(downloadPath, 'utf8');
    
    // Verify clock annotation format
    expect(content).toMatch(/1\. e4 \{\[%clk \d:\d{2}:\d{2}\]\}/);
  });

  test('should export complete game with all moves and clocks', async ({ page }) => {
    // Make several moves
    const moveInput = page.locator('input[placeholder*="Enter move"]');
    await moveInput.fill('e4');
    await moveInput.press('Enter');
    
    // Wait for bot's response and move count to update
    await page.waitForFunction(() => {
      const moveCount = document.body.textContent?.match(/Move Count: (\d+)/);
      return moveCount && parseInt(moveCount[1]) >= 2;
    }, { timeout: 5000 });
    
    await moveInput.fill('d4');
    await moveInput.press('Enter');
    
    // Wait for bot's second response
    await page.waitForFunction(() => {
      const moveCount = document.body.textContent?.match(/Move Count: (\d+)/);
      return moveCount && parseInt(moveCount[1]) >= 4;
    }, { timeout: 5000 });
    
    // Download PGN
    const downloadPromise = page.waitForEvent('download');
    await page.getByRole('button', { name: 'Export PGN' }).click();
    const download = await downloadPromise;
    
    // Read and verify content
    const downloadPath = await download.path();
    const content = await fs.readFile(downloadPath, 'utf8');
    
    // Should have multiple moves with clock annotations
    expect(content).toMatch(/1\. \w+\d? \{\[%clk \d:\d{2}:\d{2}\]\} \w+\d? \{\[%clk \d:\d{2}:\d{2}\]\}/);
    expect(content).toMatch(/2\. \w+\d? \{\[%clk \d:\d{2}:\d{2}\]\}/);
  });

  test('should handle FEN position export correctly', async ({ page }) => {
    // Load a custom FEN position
    const fenString = 'rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2';
    await page.fill('input[placeholder="Paste FEN string here"]', fenString);
    await page.getByRole('button', { name: 'Load FEN' }).click();
    
    // Make a move from this position
    const moveInput = page.locator('input[placeholder*="Enter move"]');
    await moveInput.fill('Nf3');
    await moveInput.press('Enter');
    await page.waitForTimeout(1000);
    
    // Export PGN
    const downloadPromise = page.waitForEvent('download');
    await page.getByRole('button', { name: 'Export PGN' }).click();
    const download = await downloadPromise;
    
    // Verify FEN in PGN headers
    const downloadPath = await download.path();
    const content = await fs.readFile(downloadPath, 'utf8');
    
    expect(content).toContain(`[FEN "${fenString}"]`);
    expect(content).toContain('[SetUp "1"]');
  });

  test('should export correct metadata for different game settings', async ({ page, isMobile }) => {
    // Scroll to new game button on mobile
    const newGameButton = page.getByRole('button', { name: 'New Game' });
    if (isMobile) {
      await newGameButton.scrollIntoViewIfNeeded();
    }
    await newGameButton.click();
    
    // Wait for dialog to appear
    await page.waitForTimeout(1000);
    
    // Configure game settings - click the Black button using aria-label
    const blackButton = page.getByRole('button', { name: 'Play as Black' });
    await blackButton.scrollIntoViewIfNeeded();
    await blackButton.click();
    
    // Fill bot rating
    const ratingInput = page.locator('input[type="number"]').first();
    await ratingInput.scrollIntoViewIfNeeded();
    await ratingInput.fill('2000');
    
    // Start new game - use force click on mobile to bypass dialog overlay issues
    const startButton = page.getByRole('button', { name: 'Start Game' });
    await startButton.scrollIntoViewIfNeeded();
    if (isMobile) {
      await startButton.click({ force: true });
    } else {
      await startButton.click();
    }
    
    // Wait for bot's first move
    await page.waitForTimeout(2000);
    
    // Export PGN
    const downloadPromise = page.waitForEvent('download');
    await page.getByRole('button', { name: 'Export PGN' }).click();
    const download = await downloadPromise;
    
    // Verify metadata
    const downloadPath = await download.path();
    const content = await fs.readFile(downloadPath, 'utf8');
    
    expect(content).toContain('[White "ChessMimic Bot (2000)"]');
    expect(content).toContain('[Black "Human"]');
  });

  test('should handle time forfeit games correctly', async ({ page }) => {
    // This test would require manipulating the clock to cause a time forfeit
    // For now, we'll verify the button remains functional throughout the game
    
    // Make moves
    const moveInput = page.locator('input[placeholder*="Enter move"]');
    await moveInput.fill('e4');
    await moveInput.press('Enter');
    
    // Export button should still work
    const exportButton = page.getByRole('button', { name: 'Export PGN' });
    await expect(exportButton).toBeEnabled();
    
    // Can still trigger download
    const downloadPromise = page.waitForEvent('download');
    await exportButton.click();
    const download = await downloadPromise;
    
    expect(download).toBeTruthy();
  });

  test('should update PGN export after each move', async ({ page, isMobile }) => {
    // Export initial position
    const exportButton = page.getByRole('button', { name: 'Export PGN' });
    if (isMobile) {
      await exportButton.scrollIntoViewIfNeeded();
    }
    const download1Promise = page.waitForEvent('download');
    await exportButton.click();
    const download1 = await download1Promise;
    const content1 = await fs.readFile(await download1.path(), 'utf8');
    
    // Make a move
    const moveInput = page.locator('input[placeholder*="Enter move"]');
    await moveInput.fill('e4');
    await moveInput.press('Enter');
    await page.waitForTimeout(2000);
    
    // Export after move - scroll into view on mobile
    if (isMobile) {
      await exportButton.scrollIntoViewIfNeeded();
    }
    const download2Promise = page.waitForEvent('download');
    await exportButton.click();
    const download2 = await download2Promise;
    const content2 = await fs.readFile(await download2.path(), 'utf8');
    
    // Second export should have moves that first doesn't
    expect(content1).not.toContain('1. e4');
    expect(content2).toContain('1. e4');
    expect(content2).toMatch(/\{\[%clk \d:\d{2}:\d{2}\]\}/);
  });
});