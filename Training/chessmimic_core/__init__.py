"""
This file allows importing from the chessmimic_core package.

Type stubs are provided in __init__.pyi for better IDE integration,
but the actual module is implemented as a C++ extension.
"""

# Simply import from the extension module that was built
from .chessmimic_core import *
# Re-export underscored constants for convenient access
from .chessmimic_core import _CHARACTERS, _CHARACTERS_INDEX, _SPACES_CHARACTERS

__all__ = [
    'tokenize',
    'prepare_recent_moves_tokens',
    'recent_moves_and_fen_to_inputs',
    'add',
    'SEQUENCE_LENGTH',
    'INPUT_VOCAB_SIZE',
    'CLASS_TOKEN',
    'PAD_TOKEN',
    'MOVE_TO_ACTION',
    'ACTION_TO_MOVE',
    'NUM_ACTIONS',
    '_CHARACTERS',
    '_CHARACTERS_INDEX',
    '_SPACES_CHARACTERS',
]

