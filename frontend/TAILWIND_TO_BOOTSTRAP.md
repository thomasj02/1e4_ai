# Migration Plan: Tailwind CSS to Bootstrap

This document outlines the detailed steps for migrating the frontend from Tailwind CSS to Bootstrap. This plan is designed to be executed by an LLM agent.

## Step 1: Dependency Management ✅

1.  **Uninstall Tailwind CSS Packages:**
    Open a terminal and run the following command to remove all Tailwind-related dependencies from `package.json`:

    ```bash
    npm uninstall tailwindcss postcss autoprefixer
    ```

2.  **Install Bootstrap:**
    Next, install Bootstrap and its required peer dependency, `react-bootstrap`:

    ```bash
    npm install bootstrap react-bootstrap
    ```

3.  **Verify Dependencies:**
    After the commands complete, open `package.json` and confirm that `tailwindcss`, `postcss`, and `autoprefixer` have been removed and that `bootstrap` and `react-bootstrap` are listed in the `dependencies` section.

## Step 2: Configuration Removal ✅

1.  **Delete Tailwind Configuration Files:**
    Delete the following Tailwind-specific configuration files from the project root:
    *   `postcss.config.mjs`
    *   `tailwind.config.ts` (if it exists)

2.  **Update `next.config.ts`:**
    If there are any Tailwind-related configurations inside `next.config.ts`, remove them.

## Step 3: Global Styles and Imports ✅

1.  **Remove Tailwind Imports:**
    Open `src/app/globals.css` and remove the following Tailwind CSS import statements at the top of the file:

    ```css
    @tailwind base;
    @tailwind components;
    @tailwind utilities;
    ```

2.  **Import Bootstrap CSS:**
    In `src/app/layout.tsx` (or your main layout file), add the following import statement at the top to include Bootstrap's stylesheet:

    ```javascript
    import 'bootstrap/dist/css/bootstrap.min.css';
    ```

## Step 4: Component-Level Migration

This is the most intensive part of the migration. Each component file needs to be updated to replace Tailwind utility classes with Bootstrap's classes and components.

**General Guidelines for Component Migration:**

*   **Flexbox and Grid:** Replace `flex`, `grid`, `justify-*`, `items-*` with Bootstrap's flexbox utilities (`d-flex`, `justify-content-*`, `align-items-*`) or the `Row` and `Col` components from `react-bootstrap`.
*   **Buttons:** Replace `<button className="...">` with the `<Button>` component from `react-bootstrap` and use the `variant` prop for styling (e.g., `variant="primary"`).
*   **Spacing:** Replace `p-*`, `m-*` with Bootstrap's spacing classes (e.g., `p-4`, `m-2`).
*   **Colors:** Replace `bg-*`, `text-*` with Bootstrap's background and color utilities (e.g., `bg-primary`, `text-white`).
*   **Typography:** Replace `font-*`, `text-*` with Bootstrap's typography classes (e.g., `fw-bold`, `fs-4`).

**List of Files to Migrate:**

*   `src/app/page.tsx`
*   `src/components/AppIntegrations.tsx`
*   `src/components/ChessboardArea.tsx`
*   `src/components/ChessClock.tsx`
*   `src/components/ConsoleCommandInput.tsx`
*   `src/components/EvalBar.tsx`
*   `src/components/GameControls.tsx`
*   `src/components/GameStatus.tsx`
*   `src/components/MoveHistory.tsx`
*   `src/components/MoveList.tsx`
*   `src/components/NewGameDialog.tsx`
*   `src/components/PlayerInfoPanel.tsx`
*   `src/components/ThemeSwitcher.tsx`

## Step 4a: Detailed Component-Level Migration Plan

This is a detailed breakdown of the changes required for each component.

### `src/app/page.tsx` ✅

*   **Analysis:** This file uses basic flexbox for layout and some text styling for the loading state.
*   **Plan:**
    1.  Replace `flex-1 flex w-full` with `flex-grow-1 d-flex w-100`.
    2.  For the loading state, replace `p-5 flex justify-center` with `p-5 d-flex justify-content-center`.
    3.  Replace `text-center` with `text-center` (no change).
    4.  Replace `text-3xl font-bold mb-4` with `h1 mb-4` on the `<h1>` element.
    5.  Replace `text-gray-600` with `text-muted`.

### `src/components/ChessClock.tsx` ✅

*   **Analysis:** This component uses DaisyUI classes (`stat`, `stat-title`, `stat-value`, `ring`, `divider`) and Tailwind for colors and animations (`text-error`, `animate-pulse`).
*   **Plan:**
    1.  The overall structure will be changed to use Bootstrap's `Card` and `ListGroup` components.
    2.  Replace `stats stats-horizontal shadow w-full mb-5` with `card shadow w-100 mb-3`.
    3.  Create two `ListGroup.Item` elements for black and white clocks.
    4.  Replace `getStatClasses` logic with Bootstrap classes. Active player will have `active` class on the `ListGroup.Item`.
    5.  Replace `getTimeClasses` logic. Time warnings will use `text-danger`, `text-warning`.
    6.  The divider will be replaced by the natural separation of `ListGroup.Item`s.

### `src/components/ChessboardArea.tsx` ✅

