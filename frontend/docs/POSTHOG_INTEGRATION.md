# PostHog Integration Guide

This document explains how PostHog is integrated into the ChessMimic frontend for analytics and user tracking.

## Overview

PostHog is integrated with full identity management, allowing us to:
- Track anonymous users before they sign in
- Identify users when they authenticate via Clerk
- Link pre-login activity to authenticated users
- Track user properties and custom events
- Reset tracking when users sign out

## Setup

### 1. Environment Variables

Add the following to your `.env.local` file:

```env
NEXT_PUBLIC_POSTHOG_KEY=your_posthog_project_api_key
NEXT_PUBLIC_POSTHOG_HOST=https://us.i.posthog.com
NEXT_PUBLIC_POSTHOG_UI_HOST=https://us.posthog.com
```

### 2. Architecture

The integration consists of:

- **PostHog Provider (`/src/providers/posthog-provider.tsx`)**: Handles initialization and identity management
- **Layout Integration**: Provider is wrapped around the entire app in `layout.tsx`
- **Clerk Integration**: Automatic user identification when users sign in/out

## Identity Management

### User Identification Flow

1. **Anonymous Users**: PostHog automatically assigns a distinct ID to new visitors
2. **User Sign In**: When a user signs in via Clerk:
   - PostHog identifies the user with their Clerk user ID
   - User properties (email, name, etc.) are set
   - Anonymous ID is aliased to the user ID (linking pre-login activity)
   - A "user signed in" event is tracked
3. **User Sign Out**: PostHog is reset to clear user identity

### Key Features

- **Persistence**: Uses localStorage + cookies for cross-subdomain tracking
- **Autocapture**: Limited to specific events and elements for performance
- **Person Profiles**: Only created for identified users to save costs
- **Page Views**: Handled by Next.js router integration (not duplicated)

## Usage in Components

### Using PostHog Hooks

```tsx
import { usePostHog } from 'posthog-js/react'

function MyComponent() {
  const posthog = usePostHog()
  
  const handleClick = () => {
    posthog.capture('button_clicked', {
      button_name: 'start_game',
      game_mode: 'vs_ai'
    })
  }
  
  return <button onClick={handleClick}>Start Game</button>
}
```

### Feature Flags

```tsx
import { useFeatureFlag } from 'posthog-js/react'

function GameComponent() {
  const showNewFeature = useFeatureFlag('new-chess-feature')
  
  return showNewFeature ? <NewFeature /> : <OldFeature />
}
```

## Best Practices

1. **Event Naming**: Use descriptive, consistent event names (e.g., "game_started", "move_made")
2. **Properties**: Include relevant context with events but avoid PII
3. **Performance**: Limit autocapture to essential elements
4. **Privacy**: PostHog respects user privacy settings and DNT headers

## Testing

To test the integration:

1. Set up PostHog environment variables
2. Run the app and check the browser console for PostHog initialization
3. Sign in with Clerk and verify user identification in PostHog dashboard
4. Check that events are being tracked properly
5. Sign out and verify PostHog reset

## Troubleshooting

- **No events appearing**: Check environment variables are set correctly
- **User not identified**: Ensure Clerk is properly configured and user is signed in
- **Duplicate events**: Check that PostHog isn't initialized multiple times
