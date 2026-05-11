# ChessMimic Specifications

This directory contains design specifications and architecture documents for various components of the ChessMimic project.

## Contents

### Clock Model
- **[clock_model.md](clock_model.md)** - Specification for the clock prediction model that estimates how long a player will take to make a move
  - Enhanced clock embedding approach using 3D input (player clock, opponent clock, increment)
  - Hybrid discrete-continuous prediction for fast moves vs long thinks
  - Fine-tuning strategy from pre-trained PatzerModel checkpoints

### Data Processing
- **[cpp_clock_data_converter_spec.md](cpp_clock_data_converter_spec.md)** - Specification for C++ tool to generate clock training data
  - Processes PGN files with clock annotations to create specialized BAGZ format
  - Extracts thinking times and time management features
  - Handles common position filtering for train/validation splits

## Design Principles

1. **Minimal Architecture Changes** - Prefer modifications that work within existing model structures
2. **Data-Driven Decisions** - All design choices backed by statistical analysis
3. **Backward Compatibility** - New features should not break existing functionality
4. **Efficient Implementation** - Optimize for both training efficiency and inference speed