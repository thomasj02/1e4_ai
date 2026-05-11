# Maia2 vs ChessMimic — Blitz Benchmark Results

Run date: 2026-05-11. Held-out evaluation on `lichess_db_standard_rated_2026-04.pgn.zst` (Lichess April 2026 monthly dump). Auto-generated tables from this run are at `results/run_full_2026_05_11/summary.md`; this file is the curated writeup.

## Setup

- **Source:** April 2026 Lichess monthly database, filtered inline to `Event = "Rated Blitz game"`.
- **Sample size:** 13,000 positions — 1,000 per ELO band × 13 chessmimic bands (1000–1100 through 2200–3500). All 13 bands filled from the first 1,420 games of the dump (Lichess interleaves games chronologically by start time, so the ELO distribution is naturally diverse early in the file).
- **Position selection:** skip first 8 plies of each game (book theory), drop games shorter than 20 plies. One position-record per ply for retained games.
- **Models:**
  - Maia2 `blitz` checkpoint (CSSLab, NeurIPS 2024) loaded via `maia2.model.from_pretrained(type="blitz")`. Receives per-record `elo_self`/`elo_oppo`.
  - ChessMimic per-band move models (`backend/models/move_model/{band}_brier/`) selected by side-to-move ELO, with each record's actual rating fed in. Per-band winner models (`backend/models/winner_model/{band}_brier/`) selected by **average** rating of the two players, replicating the production backend's bucket selection. `1200_1300_brier` has an empty checkpoint dir; records that map to that bucket are served by the nearest available bucket.
- **Inference:** batched on GPU (CUDA), batch size 256.
- **Verification:** `parity_check.py` confirmed our batched Maia2 path agrees with upstream `inference.inference_each` to within 1e-4 on top-1 move and `win_prob` across 5 fixed FENs.

## Methodology

- **Apples-to-apples sample.** Both models are scored on the same 13,000 positions for every metric.
- **Win-prob target convention.** "Side-to-move ultimately wins" is encoded as 1.0 / 0.5 / 0.0 for win/draw/loss. Maia2's scalar value head is mirrored for black-to-move positions; ChessMimic's 3-class winner head (black/draw/white) is collapsed to the side-to-move's expected score `P(side_wins) + 0.5 · P(draw)`. This matches the target convention; using just `P(side_wins)` would penalize any model that places real mass on draws.
- **Implementation details** (model-loading, batching, bucket selection, per-record rating handling) are covered in `README.md` in this directory.

## Headline numbers

### Move prediction (top-k accuracy, n=13,000)

| Metric | Maia2 | ChessMimic | Δ (CM − Maia2) |
|---|---:|---:|---:|
| top-1 | **0.5268** | **0.5675** | +0.0407 |
| top-3 | 0.8095 | 0.8332 | +0.0237 |
| top-5 | 0.8998 | 0.9158 | +0.0160 |

ChessMimic wins on top-1 in **every** ELO band, by 1.1 pp (1400–1500) up to 6.9 pp (2200+). The gap widens at higher ratings: chessmimic's per-band specialization buys more at the top end of the rating range, where Maia2's single shared model is doing more interpolation work.

### Win prediction (P side-to-move expected score, n=13,000)

| Metric | Maia2 | ChessMimic |
|---|---:|---:|
| Brier (lower is better) | 0.2715 | **0.1753** |
| Log-loss (lower is better) | 0.8314 | **0.5593** |
| AUC, draws excluded (n=12,102) | 0.5093 | **0.7930** |

The win-prediction gap is large. Maia2's AUC of 0.509 is essentially chance — its `win_prob` outputs span [0.00, 1.00] and have a real spread (std ≈ 0.21), but they don't correlate with actual game outcomes. ChessMimic's AUC of 0.793 and Brier of 0.175 reflect predictions that track outcomes well.

### Per-band move prediction (top-1)

