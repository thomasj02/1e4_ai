#!/usr/bin/env python3
"""
Test the extract_common_positions executable with a small sample bagz file.
"""

import subprocess
import tempfile
import json
from pathlib import Path

import pytest


pytestmark = [pytest.mark.cpp, pytest.mark.data]


def test_extract_common_positions():
    training_root = Path(__file__).resolve().parents[1]

    # Check if executable exists
    exe_path = training_root / "build" / "extract_common_positions"
    if not exe_path.exists():
        pytest.skip(f"{exe_path} not found; run ./build_extract_common_positions.sh first")

    # Use the sample bagz file
    bagz_path = training_root / "sample_data" / "train.tiny.bagz"
    if not bagz_path.exists():
        pytest.skip(f"Sample bagz file {bagz_path} not found")

    # Create temporary output file
    with tempfile.NamedTemporaryFile(mode='w', suffix='.jsonl', delete=False) as tmp:
        output_path = tmp.name

    # Create temporary directory for intermediate files
    with tempfile.TemporaryDirectory() as temp_dir:
        # Run the extractor with a low threshold for testing
        cmd = [
            str(exe_path),
            str(bagz_path),
            output_path,
            "--threshold", "1",  # Low threshold for testing
            "--chunk-size", "100",  # Small chunks for testing
            "--temp-dir", temp_dir,
            "--threads", "4"
        ]

        try:
            result = subprocess.run(cmd, capture_output=True, text=True)

            if result.returncode != 0:
                pytest.fail(
                    "extract_common_positions exited with "
                    f"code {result.returncode}\nSTDOUT:\n{result.stdout}\nSTDERR:\n{result.stderr}"
                )

            # Check output file
            if Path(output_path).exists():
                # Validate output lines are parseable and contain the expected fields.
                with open(output_path, 'r') as f:
                    rows = [json.loads(line) for line in f]

                for row in rows:
                    assert "fen" in row
                    assert "total" in row
                    assert "moves" in row
            else:
                pytest.fail(f"Output file {output_path} was not created")

        except subprocess.CalledProcessError as e:
            pytest.fail(f"Error running command: {e}")
        finally:
            # Clean up output file
            Path(output_path).unlink(missing_ok=True)

if __name__ == "__main__":
    test_extract_common_positions()
