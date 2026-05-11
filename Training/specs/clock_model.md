# Clock Model Specification

## Overview

The Clock Model predicts how long a chess player will take to make a move, given the position, game context, and time situation. It uses a hybrid discrete-continuous approach with unified soft transitions to handle both fast moves (0-20 seconds) and long thinks (>20 seconds).

The model is fine-tuned from existing PatzerModel checkpoints by:
1. Enhancing the clock embedding from 1D to 3D input (player clock, opponent clock, increment)
2. Replacing the move prediction output with clock-specific prediction heads
3. Training on blitz games with clock annotations

## Model Architecture

### Input Features

1. **Board Representation** (same as Move Model)
   - 64 squares × vocabulary size
   - Includes special CLS token
   - **Important**: The FEN tokenization already includes:
     - Piece positions
     - Side to move
     - Castling rights
     - En passant square
     - Halfmove clock (for 50-move rule)
     - **Fullmove number** (move count)

2. **Recent Moves** (same as Move Model)
   - Last 12 moves encoded as action tokens

3. **Player Rating** (scaled)
   - Single float value

4. **Time Features** (enhanced for clock prediction)
   - **Player's remaining time** (log-scaled and standardized)
   - **Opponent's remaining time** (log-scaled and standardized)
   - **Increment per move** (log-scaled and standardized)
   - Move number - already encoded in FEN
   
All three time features are passed as a vector through the enhanced clock embedding layer.

### Model Structure

The ClockModel enhances the PatzerModel with a simple but effective modification to capture all relevant time features:

#### Enhanced Clock Embedding

The PatzerModel's single-feature clock embedding:
```
clock_time_embedding: nn.Linear(1, embedding_dim)
```

Is replaced with a multi-feature embedding:
```
clock_time_embedding: nn.Linear(3, embedding_dim)
```

The input becomes a 3-dimensional vector:
```
[player_clock_scaled, opponent_clock_scaled, increment_scaled]
```

**Key Design Decisions**:
- No architectural changes - preserves sequence length and positional encoding
- Utilizes the 256-dimensional embedding space to jointly encode related time features
- The embedding layer learns optimal feature combinations and interactions
- Maintains backward compatibility through input padding if needed

**Implementation Steps**:
1. Modify the clock embedding layer to accept 3 inputs instead of 1
2. Update data loading to provide all three scaled time features
3. Replace the output layer with clock-specific prediction heads
4. All other model components remain unchanged

## Unified Loss Function

The loss function combines discrete and continuous distributions with soft transitions:

- **Discrete Loss**: Cross-entropy loss for moves taking 0-20 seconds (exact second prediction)
- **Continuous Loss**: Gaussian negative log-likelihood for moves >20 seconds (in log-space)
- **Mode Prediction**: Binary cross-entropy to predict whether a move will be fast or slow
- **Soft Transitions**: Uses sigmoid probability to blend discrete and continuous losses
- **Regularization**: Encourages accurate mode prediction with a weighted penalty

Key parameters:
- `discrete_threshold`: 20 seconds (boundary between fast and slow moves)
- `discrete_weight`: Weight for discrete distribution loss
- `continuous_weight`: Weight for continuous distribution loss
- `mode_reg_weight`: Weight for mode prediction regularization (typically 0.1)

## Data Preprocessing

### Common Position Filtering

Similar to the move training pipeline, we will skip common FENs and positions where we have abundant samples. For these positions, we can sample from the empirical distribution instead of using model predictions.

- Load common positions database with occurrence counts
- Skip positions with count >= threshold (default: 1000)
- This reduces training time and prevents overfitting on opening positions

### Game Filtering

For initial training, we focus exclusively on blitz games:

- Parse time control formats (e.g., "300+0", "180+2")
- Calculate total game time: base_time + (40 moves × increment)
- Select games with total time between 180-600 seconds (3-10 minutes)
- Skip bullet games (<3 min) and rapid/classical games (>10 min)

### Scaling Functions

Time-related features are preprocessed for the enhanced embedding:

- **Player's remaining time**: Log-scale transformation: log(time + 1), then standardized
- **Opponent's remaining time**: Log-scale transformation: log(time + 1), then standardized  
- **Increment**: Log-scale transformation: log(1 + increment), then standardized
- **Move number**: Already encoded in FEN, no additional scaling needed
- **Ratings**: Standardized using pre-computed mean and standard deviation

