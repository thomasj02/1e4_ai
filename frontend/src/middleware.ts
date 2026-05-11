import { clerkMiddleware, createRouteMatcher } from '@clerk/nextjs/server'
import { NextResponse } from 'next/server'

// Define protected routes (currently just the main game)
const isProtectedRoute = createRouteMatcher(['/'])

// Skip Clerk middleware in e2e tests
const middleware = process.env.NEXT_PUBLIC_DISABLE_AUTH === 'true' 
  ? () => NextResponse.next()
  : clerkMiddleware(async (auth, req) => {
      // Protect the main game route
      if (isProtectedRoute(req)) {
        await auth.protect()
      }
    })

export default middleware

export const config = {
  matcher: [
    // Skip Next.js internals and all static files, unless found in search params
    '/((?!_next|[^?]*\\.(?:html?|css|js(?!on)|jpe?g|webp|png|gif|svg|ttf|woff2?|ico|csv|docx?|xlsx?|zip|webmanifest)).*)',
    // Always run for API routes
    '/(api|trpc)(.*)',
  ],
}