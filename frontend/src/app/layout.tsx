import type { Metadata } from "next";
import { Geist, Geist_Mono } from "next/font/google";
import Link from "next/link";
import {
  ClerkProvider,
  SignInButton,
  SignUpButton,
  SignedIn,
  SignedOut,
} from "@clerk/nextjs";
import CustomUserButton from "../components/CustomUserButton";
import 'bootstrap/dist/css/bootstrap.min.css';
import 'bootstrap-icons/font/bootstrap-icons.css';
import "../index.css";
import "../app.css";
import "./globals.css";
import AppIntegrations from "../components/AppIntegrations";
import ThemeScript from "./theme-script";
import ViewportHeightFix from "./viewport-height";
import { PHProvider } from "../providers/posthog-provider";

const geistSans = Geist({
  variable: "--font-geist-sans",
  subsets: ["latin"],
});

const geistMono = Geist_Mono({
  variable: "--font-geist-mono",
  subsets: ["latin"],
});

export const metadata: Metadata = {
  title: "ChessMimic",
  description: "Play chess against a neural network that mimics human play",
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  // Skip Clerk authentication in e2e tests
  if (process.env.NEXT_PUBLIC_DISABLE_AUTH === 'true') {
    return (
      <html lang="en" data-theme="dark" data-bs-theme="dark">
        <head>
          <ThemeScript />
        </head>
        <body
          className={`${geistSans.variable} ${geistMono.variable} d-flex flex-column bg-dark text-light overflow-hidden`}
        >
          <PHProvider>
            <AppIntegrations />
            <ViewportHeightFix />
            <header className="navbar navbar-expand-lg navbar-dark bg-dark shadow w-100 d-none d-md-flex flex-shrink-0" style={{ zIndex: 50 }}>
              <div className="container-fluid px-4 d-flex justify-content-between">
                <div className="flex-fill">
                  <Link href="/" className="btn btn-outline-light fs-5">ChessMimic</Link>
                </div>
                <div className="d-flex gap-2">
                  <button className="btn btn-primary">Sign In</button>
                  <button className="btn btn-outline-secondary">Sign Up</button>
                </div>
              </div>
            </header>
            <div className="flex-grow-1 d-flex w-100 overflow-hidden main-content-wrapper">
              {children}
            </div>
          </PHProvider>
        </body>
      </html>
    );
  }

  return (
    <ClerkProvider>
      <html lang="en" data-theme="dark" data-bs-theme="dark">
        <head>
          <ThemeScript />
        </head>
        <body
          className={`${geistSans.variable} ${geistMono.variable} d-flex flex-column bg-dark text-light overflow-hidden`}
        >
          <PHProvider>
            <AppIntegrations />
            <ViewportHeightFix />
            <header className="navbar navbar-expand-lg navbar-dark bg-dark shadow w-100 d-none d-md-flex flex-shrink-0" style={{ zIndex: 50 }}>
              <div className="container-fluid px-4 d-flex justify-content-between">
                <div className="flex-fill">
                  <Link href="/" className="btn btn-outline-light fs-5">ChessMimic</Link>
                </div>
                <div className="d-flex gap-2">
                  <SignedOut>
                    <SignInButton mode="modal">
                      <button className="btn btn-primary">Sign In</button>
                    </SignInButton>
                    <SignUpButton mode="modal">
                      <button className="btn btn-outline-secondary">Sign Up</button>
                    </SignUpButton>
                  </SignedOut>
                  <SignedIn>
                    <CustomUserButton />
                  </SignedIn>
                </div>
              </div>
            </header>
            <div className="flex-grow-1 d-flex w-100 overflow-hidden main-content-wrapper">
              {children}
            </div>
          </PHProvider>
        </body>
      </html>
    </ClerkProvider>
  );
}
