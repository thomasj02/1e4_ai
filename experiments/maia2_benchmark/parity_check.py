#!/usr/bin/env python3
"""Verify our batched Maia2 path agrees with the upstream inference_each on
top-1 move and win_prob to within 1e-3.
"""
from __future__ import annotations

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT / "experiments" / "maia2_benchmark"))

import torch  # noqa: E402

from maia2 import model as maia_model, inference as maia_inference  # noqa: E402

from run_benchmark import maia2_batch_inference  # noqa: E402


FENS = [
    # White to move (start position)
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    # Black to move (after 1.e4)
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1",
    # Mid-game white to move
    "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQ1RK1 w kq - 0 5",
    # Endgame black to move
    "8/5k2/4p3/3p4/3P4/4P3/5K2/8 b - - 0 30",
    # Tactic, white to move
    "r3k2r/pp1n1ppp/2pq1n2/3p4/3P4/2P2N2/PP3PPP/RNBQR1K1 w kq - 0 10",
]

ELO_PAIRS = [(1500, 1500), (1900, 1900), (1100, 1500), (2200, 1800), (1700, 1700)]


def main() -> int:
    device = "gpu" if torch.cuda.is_available() else "cpu"
    print(f"Loading maia2 (blitz, {device})")
    m = maia_model.from_pretrained(type="blitz", device=device)
    prepared = maia_inference.prepare()

    # Per-position reference
    ref_top1 = []
    ref_win = []
    for fen, (es, eo) in zip(FENS, ELO_PAIRS):
        mp, wp = maia_inference.inference_each(m, prepared, fen, es, eo)
        ref_top1.append(next(iter(mp.keys())))
        ref_win.append(wp)

    # Batched
    records = [
        {"fen": f, "elo_self": es, "elo_oppo": eo}
        for f, (es, eo) in zip(FENS, ELO_PAIRS)
    ]
    move_probs, win_probs = maia2_batch_inference(m, prepared, records, batch_size=2)
    bat_top1 = []
    for mp in move_probs:
        sorted_mp = sorted(mp.items(), key=lambda x: x[1], reverse=True)
        bat_top1.append(sorted_mp[0][0] if sorted_mp else None)
    bat_win = win_probs.tolist()

    print()
    fail = False
    for i, fen in enumerate(FENS):
        top1_match = ref_top1[i] == bat_top1[i]
        win_diff = abs(ref_win[i] - bat_win[i])
        win_match = win_diff < 1e-3
        ok = "OK " if (top1_match and win_match) else "FAIL"
        if not (top1_match and win_match):
            fail = True
        print(f"  [{ok}] FEN[{i}]: ref top1={ref_top1[i]} batch top1={bat_top1[i]} | "
              f"ref win={ref_win[i]:.4f} batch win={bat_win[i]:.4f} (Δ={win_diff:.5f})")

    print()
    if fail:
        print("PARITY CHECK FAILED")
        return 1
    print("PARITY CHECK PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
