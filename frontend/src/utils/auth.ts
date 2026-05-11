import { useAuth as useClerkAuth } from '@clerk/nextjs';

/**
 * Mock auth hook for when authentication is disabled
 */
function getMockAuth() {
  return {
    getToken: async () => null,
    isLoaded: true,
    isSignedIn: false,
    userId: null,
    sessionId: null,
    orgId: null,
    orgRole: null,
    orgSlug: null,
  };
}

/**
 * Conditional auth hook that returns mock auth when disabled
 */
export function useAuth() {
  if (process.env.NEXT_PUBLIC_DISABLE_AUTH === 'true') {
    return getMockAuth();
  }
  // Auth-disabled builds do not mount ClerkProvider, so this hook must only run
  // when authentication is enabled.
  // eslint-disable-next-line react-hooks/rules-of-hooks
  return useClerkAuth();
}

/**
 * Hook to get authenticated fetch options
 * Returns headers with Authorization token if user is authenticated
 */
export function useAuthHeaders() {
  const { getToken } = useAuth();

  const getAuthHeaders = async (): Promise<HeadersInit> => {
    // Check if auth is disabled for e2e tests
    if (process.env.NEXT_PUBLIC_DISABLE_AUTH === 'true') {
      return {
        'Content-Type': 'application/json',
      };
    }

    try {
      const token = await getToken();
      if (token) {
        return {
          'Content-Type': 'application/json',
          'Authorization': `Bearer ${token}`,
        };
      }
    } catch (error) {
      console.error('Failed to get auth token:', error);
    }

    // Return basic headers if no token
    return {
      'Content-Type': 'application/json',
    };
  };

  return { getAuthHeaders };
}

/**
 * Wrapper for authenticated fetch requests
 * Automatically adds authorization header if user is authenticated
 */
export async function authenticatedFetch(
  url: string,
  options: RequestInit,
  getToken: () => Promise<string | null>
): Promise<Response> {
  // Check if auth is disabled for e2e tests
  if (process.env.NEXT_PUBLIC_DISABLE_AUTH === 'true') {
    return fetch(url, options);
  }

  try {
    const token = await getToken();
    if (token) {
      options.headers = {
        ...options.headers,
        'Authorization': `Bearer ${token}`,
      };
    }
  } catch (error) {
    console.error('Failed to get auth token:', error);
  }

  return fetch(url, options);
}
