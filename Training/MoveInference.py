import MoveTrainer
import tokenizer
import torch
import MoveDataset
import pickle
import math
import argparse

def run_inference(checkpoint_path, scalers_path, fen=None, rating=1800, clock_times=None):
    """
    Run inference on a chess position with different time controls.
    
    Args:
        checkpoint_path: Path to the model checkpoint
        scalers_path: Path to the scalers pickle file
        fen: Chess position in FEN format (default: starting position)
        rating: Player rating to simulate (default: 1800)
        clock_times: List of clock times in seconds to simulate (default: [180, 10])
    """
    if fen is None:
        fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"  # Starting position
    
    if clock_times is None:
        clock_times = [180.0, 10.0]  # Default to 3 minutes and 10 seconds
    
    # Load scalers
    print(f"Loading scalers from {scalers_path}")
    with open(scalers_path, "rb") as f:
        scalers = pickle.load(f)
        
    rating_mean = float(scalers['rating']['mean'])
    rating_std = float(scalers['rating']['std'])
    log_time_mean = float(scalers['log_time']['mean'])
    log_time_std = float(scalers['log_time']['std'])
    
    print(f"Scalers loaded: rating (mean={rating_mean:.2f}, std={rating_std:.2f}), log_time (mean={log_time_mean:.4f}, std={log_time_std:.4f})")
    
    # Create model
    model = MoveTrainer.PatzerModel(
        recent_moves_sequence_length=12,
        recent_moves_vocab_size=len(tokenizer.MOVE_TO_ACTION),
        board_sequence_length=tokenizer.SEQUENCE_LENGTH,
        board_input_vocab_size=tokenizer.INPUT_VOCAB_SIZE,
        embedding_dim=256,
        widening_factor=4,
        num_layers=8,
        num_heads=8,
        output_vocab_size=tokenizer.NUM_ACTIONS)
    
    # Load checkpoint
    print(f"Loading checkpoint from {checkpoint_path}")
    state_dict = torch.load(checkpoint_path, map_location=torch.device('cpu'))["state_dict"]
    
    # The checkpoint has a 'model.' prefix that we need to remove
    new_state_dict = {}
    for k, v in state_dict.items():
        if k.startswith('model.'):
            new_key = k[6:]  # Remove 'model.' prefix (length is 6)
            new_state_dict[new_key] = v
    
    # Handle any other prefixes, if they exist
    remove_prefix = '_orig_mod.'
    new_state_dict = {k[len(remove_prefix):] if k.startswith(remove_prefix) else k: v for k, v in new_state_dict.items()}
    
    print(f"Loaded state dict with {len(new_state_dict)} keys")
    model.load_state_dict(new_state_dict)
    
    model.eval()
    torch.set_grad_enabled(False)
    print("Model loaded successfully")
    
    # Parse FEN position
    recent_moves = []  # Start with empty recent moves
    recent_moves_tokens, fen_tokens, move_mask = MoveDataset.BagzDataset.recent_moves_and_fen_to_inputs(recent_moves, fen)
    recent_moves_tokens = recent_moves_tokens.unsqueeze(0)
    fen_tokens = fen_tokens.unsqueeze(0)
    
    # Scale rating
    scaled_rating = (rating - rating_mean) / rating_std
    scaled_rating = torch.tensor([scaled_rating], dtype=torch.float32)
    
    print(f"\nPosition: {fen}")
    print(f"Using player rating: {rating}")
    
    # Run inference for each clock time
    for clock_time in clock_times:
        # Scale log time
        log_time = math.log(1 + clock_time)
        scaled_log_time = (log_time - log_time_mean) / log_time_std
        scaled_log_time = torch.tensor([scaled_log_time], dtype=torch.float32)
        
        # Predict
        y_hat = model(recent_moves_tokens, fen_tokens, scaled_rating, scaled_log_time)
        probabilities = MoveTrainer.LightningModel.logits_to_probabilities(y_hat, move_mask).numpy()[0]
        
        # Sort by probability
        sorted_moves = [(tokenizer.ACTION_TO_MOVE[idx], prob) for idx, prob in enumerate(probabilities) if prob > 0]
        sorted_moves.sort(key=lambda x: x[1], reverse=True)
        
        # Print top 10 moves
        print(f"\nTop moves (clock time: {clock_time}s):")
        for move, prob in sorted_moves[:10]:
            print(f"{move}: {prob:.6f}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Run move inference with different time controls')
    parser.add_argument('--checkpoint', type=str, help='Path to the model checkpoint', required=True)
    parser.add_argument('--scalers', type=str, help='Path to the scalers pickle file', required=True)
    parser.add_argument('--fen', type=str, help='Chess position in FEN format (default: starting position)', 
                        default="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")
    parser.add_argument('--rating', type=int, help='Player rating to simulate (default: 1800)', default=1800)
    parser.add_argument('--clock-times', type=float, nargs='+', 
                        help='List of clock times in seconds to simulate (default: 180 10)', 
                        default=[180.0, 10.0])
    
    args = parser.parse_args()
    
    run_inference(
        checkpoint_path=args.checkpoint, 
        scalers_path=args.scalers, 
        fen=args.fen, 
        rating=args.rating, 
        clock_times=args.clock_times
    )
