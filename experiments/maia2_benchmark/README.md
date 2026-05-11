# Maia2 vs ChessMimic — Blitz Benchmark

Head-to-head accuracy of [Maia2](https://github.com/CSSLab/maia2) and ChessMimic
on Lichess Rated Blitz games. Reports:

- **Move prediction:** top-1 / top-3 / top-5 accuracy of the actually played
  move, stratified by player ELO band.
- **Win prediction:** Brier score, log-loss, and AUC of "side-to-move ultimately
  wins," with draws counted as 0.5 (excluded from AUC).

## Setup

```bash
source <repo-root>/.venv/bin/activate
uv pip install maia2 zstandard
```

(`maia2` pulls in `pyzstd`, `gdown`, `einops` automatically.)

The first run downloads the Maia2 blitz checkpoint (~280 MB) into
`<repo-root>/maia2_models/blitz_model.pt`.

## Running the benchmark

```bash
# 1. Build the eval dataset from a Lichess monthly dump (.pgn or .pgn.zst).
#    Streams the file, filters to Rated Blitz, stratifies by ELO band.
#    Lichess monthly dumps are at https://database.lichess.org/.
python experiments/maia2_benchmark/build_dataset.py \
    --input path/to/lichess_db_standard_rated_YYYY-MM.pgn.zst \
    --output experiments/maia2_benchmark/results/eval.jsonl \
    --per-band 1000

# 2. Run both models. Maia2 once over the full dataset, ChessMimic per ELO band.
CHESSMIMIC_FORCE_CPU=false python experiments/maia2_benchmark/run_benchmark.py \
    --input experiments/maia2_benchmark/results/eval.jsonl \
    --output-dir experiments/maia2_benchmark/results/run \
    --batch-size 256
```

`--batch-size` is the same for both models. Reduce if you run into GPU OOM
(blitz model is ~280 MB; chessmimic per-band ~150 MB; both small enough to fit
comfortably alongside batch-size 256 on a 12+ GB card).

## Output

In `<output-dir>/`:

- `summary.md` — human-readable comparison tables (overall + per-band) and
  win-prob calibration deciles for both models.
- `summary.json` — same, machine-readable.
- `per_position.csv` — one row per evaluated position with both models'
  top-1/3/5 hit flags and win probabilities.

## Verification

Before trusting the held-out numbers, run:

```bash
# Confirm our batched Maia2 path matches the upstream inference_each
# (top-1 move + win_prob on 5 fixed FENs, must agree to ~1e-4)
python experiments/maia2_benchmark/parity_check.py
```

## Implementation notes

- `build_dataset.py` skips the first 8 plies of each game (book theory) and
  drops games shorter than 20 plies. Both are configurable via `--skip-plies`
  and `--min-total-plies`.
- Move models are selected by the side-to-move's ELO band; winner models are
  selected by `(white_elo + black_elo) // 2`, matching `backend/main.py` and
  the winner-converter training pipeline. Each record's actual `elo_self`
  is fed to the move model as the rating input (matching production
  `predict_move` and the per-record `elo_self` Maia2 receives).
- For Maia2, our batched code calls `maia2.inference.preprocessing` per
  position to handle the black-to-move board mirroring and the legal-move
  mask, then stacks tensors and runs a single forward pass per batch. This
  matches `inference.inference_each` exactly (verified by `parity_check.py`).
- For ChessMimic, we read pre-tokenized inputs from
  `chessmimic_core.recent_moves_and_fen_to_inputs` and run batched forwards
  through the existing `ModelMovePredictor.model` and
  `WinnerPredictor.model` directly. No model surgery.
- The 3-class winner output (black/draw/white) is collapsed to the side-to-
  move's expected score `P(side_wins) + 0.5 · P(draw)` for comparison with
  Maia2's scalar `win_prob` (Maia2's value head is similarly mirrored for
  black-to-move positions). Dropping the draw component would bias the
  Brier/log-loss scores against any model that places real mass on draws.
- `run_benchmark.py` processes one chessmimic checkpoint at a time so the
  per-band model files are never simultaneously resident in GPU memory.
  Move and winner inference are run as two separate per-band passes (move
  by side-to-move band, winner by avg-rating band).

## Known caveats

- Maia2 doesn't take clock or move history as input; we pass them only to
  ChessMimic. This is structural to the models, not an unfairness.
- Win-probability calibration is sample-size sensitive — the decile table is
  noisy below ~5k positions per model.
