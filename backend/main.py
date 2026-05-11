from fastapi import FastAPI, Depends, HTTPException, status
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field
from common_moves_db import load_common_moves_db
from model_manager import ModelManager
from auth import get_current_user
import chess
import random
import time
import os
import sentry_sdk
from logging_config import setup_logging, get_logger
from datetime import datetime, timezone

# Rating system imports
from rating import (
    calculate_rating_update,
    increase_rd_for_inactivity,
    result_string_to_float,
    get_rating_tier,
    INITIAL_RATING,
    INITIAL_RD,
)
from supabase_client import get_supabase

# Setup logging
setup_logging()
logger = get_logger(__name__)


# Track startup time
startup_start_time = time.time()


def _env_bool(name: str, default: bool = False) -> bool:
    """Read a boolean environment variable."""
    value = os.getenv(name)
    if value is None:
        return default
    return value.strip().lower() in {"1", "true", "yes", "on"}


def _env_float(name: str, default: float = 0.0) -> float:
    """Read a float environment variable with a safe fallback."""
    value = os.getenv(name)
    if value is None or value.strip() == "":
        return default
    try:
        return float(value)
    except ValueError:
        logger.warning("Invalid %s=%r; using %.3f", name, value, default)
        return default


def _csv_env(name: str, default: list[str]) -> list[str]:
    """Read a comma-separated environment variable."""
    value = os.getenv(name)
    if not value:
        return default
    parsed = [item.strip() for item in value.split(",") if item.strip()]
    return parsed or default


def _init_sentry() -> None:
    """Initialize Sentry only when explicitly configured."""
    dsn = os.getenv("SENTRY_DSN", "").strip()
    if not dsn:
        logger.info("Sentry disabled; SENTRY_DSN is not set")
        return

    sentry_sdk.init(
        dsn=dsn,
        send_default_pii=_env_bool("SENTRY_SEND_DEFAULT_PII", False),
        traces_sample_rate=_env_float("SENTRY_TRACES_SAMPLE_RATE", 0.0),
        profile_session_sample_rate=_env_float("SENTRY_PROFILES_SAMPLE_RATE", 0.0),
        profile_lifecycle=os.getenv("SENTRY_PROFILE_LIFECYCLE", "trace"),
    )


_init_sentry()

app = FastAPI()

