'use client';

import dynamic from 'next/dynamic';

// Dynamically import the ChessGame component with SSR disabled
// This prevents "window is not defined" errors during prerendering
const ChessGame = dynamic(
  () => import('./ChessGame'),
  { 
    ssr: false,
    loading: () => (
      <div className="p-5 d-flex justify-content-center">
        <div className="text-center">
          <h1 className="h1 mb-4">Chessmimic MVP</h1>
          <p className="text-muted">Loading chess game...</p>
        </div>
      </div>
    )
  }
);

export default function Home() {
  return (
    <main className="flex-grow-1 d-flex w-100 h-100 overflow-hidden">
      <ChessGame />
    </main>
  );
}