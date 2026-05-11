"""Tests for clock inference module."""

import pytest
import torch
import numpy as np
from pathlib import Path
from unittest.mock import Mock, patch, MagicMock, ANY
import os
import math


@pytest.fixture
def mock_dependencies():
    """Fixture to provide mocked dependencies."""
    mock_model = Mock()
    mock_model.eval = Mock()
    mock_model.to = Mock(return_value=mock_model)
    mock_model.load_state_dict = Mock()
    
    mock_bucket = Mock()
    mock_bucket.n_buckets = 30
    mock_bucket.sample_from_bucket_empirical = Mock(return_value=2.5)
    
    mock_scalers = {
        'rating': {'mean': 1800.0, 'std': 400.0},
        'log_player_clock': {'mean': 5.0, 'std': 1.5},
        'log_opponent_clock': {'mean': 5.0, 'std': 1.5},
        'log_increment': {'mean': 1.0, 'std': 0.5}
    }
    
    return mock_model, mock_bucket, mock_scalers


@pytest.fixture
def mock_predictor(mock_dependencies):
    """Fixture to create a mocked ClockPredictor."""
    mock_model, mock_bucket, mock_scalers = mock_dependencies
    
    with patch('clock_inference.torch.load') as mock_load, \
         patch('clock_inference.Path.exists', return_value=True), \
         patch('builtins.open', create=True), \
         patch('clock_inference.pickle.load') as mock_pickle, \
         patch('clock_inference.load_bucket_info') as mock_bucket_info, \
         patch('clock_inference.ClockPatzerModel') as mock_model_class:
        
        mock_model_class.return_value = mock_model
        mock_load.return_value = {'state_dict': {'dummy': torch.tensor(0.0)}}
        mock_pickle.return_value = mock_scalers
        mock_bucket_info.return_value = mock_bucket
        
        from clock_inference import ClockPredictor
        predictor = ClockPredictor(
            model_path=Path("test_model.ckpt"),
            scalers_path=Path("test_scalers.pkl"),
            bucket_info_path=Path("test_buckets.json")
        )
        predictor.model = mock_model  # Ensure we're using the mocked model
        
        yield predictor, mock_model, mock_bucket


