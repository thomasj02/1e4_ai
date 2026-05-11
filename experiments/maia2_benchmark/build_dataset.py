#!/usr/bin/env python3
"""Build a stratified blitz eval dataset from a Lichess monthly PGN dump.

Streams a `.pgn.zst` (or plain `.pgn`) with python-chess + zstandard, filters to
Rated Blitz games, walks each game's plies, and writes one JSONL record per
(position, played-move) pair. Records are stratified by ELO band so each band
gets at most `--per-band` records; streaming stops once every band is full.

Output schema (one JSON object per line):
    {
        "fen": str,
        "played_move": str,         # UCI
        "side_to_move": "w" | "b",
        "elo_self": int,            # rating of the side to move
        "elo_oppo": int,
        "white_elo": int,
        "black_elo": int,
        "white_clock_s": float,
        "black_clock_s": float,
        "increment_s": float,
        "move_history": [str, ...], # UCI moves leading to this position
        "elo_band": str,            # e.g. "1700_1800" — chessmimic bucket name
        "side_to_move_result": float  # 1.0 / 0.0 / 0.5
    }
"""

from __future__ import annotations

import argparse
import io
import json
import re
import sys
from pathlib import Path
from typing import IO, Iterator

import chess
import chess.pgn
import zstandard as zstd
from tqdm import tqdm


# ChessMimic ELO buckets (must match backend/models/move_model/{lo}_{hi}_brier/)
ELO_BANDS: list[tuple[int, int]] = [
    (1000, 1100), (1100, 1200), (1200, 1300), (1300, 1400),
    (1400, 1500), (1500, 1600), (1600, 1700), (1700, 1800),
    (1800, 1900), (1900, 2000), (2000, 2100), (2100, 2200),
    (2200, 3500),
]


def elo_to_band(elo: int) -> str | None:
    """Map an ELO to a chessmimic band string like '1700_1800', or None if out of range."""
    for lo, hi in ELO_BANDS:
        if lo <= elo < hi:
            return f"{lo}_{hi}"
    return None


def open_pgn_stream(path: Path) -> IO[str]:
    """Open a .pgn or .pgn.zst as a text stream of PGN content."""
    if path.suffix == ".zst":
        f = path.open("rb")
        dctx = zstd.ZstdDecompressor()
        # stream_reader yields decompressed bytes; wrap in TextIOWrapper for line iteration
        reader = dctx.stream_reader(f)
        return io.TextIOWrapper(reader, encoding="utf-8", errors="replace")
    return path.open("r", encoding="utf-8", errors="replace")


_TIMECONTROL_RE = re.compile(r"^(\d+)\+(\d+)$")


def parse_increment(time_control: str) -> float:
    """Extract the increment in seconds from a TimeControl tag like '300+0' or '180+2'."""
    m = _TIMECONTROL_RE.match(time_control or "")
    if not m:
        return 0.0
    return float(m.group(2))


def result_for_side(result: str, side: chess.Color) -> float | None:
    """Convert PGN Result tag → outcome from the given side's perspective.

    Returns 1.0 win / 0.0 loss / 0.5 draw, or None for incomplete/unknown results.
    """
    if result == "1-0":
        return 1.0 if side == chess.WHITE else 0.0
    if result == "0-1":
        return 1.0 if side == chess.BLACK else 0.0
    if result == "1/2-1/2":
        return 0.5
    return None


def iter_games(stream: IO[str]) -> Iterator[chess.pgn.Game]:
    """Yield games one at a time from a PGN stream."""
    while True:
        game = chess.pgn.read_game(stream)
        if game is None:
            return
        yield game


