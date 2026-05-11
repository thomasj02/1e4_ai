import json

import pytest

import bagz


pytestmark = pytest.mark.cpp


def test_python_bagz_roundtrip(tmp_path):
    path = tmp_path / "records.bagz"
    records = [b"alpha", b"bravo", b"charlie"]

    with bagz.BagWriter(str(path)) as writer:
        for record in records:
            writer.write(record)

    reader = bagz.BagReader(str(path))

    assert len(reader) == len(records)
    assert [reader[i] for i in range(len(reader))] == records


def test_filter_bagz_by_fullmove(tmp_path):
    chessmimic_core = pytest.importorskip("chessmimic_core")

    path = tmp_path / "positions.bagz"
    records = [
        {
            "recent_and_fen": [
                [],
                "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            ]
        },
        {
            "recent_and_fen": [
                ["e2e4", "e7e5"],
                "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 8",
            ]
        },
        {"not_a_position": True},
    ]

    with bagz.BagWriter(str(path)) as writer:
        for record in records:
            writer.write(json.dumps(record).encode("utf-8"))

    assert chessmimic_core.filter_bagz_by_fullmove(str(path), 5) == [1]
