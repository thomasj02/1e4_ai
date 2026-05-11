# Clock Logging Fix - Showing Actual Sampled Bucket

## Problem Identified

The original logging was confusing because it showed:
- The **highest probability bucket** (e.g., bucket 2)
- But the **thinking time from a different bucket** (e.g., 14.0s from bucket 14)

This happened because:
1. The code found the bucket with highest probability
2. But then sampled from the entire probability distribution
3. The sampled bucket could be different from the highest probability bucket

## Solution Implemented

### Changes to clock_inference.py

1. **Modified `sample_thinking_time`** to return both thinking time and sampled bucket index:
   ```python
   def sample_thinking_time(self, bucket_probabilities: np.ndarray) -> tuple[float, int]:
       bucket_idx = np.random.choice(len(bucket_probabilities), p=bucket_probabilities)
       thinking_time = self.bucket_info.sample_from_bucket_empirical(bucket_idx)
       return thinking_time, bucket_idx
   ```

2. **Updated `predict_thinking_time`** to track both sampled and highest probability buckets:
   ```python
   info = {
       'predicted_bucket': sampled_bucket,  # The actual bucket we sampled from
       'bucket_probability': float(bucket_probs[sampled_bucket]),
       'highest_prob_bucket': highest_prob_bucket,  # For reference
       'highest_probability': float(bucket_probs[highest_prob_bucket]),
       ...
   }
   ```

### Changes to main.py

Enhanced logging to clearly show:
- Which bucket was actually sampled
- When it differs from the highest probability bucket
- Visual markers in the probability distribution

## New Output Format

```
[CLOCK] Sampled from bucket 14 [14.0, 15.0) (prob=3.2%), thinking time: 14.0s
[CLOCK] Note: Highest probability bucket was 2 [2.0, 3.0) (prob=30.6%)
[CLOCK] Full bucket probabilities:
  Bucket  0      [0.0, 1.0):   1.2%
  Bucket  1      [1.0, 2.0):  19.9%
  Bucket  2      [2.0, 3.0):  30.6%
  ...
  Bucket 14    [14.0, 15.0):   3.2% <-- SAMPLED
  ...
```

## Benefits

1. **No more confusion** - The bucket range always matches the thinking time
2. **Transparency** - Shows both what was most likely and what actually happened
3. **Debugging** - Easy to spot unusual sampling (low probability events)
4. **Validation** - Can verify the stochastic sampling is working correctly

## Example Scenarios

### Common Case (sampled = highest probability)
```
[CLOCK] Sampled from bucket 2 [2.0, 3.0) (prob=35.0%), thinking time: 2.0s
```

### Less Common Case (sampled ≠ highest probability)
```
[CLOCK] Sampled from bucket 5 [5.0, 6.0) (prob=8.5%), thinking time: 5.0s
[CLOCK] Note: Highest probability bucket was 2 [2.0, 3.0) (prob=35.0%)
```

This fix ensures the logging accurately represents what the clock model is actually doing!