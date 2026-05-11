# ChessMimic

ChessMimic is the engine behind **[1e4.ai](https://1e4.ai)**, a chess web app
for playing against neural-network models that mimic human move choices, clock
usage, and game outcome tendencies across rating ranges. The repository
includes the Next.js frontend, FastAPI backend, trained model artifacts, C++
data-processing extension, training code, and tests used by the application.

> **Try the live demo at [1e4.ai](https://1e4.ai)** — no install required.

This repository is published at
[`thomasj02/1e4_ai`](https://github.com/thomasj02/1e4_ai); the GitHub repo
takes its name from the live site, while the codebase still refers to the
project internally as "ChessMimic". It is a public non-commercial release,
source-available under the PolyForm Noncommercial License 1.0.0. Issues, 
pull requests, and outside contributions are not accepted for this repository.

Built by [Thomas Johnson (@ThomasJ02)](https://x.com/ThomasJ02) — follow on X
for project updates.

## Repository Layout

- `frontend/` - Next.js app for the playable chess interface.
- `backend/` - FastAPI API, model loading, authentication, rating persistence,
  and inference endpoints.
- `backend/models/` - trained move, clock, and winner model artifacts.
- `Training/` - model training code, C++/nanobind extension, data converters,
  and C++ tests.
- `experiments/` - local experiment and benchmark scripts.

## License And Artifact Terms

The source code, trained model checkpoints, common-move databases, and included
sample artifacts are licensed under the PolyForm Noncommercial License 1.0.0
unless a file explicitly says otherwise. See `LICENSE` and `NOTICE`.

The non-commercial restriction applies to the included trained artifacts as well
as the source code.

## Git LFS

The model checkpoints and several common-move databases are stored with Git LFS.
Install and enable LFS before cloning or checking out the repository:

```bash
git lfs install
git clone <repository-url>
cd chessmimic
git lfs pull
```

Expect a multi-GB checkout when the model artifacts are present.

## Prerequisites

- Python 3.12
- Node.js 20 or newer
- Git LFS
- CMake 3.15 or newer
- A C++23-capable compiler for the training extension
- System libraries used by the C++ pipeline: zstd, lz4, xxhash, fmt, zlib, and
  optionally jemalloc
- `uv` for Python package management

The project expects a local virtual environment at `.venv/`. Activate it before
running Python code:

```bash
source .venv/bin/activate
```

For a fresh public checkout, create and populate a venv with the backend and
training requirements:

```bash
python -m venv .venv
source .venv/bin/activate
uv pip install -r backend/requirements.txt
uv pip install -r Training/requirements.txt
```

## Backend

Create `backend/.env` from `backend/.env.example`, then start the API:

```bash
source .venv/bin/activate
cd backend
python -m uvicorn main:app --reload
```

Useful environment variables:

- `CHESSMIMIC_MODELS_PATH` - path to the model directory, default `models`
  when running from `backend/`.
- `CHESSMIMIC_FORCE_CPU=true` - force CPU inference. Set to `false` to allow
  CUDA if available.
- `DISABLE_AUTH=true` - disable Clerk auth for local testing.
- `BACKEND_CORS_ORIGINS=http://localhost:3000,http://127.0.0.1:3000` -
  comma-separated allowed frontend origins.
- `SENTRY_DSN` - optional backend Sentry DSN. Sentry is disabled when unset.

If authentication is enabled, configure Clerk with `CLERK_ISSUER_URL` or
`NEXT_PUBLIC_CLERK_PUBLISHABLE_KEY`. Rating persistence requires Supabase
configuration.

## Frontend

Create `frontend/.env.local` from `frontend/.env.local.example`, then run:

```bash
cd frontend
npm install
npm run dev
```

The app defaults to `http://localhost:8000` for the backend API. Override it
with `NEXT_PUBLIC_API_URL`.

## Training And C++ Extension

The training tree contains the C++ data converters, nanobind extension, and
Python training scripts. Full private training datasets are not included in this
public release. Tiny/sample data used for smoke tests may be included.

Build and test the C++/training code:

```bash
cd Training
./cmake_configure.sh --ninja
cd build
ninja chessmimic_core
```

To build and run the training test suite:

```bash
cd Training
./build_and_run_tests.sh
```

## Local Verification

Run the main checks before publishing a release:

```bash
source .venv/bin/activate && cd backend && python -m pytest
cd frontend && npm test && npm run build
cd Training && ./build_and_run_tests.sh
```

Also review `RELEASE_CHECKLIST.md` before making a public branch or release.

## Acknowledgments

ChessMimic is inspired by prior research on chess as a testbed for human and
machine play:

- **Maia-2: A Unified Model for Human-AI Alignment in Chess** - Tang, Jiao,
  McIlroy-Young, Kleinberg, Sen, Anderson (2024).
  [arXiv:2409.20553](https://arxiv.org/abs/2409.20553). The framing of
  modeling human move choices across rating ranges, and the comparison
  baselines used in `experiments/maia2_benchmark/`, follow this line of work.
- **Amortized Planning with Large-Scale Transformers: A Case Study on Chess**
  - Ruoss, Delétang, Medapati, Grau-Moya, Wenliang, Catt, Reid, Lewis,
  Veness, Genewein (2024). [arXiv:2402.04494](https://arxiv.org/abs/2402.04494).
  The transformer-only approach to chess move prediction informed the
  ChessMimic model design. `Training/bagz.py` and `Training/tokenizer.py` are
  derived from the accompanying
  [google-deepmind/searchless_chess](https://github.com/google-deepmind/searchless_chess)
  release (Apache 2.0); see `THIRD_PARTY_LICENSES.md`.

These references credit inspiration only; the cited authors and projects are
not affiliated with ChessMimic and do not endorse it.

## Support

This repository is published for use and inspection, but issues, pull requests,
and contribution workflows are not accepted. See `SUPPORT.md`.
