# Authentication Setup

ChessMimic uses Clerk for user authentication. This document explains how authentication is configured and how to work with it.

## Configuration

### Environment Variables

Authentication is configured through environment variables in `.env.local`:

```env
# Required: Your Clerk API keys
NEXT_PUBLIC_CLERK_PUBLISHABLE_KEY=pk_test_...
CLERK_SECRET_KEY=your_clerk_secret_key

# After authentication redirects
NEXT_PUBLIC_CLERK_AFTER_SIGN_IN_URL=/
NEXT_PUBLIC_CLERK_AFTER_SIGN_UP_URL=/
```

### Using Clerk's Hosted Pages

By default, ChessMimic uses Clerk's hosted Account Portal for sign-in and sign-up pages. This means:
- No custom authentication pages need to be maintained
- Users are redirected to `https://your-domain.accounts.dev/sign-in` for authentication
- After signing in, users are redirected back to the app

To use custom sign-in/sign-up pages instead, uncomment these lines in `.env.local`:
```env
NEXT_PUBLIC_CLERK_SIGN_IN_URL=/sign-in
NEXT_PUBLIC_CLERK_SIGN_UP_URL=/sign-up
```

## Protected Routes

The main game route (`/`) is protected and requires authentication. This is configured in `src/middleware.ts`.

## E2E Testing

During e2e tests, authentication is disabled to allow tests to run without requiring sign-in:

1. Set `NEXT_PUBLIC_DISABLE_AUTH=true` environment variable
2. The middleware bypasses Clerk authentication
3. Mock sign-in/sign-up buttons are shown in the UI

This is automatically configured in:
- `playwright.config.js` - for Playwright tests
- `package.json` scripts - `test:e2e` and `dev:e2e`

## Development

To test with authentication enabled:
```bash
npm run dev
```

To test with authentication disabled (like e2e tests):
```bash
NEXT_PUBLIC_DISABLE_AUTH=true npm run dev
```

The backend must also run with `DISABLE_AUTH=true` for protected API endpoints
to accept unauthenticated local requests.

## Troubleshooting

### "Page could not be found" error
If you see a 404 error when redirected to `/sign-in`, ensure that:
1. The `NEXT_PUBLIC_CLERK_SIGN_IN_URL` and `NEXT_PUBLIC_CLERK_SIGN_UP_URL` are commented out in `.env.local`
2. Or, if they are set, ensure you have created the corresponding pages in your app

### Authentication not working
1. Verify your Clerk API keys are correct in `.env.local`
2. Check that the middleware is not being bypassed (ensure `NEXT_PUBLIC_DISABLE_AUTH` is not set to `true`)
3. Restart the development server after changing environment variables