def extract_records(
    game: chess.pgn.Game,
    skip_plies: int,
    min_total_plies: int,
) -> Iterator[dict]:
    """Yield one record per ply from a single game, skipping book + truncated games."""
    headers = game.headers
    if headers.get("Event", "") != "Rated Blitz game":
        return
    result = headers.get("Result", "*")
    side_to_move_result_white = result_for_side(result, chess.WHITE)
    side_to_move_result_black = result_for_side(result, chess.BLACK)
    if side_to_move_result_white is None:
        return  # unfinished / unknown

    try:
        white_elo = int(headers.get("WhiteElo", ""))
        black_elo = int(headers.get("BlackElo", ""))
    except ValueError:
        return

    time_control = headers.get("TimeControl", "")
    base_clock_match = re.match(r"^(\d+)\+(\d+)$", time_control)
    if not base_clock_match:
        return
    base_clock = float(base_clock_match.group(1))
    increment = parse_increment(time_control)

    white_band = elo_to_band(white_elo)
    black_band = elo_to_band(black_elo)
    if white_band is None and black_band is None:
        return  # no relevant band

    # Walk the mainline
    board = game.board()
    move_history: list[str] = []
    white_clock = base_clock
    black_clock = base_clock
    plies_seen = 0
    nodes = list(game.mainline())
    if len(nodes) < min_total_plies:
        return

    for node in nodes:
        move = node.move
        if move is None:
            break

        side = board.turn  # side about to move (i.e., side that plays `move`)
        # Read clock annotation from this node (clock remaining AFTER the move)
        clock_remaining = node.clock()
        # Snapshot clocks BEFORE the move was played: remaining + (move time consumed
        # is unknown without prior clock, so we approximate using node.clock() as remaining
        # at the side's clock for this position. For chessmimic both clocks are inputs;
        # we use the most recent known clock for the other side.
        elo_self = white_elo if side == chess.WHITE else black_elo
        elo_oppo = black_elo if side == chess.WHITE else white_elo

        side_band = elo_to_band(elo_self)
        if side_band is not None and plies_seen >= skip_plies:
            stm_result = (
                side_to_move_result_white if side == chess.WHITE
                else side_to_move_result_black
            )
            yield {
                "fen": board.fen(),
                "played_move": move.uci(),
                "side_to_move": "w" if side == chess.WHITE else "b",
                "elo_self": elo_self,
                "elo_oppo": elo_oppo,
                "white_elo": white_elo,
                "black_elo": black_elo,
                "white_clock_s": white_clock,
                "black_clock_s": black_clock,
                "increment_s": increment,
                "move_history": list(move_history),
                "elo_band": side_band,
                "side_to_move_result": stm_result,
            }

        # Apply the move and update bookkeeping
        board.push(move)
        move_history.append(move.uci())
        plies_seen += 1

        if clock_remaining is not None:
            # `node.clock()` is the side-to-move's clock AFTER they made the move
            # which means it's the clock for the player who just moved.
            if side == chess.WHITE:
                white_clock = float(clock_remaining)
            else:
                black_clock = float(clock_remaining)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build a stratified Rated Blitz eval dataset from a Lichess PGN dump."
    )
    parser.add_argument(
        "--input", type=Path, required=True,
        help="Path to a .pgn.zst (or plain .pgn) Lichess monthly dump "
             "(see https://database.lichess.org/).",
    )
    parser.add_argument(
        "--output", type=Path, required=True,
        help="Output JSONL path.",
    )
    parser.add_argument(
        "--per-band", type=int, default=2000,
        help="Max records to keep per ELO band (default: 2000).",
    )
    parser.add_argument(
        "--skip-plies", type=int, default=8,
        help="Number of opening plies to skip per game (default: 8).",
    )
    parser.add_argument(
        "--min-total-plies", type=int, default=20,
        help="Drop games with fewer than this many total plies (default: 20).",
    )
    parser.add_argument(
        "--max-games", type=int, default=None,
        help="Stop after scanning this many games (mostly for smoke tests).",
    )
    args = parser.parse_args()

    if not args.input.exists():
        print(f"Input not found: {args.input}", file=sys.stderr)
        return 1

    args.output.parent.mkdir(parents=True, exist_ok=True)

    band_counts: dict[str, int] = {f"{lo}_{hi}": 0 for lo, hi in ELO_BANDS}
    target_bands = set(band_counts.keys())

    games_seen = 0
    games_kept = 0
    records_written = 0
    pbar = tqdm(unit="game", desc="Scanning games")

    with open_pgn_stream(args.input) as stream, args.output.open("w", encoding="utf-8") as out:
        for game in iter_games(stream):
            games_seen += 1
            pbar.update(1)
            if args.max_games is not None and games_seen >= args.max_games:
                break

            game_records = list(extract_records(game, args.skip_plies, args.min_total_plies))
            if not game_records:
                continue

            wrote_anything = False
            for rec in game_records:
                band = rec["elo_band"]
                if band not in target_bands:
                    continue
                if band_counts[band] >= args.per_band:
                    continue
                out.write(json.dumps(rec))
                out.write("\n")
                band_counts[band] += 1
                records_written += 1
                wrote_anything = True

            if wrote_anything:
                games_kept += 1

            # Refresh progress bar postfix periodically
            if games_seen % 5000 == 0:
                full_bands = sum(1 for v in band_counts.values() if v >= args.per_band)
                pbar.set_postfix(kept=records_written, full_bands=f"{full_bands}/{len(band_counts)}")

            # Stop early once every band is full
            if all(v >= args.per_band for v in band_counts.values()):
                break

    pbar.close()
    print()
    print(f"Games scanned : {games_seen:,}")
    print(f"Games kept    : {games_kept:,}")
    print(f"Records written: {records_written:,}")
    print(f"Per-band counts:")
    for band, count in band_counts.items():
        marker = " (full)" if count >= args.per_band else ""
        print(f"  {band}: {count:,}{marker}")
    print(f"Output: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