class TestClockPredictor:
    """Test ClockPredictor initialization and methods."""
    
    def test_initialization_success(self, mock_dependencies):
        """Test successful initialization with all required files."""
        mock_model, mock_bucket, mock_scalers = mock_dependencies
        
        with patch('clock_inference.torch.load') as mock_load, \
             patch('clock_inference.Path.exists', return_value=True), \
             patch('builtins.open', create=True), \
             patch('clock_inference.pickle.load') as mock_pickle, \
             patch('clock_inference.load_bucket_info') as mock_bucket_info, \
             patch('clock_inference.ClockPatzerModel') as mock_model_class:
            
            mock_model_class.return_value = mock_model
            mock_load.return_value = {'state_dict': {'dummy': torch.tensor(0.0)}}
            mock_pickle.return_value = mock_scalers
            mock_bucket_info.return_value = mock_bucket
            
            from clock_inference import ClockPredictor
            predictor = ClockPredictor(
                model_path=Path("test_model.ckpt"),
                scalers_path=Path("test_scalers.pkl"),
                bucket_info_path=Path("test_buckets.json")
            )
            
            assert predictor.device == torch.device('cpu')  # Default is CPU
            assert predictor.rating_mean == 1800.0
            assert predictor.bucket_info == mock_bucket
    
    def test_initialization_missing_files(self):
        """Test initialization fails when files are missing."""
        from clock_inference import ClockPredictor
        
        # Test missing model file
        with pytest.raises(FileNotFoundError, match="Model file not found"):
            ClockPredictor(
                model_path=Path("nonexistent_model.ckpt"),
                scalers_path=Path("test_scalers.pkl"),
                bucket_info_path=Path("test_buckets.json")
            )
    
    def test_predict_bucket_returns_valid_distribution(self, mock_predictor):
        """Test that predict_bucket returns valid probability distribution."""
        predictor, mock_model, mock_bucket = mock_predictor
        
        # Mock the model forward pass
        # Return logits for 30 buckets
        mock_model.return_value = torch.randn(1, 30)
        
        # Test predict_bucket
        probabilities = predictor.predict_bucket(
            fen="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            recent_moves=[],
            rating=1500,
            player_clock=300.0,
            opponent_clock=300.0,
            increment=5.0
        )
        
        # Check it's a valid probability distribution
        assert probabilities.shape == (30,)
        assert abs(probabilities.sum() - 1.0) < 1e-6
        assert (probabilities >= 0).all()
        assert (probabilities <= 1).all()
    
    def test_no_bucket_masking_applied(self, mock_predictor):
        """Test that NO bucket masking is applied (all buckets can be predicted)."""
        predictor, mock_model, mock_bucket = mock_predictor
        
        # Mock the model to return high logits for the last bucket
        logits = torch.zeros(1, 30)
        logits[0, 29] = 10.0  # High logit for last bucket (long thinking time)
        mock_model.return_value = logits
        
        # Test with very low clock time
        probabilities = predictor.predict_bucket(
            fen="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            recent_moves=[],
            rating=1500,
            player_clock=1.0,  # Only 1 second left!
            opponent_clock=300.0,
            increment=0.0
        )
        
        # The last bucket should still have non-zero probability
        # (no masking applied)
        assert probabilities[29] > 0.9  # Should be close to 1 due to high logit
    
    def test_sample_thinking_time_uses_empirical_distribution(self, mock_predictor):
        """Test that sample_thinking_time uses empirical distributions."""
        predictor, mock_model, mock_bucket = mock_predictor
        
        # Create bucket probabilities (bucket 2 has 100% probability)
        bucket_probs = np.zeros(30)
        bucket_probs[2] = 1.0
        
        # Sample thinking time
        thinking_time, bucket_idx, num_filtered = predictor.sample_thinking_time(bucket_probs)
        
        # Check that empirical distribution was used
        mock_bucket.sample_from_bucket_empirical.assert_called_once_with(2)
        assert thinking_time == 2.5
        assert bucket_idx == 2
        assert num_filtered == 0
    
    def test_predict_thinking_time_can_exceed_clock(self, mock_predictor):
        """Test that predict_thinking_time can return times > available clock time."""
        predictor, mock_model, mock_bucket = mock_predictor
        
        # Return a time longer than available clock
        mock_bucket.sample_from_bucket_empirical = Mock(return_value=120.0)
        
        # Mock the model to predict a high bucket
        logits = torch.zeros(1, 30)
        logits[0, 29] = 10.0  # High logit for last bucket
        mock_model.return_value = logits
        
        # Predict with low clock time
        thinking_time, info = predictor.predict_thinking_time(
            fen="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            recent_moves=[],
            rating=1500,
            player_clock=10.0,  # Only 10 seconds
            opponent_clock=300.0,
            increment=0.0
        )
        
        # Should be able to return > 10 seconds
        assert thinking_time == 120.0
        assert info['predicted_bucket'] == 29
        assert info['player_clock'] == 10.0
        assert info['would_forfeit'] == True
    
    def test_full_pipeline_integration(self, mock_predictor):
        """Test the full pipeline with various clock states."""
        predictor, mock_model, mock_bucket = mock_predictor
        
        # Different times for different buckets
        def mock_sample(bucket_idx):
            times = {0: 0.5, 5: 5.5, 10: 10.5, 29: 60.0}
            return times.get(bucket_idx, float(bucket_idx))
        
        mock_bucket.sample_from_bucket_empirical = Mock(side_effect=mock_sample)
        
        # Mock different model outputs based on clock time
        def mock_forward(*args, **kwargs):
            # For this test, always return bucket 10
            logits = torch.zeros(1, 30)
            logits[0, 10] = 10.0
            return logits
        
        mock_model.side_effect = mock_forward
        
        # Test scenario
        thinking_time, info = predictor.predict_thinking_time(
            fen="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            recent_moves=[],
            rating=1500,
            player_clock=300.0,
            opponent_clock=300.0,
            increment=0.0
        )
        
        assert info['predicted_bucket'] == 10
        assert thinking_time == 10.5


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