app.add_middleware(
    CORSMiddleware,
    allow_origins=_csv_env(
        "BACKEND_CORS_ORIGINS",
        ["http://localhost:3000", "http://127.0.0.1:3000"],
    ),
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


# Initialize the model manager (loads all models)
logger.info("Initializing model manager...")
model_manager_start = time.time()
model_manager = ModelManager(os.getenv("CHESSMIMIC_MODELS_PATH", "models"))
model_manager_time = (time.time() - model_manager_start) * 1000
logger.info(f"Model manager initialized in {model_manager_time:.0f}ms")

# Log model info
model_info = model_manager.get_model_info()
logger.info(f"Available models: {model_info['move_models']['count']} move, "
           f"{model_info['clock_models']['count']} clock, "
           f"{model_info['winner_models']['count']} winner")

# Initialize the common moves database from all move models
logger.info("Initializing common moves database...")
common_moves_start = time.time()
common_moves_files = model_manager.registry.get_all_common_moves_files()
if common_moves_files:
    logger.info(f"Loading common moves from {len(common_moves_files)} rating range(s)")
    common_moves_db = load_common_moves_db(common_moves_files)
else:
    logger.warning("No common moves files found")
    common_moves_db = load_common_moves_db([])
common_moves_time = (time.time() - common_moves_start) * 1000
logger.info(f"Common moves database loaded in {common_moves_time:.0f}ms")

# Print total startup time
total_startup_time = (time.time() - startup_start_time) * 1000
logger.info(f"Total startup time: {total_startup_time:.0f}ms")
logger.info("Backend initialization complete")

@app.get('/')
async def root():
    return {'message': 'Chessmimic AI Backend Root'}


@app.get('/health')
async def health_check():
    """Health check endpoint - no authentication required."""
    return {
        'status': 'healthy',
        'timestamp': time.time(),
        'auth_enabled': os.getenv('DISABLE_AUTH', 'false').lower() != 'true'
    }


@app.get('/models')
async def get_models(user: dict = Depends(get_current_user)):
    """Get information about available models."""
    return model_manager.get_model_info()


@app.get('/auth/me')
async def get_me(user: dict = Depends(get_current_user)):
    """Get current authenticated user information."""
    return {
        'user_id': user.get('user_id'),
        'email': user.get('email'),
        'authenticated': True
    }


# Add your endpoints for games, websockets etc. here later


class MoveRequest(BaseModel):
    fen: str
    moves: list[str]


class MoveResponse(BaseModel):
    move: str | None
    thinking_time: float


class MoveRequestWithParams(BaseModel):
    """Extended move request with optional parameters."""
    fen: str
    moves: list[str]
    rating: int | None = None
    clock_time: float | None = None
    opponent_clock_time: float | None = None
    increment: float | None = None
    min_probability: float | None = None
    include_evaluation: bool | None = None


class EvaluationRequest(BaseModel):
    """Request for position evaluation."""
    fen: str
    moves: list[str] = []
    white_rating: int | None = None
    black_rating: int | None = None
    white_clock: float | None = None
    black_clock: float | None = None
    increment: float | None = None


class EvaluationResponse(BaseModel):
    """Response with position evaluation."""
    evaluation: float  # -1 (black wins) to +1 (white wins)
    black_prob: float | None = None  # Probability of black winning
    draw_prob: float | None = None   # Probability of draw
    white_prob: float | None = None  # Probability of white winning
    error: str | None = None


@app.post('/get_move', response_model=MoveResponse)
async def get_move(req: MoveRequest | MoveRequestWithParams, user: dict = Depends(get_current_user)):
    """Return a move for the given position.
    
    Priority order:
    1. Common moves database (if position is known)
    2. Model prediction (uses rating and clock time if provided)
    
    Never returns a random move.
    """
    board = chess.Board(req.fen)

    # Extract optional parameters if provided
    rating = getattr(req, 'rating', 1800) or 1800
    clock_time = getattr(req, 'clock_time', 300.0) or 300.0
    opponent_clock_time = getattr(req, 'opponent_clock_time', 300.0) or 300.0
    increment = getattr(req, 'increment', 0.0) or 0.0
    min_probability = getattr(req, 'min_probability', 0.0) or 0.0

    # Predict thinking time using clock model
    thinking_time = float(random.randint(3, 5))  # Default fallback

    clock_predictor = model_manager.get_clock_model(rating)
    if clock_predictor is not None:
        try:
            # Convert SAN moves to UCI for clock model
            uci_moves = None
            if req.moves:
                temp_board = chess.Board()
                uci_moves = []
                try:
                    for san_move in req.moves:
                        move = temp_board.push_san(san_move)
                        uci_moves.append(move.uci())
                except Exception as e:
                    logger.warning(f"Failed to convert move history to UCI for clock model: {e}")
                    uci_moves = None
            
            # Get clock prediction
            predicted_time, clock_info = clock_predictor.predict_thinking_time(
                fen=req.fen,
                recent_moves=uci_moves,
                rating=rating,
                player_clock=clock_time,
                opponent_clock=opponent_clock_time,
                increment=increment,
                min_probability=min_probability
            )
            
            thinking_time = predicted_time
            
            # Debug output
            sampled_range = clock_predictor.bucket_info.get_bucket_range(clock_info['predicted_bucket'])
            sampled_range_str = f"[{sampled_range[0]:.1f}, {sampled_range[1]:.1f})" if sampled_range[1] != float('inf') else f"[{sampled_range[0]:.1f}, ∞)"
            
            logger.info(f"CLOCK: Sampled from bucket {clock_info['predicted_bucket']} {sampled_range_str} "
                       f"(prob={clock_info['bucket_probability']:.1%}), thinking time: {thinking_time:.1f}s")
            
            if clock_info.get('num_buckets_filtered_out', 0) > 0:
                logger.debug(f"CLOCK: Filtered out {clock_info['num_buckets_filtered_out']} buckets below {min_probability:.1%} threshold")
                logger.debug(f"CLOCK: Buckets after filter: {clock_info['num_buckets_after_filter']}")
            
            if clock_info['predicted_bucket'] != clock_info['highest_prob_bucket']:
                highest_range = clock_predictor.bucket_info.get_bucket_range(clock_info['highest_prob_bucket'])
                highest_range_str = f"[{highest_range[0]:.1f}, {highest_range[1]:.1f})" if highest_range[1] != float('inf') else f"[{highest_range[0]:.1f}, ∞)"
                logger.debug(f"CLOCK: Note: Highest probability bucket was {clock_info['highest_prob_bucket']} {highest_range_str} "
                            f"(prob={clock_info['highest_probability']:.1%})")
            
            # Log full probability distribution
            if 'top_buckets' in clock_info:
                logger.debug("CLOCK: Full bucket probabilities:")
                for bucket_idx, prob in clock_info['top_buckets']:
                    bucket_range = clock_predictor.bucket_info.get_bucket_range(bucket_idx)
                    range_str = f"[{bucket_range[0]:.1f}, {bucket_range[1]:.1f})" if bucket_range[1] != float('inf') else f"[{bucket_range[0]:.1f}, ∞)"
                    # Mark the sampled bucket
                    marker = " <-- SAMPLED" if bucket_idx == clock_info['predicted_bucket'] else ""
                    logger.debug(f"  Bucket {bucket_idx:2d} {range_str:>15}: {prob:6.1%}{marker}")
            
            if clock_info.get('would_forfeit', False):
                logger.warning(f"CLOCK: Predicted time exceeds available clock! "
                              f"({thinking_time:.1f}s > {clock_time:.1f}s)")
        except Exception as e:
            logger.warning(f"Clock prediction failed: {e}, using random time", exc_info=True)
            # Fall back to random time
            thinking_time = float(random.randint(3, 5))
    
    if board.is_game_over():
        return {'move': None, 'thinking_time': thinking_time}

    # Debug: show what we're searching for
    logger.debug(f"Searching for position: {req.fen}")
    logger.debug(f"Move history: {req.moves if req.moves else 'None'}")
    logger.debug(f"Rating: {rating}, Clock time: {clock_time}s, Opp clock: {opponent_clock_time}s, "
                f"Increment: {increment}s, Min probability: {min_probability}")
    
    # PRIORITY 1: Try to get a move from the common moves database
    common_move_uci, strategy, info = common_moves_db.sample_move(
        req.fen, 
        req.moves if req.moves else None,
        move_format="san"  # Specify that moves are in SAN format
    )
    
    if common_move_uci and info:
        # Convert UCI notation (e.g., "e2e4") to a chess.Move object
        try:
            move = chess.Move.from_uci(common_move_uci)
            # Verify the move is legal in the current position
            if move in board.legal_moves:
                san = board.san(move)
                logger.info(f"Playing {san} ({common_move_uci}) from COMMON MOVES DB")
                logger.debug(f"Strategy: {strategy}")
                logger.debug(f"Move probability: {info['probability']:.1%} ({info['move_count']:,}/{info['total_count']:,} occurrences)")
                logger.debug(f"Position has {info['num_moves']} different moves in database")
                return {'move': san, 'thinking_time': thinking_time}
            else:
                logger.error(f"Common move {common_move_uci} is not legal in position {req.fen}")
        except Exception as e:
            logger.error(f"Error parsing common move {common_move_uci}: {e}", exc_info=True)
    else:
        logger.debug(f"No common move found (strategy: {strategy})")
    
    # PRIORITY 2: Use model prediction
    model_predictor = model_manager.get_move_model(rating)
    if model_predictor is not None:
        logger.debug(f"Using MODEL PREDICTION (rating {rating})")
        try:
            # Convert SAN moves to UCI for model
            uci_moves = None
            if req.moves:
                temp_board = chess.Board()
                uci_moves = []
                try:
                    for san_move in req.moves:
                        move = temp_board.push_san(san_move)
                        uci_moves.append(move.uci())
                except Exception as e:
                    logger.warning(f"Failed to convert move history to UCI: {e}")
                    uci_moves = None
            
            # Get model prediction
            model_move_uci, model_info = model_predictor.predict_move(
                req.fen, 
                uci_moves,
                rating=rating,
                clock_time=clock_time,
                min_probability=min_probability
            )
            
            if model_move_uci:
                move = chess.Move.from_uci(model_move_uci)
                if move in board.legal_moves:
                    san = board.san(move)
                    logger.info(f"Playing {san} ({model_move_uci}) from MODEL")
                    logger.debug(f"Move probability: {model_info['chosen_probability']:.1%}")
                    
                    # Log timing information
                    if 'inference_time_ms' in model_info:
                        logger.info(f"TIMING: Total inference: {model_info['inference_time_ms']}ms (model forward pass: {model_info['model_forward_time_ms']}ms)")
                    
                    if 'num_moves_filtered_out' in model_info and model_info['num_moves_filtered_out'] > 0:
                        logger.debug(f"Filtered out {model_info['num_moves_filtered_out']} moves below {min_probability:.1%} threshold")
                        logger.debug(f"Moves after filter: {model_info['num_moves_after_filter']}")
                    logger.debug("Top 5 moves:")
                    for m, p in model_info['top_moves'][:5]:
                        logger.debug(f"  - {m}: {p:.1%}")
                    return {'move': san, 'thinking_time': thinking_time}
                else:
                    logger.error(f"Model move {model_move_uci} is not legal")
        except Exception as e:
            logger.error(f"Model prediction failed: {e}", exc_info=True)
    else:
        logger.debug("Model not available")
    
    # This should never happen - either common moves or model should provide a move
    logger.error("No move source available! This should not happen.")
    # As a last resort, pick the first legal move
    legal_moves = list(board.legal_moves)
    if legal_moves:
        move = legal_moves[0]
        san = board.san(move)
        logger.warning(f"Using first legal move as last resort: {san} ({move.uci()})")
        return {'move': san, 'thinking_time': thinking_time}
    
    return {'move': None, 'thinking_time': thinking_time}


@app.post('/evaluate_position', response_model=EvaluationResponse)
async def evaluate_position(req: EvaluationRequest, user: dict = Depends(get_current_user)):
    """Evaluate a chess position using the winner prediction model.
    
    Returns win probabilities for white, draw, and black.
    """
    # Log the incoming request
    logger.info("EVAL: Evaluation request received")
    logger.debug(f"EVAL: FEN: {req.fen}")
    logger.debug(f"EVAL: Moves ({len(req.moves) if req.moves else 0}): {req.moves[:5] if req.moves else 'None'}{'...' if req.moves and len(req.moves) > 5 else ''}")
    logger.debug(f"EVAL: Ratings: W={req.white_rating or 1500}, B={req.black_rating or 1500}")
    logger.debug(f"EVAL: Clocks: W={req.white_clock or 300.0:.1f}s, B={req.black_clock or 300.0:.1f}s, Inc={req.increment or 0.0}s")
    
    # Default values
    default_response = {
        'evaluation': 0.0,  # Draw/equal position
        'black_prob': None,
        'draw_prob': None,
        'white_prob': None,
        'error': None
    }

    # Get winner model for the average rating
    avg_rating = ((req.white_rating or 1500) + (req.black_rating or 1500)) // 2
    winner_predictor = model_manager.get_winner_model(avg_rating)

    # Check if winner predictor is available
    if winner_predictor is None:
        error_msg = f"Winner model not available for rating {avg_rating}"
        logger.error(f"EVAL: {error_msg}")
        default_response['error'] = error_msg
        return default_response
    
    try:
        import time
        start_time = time.time()
        
        # Convert moves from SAN to UCI if provided
        uci_moves = None
        if req.moves:
            board = chess.Board()
            uci_moves = []
            try:
                for san_move in req.moves:
                    move = board.push_san(san_move)
                    uci_moves.append(move.uci())
                logger.debug(f"EVAL: Converted {len(uci_moves)} moves to UCI format")
            except Exception as e:
                logger.warning(f"EVAL: Failed to convert move history to UCI: {e}")
                uci_moves = None
        
        # Get evaluation (enable debug if EVAL_DEBUG env var is set)
        debug_mode = os.environ.get('EVAL_DEBUG', '').lower() in ['true', '1', 'yes']
        eval_value, info = winner_predictor.predict_winner(
            fen=req.fen,
            recent_moves=uci_moves,
            white_rating=req.white_rating or 1500,
            black_rating=req.black_rating or 1500,
            white_clock=req.white_clock or 300.0,
            black_clock=req.black_clock or 300.0,
            increment=req.increment or 0.0,
            debug=debug_mode
        )
        
        # Calculate inference time
        inference_time_ms = (time.time() - start_time) * 1000
        
        # Log the response
        logger.info(f"EVAL: Evaluation complete in {inference_time_ms:.1f}ms")
        logger.info(f"EVAL: Result: {eval_value:+.3f}")
        
        # Interpret the evaluation
        if eval_value > 0.1:
            assessment = "White is better"
        elif eval_value < -0.1:
            assessment = "Black is better"
        else:
            assessment = "Position is equal"
        logger.debug(f"EVAL: Assessment: {assessment}")
        
        return {
            'evaluation': eval_value,
            'black_prob': info.get('black_prob'),
            'draw_prob': info.get('draw_prob'),
            'white_prob': info.get('white_prob'),
            'error': None
        }
        
    except Exception as e:
        error_msg = str(e)
        logger.error(f"EVAL: Evaluation failed: {error_msg}", exc_info=True)
        logger.debug("EVAL: Returning default values")
        default_response['error'] = error_msg
        return default_response


# ============================================================================
# Rating System Models and Endpoints
# ============================================================================

class UserRatingResponse(BaseModel):
    """Response model for user rating information."""
    rating: float
    rating_deviation: float
    games_played: int
    wins: int
    losses: int
    draws: int
    rating_tier: str


class MoveEval(BaseModel):
    """Position evaluation for a move."""
    white: float
    draw: float
    black: float


class MoveRecord(BaseModel):
    """Record of a single move with clock and evaluation data."""
    move: str  # SAN notation
    clock: int  # Milliseconds remaining after this move
    eval: MoveEval


class GameResultRequest(BaseModel):
    """Request model for recording a game result."""
    bot_rating: int = Field(..., ge=100, le=3500)
    player_color: str = Field(..., pattern="^(white|black)$")
    result: str = Field(..., pattern="^(win|loss|draw)$")
    result_reason: str | None = None
    move_count: int | None = None
    time_control: str | None = None
    game_moves: list[MoveRecord] | None = None


class RatingUpdateResponse(BaseModel):
    """Response model for rating update after a game."""
    new_rating: float
    rating_change: float
    new_rd: float
    rating_tier: str


class RatingEditRequest(BaseModel):
    """Request model for manually editing rating."""
    new_rating: float = Field(..., ge=100, le=3500)


class GameHistoryItem(BaseModel):
    """Single game history entry."""
    id: str
    bot_rating: int
    player_color: str
    result: str
    result_reason: str | None
    move_count: int | None
    time_control: str | None
    rating_before: float
    rating_after: float
    rating_change: float
    played_at: str
    game_moves: list[dict] | None = None


class GameHistoryResponse(BaseModel):
    """Response model for game history."""
    games: list[GameHistoryItem]
    total_count: int


async def get_or_create_user_rating(user_id: str) -> dict:
    """Get user rating from database, creating default if not exists."""
    supabase = get_supabase()

    # Try to get existing rating
    result = supabase.table('user_ratings').select('*').eq('clerk_user_id', user_id).execute()

    if result.data and len(result.data) > 0:
        user_rating = result.data[0]
        # Update RD for inactivity
        last_game_at = user_rating.get('last_game_at')
        if last_game_at:
            last_game_dt = datetime.fromisoformat(last_game_at.replace('Z', '+00:00'))
            new_rd = increase_rd_for_inactivity(
                float(user_rating['rating_deviation']),
                last_game_dt
            )
            if new_rd != float(user_rating['rating_deviation']):
                # Update the RD in database
                supabase.table('user_ratings').update({
                    'rating_deviation': new_rd,
                    'updated_at': datetime.now(timezone.utc).isoformat()
                }).eq('clerk_user_id', user_id).execute()
                user_rating['rating_deviation'] = new_rd
        return user_rating

    # Create new user rating with defaults
    new_user = {
        'clerk_user_id': user_id,
        'rating': INITIAL_RATING,
        'rating_deviation': INITIAL_RD,
        'games_played': 0,
        'wins': 0,
        'losses': 0,
        'draws': 0,
        'created_at': datetime.now(timezone.utc).isoformat(),
        'updated_at': datetime.now(timezone.utc).isoformat()
    }

    result = supabase.table('user_ratings').insert(new_user).execute()

    if not result.data or len(result.data) == 0:
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to create user rating record"
        )

    return result.data[0]