*   **Analysis:** Uses flexbox for layout (`flex`, `items-center`, `justify-center`) and a background overlay for the history browsing mode.
*   **Plan:**
    1.  Replace `flex items-center justify-center gap-0 w-full h-full` with `d-flex align-items-center justify-content-center gap-0 w-100 h-100`.
    2.  The `EvalBar` will be positioned next to the board.
    3.  The history browsing overlay (`absolute inset-0...`) will be replaced with a Bootstrap overlay. A `div` with class `position-absolute top-0 start-0 w-100 h-100` will be used, with `bg-dark bg-opacity-50` for the background.

### `src/components/ConsoleCommandInput.tsx` ✅

*   **Analysis:** Uses a form with an input and a button, styled with custom constants `INPUT_CLASS` and `BUTTON_CLASS`.
*   **Plan:**
    1.  Replace the `form`'s `flex gap-2.5` with `d-flex gap-2`.
    2.  The `input` will get the `form-control` class.
    3.  The `button` will get `btn btn-primary` classes.
    4.  The `INPUT_CLASS` and `BUTTON_CLASS` constants will be removed from `src/constants/chess.ts`.

### `src/components/EvalBar.tsx` ✅

*   **Analysis:** Complex component with vertical and horizontal modes, using flexbox, absolute positioning, and custom styling for the evaluation bar.
*   **Plan:**
    1.  The main container will use Bootstrap's `d-flex` and `flex-column` for the vertical layout.
    2.  The bar itself will be a `div` with `position-relative`.
    3.  The fill will be a `div` with `position-absolute` and its height/width will be set via inline styles as before.
    4.  The probability regions will also be `div`s with `position-absolute`.
    5.  Colors will be mapped to Bootstrap theme colors (e.g., `bg-black` to `bg-dark`, `bg-white` to `bg-light`).

### `src/components/GameControls.tsx` ✅

*   **Analysis:** Uses flexbox, custom input/button classes, and DaisyUI `alert` and `checkbox`.
*   **Plan:**
    1.  The main container will use Bootstrap's stacking utilities (`vstack` or `hstack`).
    2.  The FEN input area will use a Bootstrap `InputGroup`.
    3.  The `alert-error` will be replaced with `alert alert-danger`.
    4.  The buttons will use `btn btn-primary`.
    5.  The sound setting checkbox will be replaced with a Bootstrap `Form.Check` component from `react-bootstrap`.

### `src/components/GameStatus.tsx` ✅

*   **Analysis:** Uses flexbox and DaisyUI `alert`.
*   **Plan:**
    1.  The main container will use `d-flex flex-column justify-content-center align-items-center gap-2`.
    2.  The `alert` classes will be mapped to Bootstrap: `alert-error` to `alert alert-danger`, and `alert-warning` to `alert alert-warning`.

### `src/components/MoveHistory.tsx` ✅

*   **Analysis:** A simple container for `MoveList`.
*   **Plan:**
    1.  Replace `card bg-base-200 shadow-sm h-full overflow-y-auto` with `card shadow-sm h-100` and an inner div with `overflow-auto`.

### `src/components/MoveList.tsx` ✅

*   **Analysis:** Complex component with two different layouts (desktop/mobile) and a lot of custom styling.
*   **Plan:**
    1.  **Mobile:** The horizontal scrolling container will use `d-flex align-items-center gap-2 p-2`.
    2.  **Desktop:** The vertical list will use Bootstrap's `ListGroup` component. Each move pair will be a `ListGroup.Item`.
    3.  The active move will have the `active` class.
    4.  The start position button will be a `ListGroup.Item` with `action` prop.

### `src/components/NewGameDialog.tsx` ✅

*   **Analysis:** Uses DaisyUI `modal` and form components (`range`, `input`, `btn`).
*   **Plan:**
    1.  This will be entirely refactored to use the `Modal` component from `react-bootstrap`.
    2.  The rating slider will be a `Form.Range`.
    3.  The time control and color selections will use `ButtonGroup` and `Button` components.

### `src/components/PlayerInfoPanel.tsx` ✅

*   **Analysis:** Uses flexbox and DaisyUI `avatar`.
*   **Plan:**
    1.  The main container will use `d-flex align-items-center justify-content-between`.
    2.  The avatar will be replaced with a simple `div` with `rounded-circle`.
    3.  Time warning classes will be mapped to Bootstrap's `text-danger` and `text-warning`.

### `src/components/ThemeSwitcher.tsx` ✅

*   **Analysis:** Uses DaisyUI `dropdown`.
*   **Plan:**
    1.  This will be refactored to use the `Dropdown` component from `react-bootstrap`.
    2.  The theme options will be `Dropdown.Item` components.

## Step 5: Verification

1.  **Run the Development Server:**
    Start the application to visually inspect the changes:

    ```bash
    npm run dev
    ```

2.  **Check for Errors:**
    Open the browser's developer console and check for any errors related to missing styles or incorrect class names.

3.  **Visual Review:**
    Thoroughly review every component to ensure that the Bootstrap styles have been applied correctly and that the UI appears as expected.

4.  **Run Tests:**
    Execute the existing test suite to catch any regressions.

    ```bash
    npm test
    ```
