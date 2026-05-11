import sys
from pathlib import Path

import pytest


TRAINING_ROOT = Path(__file__).resolve().parents[1]
if str(TRAINING_ROOT) not in sys.path:
    sys.path.insert(0, str(TRAINING_ROOT))


def pytest_collection_modifyitems(config, items):
    try:
        import chessmimic_core  # noqa: F401
    except Exception as exc:
        skip_cpp = pytest.mark.skip(reason=f"chessmimic_core extension is unavailable: {exc}")
        for item in items:
            if "cpp" in item.keywords:
                item.add_marker(skip_cpp)
