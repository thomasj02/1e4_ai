# ChessMimic Frontend

This directory contains the Next.js application for ChessMimic.

## Setup

```bash
cp .env.local.example .env.local
npm install
npm run dev
```

Open [http://localhost:3000](http://localhost:3000) with your browser to see the result.

By default the frontend calls the backend at `http://localhost:8000`. Override
that with `NEXT_PUBLIC_API_URL`.

For local development without Clerk, keep `NEXT_PUBLIC_DISABLE_AUTH=true` and
set `DISABLE_AUTH=true` in `backend/.env`.

## Checks

```bash
npm test
npm run build
```

See the root `README.md` for full repository setup, licensing, model artifact,
and support information.
