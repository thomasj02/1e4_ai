# Release Checklist

Use this local checklist before making a public branch, tag, or repository
visibility change.

## Repository State

- Confirm `git status --short` contains only intentional release changes.
- Confirm no `.env`, `.env.local`, local MCP, local Claude, IDE, cache, build,
  W&B, tensorboard, Playwright, or log files are tracked.
- Confirm no GitHub Actions workflows, issue templates, pull request templates,
  or contribution docs were added.
- Confirm `LICENSE`, `NOTICE`, `SUPPORT.md`, and `README.md` accurately describe
  the release as source-available and non-commercial.

## Artifacts

- Run `git lfs install`.
- Run `git lfs ls-files` and confirm model checkpoints and intended
  common-move artifacts are listed.
- Review large tracked files with:

```bash
git ls-files -s | awk '{print $4}' | xargs -r du -h 2>/dev/null | sort -h | tail -80
```

## Secrets And Service Defaults

- Scan tracked source and docs for tokens, private keys, and production service
  URLs before publishing.
- Confirm Sentry, PostHog, Clerk, Supabase, and CORS settings are configured by
  environment variables and are safe when unset.
- Confirm `SENTRY_SEND_DEFAULT_PII` and frontend Sentry PII settings default to
  disabled.

## Verification

```bash
source .venv/bin/activate && cd backend && python -m pytest
cd frontend && npm test && npm run build
cd Training && ./build_and_run_tests.sh
```

For the final check, do a fresh clone with Git LFS enabled and run a backend
health check plus a frontend production build.
