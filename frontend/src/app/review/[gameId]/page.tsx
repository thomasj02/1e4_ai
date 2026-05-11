'use client';

import { useParams } from 'next/navigation';
import { SignedIn, SignedOut, RedirectToSignIn } from '@clerk/nextjs';
import GameReview from '@/components/GameReview';

function ReviewContent() {
  const params = useParams();
  const gameId = params.gameId as string;

  return <GameReview gameId={gameId} />;
}

export default function ReviewPage() {
  return (
    <>
      <SignedIn>
        <ReviewContent />
      </SignedIn>
      <SignedOut>
        <RedirectToSignIn />
      </SignedOut>
    </>
  );
}