@app.get('/rating', response_model=UserRatingResponse)
async def get_user_rating(user: dict = Depends(get_current_user)):
    """Get current user's rating. Creates default rating if none exists."""
    user_id = user.get('user_id')
    if not user_id:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="User ID not found in token"
        )

    user_rating = await get_or_create_user_rating(user_id)

    return UserRatingResponse(
        rating=float(user_rating['rating']),
        rating_deviation=float(user_rating['rating_deviation']),
        games_played=int(user_rating['games_played']),
        wins=int(user_rating['wins']),
        losses=int(user_rating['losses']),
        draws=int(user_rating['draws']),
        rating_tier=get_rating_tier(float(user_rating['rating']))
    )


@app.post('/rating/game', response_model=RatingUpdateResponse)
async def record_game_result(req: GameResultRequest, user: dict = Depends(get_current_user)):
    """Record a game result and update user's rating."""
    user_id = user.get('user_id')
    if not user_id:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="User ID not found in token"
        )

    # Get current rating
    user_rating = await get_or_create_user_rating(user_id)
    current_rating = float(user_rating['rating'])
    current_rd = float(user_rating['rating_deviation'])

    # Calculate new rating
    result_value = result_string_to_float(req.result)
    rating_update = calculate_rating_update(
        player_rating=current_rating,
        player_rd=current_rd,
        opponent_rating=req.bot_rating,
        result=result_value
    )

    # Update statistics
    new_games_played = int(user_rating['games_played']) + 1
    new_wins = int(user_rating['wins']) + (1 if req.result == 'win' else 0)
    new_losses = int(user_rating['losses']) + (1 if req.result == 'loss' else 0)
    new_draws = int(user_rating['draws']) + (1 if req.result == 'draw' else 0)

    supabase = get_supabase()
    now = datetime.now(timezone.utc).isoformat()

    # Update user rating
    supabase.table('user_ratings').update({
        'rating': rating_update.new_rating,
        'rating_deviation': rating_update.new_rd,
        'games_played': new_games_played,
        'wins': new_wins,
        'losses': new_losses,
        'draws': new_draws,
        'last_game_at': now,
        'updated_at': now
    }).eq('clerk_user_id', user_id).execute()

    # Record game in history
    game_record = {
        'clerk_user_id': user_id,
        'bot_rating': req.bot_rating,
        'player_color': req.player_color,
        'result': req.result,
        'result_reason': req.result_reason,
        'move_count': req.move_count,
        'time_control': req.time_control,
        'rating_before': current_rating,
        'rating_after': rating_update.new_rating,
        'rating_change': rating_update.rating_change,
        'played_at': now,
        'game_moves': [m.model_dump() for m in req.game_moves] if req.game_moves else None
    }
    supabase.table('game_history').insert(game_record).execute()

    logger.info(f"Rating update for user {user_id}: {current_rating} -> {rating_update.new_rating} "
                f"({rating_update.rating_change:+.1f}) after {req.result} vs {req.bot_rating}")

    return RatingUpdateResponse(
        new_rating=rating_update.new_rating,
        rating_change=rating_update.rating_change,
        new_rd=rating_update.new_rd,
        rating_tier=get_rating_tier(rating_update.new_rating)
    )


