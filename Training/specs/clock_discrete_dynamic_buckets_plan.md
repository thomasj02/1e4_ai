# Clock Model Discrete Implementation with Dynamic Buckets

## Overview

This document updates the clock model implementation plan to support dynamic bucket boundaries that are:
1. Generated algorithmically based on actual data distribution
2. Configurable as hyperparameters
3. Different for each time control (bullet, blitz, rapid, classical)

## 1. Bucket Generation Pipeline

### Preprocessing Step

Before training, generate bucket boundaries using `generate_clock_buckets.py`:

```bash
# Generate boundaries for blitz games
python generate_clock_buckets.py \
    --bagz_path /path/to/train.bagz \
    --output_path data/clock_buckets/blitz_buckets.json \
    --n-buckets 28 \
    --scheme linear_logarithmic \
    --time-control blitz
```

### Supported Schemes

1. **Linear-Logarithmic** (recommended)
   - Linear buckets for fast moves
   - Logarithmic progression for slower moves
   - Adapts thresholds by time control

2. **Quantile**
   - Equal number of samples per bucket
   - May create many single-value buckets

3. **Adaptive**
   - Uses kernel density estimation
   - More buckets where data is dense

### Output Format

The generated JSON file contains:
```json
{
  "boundaries": [0, 1, 2, ..., 180, null],  // null represents infinity
  "n_buckets": 28,
  "scheme": "linear_logarithmic",
  "time_control": "blitz",
  "statistics": {
    "total_samples": 1000000,
    "mean_time": 5.2,
    "median_time": 3.0,
    "p95_time": 17.0,
    "p99_time": 32.0
  },
  "bucket_probabilities": [0.049, 0.191, ...],
  "bucket_widths": [1.0, 1.0, ...]
}
```

## 2. ClockDataset.py Updates

### Configuration

```python
@dataclass
class ClockDatasetConfig:
    # Existing fields...
    
    # New field for bucket boundaries
    bucket_boundaries_path: str  # Path to JSON file with boundaries
    
    # Remove hardcoded bucket configuration
    # Remove: discrete_threshold, n_buckets
```

### Implementation

```python
class ClockDataset(torch.utils.data.Dataset):
    def __init__(self, bagz_path: str, config: ClockDatasetConfig):
        super().__init__()
        self.bagz_path = bagz_path
        self.config = config
        
        # Load bucket boundaries
        self._load_bucket_boundaries()
        
        # ... rest of initialization ...
        
    def _load_bucket_boundaries(self):
        """Load bucket boundaries from JSON file."""
        with open(self.config.bucket_boundaries_path, 'r') as f:
            bucket_data = json.load(f)
            
        # Convert None to float('inf')
        self.boundaries = []
        for b in bucket_data['boundaries']:
            if b is None:
                self.boundaries.append(float('inf'))
            else:
                self.boundaries.append(float(b))
                
        self.n_buckets = bucket_data['n_buckets']
        self.time_control = bucket_data['time_control']
        self.bucket_probs = np.array(bucket_data['bucket_probabilities'])
        
        # Compute class weights if needed
        if self.config.compute_class_weights:
            # Use inverse of bucket probabilities
            self.class_weights = 1.0 / (self.bucket_probs + 1e-6)
            self.class_weights = self.class_weights / self.class_weights.mean()
        else:
            self.class_weights = None
            
        print(f"Loaded {self.n_buckets} buckets for {self.time_control} time control")
        
    def __getitem__(self, idx):
        # ... existing code ...
        
        # Assign to bucket
        bucket_idx = np.searchsorted(self.boundaries[:-1], thinking_time, side='right') - 1
        bucket_idx = np.clip(bucket_idx, 0, self.n_buckets - 1)
        
        return {
            'input_ids': input_ids,
            'attention_mask': attention_mask,
            'rating': rating_scaled,
            'clock_features': clock_features,
            'move_mask': move_mask,
            'target_bucket': bucket_idx,
        }
        
    def get_n_buckets(self) -> int:
        """Return number of buckets for model construction."""
        return self.n_buckets
        
    def get_class_weights(self) -> Optional[np.ndarray]:
        """Return class weights for balanced training."""
        return self.class_weights
        
    def get_bucket_info(self) -> Dict:
        """Return bucket information for evaluation/generation."""
        return {
            'boundaries': self.boundaries,
            'n_buckets': self.n_buckets,
            'time_control': self.time_control,
        }
```

## 3. ClockTrainer.py Updates

### Model Construction

```python
def create_clock_model(
    base_model_path: str,
    train_dataset: ClockDataset,
    device: torch.device
) -> ClockModel:
    """
    Create clock model with appropriate number of output buckets.
    
    Args:
        base_model_path: Path to pretrained transformer
        train_dataset: Training dataset (to get bucket info)
        device: Device to place model on
    """
    # Get bucket information from dataset
    n_buckets = train_dataset.get_n_buckets()
    class_weights = train_dataset.get_class_weights()
    
    # Load base model
    base_model = AutoModel.from_pretrained(base_model_path)
    
    # Create clock model
    model = ClockModel(
        base_model=base_model,
        n_buckets=n_buckets,
        class_weights=torch.tensor(class_weights, device=device) if class_weights is not None else None
    )
    
    return model.to(device)
```

### Training Script

```python
def main():
    args = parse_args()
    
    # Load datasets
    train_dataset = ClockDataset(
        args.train_bagz,
        ClockDatasetConfig(
            bucket_boundaries_path=args.bucket_boundaries,
            # ... other config ...
        )
    )
    
    val_dataset = ClockDataset(
        args.val_bagz,
        ClockDatasetConfig(
            bucket_boundaries_path=args.bucket_boundaries,  # Same boundaries!
            # ... other config ...
        )
    )
    
    # Create model with dynamic bucket count
    model = create_clock_model(
        args.base_model_path,
        train_dataset,
        device
    )
    
    # Get bucket info for generation
    bucket_info = train_dataset.get_bucket_info()
    
    # ... rest of training ...
```

