import numpy as np
import pytest


chessmimic_core = pytest.importorskip("chessmimic_core")
pytestmark = pytest.mark.cpp


def test_prepare_recent_moves_tokens_pads_and_truncates():
    empty = chessmimic_core.prepare_recent_moves_tokens([])
    assert empty.shape == (12,)
    assert np.all(empty == chessmimic_core.PAD_TOKEN)

    one_move = chessmimic_core.prepare_recent_moves_tokens(["e2e4"])
    assert one_move.shape == (12,)
    assert np.all(one_move[:-1] == chessmimic_core.PAD_TOKEN)
    assert one_move[-1] == chessmimic_core.MOVE_TO_ACTION["e2e4"]

    moves = [
        "e2e4",
        "e7e5",
        "g1f3",
        "b8c6",
        "f1c4",
        "g8f6",
        "d2d3",
        "f8c5",
        "c2c3",
        "d7d6",
        "e1g1",
        "e8g8",
        "h2h3",
        "a7a6",
    ]
    many_moves = chessmimic_core.prepare_recent_moves_tokens(moves)
    expected = [chessmimic_core.MOVE_TO_ACTION[move] for move in moves[-12:]]

    assert many_moves.tolist() == expected


def test_recent_moves_and_fen_to_inputs_shapes_and_legal_move_mask():
    recent_moves, fen_tokens, move_mask = chessmimic_core.recent_moves_and_fen_to_inputs(
        ["e2e4", "e7e5"],
        "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2",
    )

    assert recent_moves.shape == (12,)
    assert fen_tokens.shape == (chessmimic_core.SEQUENCE_LENGTH,)
    assert move_mask.shape == (chessmimic_core.NUM_ACTIONS,)
    assert set(np.unique(move_mask)).issubset({0.0, 1.0})
    assert move_mask[chessmimic_core.MOVE_TO_ACTION["g1f3"]] == 1.0
