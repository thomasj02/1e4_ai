# Touch-Based Drag Operations in Playwright for Mobile Browsers

## Overview

Playwright's built-in `dragTo()` method uses mouse events, which don't work reliably on mobile browsers or WebKit. For mobile testing, you need to implement touch-based drag using touch events.

## Key Differences: Mouse Drag vs Touch Drag

1. **Mouse Drag** (desktop):
   - Uses `mousedown`, `mousemove`, `mouseup` events
   - Works with `locator.dragTo()` method
   - Reliable on desktop browsers

2. **Touch Drag** (mobile):
   - Uses `touchstart`, `touchmove`, `touchend` events
   - Requires manual event dispatching
   - Needed for mobile browsers and touch-enabled devices

## Implementing Touch-Based Drag

### Basic Touch Drag Function

```javascript
async function touchDrag(page, sourceLocator, targetLocator, steps = 10) {
  // Get source element center
  const sourceBox = await sourceLocator.boundingBox();
  const sourceCenterX = sourceBox.x + sourceBox.width / 2;
  const sourceCenterY = sourceBox.y + sourceBox.height / 2;
  
  // Get target element center
  const targetBox = await targetLocator.boundingBox();
  const targetCenterX = targetBox.x + targetBox.width / 2;
  const targetCenterY = targetBox.y + targetBox.height / 2;
  
  // Calculate delta
  const deltaX = targetCenterX - sourceCenterX;
  const deltaY = targetCenterY - sourceCenterY;
  
  // Initial touch
  const startTouch = [{
    identifier: 0,
    clientX: sourceCenterX,
    clientY: sourceCenterY,
    pageX: sourceCenterX,
    pageY: sourceCenterY,
    screenX: sourceCenterX,
    screenY: sourceCenterY,
  }];
  
  await sourceLocator.dispatchEvent('touchstart', {
    touches: startTouch,
    changedTouches: startTouch,
    targetTouches: startTouch
  });
  
  // Move touch point in steps
  for (let i = 1; i <= steps; i++) {
    const progress = i / steps;
    const currentTouch = [{
      identifier: 0,
      clientX: sourceCenterX + deltaX * progress,
      clientY: sourceCenterY + deltaY * progress,
      pageX: sourceCenterX + deltaX * progress,
      pageY: sourceCenterY + deltaY * progress,
      screenX: sourceCenterX + deltaX * progress,
      screenY: sourceCenterY + deltaY * progress,
    }];
    
    await page.dispatchEvent('touchmove', {
      touches: currentTouch,
      changedTouches: currentTouch,
      targetTouches: currentTouch
    });
    
    // Small delay between moves for realistic simulation
    await page.waitForTimeout(10);
  }
  
  // End touch
  const endTouch = [{
    identifier: 0,
    clientX: targetCenterX,
    clientY: targetCenterY,
    pageX: targetCenterX,
    pageY: targetCenterY,
    screenX: targetCenterX,
    screenY: targetCenterY,
  }];
  
  await page.dispatchEvent('touchend', {
    touches: [],
    changedTouches: endTouch,
    targetTouches: []
  });
}
```

## Improved makeMove Function for Chess Tests

```javascript
async function makeMove(page, from, to, browserName, isMobile) {
  const sourceSquare = page.locator(`[data-square="${from}"]`);
  const targetSquare = page.locator(`[data-square="${to}"]`);
  const sourcePiece = sourceSquare.locator('[data-piece]').first();
  
  if (browserName === 'webkit' || isMobile) {
    // Use touch events for WebKit and mobile
    await touchDrag(page, sourcePiece, targetSquare);
  } else {
    // Use standard drag for desktop browsers
    await sourcePiece.dragTo(targetSquare);
  }
  
  // Wait for move animation and processing
  await page.waitForTimeout(500);
}
```

## Usage in Tests

```javascript
test('should make a valid move by dragging', async ({ page, browserName, isMobile }) => {
  // Skip test if drag is not reliable
  // test.skip(browserName === 'webkit' || isMobile, 'Drag not reliable on mobile/webkit');
  
  // Instead of skipping, use appropriate drag method
  if (browserName === 'webkit' || isMobile) {
    // Use touch-based drag
    const e2Pawn = page.locator('[data-square="e2"] [data-piece="wP"]');
    const e4Square = page.locator('[data-square="e4"]');
    await touchDrag(page, e2Pawn, e4Square);
  } else {
    // Use mouse-based drag
    const e2Pawn = page.locator('[data-square="e2"] [data-piece="wP"]');
    const e4Square = page.locator('[data-square="e4"]');
    await e2Pawn.dragTo(e4Square);
  }
  
  // Verify move
  await page.waitForTimeout(1000);
  const e4Pawn = await page.locator('[data-square="e4"] [data-piece="wP"]').count();
  expect(e4Pawn).toBe(1);
});
```

## Important Notes

1. **Touch Context**: For touch events to work properly, the browser context should be created with `hasTouch: true`:
   ```javascript
   const context = await browser.newContext({
     hasTouch: true,
     isMobile: true
   });
   ```

2. **Coordinate Systems**: Touch events use different coordinate properties:
   - `clientX/clientY`: Relative to viewport
   - `pageX/pageY`: Relative to page (includes scroll)
   - `screenX/screenY`: Relative to screen
   - Most web apps use `clientX/clientY`

3. **Event Properties**: Touch events have three touch lists:
   - `touches`: All current touch points
   - `targetTouches`: Touch points on the target element
   - `changedTouches`: Touch points that changed in this event

4. **Gesture Recognition**: Some libraries (like react-chessboard) might have their own gesture recognition that could interfere with synthetic touch events.

## Testing Strategy

1. **Progressive Enhancement**: Support both mouse and touch
2. **Feature Detection**: Check if touch events are supported
3. **Fallback Options**: Provide alternative interaction methods
4. **Cross-browser Testing**: Test on real mobile devices when possible

## Limitations

1. Playwright's touchscreen API only supports `tap()` currently
2. Complex gestures require manual event dispatching
3. Some frameworks may not respond to synthetic touch events
4. WebKit and mobile browsers have different event handling

## Alternative Approaches

If touch-based drag doesn't work reliably:

1. **Click-based moves**: Click source, then click destination
2. **Keyboard navigation**: Use arrow keys to select and move
3. **API-based moves**: Directly call move functions if exposed
4. **Visual regression**: Test UI state rather than interactions

## References

- [Playwright Touch Events Documentation](https://playwright.dev/docs/touch-events)
- [Playwright Touchscreen API](https://playwright.dev/docs/api/class-touchscreen)
- [MDN Touch Events](https://developer.mozilla.org/en-US/docs/Web/API/Touch_events)
- [GitHub Issue #2903 - Touch Events Support](https://github.com/microsoft/playwright/issues/2903)