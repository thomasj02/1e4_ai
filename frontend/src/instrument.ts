import * as Sentry from "@sentry/nextjs";

const sentryDsn = process.env.NEXT_PUBLIC_SENTRY_DSN;
const sentryDisabled = process.env.NEXT_PUBLIC_DISABLE_SENTRY === 'true' || !sentryDsn;

const parseRate = (value: string | undefined, fallback = 0): number => {
  if (!value) return fallback;
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : fallback;
};

if (sentryDisabled) {
  if (process.env.NODE_ENV !== 'production') {
    console.log('Sentry disabled');
  }
} else {
  Sentry.init({
    dsn: sentryDsn as string,
    sendDefaultPii: process.env.NEXT_PUBLIC_SENTRY_SEND_DEFAULT_PII === 'true',

    // Performance monitoring
    tracesSampleRate: parseRate(process.env.NEXT_PUBLIC_SENTRY_TRACES_SAMPLE_RATE, 0),

    // Session replay
    replaysSessionSampleRate: parseRate(process.env.NEXT_PUBLIC_SENTRY_REPLAYS_SESSION_SAMPLE_RATE, 0),
    replaysOnErrorSampleRate: parseRate(process.env.NEXT_PUBLIC_SENTRY_REPLAYS_ON_ERROR_SAMPLE_RATE, 0),

    // Enable experiments
    _experiments: {
      enableLogs: true,
    },

    integrations: [
      // Browser tracing for performance monitoring
      Sentry.browserTracingIntegration(),

      // Session replay
      Sentry.replayIntegration({
        maskAllText: true,
        blockAllMedia: true,
      }),

      // Console logging integration
      Sentry.consoleLoggingIntegration({
        levels: ["error", "warn"],
      }),
    ],

    // Environment configuration
    environment: process.env.NODE_ENV || 'development',

    // Filter out certain errors
    beforeSend(event) {
      // Filter out errors from browser extensions
      if (event.exception?.values?.[0]?.stacktrace?.frames?.some(frame =>
        frame.filename?.includes('chrome-extension://') ||
        frame.filename?.includes('moz-extension://')
      )) {
        return null;
      }

      return event;
    },
  });
}

// Export typed Sentry for use in other files
export { Sentry };
