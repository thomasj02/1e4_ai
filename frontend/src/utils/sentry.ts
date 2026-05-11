import * as Sentry from '@sentry/nextjs';

// Re-export commonly used Sentry functionality with proper types
export { Sentry };

// Typed logger instance
export const logger = Sentry.logger;

// Re-export startSpan directly from Sentry
export const startSpan = Sentry.startSpan;

// Re-export captureException directly from Sentry
export const captureException = Sentry.captureException;

// Typed error boundary props
export interface ErrorBoundaryProps {
  fallback?: React.ComponentType<{
    error: Error;
    resetError: () => void;
  }>;
  showDialog?: boolean;
  dialogOptions?: {
    title?: string;
    subtitle?: string;
    subtitle2?: string;
  };
  onError?: (error: Error, errorInfo: React.ErrorInfo) => void;
  onReset?: () => void;
  children: React.ReactNode;
}

// Re-export the error boundary component
export const ErrorBoundary = Sentry.ErrorBoundary;

// Re-export withErrorBoundary HOC with proper types
export const withErrorBoundary = Sentry.withErrorBoundary;