@app.put('/rating/edit', response_model=UserRatingResponse)
async def edit_user_rating(req: RatingEditRequest, user: dict = Depends(get_current_user)):
    """Manually edit user's rating (for user self-adjustment)."""
    user_id = user.get('user_id')
    if not user_id:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="User ID not found in token"
        )

    # Get current rating (creates if not exists)
    user_rating = await get_or_create_user_rating(user_id)

    supabase = get_supabase()
    now = datetime.now(timezone.utc).isoformat()

    # Update rating (reset RD to higher value when manually editing)
    # This reflects increased uncertainty after manual adjustment
    new_rd = min(INITIAL_RD, float(user_rating['rating_deviation']) + 50)

    result = supabase.table('user_ratings').update({
        'rating': req.new_rating,
        'rating_deviation': new_rd,
        'updated_at': now
    }).eq('clerk_user_id', user_id).execute()

    if not result.data or len(result.data) == 0:
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Failed to update rating"
        )

    updated_rating = result.data[0]

    logger.info(f"Manual rating edit for user {user_id}: {user_rating['rating']} -> {req.new_rating}")

    return UserRatingResponse(
        rating=float(updated_rating['rating']),
        rating_deviation=float(updated_rating['rating_deviation']),
        games_played=int(updated_rating['games_played']),
        wins=int(updated_rating['wins']),
        losses=int(updated_rating['losses']),
        draws=int(updated_rating['draws']),
        rating_tier=get_rating_tier(float(updated_rating['rating']))
    )