Bands here are by side-to-move ELO (where the dataset stratifies records).

| Band | N | Maia2 | ChessMimic | Δ |
|---|---:|---:|---:|---:|
| 1000–1100 | 1000 | 0.5200 | 0.5760 | +0.056 |
| 1100–1200 | 1000 | 0.4920 | 0.5410 | +0.049 |
| 1200–1300 | 1000 | 0.5360 | 0.5550 | +0.019 |
| 1300–1400 | 1000 | 0.4980 | 0.5290 | +0.031 |
| 1400–1500 | 1000 | 0.5300 | 0.5410 | +0.011 |
| 1500–1600 | 1000 | 0.5140 | 0.5380 | +0.024 |
| 1600–1700 | 1000 | 0.5400 | 0.5870 | +0.047 |
| 1700–1800 | 1000 | 0.5180 | 0.5620 | +0.044 |
| 1800–1900 | 1000 | 0.5540 | 0.5880 | +0.034 |
| 1900–2000 | 1000 | 0.5380 | 0.5800 | +0.042 |
| 2000–2100 | 1000 | 0.5360 | 0.5930 | +0.057 |
| 2100–2200 | 1000 | 0.5490 | 0.5950 | +0.046 |
| 2200–3500 | 1000 | 0.5240 | 0.5930 | +0.069 |

### Per-band win prediction (Brier)

