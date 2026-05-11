# Migration Plan: React (Vite) to Next.js (AI Agent Edition v2)

**Objective:** Migrate the existing React+TypeScript application in `frontend/` to a new, modern Next.js application in `frontend-next/`.

**Guiding Principle:** This migration will follow a **test-driven development (TDD)** methodology. Each step is designed to be a discrete, verifiable action for an AI agent. The migration will proceed by first establishing a working test environment, then migrating tests and code component-by-component, and finally assembling the application while verifying with end-to-end tests.

---

### **Phase 0: Application Analysis**

Programmatically analyze the existing application to gather all necessary information for the migration.

1.  **Analyze Dependencies:**
    *   **Action:** Read `frontend/package.json`.
    *   **Goal:** Extract two lists: `dependencies` and `devDependencies`. These will be installed in the new project.

2.  **Analyze Configuration:**
    *   **Action:** Read `frontend/vite.config.js`.
    *   **Goal:** Extract the key-value pairs of path aliases from the `resolve.alias` configuration. These will be replicated in `tsconfig.json`.

3.  **Analyze Component and Test Files:**
    *   **Action:** List the files in `frontend/src/` and `frontend/src/components/`.
    *   **Goal:** Identify the exact filenames and extensions (`.tsx`, `.test.tsx`) of all components and tests to be migrated.

---

### **Phase 1: Project Setup & Test Framework Configuration**

Set up the project structure and fully configure the testing frameworks before any application code is migrated.

1.  **Create the Next.js App:**
    *   **Action:** Run the following shell command from the project root (`<repo-root>`).
    ```bash
    npx create-next-app@latest frontend-next --ts --tailwind --eslint --app --src-dir --import-alias "@/*" --use-npm
    ```
    *   **Verification:** Run `ls frontend-next` to confirm the directory was created.

2.  **Navigate into the New Project:**
    *   **Action:** The working directory for all subsequent steps is `<repo-root>/frontend-next`.

3.  **Install Dependencies:**
    *   **Action:** Based on the lists from Phase 0, install the required packages.
    ```bash
    # Application Dependencies
    npm install axios chess.js react-chessboard @sentry/react posthog-js

    # Development Dependencies for Testing
    npm install --save-dev jest jest-environment-jsdom @testing-library/react @testing-library/jest-dom @types/jest @playwright/test
    ```

4.  **Configure Path Aliases:**
    *   **Action:** Modify `frontend-next/tsconfig.json`. Add the aliases extracted from `vite.config.js` to the `compilerOptions.paths` object.
    ```json
    {
      "compilerOptions": {
        // ... other options
        "paths": {
          "@/*": ["./src/*"],
          "@components/*": ["./src/components/*"],
          "@hooks/*": ["./src/hooks/*"],
          "@utils/*": ["./src/utils/*"],
          "@constants/*": ["./src/constants/*"],
          "@types/*": ["./src/types/*"]
        }
      },
      // ... other config
    }
    ```

5.  **Configure Jest (Unit Tests):**
    *   **Action:** Create `jest.config.mjs` in the `frontend-next` root with the following content.
    ```javascript
    import nextJest from 'next/jest.js'
 
    const createJestConfig = nextJest({
      dir: './',
    })
     
    /** @type {import('jest').Config} */
    const config = {
      setupFilesAfterEnv: ['<rootDir>/jest.setup.js'],
      testEnvironment: 'jest-environment-jsdom',
      moduleNameMapper: {
        '^@/components/(.*)$': '<rootDir>/src/components/$1',
        '^@/hooks/(.*)$': '<rootDir>/src/hooks/$1',
      }
    }
     
    export default createJestConfig(config)
    ```
    *   **Action:** Create `jest.setup.js` in the `frontend-next` root.
    ```javascript
    import '@testing-library/jest-dom'
    ```
    *   **Verification:** Run `npm test`. Expect it to pass with a "No tests found" message.

6.  **Configure Playwright (E2E Tests):**
    *   **Action:** Run `npx playwright install`.
    *   **Action:** Copy `frontend/e2e` to `frontend-next/e2e`.
    *   **Action:** Copy `frontend/playwright.config.js` to `frontend-next/playwright.config.js`.
    *   **Action:** Modify `frontend-next/playwright.config.js`, updating the `webServer` object:
    ```javascript
    webServer: {
      command: 'npm run dev',
      url: 'http://localhost:3000',
      reuseExistingServer: !process.env.CI,
    },
    ```
    *   **Verification:** Run `npm run test:e2e`. Expect all tests to fail.

---

### **Phase 2: Test-Driven Component Migration**

Migrate each component using its corresponding test file.

**For each component identified in Phase 0 (e.g., `ChessClock.tsx`, `EvalBar.tsx`):**

1.  **Red (Failing Test):**
    *   Copy the component's test file (e.g., `frontend/src/components/ChessClock.test.tsx`) to `frontend-next/src/components/`.
    *   Run `npm test`. The test should fail, reporting the missing component file.

2.  **Green (Passing Test):**
    *   Copy the component's source file (e.g., `frontend/src/components/ChessClock.tsx`) to `frontend-next/src/components/`.
    *   Add `'use client';` to the top of the new component file.
    *   Update any relative import paths to use the new path aliases.
    *   Run `npm test` again. The test for this component should now pass.

---

### **Phase 3: E2E-Driven Application Assembly**

Incrementally build the main application page, using the E2E tests to verify progress.

1.  **Initial Setup:**
    *   **Action:** Overwrite `frontend-next/src/app/page.tsx` with a basic layout from `frontend/src/App.tsx`. This includes the main `div`, `h1`, and an empty `Chessboard` component. Add `'use client';` to the top.
    *   **Action:** Create `frontend-next/.env.local` with the content: `NEXT_PUBLIC_API_URL=http://localhost:8000`.
    *   **Action:** In the new `page.tsx`, change `import.meta.env.VITE_API_URL` to `process.env.NEXT_PUBLIC_API_URL`.

2.  **Incremental Build & Test:**
    *   **Cycle 1: State and Core Logic.** Copy all `useState`, `useEffect`, `useMemo`, and `useCallback` hooks from `frontend/src/App.tsx` into the `Home` component in `page.tsx`.
    *   **Verification:** Run `npm run test:e2e`.
    *   **Cycle 2: Components.** Import and add the migrated components (`EvalBar`, `MoveHistory`, etc.) into the JSX, passing the required props.
    *   **Verification:** Run `npm run test:e2e`.
    *   **Cycle 3: User Interaction.** Ensure all event handlers like `handlePlayerMove` and `startNewGame` are copied over and correctly connected.
    *   **Verification:** Run `npm run test:e2e` until all tests pass.

3.  **Migrate Third-Party Integrations:**
    *   Create `frontend-next/src/components/AppIntegrations.tsx` and place the Sentry/PostHog initialization logic there, wrapped in a client-side `useEffect` hook.
    *   Import and use `<AppIntegrations />` in the root `layout.tsx`.

---

### **Phase 4: Final Cleanup**

1.  **Review and Refactor:** Analyze the completed code for any final improvements.
2.  **WARNING: Destructive Action.** The following step will permanently delete the original frontend application.
    *   **Action:** Upon explicit user confirmation, run `rm -rf <repo-root>/frontend`.
