import { test, expect } from '@playwright/test';

test.describe('Evaluation Bar Screenshot Tests', () => {
  test('capture evaluation bar at different values', async ({ page }) => {
    const testCases = [
      { eval: -1.0, name: 'max-black' },
      { eval: -0.5, name: 'moderate-black' },
      { eval: -0.2, name: 'slight-black' },
      { eval: 0.0, name: 'neutral' },
      { eval: 0.2, name: 'slight-white' },
      { eval: 0.5, name: 'moderate-white' },
      { eval: 1.0, name: 'max-white' }
    ];
    
    // Mock API endpoints
    await page.route('**/get_move', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          move: 'Nf6',
          thinking_time: 0.5
        })
      });
    });
    
    for (const testCase of testCases) {
      console.log(`Testing evaluation: ${testCase.eval} (${testCase.name})`);
      
      // Mock specific evaluation
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
      await page.waitForTimeout(500); // Wait for animations
      
      // Capture screenshot of the eval bar
      const evalBar = page.locator('.eval-bar-vertical-container');
      await expect(evalBar).toBeVisible();
      
      await evalBar.screenshot({ 
        path: `eval-bar-${testCase.name}.png`,
        animations: 'disabled'
      });
      
      // Also capture full page for context
      await page.screenshot({
        path: `full-page-${testCase.name}.png`,
        fullPage: true
      });
      
      // Verify the numeric display
      const evalValue = page.locator('.eval-bar-vertical-value');
      const expectedText = testCase.eval.toFixed(2);
      await expect(evalValue).toContainText(expectedText);
      
      // Verify bar presence/absence
      const barFill = page.locator('.eval-bar-vertical-fill');
      if (testCase.eval === 0) {
        const count = await barFill.count();
        expect(count).toBe(0);
        console.log(`  ✓ No bar rendered for neutral position`);
      } else {
        await expect(barFill).toBeVisible();
        const fillStyle = await barFill.getAttribute('style');
        
        // Calculate expected height
        const expectedHeight = Math.abs(testCase.eval) * 50;
        expect(fillStyle).toContain(`height: ${expectedHeight}%`);
        
        // Check position (top or bottom)
        if (testCase.eval > 0) {
          expect(fillStyle).toContain('bottom: 50%');
          await expect(barFill).toHaveClass(/white-advantage/);
          console.log(`  ✓ White advantage bar extends ${expectedHeight}% upward from center`);
        } else {
          expect(fillStyle).toContain('top: 50%');
          await expect(barFill).toHaveClass(/black-advantage/);
          console.log(`  ✓ Black advantage bar extends ${expectedHeight}% downward from center`);
        }
      }
      
      // Verify center line is always visible
      const centerLine = page.locator('.eval-bar-vertical-center-line');
      await expect(centerLine).toBeVisible();
    }
    
    console.log('\nScreenshots saved:');
    console.log('- eval-bar-*.png: Close-up of evaluation bar at different values');
    console.log('- full-page-*.png: Full page context for each evaluation');
  });

  test('capture evaluation bar animation during gameplay', async ({ page }) => {
    let evalIndex = 0;
    const evaluations = [0.1, -0.3, 0.5, -0.8, 0.0, 0.7];
    
    // Mock evaluation to change with each position
    await page.route('**/evaluate_position', async (route) => {
      const evaluation = evaluations[evalIndex % evaluations.length];
      evalIndex++;
      
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
          move: evalIndex % 2 === 0 ? 'e5' : 'Nf6',
          thinking_time: 0.5
        })
      });
    });
    
    await page.goto('/');
    await page.waitForLoadState('networkidle');
    
    // Take initial screenshot
    await page.locator('.eval-bar-vertical-container').screenshot({ 
      path: 'eval-bar-gameplay-0-initial.png' 
    });
    
    // Make moves and capture bar changes
    const moves = [
      { from: 'e2', to: 'e4', name: '1-e4' },
      { from: 'd2', to: 'd4', name: '2-d4' },
      { from: 'g1', to: 'f3', name: '3-Nf3' }
    ];
    
    for (let i = 0; i < moves.length; i++) {
      const move = moves[i];
      console.log(`Making move: ${move.name}`);
      
      // Make the move
      await page.locator(`[data-square="${move.from}"] [data-piece]`).first().click();
      await page.waitForTimeout(200);
      await page.locator(`[data-square="${move.to}"]`).click();
      await page.waitForTimeout(1000); // Wait for bot response and evaluation
      
      // Capture screenshot after move
      await page.locator('.eval-bar-vertical-container').screenshot({ 
        path: `eval-bar-gameplay-${i + 1}-${move.name}.png` 
      });
      
      // Log the current evaluation
      const evalValue = await page.locator('.eval-bar-vertical-value').textContent();
      console.log(`  Evaluation after move: ${evalValue}`);
    }
    
    console.log('\nGameplay screenshots saved as eval-bar-gameplay-*.png');
  });
});