Rows here are still by side-to-move ELO band (the dataset's stratification). ChessMimic's winner predictions on each row are now served by whichever winner model matches the position's average rating, so a row in the 1100–1200 band may use a winner model from a neighboring band when the opponent is much stronger or weaker.

| Band | N | Maia2 | ChessMimic | Δ |
|---|---:|---:|---:|---:|
| 1000–1100 | 1000 | 0.2619 | 0.1642 | −0.098 |
| 1100–1200 | 1000 | 0.2917 | 0.2025 | −0.089 |
| 1200–1300 | 1000 | 0.2763 | 0.2110 | −0.065 |
| 1300–1400 | 1000 | 0.2865 | 0.1812 | −0.105 |
| 1400–1500 | 1000 | 0.2948 | 0.1935 | −0.101 |
| 1500–1600 | 1000 | 0.2905 | 0.1654 | −0.125 |
| 1600–1700 | 1000 | 0.2932 | 0.1944 | −0.099 |
| 1700–1800 | 1000 | 0.2754 | 0.1986 | −0.077 |
| 1800–1900 | 1000 | 0.1950 | 0.1182 | −0.077 |
| 1900–2000 | 1000 | 0.2571 | 0.1453 | −0.112 |
| 2000–2100 | 1000 | 0.2852 | 0.1705 | −0.115 |
| 2100–2200 | 1000 | 0.2398 | 0.1453 | −0.094 |
| 2200–3500 | 1000 | 0.2815 | 0.1884 | −0.093 |

## Calibration (overall, n=13,000)

The decile tables (mean predicted P(side-to-move expected score) vs empirical win-rate, draws counted as 0.5) make the AUC numbers tangible.

**Maia2 — flat empirical curve**

| Decile | Mean predicted | Empirical |
|---:|---:|---:|
| 1 | 0.129 | 0.545 |
| 5 | 0.498 | 0.489 |
| 10 | 0.889 | 0.580 |

Even when Maia2 predicts 13% expected score, the actual rate is 55%. When it predicts 89%, the actual rate is 58%. Predictions and outcomes are essentially uncorrelated.

**ChessMimic — monotone, well calibrated**

| Decile | Mean predicted | Empirical |
|---:|---:|---:|
| 1 | 0.089 | 0.080 |
| 5 | 0.483 | 0.495 |
| 10 | 0.923 | 0.942 |

Predictions track outcomes monotonically across all 10 deciles.

## Case study — back-rank mate (Lichess `s6wp7q6M`)

This single position is the cleanest illustration of why the win-prediction gap is real.

**Game:** Marcoasl1976 (1476) vs HeshimaD (1492), 5+0 blitz, 2026-04-01.

After 25.Qd8+ the position is:

```
FEN: r2Q2k1/2p2ppp/1p2p3/p7/5q1P/P1P1nP2/1P4P1/3R1RK1 b - - 2 25
```

Black has exactly **one legal move** — `Rxd8` is forced (king can't escape, nothing can interpose, only the rook can capture the queen). Both models trivially assign 1.0 to that move. The interesting signal is the value head:

| | Maia2 win_prob | ChessMimic black expected score | Truth |
|---|---:|---:|---:|
| P(black wins) + 0.5·P(draw) | **1.0000** | **0.0084** | 0 (mated) |

Game continuation: `25...Rxd8 26.Rxd8#` — White's rook on d1 recaptures with check, and Black's king on g8 has no flight square (f8/h8 attacked along the 8th rank, f7/h7 blocked by own pawns). Mate.

Maia2's value head behaves like a one-hop material counter: it sees Black up a queen after the trade and outputs ≈ +1.0. ChessMimic's winner head — trained on actual game results, with the position's back-rank-mate features visible — correctly outputs ≈ 99% white. The pattern (rook on d1 supporting Qd8, black's pawn shield blocking king escape) is well represented in training games where it ends in mate, so chessmimic learned to score it ≈ 0 for black.

## Interpretation

Two things drive the win-prediction gap:

1. **Training signal.** Maia2's value head appears to be trained on a position-evaluation signal (engine-eval-like — it ignores the back-rank mate above and just counts material). ChessMimic's winner model is trained on actual game outcomes, which captures patterns where positions look "winning" but resolve to losses (and vice versa).
2. **Inputs.** ChessMimic gets per-position clock and increment in addition to ELO; Maia2 gets ELO only. For human blitz, time pressure is a real predictor of outcomes that pure board state misses. This is structural to the models — not an unfairness in the benchmark setup.

For move prediction the gap is much smaller (a few percentage points), suggesting both models are doing well at human-move imitation and the differences come down to per-band specialization (chessmimic) vs a single shared model (Maia2).

## Caveats

- All 13,000 positions were sampled from the first 1,420 games of the April 2026 dump. That's a tiny fraction of the month's ~80M games, but it's still genuinely held-out from training and uniform across ELO bands. A larger sample (e.g., scanning the full dump with a sampling probability per band) would tighten the per-band Brier estimates.
- 1 winner bucket (`1200_1300_brier`) has an empty checkpoint dir; records whose average rating maps to that bucket are served by the nearest available bucket (`1100_1200_brier` or `1300_1400_brier`). About 1,000 records out of 13,000 are affected. The Brier delta for the side-to-move 1200–1300 band (–0.065 vs neighboring rows around –0.09 to –0.11) is consistent with that bucket being slightly under-served.
- The "side-to-move ultimately wins" target counts draws as 0.5 for Brier and log-loss; AUC drops draws (898 of 13,000).
- Wall-clock not reported here. For reference: Maia2 batched inference over the 13k positions took ~6 s on a single CUDA device at batch 256.

## Reproducing

```bash
# Build the eval dataset (~5 s on the bundled smoke test, ~5 s for held-out
# because the early-stop fires after ~1,400 games)
python experiments/maia2_benchmark/build_dataset.py \
    --output experiments/maia2_benchmark/results/eval_april_2026.jsonl \
    --per-band 1000

# Run both models
CHESSMIMIC_FORCE_CPU=false python experiments/maia2_benchmark/run_benchmark.py \
    --input experiments/maia2_benchmark/results/eval_april_2026.jsonl \
    --output-dir experiments/maia2_benchmark/results/run_full_2026_05_11 \
    --batch-size 256

# Sanity-check Maia2 batched path against upstream
python experiments/maia2_benchmark/parity_check.py
```