For the enhanced clock embedding, all three time features should be scaled consistently:
```
player_clock_scaled = (log(player_time + 1) - log_time_mean) / log_time_std
opponent_clock_scaled = (log(opponent_time + 1) - log_time_mean) / log_time_std  
increment_scaled = (log(increment + 1) - log_increment_mean) / log_increment_std
clock_features = [player_clock_scaled, opponent_clock_scaled, increment_scaled]
```

This ensures all features are on similar scales for the embedding layer.

## C++ Data Converter Specification

### New Binary: `pgn_to_clock_bagz`

Create a new converter specifically for clock prediction data:

**File: `cpp_src/pgn_to_clock_bagz_main.cpp`**

```cpp
// Main differences from pgn_to_bagz_main.cpp:
// 1. Extract clock annotations from PGN comments
// 2. Calculate time spent per move
// 3. Store additional time features
// 4. Skip games without clock data

The clock data record structure extends the existing bagz format:
- Recent moves: Last 12 moves as strings
- FEN: Complete position including move number
- Player rating: Rating of the player to move
- Player time remaining: Clock time when the move was made
- Opponent time remaining: Opponent's clock time
- Increment: Seconds added per move
- Time spent: Target value - seconds taken for this move

All three time features (player, opponent, increment) are stored to support the enhanced embedding approach.
```

### Key Implementation Details

1. **Clock Parsing**
   - Parse PGN clock annotations in [%clk H:MM:SS] format
   - Extract hours, minutes, and seconds from comment strings
   - Convert to total seconds for storage

2. **Time Calculation**
   - Track remaining time for both players after each move
   - Calculate time spent as: previous_time - current_time
   - Handle increment additions correctly
   - Validate time values (skip if >3600s or 0s)

3. **Game Filtering**
   - Skip games without clock annotations
   - Filter for blitz games only (3-10 minute time controls)
   - Parse time control strings (e.g., "300+0", "180+2")
   - Estimate total game time using 40-move assumption

4. **Common Position Filtering**
   - Load common positions database during initialization
   - Check each position against the database
   - Skip positions with occurrence count >= threshold
   - Track statistics for skipped positions

### Bagz Format for Clock Data

The clock bagz files use the same format as regular bagz files (compressed records + index), but store different record structures containing clock-specific data. The ClockRecord struct will be serialized to bytes for each position.

## Training Configuration

### Fine-tuning Strategy

**Model Initialization**
- Load pre-trained PatzerModel checkpoint
- Replace `clock_time_embedding` layer: `nn.Linear(1, 256)` → `nn.Linear(3, 256)`
- Replace output layer with clock-specific heads
- Initialize new layers with appropriate schemes

**Hyperparameters**
- Discrete threshold: 20 seconds
- Batch size: 2048
- Learning rate: 1e-4 (lower than training from scratch)
- Loss weights: discrete=1.0, continuous=1.0, mode_regularization=0.1
- Max epochs: 50 (fewer needed due to pre-training)
- Gradient clipping: 1.0

**Training Schedule**
- Warmup: 1000 steps with linear learning rate increase
- Main training: Linear decay after warmup
- Early stopping based on validation loss
- Optional: Freeze transformer blocks for first few epochs

**Data Configuration**
- Time control: Blitz games only (3-10 minutes)
- Skip common positions with count >= 1000
- Provide all three time features for each position
- Use consistent scaling across train/validation sets

## Evaluation Metrics

1. **Mode Prediction Accuracy**: How well does the model predict discrete vs continuous?
2. **Discrete Accuracy**: For moves ≤20s, exact match accuracy
3. **Continuous RMSE**: For moves >20s, root mean squared error in log-space
4. **Percentile Accuracy**: Accuracy within 25th, 50th, 75th percentiles
5. **Time Budget Correlation**: How well predictions correlate with actual time management

## Future Enhancements

1. **Multi-task Learning**: Train jointly with move prediction
2. **Opponent Modeling**: Include opponent's typical thinking patterns
3. **Position Complexity**: Add features for position sharpness/complexity
4. **Opening Book Detection**: Special handling for opening moves
5. **Time Pressure Modeling**: Explicit modeling of time pressure effects