## 4. Generation Utilities

### Updated generation functions

```python
class ClockGenerator:
    """Generate human-like thinking times."""
    
    def __init__(self, model: ClockModel, bucket_info: Dict):
        self.model = model
        self.boundaries = bucket_info['boundaries']
        self.n_buckets = bucket_info['n_buckets']
        self.time_control = bucket_info['time_control']
        
    def generate(self, 
                 input_features: Dict[str, torch.Tensor],
                 temperature: float = 1.0,
                 top_p: float = 0.9) -> float:
        """Generate a thinking time."""
        with torch.no_grad():
            outputs = self.model(**input_features)
            logits = outputs['logits']
            
        # Apply temperature
        if temperature != 1.0:
            logits = logits / temperature
            
        # Get probabilities
        probs = F.softmax(logits, dim=-1).squeeze(0)
        
        # Apply top-p filtering if requested
        if top_p < 1.0:
            sorted_probs, sorted_indices = torch.sort(probs, descending=True)
            cumsum = torch.cumsum(sorted_probs, dim=0)
            
            # Find cutoff
            cutoff_idx = (cumsum > top_p).nonzero(as_tuple=True)[0]
            if len(cutoff_idx) > 0:
                cutoff_idx = cutoff_idx[0]
                probs_filtered = torch.zeros_like(probs)
                probs_filtered[sorted_indices[:cutoff_idx+1]] = sorted_probs[:cutoff_idx+1]
                probs_filtered = probs_filtered / probs_filtered.sum()
                probs = probs_filtered
                
        # Sample bucket
        bucket_idx = torch.multinomial(probs, 1).item()
        
        # Sample within bucket
        low = self.boundaries[bucket_idx]
        high = self.boundaries[bucket_idx + 1]
        
        if high == float('inf'):
            # Exponential distribution for tail
            # Mean time based on time control
            tail_means = {
                'bullet': 10,
                'blitz': 30,
                'rapid': 60,
                'classical': 120
            }
            mean_time = tail_means.get(self.time_control, 30)
            return low + np.random.exponential(mean_time)
        else:
            # Uniform within bucket
            return np.random.uniform(low, high)
```

## 5. Hyperparameter Tuning

### Grid Search Example

```python
# Script to tune n_buckets hyperparameter
bucket_configs = [
    {'n_buckets': 16, 'scheme': 'linear_logarithmic'},
    {'n_buckets': 24, 'scheme': 'linear_logarithmic'},
    {'n_buckets': 28, 'scheme': 'linear_logarithmic'},
    {'n_buckets': 32, 'scheme': 'linear_logarithmic'},
    {'n_buckets': 20, 'scheme': 'quantile'},
    {'n_buckets': 24, 'scheme': 'adaptive'},
]

for config in bucket_configs:
    # Generate boundaries
    output_path = f"buckets_{config['scheme']}_{config['n_buckets']}.json"
    subprocess.run([
        'python', 'generate_clock_buckets.py',
        train_bagz_path,
        output_path,
        '--n-buckets', str(config['n_buckets']),
        '--scheme', config['scheme']
    ])
    
    # Train model with these boundaries
    # ... training code ...
```

## 6. Multi-Time Control Support

### Training Strategy

1. **Separate Models**: Train different models for each time control
   ```bash
   # Bullet model
   python train_clock.py \
       --train-bagz bullet_train.bagz \
       --bucket-boundaries data/buckets/bullet_buckets.json \
       --output-dir models/clock_bullet
   
   # Blitz model  
   python train_clock.py \
       --train-bagz blitz_train.bagz \
       --bucket-boundaries data/buckets/blitz_buckets.json \
       --output-dir models/clock_blitz
   ```

2. **Single Model with Time Control Embedding**: Add time control as input feature
   ```python
   # In model forward pass
   time_control_embed = self.time_control_embedding(time_control_id)
   features = torch.cat([pooled_output, time_control_embed], dim=-1)
   ```

## 7. Advantages of Dynamic Buckets

1. **Data-Driven**: Boundaries adapt to actual distribution
2. **Flexible**: Easy to experiment with different schemes
3. **Time Control Specific**: Optimal buckets for each game type
4. **Hyperparameter Tuning**: Can optimize bucket count
5. **Reproducible**: Boundaries saved for consistent evaluation

## 8. Migration Path

1. **Generate Boundaries**: Run `generate_clock_buckets.py` for your data
2. **Update Config**: Add `bucket_boundaries_path` to dataset config
3. **Modify Dataset**: Load boundaries dynamically
4. **Update Model Creation**: Use dataset's bucket count
5. **Test**: Verify bucket assignment and generation quality

## 9. Example Usage

### Complete training pipeline

```bash
# Step 1: Generate boundaries
python generate_clock_buckets.py \
    /data/blitz_train.bagz \
    /data/buckets/blitz_28.json \
    --n-buckets 28 \
    --scheme linear_logarithmic

# Step 2: Train model
python train_clock.py \
    --train-bagz /data/blitz_train.bagz \
    --val-bagz /data/blitz_val.bagz \
    --bucket-boundaries /data/buckets/blitz_28.json \
    --base-model microsoft/deberta-v3-base \
    --output-dir /models/clock_blitz_28 \
    --learning-rate 1e-4 \
    --batch-size 2048 \
    --max-epochs 30

# Step 3: Generate thinking times
python generate_clock_times.py \
    --model-path /models/clock_blitz_28 \
    --bucket-boundaries /data/buckets/blitz_28.json \
    --temperature 1.0 \
    --top-p 0.9
```