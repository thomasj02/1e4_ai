import { test, expect } from '@playwright/test';

test.describe('Evaluation Bar Live Updates', () => {
  test('should update bar position when evaluation changes', async ({ page }) => {
    // Start with a positive evaluation
    let currentEval = 0.4;
    
    await page.route('**/evaluate_position', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          evaluation: currentEval,
          raw_value: 0.5 + currentEval / 2
        })
      });
    });
    
    await page.route('**/get_move', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          move: 'e5',
          thinking_time: 0.1
        })
      });
    });
    
    await page.goto('/');
    await page.waitForLoadState('networkidle');
    
    // Get initial bar state
    const barFill = page.locator('.eval-bar-vertical-fill');
    const evalValue = page.locator('.eval-bar-vertical-value');
    
    // Check initial state
    await expect(evalValue).toContainText('0.40');
    await expect(barFill).toHaveClass(/white-advantage/);
    let style = await barFill.getAttribute('style');
    expect(style).toContain('height: 20%'); // 0.4 * 50
    expect(style).toContain('bottom: 50%');
    
    // Take screenshot of initial state
    await page.locator('.eval-bar-vertical-container').screenshot({ 
      path: 'eval-bar-initial.png' 
    });
    
    // Change evaluation to negative
    currentEval = -0.6;
    
    // Make a move to trigger re-evaluation
    await page.locator('[data-square="e2"] [data-piece]').first().click();
    await page.locator('[data-square="e4"]').click();
    await page.waitForTimeout(1000);
    
    // Check updated state
    await expect(evalValue).toContainText('-0.60');
    await expect(barFill).toHaveClass(/black-advantage/);
    style = await barFill.getAttribute('style');
    expect(style).toContain('height: 30%'); // 0.6 * 50
    expect(style).toContain('top: 50%');
    
    // Take screenshot of updated state
    await page.locator('.eval-bar-vertical-container').screenshot({ 
      path: 'eval-bar-updated.png' 
    });
    
    // Change to neutral
    currentEval = 0.0;
    
    // Make another move
    await page.locator('[data-square="d2"] [data-piece]').first().click();
    await page.locator('[data-square="d4"]').click();
    await page.waitForTimeout(1000);
    
    // Check neutral state
    await expect(evalValue).toContainText('0.00');
    // When eval is 0, the bar fill element is not rendered
    const barFillCount = await barFill.count();
    expect(barFillCount).toBe(0);
    
    // Take screenshot of neutral state
    await page.locator('.eval-bar-vertical-container').screenshot({ 
      path: 'eval-bar-neutral.png' 
    });
  });

  test('bar fill should be visible and change size', async ({ page }) => {
    // Test that the actual visual bar changes
    await page.route('**/evaluate_position', async (route) => {
      const url = route.request().url();
      const postData = await route.request().postDataJSON();
      
      // Return different evaluations based on move count
      const moveCount = postData.moves ? postData.moves.length : 0;
      const evaluation = moveCount === 0 ? 0.2 : moveCount === 1 ? -0.7 : 0.5;
      
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          evaluation: evaluation,
          raw_value: 0.5 + evaluation / 2
        })
      });
    });
    
    await page.route('**/get_move', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          move: 'e5',
          thinking_time: 0.1
        })
      });
    });
    
    await page.goto('/');
    await page.waitForLoadState('networkidle');
    
    const barFill = page.locator('.eval-bar-vertical-fill');
    const barTrack = page.locator('.eval-bar-vertical-track');
    
    // Verify bar track is visible
    await expect(barTrack).toBeVisible();
    
    // Get initial bar size
    await expect(barFill).toBeVisible();
    const initialBox = await barFill.boundingBox();
    console.log('Initial bar box:', initialBox);
    
    // Make a move
    await page.locator('[data-square="e2"] [data-piece]').first().click();
    await page.locator('[data-square="e4"]').click();
    await page.waitForTimeout(1000);
    
    // Get updated bar size
    await expect(barFill).toBeVisible();
    const updatedBox = await barFill.boundingBox();
    console.log('Updated bar box:', updatedBox);
    
    // Verify the bar changed
    if (initialBox && updatedBox) {
      // Height should have changed
      expect(updatedBox.height).not.toBe(initialBox.height);
      
      // Position should have changed (from bottom to top)
      expect(updatedBox.y).not.toBe(initialBox.y);
    }
    
    // Verify center line is visible
    const centerLine = page.locator('.eval-bar-vertical-center-line');
    await expect(centerLine).toBeVisible();
  });
});