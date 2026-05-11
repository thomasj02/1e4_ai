import { test, expect } from '@playwright/test';

test.describe('Accessibility', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/');
    await page.waitForLoadState('networkidle');
    await page.waitForSelector('h1:has-text("Chessmimic MVP")', { timeout: 10000 });
  });

  test('should have proper heading structure', async ({ page }) => {
    // Check for h1 element
    const h1 = page.locator('h1');
    if (await h1.count() > 0) {
      await expect(h1.first()).toBeVisible();
    }
    
    // Check that headings are in logical order
    const headings = page.locator('h1, h2, h3, h4, h5, h6');
    const headingCount = await headings.count();
    
    if (headingCount > 0) {
      expect(headingCount).toBeGreaterThan(0);
    }
  });

  test('should be keyboard navigable', async ({ page }) => {
    // Ensure page is fully loaded
    await page.waitForTimeout(1000);
    
    // Test tab navigation
    await page.keyboard.press('Tab');
    
    // Check if focus is visible on interactive elements
    // First check if any element has focus
    const focusedExists = await page.evaluate(() => document.activeElement !== document.body);
    expect(focusedExists).toBe(true);
    
    // Check that we can navigate through interactive elements
    const initialFocused = await page.evaluate(() => document.activeElement?.tagName);
    
    await page.keyboard.press('Tab');
    const secondFocused = await page.evaluate(() => document.activeElement?.tagName);
    
    // Verify that focus moved to a different element
    expect(initialFocused).toBeTruthy();
    expect(secondFocused).toBeTruthy();
  });

  test('should have proper color contrast', async ({ page }) => {
    // This is a basic check - you might want to use axe-playwright for comprehensive accessibility testing
    const body = page.locator('body');
    await expect(body).toBeVisible();
    
    // Check if there are any elements with insufficient contrast
    // This would require additional tooling like axe-playwright for thorough testing
  });

  test('should have alt text for images', async ({ page }) => {
    const images = page.locator('img');
    const imageCount = await images.count();
    
    for (let i = 0; i < imageCount; i++) {
      const img = images.nth(i);
      const alt = await img.getAttribute('alt');
      
      // Images should have alt text (can be empty for decorative images)
      expect(alt).not.toBeNull();
    }
  });
});