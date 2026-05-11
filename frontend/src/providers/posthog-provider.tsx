'use client'

import posthog from 'posthog-js'
import { PostHogProvider } from 'posthog-js/react'
import { useAuth, useUser } from '@clerk/nextjs'
import { useEffect } from 'react'

const posthogHost = process.env.NEXT_PUBLIC_POSTHOG_HOST || 'https://us.i.posthog.com'
const posthogUiHost = process.env.NEXT_PUBLIC_POSTHOG_UI_HOST || 'https://us.posthog.com'

// Initialize PostHog
if (typeof window !== 'undefined' && process.env.NEXT_PUBLIC_POSTHOG_KEY) {
  posthog.init(process.env.NEXT_PUBLIC_POSTHOG_KEY, {
    api_host: posthogHost,
    ui_host: posthogUiHost,
    person_profiles: 'identified_only',
    capture_pageview: false, // Let Next.js handle pageviews
    capture_pageleave: true,
    cross_subdomain_cookie: true,
    persistence: 'localStorage+cookie',
    autocapture: {
      dom_event_allowlist: ['submit', 'click', 'change'], // Limit autocapture events
      element_allowlist: ['button', 'input', 'select', 'textarea', 'a'],
      css_selector_allowlist: ['[data-attr]', '[data-ph-capture]'],
    },
  })
}

function PostHogIdentitySync() {
  const { isLoaded, isSignedIn, userId } = useAuth()
  const { user } = useUser()

  useEffect(() => {
    if (!isLoaded || !process.env.NEXT_PUBLIC_POSTHOG_KEY) return

    if (isSignedIn && userId && user) {
      // Get the current anonymous distinct ID before identifying
      const anonDistinctId = posthog.get_distinct_id()
      
      // Identify the user
      posthog.identify(userId, {
        email: user.primaryEmailAddress?.emailAddress,
        name: user.fullName,
        username: user.username,
        created_at: user.createdAt,
      })

      // Alias the anonymous user with the identified user
      // This links pre-login activity to the user
      if (anonDistinctId && anonDistinctId !== userId) {
        posthog.alias(userId, anonDistinctId)
      }

      // Track sign in event
      posthog.capture('user signed in', {
        method: 'clerk',
      })
    } else if (!isSignedIn && isLoaded) {
      // User signed out - reset PostHog to remove user identity
      posthog.reset()
    }
  }, [isLoaded, isSignedIn, userId, user])

  return null
}

export function PHProvider({
  children,
}: {
  children: React.ReactNode
}) {
  const authDisabled = process.env.NEXT_PUBLIC_DISABLE_AUTH === 'true'

  return (
    <PostHogProvider client={posthog}>
      {!authDisabled && <PostHogIdentitySync />}
      {children}
    </PostHogProvider>
  )
}
