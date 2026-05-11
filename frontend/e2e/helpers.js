// Common helper functions for e2e tests

export async function navigateToApp(page) {
  await page.goto('/');
  await page.waitForLoadState('networkidle');
  // Wait for the app to load by checking for the title
  await page.waitForSelector('h1:has-text("Chessmimic MVP")', { timeout: 10000 });
  // Wait a bit more for any dynamic content to load
  await page.waitForTimeout(500);
}

export async function waitForChessboard(page) {
  // Wait for chess pieces to be visible
  await page.waitForSelector('[data-piece]', { timeout: 10000 });
}

export async function makeMove(page, fromSquare, toSquare, browserName) {
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