# Copyright 2025 DeepMind Technologies Limited
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ==============================================================================
#
# Based on https://github.com/google-deepmind/searchless_chess and modified for
# use in ChessMimic. See THIRD_PARTY_LICENSES.md.
"""Implements tokenization of FEN strings."""
import chess
import numpy as np


# pyfmt: disable
_CHARACTERS = [
    '0',
    '1',
    '2',
    '3',
    '4',
    '5',
    '6',
    '7',
    '8',
    '9',
    'a',
    'b',
    'c',
    'd',
    'e',
    'f',
    'g',
    'h',
    'p',
    'n',
    'r',
    'k',
    'q',
    'P',
    'B',
    'N',
    'R',
    'Q',
    'K',
    'w',
    '.',
]
# pyfmt: enable
_CHARACTERS_INDEX = {letter: index for index, letter in enumerate(_CHARACTERS)}
_SPACES_CHARACTERS = frozenset({'1', '2', '3', '4', '5', '6', '7', '8'})
SEQUENCE_LENGTH = 77
SEQUENCE_LENGTH += 1  # +1 for the class token.
INPUT_VOCAB_SIZE = len(_CHARACTERS)
INPUT_VOCAB_SIZE += 1  # +1 for the class token
INPUT_VOCAB_SIZE += 1  # +1 for the PAD token

CLASS_TOKEN = INPUT_VOCAB_SIZE - 2
PAD_TOKEN = INPUT_VOCAB_SIZE - 1


def tokenize(fen: str) -> np.ndarray:
    """Returns an array of tokens from a fen string.

    We compute a tokenized representation of the board, from the FEN string.
    The final array of tokens is a mapping from this string to numbers, which
    are defined in the dictionary `_CHARACTERS_INDEX`.
    For the 'en passant' information, we convert the '-' (which means there is
    no en passant relevant square) to '..', to always have two characters, and
    a fixed length output.

    Args:
      fen: The board position in Forsyth-Edwards Notation.
    """
    # Extracting the relevant information from the FEN.
    board, side, castling, en_passant, halfmoves_last, fullmoves = fen.split(' ')
    board = board.replace('/', '')
    board = side + board

    indices = []

    for char in board:
        if char in _SPACES_CHARACTERS:
            indices.extend(int(char) * [_CHARACTERS_INDEX['.']])
        else:
            indices.append(_CHARACTERS_INDEX[char])

    if castling == '-':
        indices.extend(4 * [_CHARACTERS_INDEX['.']])
    else:
        for char in castling:
            indices.append(_CHARACTERS_INDEX[char])
        # Padding castling to have exactly 4 characters.
        if len(castling) < 4:
            indices.extend((4 - len(castling)) * [_CHARACTERS_INDEX['.']])

    if en_passant == '-':
        indices.extend(2 * [_CHARACTERS_INDEX['.']])
    else:
        # En passant is a square like 'e3'.
        for char in en_passant:
            indices.append(_CHARACTERS_INDEX[char])

    # Three digits for halfmoves (since last capture) is enough since the game
    # ends at 50.
    halfmoves_last += '.' * (3 - len(halfmoves_last))
    indices.extend([_CHARACTERS_INDEX[x] for x in halfmoves_last])

    # Three digits for full moves is enough (no game lasts longer than 999
    # moves).
    fullmoves += '.' * (3 - len(fullmoves))
    indices.extend([_CHARACTERS_INDEX[x] for x in fullmoves])

    # Add the class token.
    indices.append(CLASS_TOKEN)

    assert len(indices) == SEQUENCE_LENGTH

    return np.asarray(indices, dtype=np.uint8)


# The lists of the strings of the row and columns of a chess board,
# traditionally named rank and file.
_CHESS_FILE = ['a', 'b', 'c', 'd', 'e', 'f', 'g', 'h']


def _compute_all_possible_actions() -> tuple[dict[str, int], dict[int, str]]:
    """Returns two dicts converting moves to actions and actions to moves.

    These dicts contain all possible chess moves.
    """
    all_moves = []

    # First, deal with the normal moves.
    # Note that this includes castling, as it is just a rook or king move from one
    # square to another.
    board = chess.BaseBoard.empty()
    for square in range(64):
        next_squares = []

        # Place the queen and see where it attacks (we don't need to cover the case
        # for a bishop, rook, or pawn because the queen's moves includes all their
        # squares).
        board.set_piece_at(square, chess.Piece.from_symbol('Q'))
        next_squares += board.attacks(square)

        # Place knight and see where it attacks
        board.set_piece_at(square, chess.Piece.from_symbol('N'))
        next_squares += board.attacks(square)
        board.remove_piece_at(square)

        for next_square in next_squares:
            all_moves.append(chess.square_name(square) + chess.square_name(next_square))

    # Then deal with promotions.
    # Only look at the last ranks.
    promotion_moves = []
    for rank, next_rank in [('2', '1'), ('7', '8')]:
        for index_file, file in enumerate(_CHESS_FILE):
            # Normal promotions.
            move = f'{file}{rank}{file}{next_rank}'
            promotion_moves += [(move + piece) for piece in ['q', 'r', 'b', 'n']]

            # Capture promotions.
            # Left side.
            if file > 'a':
                next_file = _CHESS_FILE[index_file - 1]
                move = f'{file}{rank}{next_file}{next_rank}'
                promotion_moves += [(move + piece) for piece in ['q', 'r', 'b', 'n']]
            # Right side.
            if file < 'h':
                next_file = _CHESS_FILE[index_file + 1]
                move = f'{file}{rank}{next_file}{next_rank}'
                promotion_moves += [(move + piece) for piece in ['q', 'r', 'b', 'n']]
    all_moves += promotion_moves

    move_to_action, action_to_move = {}, {}
    for action, move in enumerate(all_moves):
        assert move not in move_to_action
        move_to_action[move] = action
        action_to_move[action] = move

    return move_to_action, action_to_move


MOVE_TO_ACTION, ACTION_TO_MOVE = _compute_all_possible_actions()
NUM_ACTIONS = len(MOVE_TO_ACTION)
