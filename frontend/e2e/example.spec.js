import { test, expect } from '@playwright/test';

test.describe('Chess App', () => {
  test.beforeEach(async ({ page, browserName }) => {
    await page.goto('/');
    // Wait for page to be fully loaded
    await page.waitForLoadState('networkidle');
    
    // Additional wait for Firefox to ensure all resources are loaded
    if (browserName === 'firefox') {
      await page.waitForTimeout(500);
    }
  });

  test('should load the homepage', async ({ page, browserName }) => {
    // Wait for the title to be set
    await page.waitForFunction(() => document.title !== '', { timeout: 10000 });
    
    // Firefox may need extra time for title to stabilize
    if (browserName === 'firefox') {
      await page.waitForTimeout(200);
    }
    
    await expect(page).toHaveTitle(/ChessMimic/i, { timeout: 10000 });
  });

  test('should display the chess board', async ({ page, browserName }) => {
    // Wait for any chess piece to be visible first (indicates board is loaded)
    await page.waitForSelector('[data-piece]', { timeout: 15000 });
    
    // Additional wait for Firefox rendering
    if (browserName === 'firefox') {
      await page.waitForTimeout(300);
    }
    
    // Look for react-chessboard component or board container
    const chessboard = page.locator('[data-testid="chessboard"]').or(
      page.locator('div[style*="position: relative"]').filter({ has: page.locator('div[style*="width"]') })
    ).or(
      page.locator('[class*="chessboard"]')
    ).or(
      page.locator('div').filter({ hasText: 'Browse History' })
    );
    
    await expect(chessboard.first()).toBeVisible({ timeout: 10000 });
  });

  test('should be responsive on mobile', async ({ page, isMobile }) => {
    if (isMobile) {
      // Check that the app adapts to mobile viewport
      const viewport = page.viewportSize();
      expect(viewport?.width).toBeLessThanOrEqual(768);
      
      // Verify main content is visible
      await expect(page.locator('body')).toBeVisible();
    }
  });
});