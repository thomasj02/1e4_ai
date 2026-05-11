# ChessMimic Experiments

This directory contains various experiments and benchmarks for the ChessMimic project.

## Contents

### Core Modules
- `glicko2.py` - Implementation of the Glicko-2 rating system

### Experiments
- `rating_experiment.py` - Simulate games between models with different ratings and track performance
- `benchmark_inference.py` - Benchmark CPU vs CUDA performance for model inference
- `clock_time_experiment.py` - Analyze how clock time affects move quality and game outcomes

### Tests (in tests/ subdirectory)
- `test_glicko2_worked_example.py` - Test Glicko-2 implementation against paper's worked example
- `test_glicko2_issue.py` - Test to understand rating convergence behavior
- `test_50_move_rule.py` - Test that the 50-move rule is properly implemented

## Running Experiments

Experiments default to the `backend/models/move_model/1800_1900_brier` move
model. Set `CHESSMIMIC_EXPERIMENT_MOVE_MODEL_DIR` to use another directory that
contains `model.ckpt` and `scalers.pkl`.

### Rating Experiment
Tests how model performance varies with different rating parameters:

```bash
cd experiments
python rating_experiment.py --rating1 1700 --rating2 1800 --num-games 1000
```

### Inference Benchmark
Compare CPU vs CUDA performance:

```bash
cd experiments
python benchmark_inference.py
```

### Clock Time Experiment
Analyze how time pressure affects move quality:

```bash
cd experiments
python clock_time_experiment.py --num-games 100 --abundant-time 300 --scarce-time 1
```

This experiment has two players with the same rating (default 1750) but different time constraints:
- One player always has abundant time (default 300s)
- The other always has scarce time (default 1s)

The experiment tracks:
- Win rates for each time condition
- Move quality metrics (probability of chosen moves, rank of chosen moves)
- How often each player chooses the top-ranked move
- Distribution of move sources (model vs common moves database)

## Results

Experiment results are saved in timestamped directories with:
- CSV logs of all games
- PGN files of individual games (optional)
- Rating progression plots
- Summary statistics
