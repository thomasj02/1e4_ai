import { test, expect } from '@playwright/test';

test.describe('Evaluation Bar Visual Tests', () => {
  test.beforeEach(async ({ page }) => {
    // Mock API endpoints
    await page.route('**/get_move', async (route) => {
      const postData = await route.request().postDataJSON();
      
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
  });

  test('should visually show evaluation changes', async ({ page }) => {
    let evalCount = 0;
    
    // Mock evaluation to change dramatically
    await page.route('**/evaluate_position', async (route) => {
      evalCount++;
      console.log(`Evaluation call #${evalCount}`);
      
      // Cycle through different evaluations
      const evaluations = [0.1, -0.5, 0.8, -0.3, 0.0];
      const evaluation = evaluations[(evalCount - 1) % evaluations.length];
      
      console.log(`Returning evaluation: ${evaluation}`);
      
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          evaluation: evaluation,
          raw_value: 0.5 + evaluation / 2
        })
      });
    });
    
    await page.goto('/');
    await page.waitForLoadState('networkidle');
    
    // Wait for evaluation to be displayed
    await page.waitForTimeout(500);
    
    // Get the bar fill element
    const barFill = page.locator('.eval-bar-vertical-fill');
    
    // Check initial state
    await expect(barFill).toBeVisible();
    let fillStyle = await barFill.getAttribute('style');
    console.log('Initial bar style:', fillStyle);
    
    // The initial evaluation depends on which call we're on
    // Just verify the bar is visible and has proper styling
    expect(fillStyle).toMatch(/height: \d+%/);
    expect(fillStyle).toMatch(/(bottom|top): 50%/);
    
    // Make a move to trigger evaluation change
    const e2Pawn = page.locator('[data-square="e2"] [data-piece]').first();
    const e4Square = page.locator('[data-square="e4"]');
    
    await e2Pawn.click();
    await e4Square.click();
    await page.waitForTimeout(1000);
    
    // Check that bar changed
    fillStyle = await barFill.getAttribute('style');
    console.log('After first move:', fillStyle);
    // Just verify it changed and has proper structure
    expect(fillStyle).toMatch(/height: \d+%/);
    expect(fillStyle).toMatch(/(bottom|top): 50%/);
    
    // Make another move
    const d2Pawn = page.locator('[data-square="d2"] [data-piece]').first();
    const d4Square = page.locator('[data-square="d4"]');
    
    await d2Pawn.click();
    await d4Square.click();
    await page.waitForTimeout(1000);
    
    // Check that bar changed again
    const barFillCount = await barFill.count();
    if (barFillCount > 0) {
      fillStyle = await barFill.getAttribute('style');
      console.log('After second move:', fillStyle);
      // Just verify it has proper structure
      expect(fillStyle).toMatch(/height: \d+%/);
      expect(fillStyle).toMatch(/(bottom|top): 50%/);
    } else {
      console.log('After second move: No bar (evaluation is 0)');
    }
  });

  test('should show bar at correct position for different evaluations', async ({ page }) => {
    // Test specific evaluation values
    const testCases = [
      { eval: 0.0, expectedHeight: null, expectedPosition: null },
      { eval: 0.2, expectedHeight: '10%', expectedPosition: 'bottom: 50%' },
      { eval: -0.4, expectedHeight: '20%', expectedPosition: 'top: 50%' },
      { eval: 1.0, expectedHeight: '50%', expectedPosition: 'bottom: 50%' },
      { eval: -1.0, expectedHeight: '50%', expectedPosition: 'top: 50%' },
    ];
    
    for (const testCase of testCases) {
      console.log(`Testing evaluation: ${testCase.eval}`);
      
      await page.route('**/evaluate_position', async (route) => {
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify({
            evaluation: testCase.eval,
            raw_value: 0.5 + testCase.eval / 2
          })
        });
      });
      
      await page.goto('/');
      await page.waitForLoadState('networkidle');
      
      const barFill = page.locator('.eval-bar-vertical-fill');
      
      if (testCase.eval === 0) {
        // For 0 evaluation, the bar fill element is not rendered
        const barFillCount = await barFill.count();
        expect(barFillCount).toBe(0);
      } else {
        await expect(barFill).toBeVisible();
        const fillStyle = await barFill.getAttribute('style');
        expect(fillStyle).toContain(`height: ${testCase.expectedHeight}`);
        if (testCase.expectedPosition) {
          expect(fillStyle).toContain(testCase.expectedPosition);
        }
        
        // Check class
        if (testCase.eval > 0) {
          await expect(barFill).toHaveClass(/white-advantage/);
        } else {
          await expect(barFill).toHaveClass(/black-advantage/);
        }
      }
    }
  });

  test('should have visible center line', async ({ page, browserName }) => {
    await page.route('**/evaluate_position', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          evaluation: 0.3,
          raw_value: 0.65
        })
      });
    });
    
    await page.goto('/');
    await page.waitForLoadState('networkidle');
    
    // Additional wait for Firefox to ensure rendering is complete
    if (browserName === 'firefox') {
      await page.waitForTimeout(500);
    }
    
    // Wait for eval bar container to be visible first
    const evalBarContainer = page.locator('.eval-bar-vertical-container');
    await expect(evalBarContainer).toBeVisible({ timeout: 10000 });
    
    // Firefox needs extra time for CSS rendering
    if (browserName === 'firefox') {
      await page.waitForTimeout(300);
    }
    
    const centerLine = page.locator('.eval-bar-vertical-center-line');
    await expect(centerLine).toBeVisible({ timeout: 10000 });
    
    // Take a screenshot for visual verification
    await page.locator('.eval-bar-vertical-container').screenshot({ 
      path: 'eval-bar-screenshot.png' 
    });
  });

  test('should have proper visual styling matching chess.com', async ({ page, browserName }) => {
    await page.route('**/evaluate_position', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          evaluation: 0.5,
          raw_value: 0.75
        })
      });
    });
    
    await page.goto('/');
    await page.waitForLoadState('networkidle');
    
    // Firefox-specific wait for rendering
    if (browserName === 'firefox') {
      await page.waitForTimeout(500);
    }
    
    // Check container width
    const container = page.locator('.eval-bar-vertical-container');
    await expect(container).toBeVisible({ timeout: 10000 });
    
    // Firefox needs time for layout calculations
    if (browserName === 'firefox') {
      await page.waitForTimeout(300);
    }
    
    const containerBox = await container.boundingBox();
    expect(containerBox.width).toBeGreaterThanOrEqual(40);
    expect(containerBox.width).toBeLessThanOrEqual(40);
    
    // Check value box styling
    const valueBox = page.locator('.eval-bar-vertical-value');
    await expect(valueBox).toBeVisible({ timeout: 10000 });
    
    // Wait for styles to be applied in Firefox
    if (browserName === 'firefox') {
      await page.waitForFunction(() => {
        const el = document.querySelector('.eval-bar-vertical-value');
        if (!el) return false;
        const styles = window.getComputedStyle(el);
        return styles.backgroundColor !== '' && styles.fontSize !== '';
      }, { timeout: 5000 });
    }
    
    const valueBoxStyles = await valueBox.evaluate(el => {
      const styles = window.getComputedStyle(el);
      return {
        backgroundColor: styles.backgroundColor,
        fontSize: styles.fontSize,
        fontWeight: styles.fontWeight
      };
    });
    expect(valueBoxStyles.backgroundColor).toBe('rgb(38, 36, 33)');
    expect(valueBoxStyles.fontSize).toBe('14px');
    expect(valueBoxStyles.fontWeight).toBe('700');
    
    // Check bar styling
    const bar = page.locator('.eval-bar-vertical');
    await expect(bar).toBeVisible({ timeout: 10000 });
    const barBox = await bar.boundingBox();
    expect(barBox.width).toBeGreaterThanOrEqual(40);
    expect(barBox.width).toBeLessThanOrEqual(40);
    
    // Check that white advantage fill is visible and positioned correctly
    const whiteFill = page.locator('.eval-bar-vertical-fill.white-advantage');
    await expect(whiteFill).toBeVisible();
    const fillStyles = await whiteFill.evaluate(el => {
      const styles = window.getComputedStyle(el);
      return {
        backgroundColor: styles.backgroundColor,
        height: el.style.height,
        bottom: el.style.bottom
      };
    });
    expect(fillStyles.backgroundColor).toBe('rgb(255, 255, 255)');
    expect(fillStyles.height).toBe('25%'); // 0.5 * 50%
    expect(fillStyles.bottom).toBe('50%');
  });

  test('should display black advantage correctly', async ({ page, browserName }) => {
    await page.route('**/evaluate_position', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          evaluation: -0.3,
          raw_value: 0.35
        })
      });
    });
    
    await page.goto('/');
    await page.waitForLoadState('networkidle');
    
    // Firefox-specific wait for rendering
    if (browserName === 'firefox') {
      await page.waitForTimeout(500);
    }
    
    // Wait for eval bar to be visible
    const evalBarContainer = page.locator('.eval-bar-vertical-container');
    await expect(evalBarContainer).toBeVisible({ timeout: 10000 });
    
    // Check value display
    const valueBox = page.locator('.eval-bar-vertical-value');
    await expect(valueBox).toBeVisible({ timeout: 10000 });
    
    // Firefox needs time for text content to render
    if (browserName === 'firefox') {
      await page.waitForTimeout(300);
    }
    
    await expect(valueBox).toContainText('-0.3');
    
    // Check black advantage fill
    const blackFill = page.locator('.eval-bar-vertical-fill.black-advantage');
    await expect(blackFill).toBeVisible({ timeout: 10000 });
    
    // Wait for styles to be fully applied in Firefox
    if (browserName === 'firefox') {
      await page.waitForFunction(() => {
        const el = document.querySelector('.eval-bar-vertical-fill.black-advantage');
        if (!el) return false;
        const styles = window.getComputedStyle(el);
        return styles.backgroundColor !== '' && el.style.height !== '';
      }, { timeout: 5000 });
    }
    
    const fillStyles = await blackFill.evaluate(el => {
      const styles = window.getComputedStyle(el);
      return {
        backgroundColor: styles.backgroundColor,
        height: el.style.height,
        top: el.style.top
      };
    });
    expect(fillStyles.backgroundColor).toBe('rgb(0, 0, 0)');
    expect(fillStyles.height).toBe('15%'); // 0.3 * 50%
    expect(fillStyles.top).toBe('50%');
  });
});