@app.get('/rating/history', response_model=GameHistoryResponse)
async def get_game_history(
    limit: int = 20,
    offset: int = 0,
    user: dict = Depends(get_current_user)
):
    """Get user's game history with rating changes."""
    user_id = user.get('user_id')
    if not user_id:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="User ID not found in token"
        )

    # Clamp limit to reasonable range
    limit = min(max(1, limit), 100)
    offset = max(0, offset)

    supabase = get_supabase()

    # Get total count
    count_result = supabase.table('game_history').select('id', count='exact').eq('clerk_user_id', user_id).execute()
    total_count = count_result.count or 0

    # Get games with pagination
    games_result = supabase.table('game_history').select('*').eq(
        'clerk_user_id', user_id
    ).order('played_at', desc=True).range(offset, offset + limit - 1).execute()

    games = [
        GameHistoryItem(
            id=str(g['id']),
            bot_rating=int(g['bot_rating']),
            player_color=g['player_color'],
            result=g['result'],
            result_reason=g.get('result_reason'),
            move_count=g.get('move_count'),
            time_control=g.get('time_control'),
            rating_before=float(g['rating_before']),
            rating_after=float(g['rating_after']),
            rating_change=float(g['rating_change']),
            played_at=g['played_at'],
            game_moves=g.get('game_moves')
        )
        for g in (games_result.data or [])
    ]

    return GameHistoryResponse(
        games=games,
        total_count=total_count
    )


@app.get('/rating/game/{game_id}', response_model=GameHistoryItem)
async def get_game_by_id(game_id: str, user: dict = Depends(get_current_user)):
    """Get a single game by ID for the current user."""
    user_id = user.get('user_id')
    if not user_id:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="User ID not found in token"
        )

    supabase = get_supabase()
    result = supabase.table('game_history').select('*').eq('id', game_id).eq('clerk_user_id', user_id).execute()

    if not result.data:
        raise HTTPException(status_code=404, detail="Game not found")

    g = result.data[0]
    return GameHistoryItem(
        id=str(g['id']),
        bot_rating=int(g['bot_rating']),
        player_color=g['player_color'],
        result=g['result'],
        result_reason=g.get('result_reason'),
        move_count=g.get('move_count'),
        time_control=g.get('time_control'),
        rating_before=float(g['rating_before']),
        rating_after=float(g['rating_after']),
        rating_change=float(g['rating_change']),
        played_at=g['played_at'],
        game_moves=g.get('game_moves')
    )


@app.get("/sentry-debug")
async def trigger_error():
    division_by_zero = 1 / 0
