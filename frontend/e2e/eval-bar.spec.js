import { test, expect } from '@playwright/test';

test.describe('Evaluation Bar Tests', () => {
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
    
    // Default mock for evaluate_position
    await page.route('**/evaluate_position', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          evaluation: 0.0,
          raw_value: 0.5
        })
      });
    });
    
    await page.goto('/');
    await page.waitForLoadState('networkidle');
  });

  test('should display evaluation bar with initial position', async ({ page }) => {
    // First check what the default evaluation is - look for the eval display container
    const evalValue = page.locator('div.flex-col > div').first();
    await expect(evalValue).toBeVisible();
    
    // Log the actual evaluation value
    const actualEvalText = await evalValue.textContent();
    console.log('Initial evaluation text:', actualEvalText);
    
    // Now mock neutral evaluation for starting position
    await page.route('**/evaluate_position', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          evaluation: 0.0,
          raw_value: 0.5
        })
      });
    });
    
    // Reload to get the mocked value
    await page.reload();
    await page.waitForLoadState('networkidle');
    
    // Wait for eval bar to appear - look for the flex column container
    const evalBar = page.locator('div.flex-col').filter({ has: page.getByRole('meter') });
    await expect(evalBar).toBeVisible();
    
    // Check that the value shows 0.0
    await expect(evalValue).toContainText('0.0');
    
    // For evaluation of 0, the bar fill should not be rendered
    const barFillCount = await page.locator('.eval-bar-vertical-fill').count();
    expect(barFillCount).toBe(0);
    
    // But the center line should still be visible
    const centerLine = page.locator('.eval-bar-vertical-center-line');
    await expect(centerLine).toBeVisible();
  });

  test('should show white advantage with positive evaluation', async ({ page }) => {
    // Mock positive evaluation
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
    
    await page.reload();
    await page.waitForLoadState('networkidle');
    
    // Check that the value shows 0.50
    const evalValue = page.locator('.eval-bar-vertical-value');
    await expect(evalValue).toContainText('0.50');
    
    // Check that the bar has white-advantage class
    const barFill = page.locator('.eval-bar-vertical-fill');
    await expect(barFill).toHaveClass(/white-advantage/);
    
    // The bar should extend upward from center (bottom: 50%)
    const fillStyle = await barFill.getAttribute('style');
    expect(fillStyle).toContain('height: 25%'); // 0.5 * 50 = 25%
    expect(fillStyle).toContain('bottom: 50%');
  });

  test('should show black advantage with negative evaluation', async ({ page }) => {
    // Mock negative evaluation
    await page.route('**/evaluate_position', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          evaluation: -0.8,
          raw_value: 0.2
        })
      });
    });
    
    await page.reload();
    await page.waitForLoadState('networkidle');
    
    // Check that the value shows -0.8
    const evalValue = page.locator('.eval-bar-vertical-value');
    await expect(evalValue).toContainText('-0.80');
    
    // Check that the bar has black-advantage class
    const barFill = page.locator('.eval-bar-vertical-fill');
    await expect(barFill).toHaveClass(/black-advantage/);
    
    // The bar should extend downward from center (top: 50%)
    const fillStyle = await barFill.getAttribute('style');
    expect(fillStyle).toContain('height: 40%'); // 0.8 * 50 = 40%
    expect(fillStyle).toContain('top: 50%');
  });

  test('should update evaluation after moves', async ({ page }) => {
    // Unroute the default evaluate_position mock from beforeEach
    await page.unroute('**/evaluate_position');
    
    // Mock evaluations based on FEN position
    await page.route('**/evaluate_position', async (route) => {
      const request = route.request();
      const postData = request.postDataJSON();
      const fen = postData?.fen || '';
      
      // Return different evaluations based on board position
      let evaluation = 0.0;
      if (fen.includes('4P3')) { // After e4
        evaluation = 0.3;
      }
      
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          evaluation: evaluation,
          raw_value: 0.5 + evaluation
        })
      });
    });
    
    // Navigate to page after setting up routes
    await page.goto('/');
    await page.waitForLoadState('networkidle');
    
    // Wait for initial evaluation to be displayed
    const evalValue = page.locator('.eval-bar-vertical-value');
    await expect(evalValue).toContainText('0.0');
    
    // Make a move (e2 to e4)
    const e2Pawn = page.locator('[data-square="e2"] [data-piece]').first();
    const e4Square = page.locator('[data-square="e4"]');
    
    await e2Pawn.click();
    // Wait for legal moves to be shown
    await page.waitForSelector('[data-square="e4"][style*="background"]', { timeout: 5000 });
    
    await e4Square.click();
    
    // Wait for evaluation to update to 0.30
    await expect(evalValue).toContainText('0.30', { timeout: 10000 });
    
    // Verify bar shows white advantage
    const barFill = page.locator('.eval-bar-vertical-fill');
    await expect(barFill).toBeVisible();
    
    // Wait for the bar animation to complete
    await page.waitForFunction(() => {
      const fill = document.querySelector('.eval-bar-vertical-fill');
      if (!fill) return false;
      const style = fill.getAttribute('style');
      return style && style.includes('height: 15%');
    }, { timeout: 5000 });
    
    const fillStyle = await barFill.getAttribute('style');
    expect(fillStyle).toContain('height: 15%'); // 0.3 * 50 = 15%
  });

  test('should show center line at 50%', async ({ page, browserName }) => {
    const isMobile = page.context()._options.isMobile === true;
    
    // Wait for the eval bar container to be visible first
    const evalBarContainer = page.locator('.eval-bar-vertical-container');
    await expect(evalBarContainer).toBeVisible({ timeout: 15000 });
    
    // Additional wait for Safari (mobile and desktop) and Firefox to ensure layout is stable
    if (browserName === 'webkit' || browserName === 'firefox') {
      await page.waitForTimeout(browserName === 'webkit' ? 700 : 500);
    }
    
    // The center line should always be visible
    const centerLine = page.locator('.eval-bar-vertical-center-line');
    await expect(centerLine).toBeVisible({ timeout: 10000 });
    
    // Wait for the center line to have stable dimensions
    await page.waitForFunction(() => {
      const line = document.querySelector('.eval-bar-vertical-center-line');
      if (!line) return false;
      const rect = line.getBoundingClientRect();
      return rect.height > 0 && rect.width > 0;
    }, { timeout: 20000 }); // Increased timeout for all browsers
    
    // Additional wait to ensure layout is complete
    const layoutWait = browserName === 'webkit' ? 600 : browserName === 'firefox' ? 500 : 200;
    await page.waitForTimeout(layoutWait);
    
    // For Firefox and Webkit, ensure the container has finished any animations
    if (browserName === 'firefox' || browserName === 'webkit') {
      await page.waitForFunction(() => {
        const container = document.querySelector('.eval-bar-vertical-track');
        if (!container) return false;
        const style = window.getComputedStyle(container);
        // Check that transitions have completed
        return style.transitionDuration === '0s' || !container.matches(':hover');
      }, { timeout: 5000 }).catch(() => {
        // Continue even if this check fails
      });
    }
    
    // For Webkit, additional check for layout stability
    if (browserName === 'webkit') {
      await page.evaluate(() => {
        return new Promise((resolve) => {
          if (window.requestAnimationFrame) {
            window.requestAnimationFrame(() => {
              window.requestAnimationFrame(resolve);
            });
          } else {
            setTimeout(resolve, 32); // ~2 frames at 60fps
          }
        });
      });
    }
    
    // Check its position
    const centerLineBox = await centerLine.boundingBox();
    const containerBox = await page.locator('.eval-bar-vertical-track').boundingBox();
    
    if (centerLineBox && containerBox) {
      // Calculate the center of the center line (not just its top edge)
      const centerLineMiddle = centerLineBox.y + (centerLineBox.height / 2);
      const containerMiddle = containerBox.y + (containerBox.height / 2);
      
      // The center line's middle should be at the container's middle
      const difference = Math.abs(centerLineMiddle - containerMiddle);
      
      // Allow more tolerance for webkit (especially mobile) and Firefox due to potential rendering differences
      // and sub-pixel positioning
      let tolerance = 5;
      if (isMobile && browserName === 'webkit') {
        tolerance = 20;
      } else if (browserName === 'webkit') {
        tolerance = 15;
      } else if (browserName === 'firefox') {
        tolerance = 10; // Firefox needs more tolerance due to rendering differences
      }
      
      expect(difference).toBeLessThan(tolerance);
    }
  });

  test('should handle extreme evaluations', async ({ page }) => {
    // Test maximum white advantage
    await page.route('**/evaluate_position', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          evaluation: 1.0,
          raw_value: 1.0
        })
      });
    });
    
    await page.reload();
    await page.waitForLoadState('networkidle');
    
    const evalValue = page.locator('.eval-bar-vertical-value');
    await expect(evalValue).toContainText('1.00');
    
    const barFill = page.locator('.eval-bar-vertical-fill');
    const fillStyle = await barFill.getAttribute('style');
    expect(fillStyle).toContain('height: 50%'); // 1.0 * 50 = 50% (max)
    
    // Test maximum black advantage
    await page.route('**/evaluate_position', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          evaluation: -1.0,
          raw_value: 0.0
        })
      });
    });
    
    await page.reload();
    await page.waitForLoadState('networkidle');
    
    await expect(evalValue).toContainText('-1.00');
    
    const fillStyle2 = await barFill.getAttribute('style');
    expect(fillStyle2).toContain('height: 50%'); // 1.0 * 50 = 50% (max)
  });
});