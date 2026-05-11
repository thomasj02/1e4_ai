#!/usr/bin/env python3
"""Run the ChessMimic vs Maia2 head-to-head benchmark.

Reads an eval JSONL produced by ``build_dataset.py``, runs both models in
batched GPU inference, and emits:
  - <output-dir>/per_position.csv
  - <output-dir>/summary.json
  - <output-dir>/summary.md

Maia2 runs once over the full dataset (single shared model). ChessMimic runs
band-by-band so we never hold all 13 per-band checkpoints in memory at once.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
import time
from pathlib import Path

import chess
import numpy as np
import torch
import torch.nn.functional as F
from sklearn.metrics import roc_auc_score
from tqdm import tqdm

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT / "backend"))
sys.path.insert(0, str(REPO_ROOT / "Training"))

from model_inference import ModelMovePredictor
from winner_inference import WinnerPredictor
import chessmimic_core
from chessmimic_core import MOVE_TO_ACTION

from maia2 import model as maia_model, inference as maia_inference
from maia2.utils import mirror_move
from maia2.inference import preprocessing as maia_preprocessing


# ---------------------------------------------------------------------------
# I/O
# ---------------------------------------------------------------------------

def load_records(path: Path) -> list[dict]:
    with path.open() as f:
        return [json.loads(line) for line in f]


# ---------------------------------------------------------------------------
# Maia2 batched inference
# ---------------------------------------------------------------------------

def maia2_batch_inference(
    model,
    prepared,
    records: list[dict],
    batch_size: int,
) -> tuple[list[dict[str, float]], np.ndarray]:
    """Returns (move_probs_per_position, win_probs[N])."""
    all_moves_dict, elo_dict, all_moves_dict_reversed = prepared
    device = next(model.parameters()).device
    model.eval()

    fens = [r["fen"] for r in records]
    elos_self = [r["elo_self"] for r in records]
    elos_oppo = [r["elo_oppo"] for r in records]
    n = len(records)

    move_probs_out: list[dict[str, float]] = []
    win_probs_out = np.empty(n, dtype=np.float64)

    with torch.no_grad():
        for start in tqdm(range(0, n, batch_size), desc="Maia2 batches"):
            end = min(start + batch_size, n)
            boards_list, eself_list, eoppo_list, lm_list = [], [], [], []
            for i in range(start, end):
                b, es, eo, lm = maia_preprocessing(
                    fens[i], elos_self[i], elos_oppo[i], elo_dict, all_moves_dict
                )
                boards_list.append(b)
                eself_list.append(es)
                eoppo_list.append(eo)
                lm_list.append(lm)

            boards = torch.stack(boards_list).to(device)
            eself_t = torch.tensor(eself_list, dtype=torch.long).to(device)
            eoppo_t = torch.tensor(eoppo_list, dtype=torch.long).to(device)
            legal = torch.stack(lm_list).to(device)

            logits_maia, _, logits_value = model(boards, eself_t, eoppo_t)
            probs = (logits_maia * legal).softmax(dim=-1)
            value = (logits_value / 2 + 0.5).clamp(0, 1)

            probs_cpu = probs.cpu().numpy()
            value_cpu = value.cpu().numpy()
            legal_cpu = legal.cpu().numpy()

            for i in range(end - start):
                global_idx = start + i
                fen = fens[global_idx]
                black_flag = fen.split(" ")[1] == "b"

                v = float(value_cpu[i])
                if black_flag:
                    v = 1.0 - v
                win_probs_out[global_idx] = v

                row_probs = probs_cpu[i]
                legal_indices = np.nonzero(legal_cpu[i])[0]
                position_probs: dict[str, float] = {}
                for idx in legal_indices:
                    uci = all_moves_dict_reversed[int(idx)]
                    if black_flag:
                        uci = mirror_move(uci)
                    position_probs[uci] = float(row_probs[idx])
                move_probs_out.append(position_probs)

    return move_probs_out, win_probs_out


# ---------------------------------------------------------------------------
# ChessMimic batched inference (per band)
# ---------------------------------------------------------------------------

def _avg_rating(record: dict) -> int:
    """Integer average of white/black ELO, matching the production
    `(white + black) // 2` convention used by the winner endpoint."""
    return (record["white_elo"] + record["black_elo"]) // 2


def _rating_to_winner_band(rating: int, available_bands: list[str]) -> str | None:
    """Map a rating to the best available winner band, replicating the
    production model_registry's distance-based selection (exact match if the
    rating falls in [min, max); otherwise the closest band)."""
    if not available_bands:
        return None
    best = None
    best_dist = float("inf")
    for band in available_bands:
        lo, hi = map(int, band.split("_"))
        if lo <= rating < hi:
            return band
        dist = (lo - rating) if rating < lo else (rating - hi + 1)
        if dist < best_dist:
            best_dist = dist
            best = band
    return best


def _discover_winner_bands(models_dir: Path) -> list[str]:
    """Return sorted band keys (e.g. '1000_1100') under models/winner_model/
    that have a loadable model.ckpt + scalers.pkl pair."""
    bands: list[str] = []
    winner_root = models_dir / "winner_model"
    if winner_root.is_dir():
        for d in winner_root.iterdir():
            if d.is_dir() and (d / "model.ckpt").exists() and (d / "scalers.pkl").exists():
                bands.append(d.name.removesuffix("_brier"))
    return sorted(bands)


def _tokenize_records(records: list[dict]) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
    """Tokenize a list of records into stacked move/fen/mask tensors."""
    rm_list, fen_list, mask_list = [], [], []
    for r in records:
        rm, fen_t, mask = chessmimic_core.recent_moves_and_fen_to_inputs(
            r["move_history"], r["fen"]
        )
        rm_list.append(torch.from_numpy(np.asarray(rm)).long())
        fen_list.append(torch.from_numpy(np.asarray(fen_t)).long())
        mask_list.append(torch.from_numpy(np.asarray(mask)).float())
    return torch.stack(rm_list), torch.stack(fen_list), torch.stack(mask_list)


def chessmimic_move_batch_inference(
    records: list[dict], band: str, models_dir: Path, batch_size: int,
) -> list[dict[str, float]]:
    """Run chessmimic move model on records (all in same side-to-move band).

    Each record's actual `elo_self` is fed to the model — matching production
    behavior in `predict_move` and matching the per-record `elo_self` Maia2
    receives. Earlier versions used a single band midpoint per band, which
    disadvantaged ChessMimic in the head-to-head against Maia2."""
    band_dir = models_dir / "move_model" / f"{band}_brier"
    predictor = ModelMovePredictor(band_dir / "model.ckpt", band_dir / "scalers.pkl")
    device = predictor.device

    n = len(records)
    out: list[dict[str, float]] = []

    with torch.no_grad():
        for start in tqdm(range(0, n, batch_size), desc=f"  CM move {band}"):
            end = min(start + batch_size, n)
            chunk = records[start:end]
            rm, fen_t, mask = _tokenize_records(chunk)
            rm = rm.to(device)
            fen_t = fen_t.to(device)
            mask = mask.to(device)

            clocks = torch.tensor(
                [r["white_clock_s"] if r["side_to_move"] == "w" else r["black_clock_s"]
                 for r in chunk],
                dtype=torch.float32, device=device,
            )
            log_time = (torch.log1p(clocks) - predictor.log_time_mean) / predictor.log_time_std
            ratings_raw = torch.tensor(
                [float(r["elo_self"]) for r in chunk],
                dtype=torch.float32, device=device,
            )
            rating_batch = (ratings_raw - predictor.rating_mean) / predictor.rating_std

            logits = predictor.model(rm, fen_t, rating_batch, log_time)
            probs = F.softmax(logits, dim=1) * mask
            probs = probs / probs.sum(dim=1, keepdim=True)
            probs_np = probs.cpu().numpy()

            for i, r in enumerate(chunk):
                board = chess.Board(r["fen"])
                row: dict[str, float] = {}
                for move in board.legal_moves:
                    uci = move.uci()
                    idx = MOVE_TO_ACTION.get(uci)
                    if idx is not None:
                        row[uci] = float(probs_np[i, idx])
                out.append(row)

    del predictor
    if torch.cuda.is_available():
        torch.cuda.empty_cache()
    return out


def chessmimic_winner_batch_inference(
    records: list[dict], band: str, models_dir: Path, batch_size: int,
) -> np.ndarray:
    """Run chessmimic winner model on records → P(side-to-move wins)."""
    band_dir = models_dir / "winner_model" / f"{band}_brier"
    predictor = WinnerPredictor(band_dir / "model.ckpt", band_dir / "scalers.pkl")
    device = predictor.device

    n = len(records)
    out = np.empty(n, dtype=np.float64)

    with torch.no_grad():
        for start in tqdm(range(0, n, batch_size), desc=f"  CM winner {band}"):
            end = min(start + batch_size, n)
            chunk = records[start:end]
            rm, fen_t, _ = _tokenize_records(chunk)
            input_ids = torch.cat([rm, fen_t], dim=1).to(device)
            attention_mask = torch.ones_like(input_ids).to(device)

            features = np.stack([
                predictor.prepare_features(
                    r["white_elo"], r["black_elo"],
                    r["white_clock_s"], r["black_clock_s"], r["increment_s"],
                )
                for r in chunk
            ])
            features_t = torch.from_numpy(features).to(device)

            logits = predictor.model(input_ids, attention_mask, features_t)
            probs_np = F.softmax(logits, dim=-1).cpu().numpy()
            black_p = probs_np[:, 0]
            draw_p = probs_np[:, 1]
            white_p = probs_np[:, 2]

            # Expected score for the side-to-move under 0/0.5/1 targets:
            # P(side wins) + 0.5 * P(draw). Dropping the draw component would bias
            # the score downward for any model that places real mass on draws.
            for i, r in enumerate(chunk):
                if r["side_to_move"] == "w":
                    out[start + i] = float(white_p[i] + 0.5 * draw_p[i])
                else:
                    out[start + i] = float(black_p[i] + 0.5 * draw_p[i])

    del predictor
    if torch.cuda.is_available():
        torch.cuda.empty_cache()
    return out


# ---------------------------------------------------------------------------
# Metrics
# ---------------------------------------------------------------------------

def compute_topk_metrics(played_moves: list[str], move_probs: list[dict[str, float]]) -> dict:
    n = len(played_moves)
    if n == 0:
        return {"top1": float("nan"), "top3": float("nan"), "top5": float("nan"), "n": 0}
    top1 = top3 = top5 = 0
    for played, probs in zip(played_moves, move_probs):
        if not probs:
            continue
        sorted_uci = [m for m, _ in sorted(probs.items(), key=lambda x: x[1], reverse=True)]
        if sorted_uci and played == sorted_uci[0]:
            top1 += 1
        if played in sorted_uci[:3]:
            top3 += 1
        if played in sorted_uci[:5]:
            top5 += 1
    return {"top1": top1 / n, "top3": top3 / n, "top5": top5 / n, "n": n}


def compute_win_metrics(win_probs, results) -> dict:
    p = np.clip(np.asarray(win_probs, dtype=np.float64), 1e-7, 1 - 1e-7)
    y = np.asarray(results, dtype=np.float64)
    if len(p) == 0:
        return {"brier": float("nan"), "log_loss": float("nan"), "auc": None, "n": 0, "n_non_draw": 0}
    brier = float(np.mean((p - y) ** 2))
    log_loss = float(-np.mean(y * np.log(p) + (1.0 - y) * np.log(1.0 - p)))
    non_draw = y != 0.5
    auc: float | None = None
    if non_draw.sum() >= 2 and len(set(y[non_draw])) == 2:
        auc = float(roc_auc_score(y[non_draw], p[non_draw]))
    return {"brier": brier, "log_loss": log_loss, "auc": auc,
            "n": int(len(y)), "n_non_draw": int(non_draw.sum())}


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

def _topk_flags(played: str, probs: dict[str, float] | None) -> tuple[str, str, str]:
    if not probs:
        return "", "", ""
    sorted_uci = [m for m, _ in sorted(probs.items(), key=lambda x: x[1], reverse=True)]
    return (
        str(int(bool(sorted_uci) and played == sorted_uci[0])),
        str(int(played in sorted_uci[:3])),
        str(int(played in sorted_uci[:5])),
    )


def write_per_position_csv(
    path: Path, records, maia2_move, maia2_win, cm_move, cm_win
):
    with path.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow([
            "fen", "played_move", "elo_self", "elo_oppo", "side_to_move", "elo_band",
            "side_to_move_result",
            "maia2_top1", "maia2_top3", "maia2_top5", "maia2_win_prob",
            "chessmimic_top1", "chessmimic_top3", "chessmimic_top5", "chessmimic_win_prob",
        ])
        for i, r in enumerate(records):
            played = r["played_move"]
            m1, m3, m5 = _topk_flags(played, maia2_move[i])
            c1, c3, c5 = _topk_flags(played, cm_move[i])
            cm_wp = cm_win[i]
            w.writerow([
                r["fen"], played, r["elo_self"], r["elo_oppo"], r["side_to_move"], r["elo_band"],
                r["side_to_move_result"],
                m1, m3, m5, f"{maia2_win[i]:.4f}",
                c1, c3, c5, f"{cm_wp:.4f}" if cm_wp is not None and not np.isnan(cm_wp) else "",
            ])


def write_summary_md(path: Path, summary: dict):
    with path.open("w") as f:
        f.write("# ChessMimic vs Maia2 — Blitz Benchmark\n\n")
        ov = summary["overall"]
        f.write(f"Records: **{summary['n_records']}**\n\n")
        f.write("## Overall — Move prediction\n\n")
        f.write("| Metric | Maia2 | ChessMimic | Δ (CM − Maia2) |\n")
        f.write("|---|---:|---:|---:|\n")
        for k in ["top1", "top3", "top5"]:
            m = ov["maia2_move"][k]
            c = ov["chessmimic_move"][k]
            f.write(f"| {k} | {m:.4f} | {c:.4f} | {c - m:+.4f} |\n")
        f.write("\n## Overall — Win prediction\n\n")
        mw = ov["maia2_win"]
        cw = ov["chessmimic_win"]
        f.write("| Metric | Maia2 | ChessMimic |\n|---|---:|---:|\n")
        f.write(f"| Brier (lower=better) | {mw['brier']:.4f} | {cw['brier']:.4f} |\n")
        f.write(f"| Log-loss (lower=better) | {mw['log_loss']:.4f} | {cw['log_loss']:.4f} |\n")
        if mw["auc"] is not None and cw["auc"] is not None:
            f.write(f"| AUC (higher=better, draws excluded n={mw['n_non_draw']}) | {mw['auc']:.4f} | {cw['auc']:.4f} |\n")
        f.write("\n## Per-band — Move prediction (top-1)\n\n")
        f.write("| Band | N | Maia2 top-1 | CM top-1 | Δ |\n|---|---:|---:|---:|---:|\n")
        for band in sorted(summary["per_band"].keys()):
            b = summary["per_band"][band]
            n = b["n"]
            m1 = b.get("maia2_move", {}).get("top1", float("nan"))
            c1 = b.get("chessmimic_move", {}).get("top1", float("nan"))
            d = c1 - m1 if not (np.isnan(m1) or np.isnan(c1)) else float("nan")
            f.write(f"| {band} | {n} | {m1:.4f} | {c1:.4f} | {d:+.4f} |\n")
        f.write("\n## Per-band — Win prediction (Brier)\n\n")
        f.write("| Band | N | Maia2 Brier | CM Brier | Δ |\n|---|---:|---:|---:|---:|\n")
        for band in sorted(summary["per_band"].keys()):
            b = summary["per_band"][band]
            n = b["n"]
            mb = b.get("maia2_win", {}).get("brier", float("nan"))
            cb = b.get("chessmimic_win", {}).get("brier", float("nan"))
            d = cb - mb if not (np.isnan(mb) or np.isnan(cb)) else float("nan")
            f.write(f"| {band} | {n} | {mb:.4f} | {cb:.4f} | {d:+.4f} |\n")
        f.write("\n## Win-prob calibration (overall, Maia2)\n\n")
        _write_calibration_table(f, "Maia2", summary["calibration"]["maia2"])
        f.write("\n## Win-prob calibration (overall, ChessMimic)\n\n")
        _write_calibration_table(f, "ChessMimic", summary["calibration"]["chessmimic"])


def _write_calibration_table(f, label, decile_table):
    f.write("| Decile | n | mean predicted | empirical win-rate (draws=0.5) |\n")
    f.write("|---:|---:|---:|---:|\n")
    for d in decile_table:
        f.write(f"| {d['decile']} | {d['n']} | {d['mean_pred']:.4f} | {d['empirical']:.4f} |\n")


def calibration_deciles(win_probs: np.ndarray, results: np.ndarray) -> list[dict]:
    """Group predicted win_probs into 10 deciles and report empirical win-rate."""
    p = np.asarray(win_probs, dtype=np.float64)
    y = np.asarray(results, dtype=np.float64)
    if len(p) < 10:
        return []
    order = np.argsort(p)
    p_sorted = p[order]
    y_sorted = y[order]
    splits = np.array_split(np.arange(len(p_sorted)), 10)
    out = []
    for d, idx in enumerate(splits, start=1):
        if len(idx) == 0:
            continue
        out.append({
            "decile": d,
            "n": int(len(idx)),
            "mean_pred": float(p_sorted[idx].mean()),
            "empirical": float(y_sorted[idx].mean()),
        })
    return out


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description="ChessMimic vs Maia2 benchmark")
    parser.add_argument("--input", type=Path, required=True, help="Eval JSONL")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--maia2-type", default="blitz", choices=["blitz", "rapid"])
    parser.add_argument("--maia2-device", default="auto",
                        help='"auto" | "cuda" | "cpu" | "gpu"')
    parser.add_argument("--chessmimic-models-dir", type=Path,
                        default=REPO_ROOT / "backend" / "models")
    parser.add_argument("--batch-size", type=int, default=256)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)

    print(f"Loading eval dataset from {args.input}")
    records = load_records(args.input)
    print(f"Loaded {len(records)} records")
    if not records:
        print("Empty dataset, aborting", file=sys.stderr)
        return 1

    indices_by_band: dict[str, list[int]] = {}
    for i, r in enumerate(records):
        indices_by_band.setdefault(r["elo_band"], []).append(i)
    print(f"Bands: {sorted(indices_by_band.keys())}")

    # ---- Maia2 ----
    print("\n--- Loading Maia2 ---")
    cuda_available = torch.cuda.is_available()
    if args.maia2_device == "auto":
        m_dev = "gpu" if cuda_available else "cpu"
    elif args.maia2_device == "cuda":
        m_dev = "gpu"
    else:
        m_dev = args.maia2_device
    print(f"  type={args.maia2_type} device={m_dev}")
    maia2_obj = maia_model.from_pretrained(type=args.maia2_type, device=m_dev)
    prepared = maia_inference.prepare()

    print(f"\nRunning Maia2 batched inference (n={len(records)}, batch={args.batch_size})")
    t0 = time.time()
    maia2_move, maia2_win = maia2_batch_inference(
        maia2_obj, prepared, records, batch_size=args.batch_size
    )
    print(f"Maia2 inference: {time.time() - t0:.1f}s")

    del maia2_obj
    if cuda_available:
        torch.cuda.empty_cache()

    # ---- ChessMimic move inference, grouped by side-to-move ELO band ----
    # Move models are selected by the side-to-move's rating in production
    # (predict_move takes a single `rating` arg), and Maia2 also receives the
    # side-to-move ELO as `elo_self`. Per-record actual ratings are passed in
    # (see chessmimic_move_batch_inference) rather than band midpoints.
    print("\n--- ChessMimic move inference (grouped by side-to-move ELO band) ---")
    cm_move: list[dict[str, float] | None] = [None] * len(records)

    for band in sorted(indices_by_band.keys()):
        idxs = indices_by_band[band]
        band_records = [records[i] for i in idxs]
        move_dir = args.chessmimic_models_dir / "move_model" / f"{band}_brier"
        if (move_dir / "model.ckpt").exists():
            print(f"\n  Move band {band} ({len(band_records)} records)")
            move_results = chessmimic_move_batch_inference(
                band_records, band, args.chessmimic_models_dir, args.batch_size
            )
            for j, idx in enumerate(idxs):
                cm_move[idx] = move_results[j]
        else:
            print(f"  WARN: missing move model at {move_dir}/model.ckpt — skipping {len(idxs)} records")

    # ---- ChessMimic winner inference, grouped by AVERAGE-rating ELO band ----
    # Production (backend/main.py) and the winner-converter training pipeline
    # both select winner models by avg(white_elo, black_elo), not by side-to-
    # move rating. We replicate that here so the benchmark measures deployed
    # behavior rather than benchmark-specific bucket assignment.
    print("\n--- ChessMimic winner inference (grouped by average-rating ELO band) ---")
    cm_win = np.full(len(records), np.nan, dtype=np.float64)
    winner_bands_avail = _discover_winner_bands(args.chessmimic_models_dir)
    print(f"  Available winner bands: {winner_bands_avail}")

    winner_indices_by_band: dict[str, list[int]] = {}
    unassigned = 0
    for i, r in enumerate(records):
        avg = _avg_rating(r)
        band = _rating_to_winner_band(avg, winner_bands_avail)
        if band is None:
            unassigned += 1
            continue
        winner_indices_by_band.setdefault(band, []).append(i)
    if unassigned:
        print(f"  WARN: {unassigned} records had no assignable winner band")

    for band in sorted(winner_indices_by_band.keys()):
        idxs = winner_indices_by_band[band]
        band_records = [records[i] for i in idxs]
        print(f"\n  Winner band {band} ({len(band_records)} records by avg rating)")
        winner_results = chessmimic_winner_batch_inference(
            band_records, band, args.chessmimic_models_dir, args.batch_size
        )
        for j, idx in enumerate(idxs):
            cm_win[idx] = winner_results[j]

    # ---- Metrics ----
    print("\n--- Computing metrics ---")
    played = [r["played_move"] for r in records]
    results_arr = np.array([r["side_to_move_result"] for r in records], dtype=np.float64)

    summary: dict = {"n_records": len(records), "overall": {}, "per_band": {}, "calibration": {}}

    cm_move_present_idx = [i for i, mp in enumerate(cm_move) if mp is not None]
    cm_win_present_idx = np.where(~np.isnan(cm_win))[0]

    # Headline overall numbers compare Maia2 and ChessMimic on the SAME subset
    # of positions (those where ChessMimic has a prediction). Without this,
    # Maia2 is being scored on more rows than ChessMimic and the comparison
    # is not apples-to-apples.
    summary["overall"]["maia2_move"] = compute_topk_metrics(
        [played[i] for i in cm_move_present_idx],
        [maia2_move[i] for i in cm_move_present_idx],
    )
    summary["overall"]["maia2_win"] = compute_win_metrics(
        maia2_win[cm_win_present_idx], results_arr[cm_win_present_idx],
    )
    summary["overall"]["chessmimic_move"] = compute_topk_metrics(
        [played[i] for i in cm_move_present_idx],
        [cm_move[i] for i in cm_move_present_idx],
    )
    summary["overall"]["chessmimic_win"] = compute_win_metrics(
        cm_win[cm_win_present_idx], results_arr[cm_win_present_idx],
    )

    # Also report Maia2 on the full sample for transparency.
    summary["overall"]["maia2_move_full"] = compute_topk_metrics(played, maia2_move)
    summary["overall"]["maia2_win_full"] = compute_win_metrics(maia2_win, results_arr)

    # Calibration tables match the headline subset so the AUC narrative is consistent.
    summary["calibration"]["maia2"] = calibration_deciles(
        maia2_win[cm_win_present_idx], results_arr[cm_win_present_idx],
    )
    summary["calibration"]["chessmimic"] = calibration_deciles(
        cm_win[cm_win_present_idx], results_arr[cm_win_present_idx],
    )

    for band, idxs in indices_by_band.items():
        band_played = [played[i] for i in idxs]
        band_results = results_arr[idxs]
        b = {"n": len(idxs)}
        b["maia2_move"] = compute_topk_metrics(band_played, [maia2_move[i] for i in idxs])
        b["maia2_win"] = compute_win_metrics(maia2_win[idxs], band_results)
        cm_idxs_present = [i for i in idxs if cm_move[i] is not None]
        if cm_idxs_present:
            b["chessmimic_move"] = compute_topk_metrics(
                [played[i] for i in cm_idxs_present],
                [cm_move[i] for i in cm_idxs_present],
            )
        cm_win_present = [i for i in idxs if not np.isnan(cm_win[i])]
        if cm_win_present:
            b["chessmimic_win"] = compute_win_metrics(
                cm_win[cm_win_present], results_arr[cm_win_present],
            )
        summary["per_band"][band] = b

    # ---- Output ----
    summary_json = args.output_dir / "summary.json"
    with summary_json.open("w") as f:
        json.dump(summary, f, indent=2, default=lambda o: None)
    print(f"\nWrote {summary_json}")

    csv_path = args.output_dir / "per_position.csv"
    write_per_position_csv(csv_path, records, maia2_move, maia2_win, cm_move, cm_win)
    print(f"Wrote {csv_path}")

    md_path = args.output_dir / "summary.md"
    write_summary_md(md_path, summary)
    print(f"Wrote {md_path}")

    # Console summary
    print("\n=== Overall ===")
    o = summary["overall"]
    if "maia2_move" in o and "chessmimic_move" in o:
        for k in ["top1", "top3", "top5"]:
            m = o["maia2_move"][k]
            c = o["chessmimic_move"][k]
            print(f"  {k}: Maia2={m:.4f}  CM={c:.4f}  Δ={c - m:+.4f}")
    if "maia2_win" in o and "chessmimic_win" in o:
        mw = o["maia2_win"]; cw = o["chessmimic_win"]
        print(f"  Brier: Maia2={mw['brier']:.4f}  CM={cw['brier']:.4f}")
        print(f"  Log-loss: Maia2={mw['log_loss']:.4f}  CM={cw['log_loss']:.4f}")
        if mw["auc"] is not None and cw["auc"] is not None:
            print(f"  AUC (n_non_draw={mw['n_non_draw']}): Maia2={mw['auc']:.4f}  CM={cw['auc']:.4f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
