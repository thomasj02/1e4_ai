"""Common moves database for chess positions."""

from pathlib import Path
import orjson
import json
import gzip
import random
import chess
import time
from typing import Any
from logging_config import get_logger

logger = get_logger(__name__)


class CommonMovesDB:
    """Database of common moves loaded from JSONL file."""
    
    def __init__(self, filepath: Path | None = None):
        self.common_moves: dict[str, dict[str, int]] = {}
        self.fen_only_moves: dict[str, dict[str, int]] = {}
        self.fen_positions_full: dict[str, dict[str, int]] = {}  # FENs from common_positions_full.jsonl.gz
        if filepath and filepath.exists():
            self.load_from_file(filepath)
    
    def load_from_file(self, filepath: Path):
        """Load common moves from JSONL file (supports .gz files)."""
        self.fen_only_moves: dict[str, dict[str, int]] = {}  # Additional index by FEN only
        
        # Open file with gzip if it's a .gz file, otherwise regular open
        open_func = gzip.open if filepath.suffix == '.gz' else open
        mode = 'rt' if filepath.suffix == '.gz' else 'r'
        
        start_time = time.time()
        line_count = 0
        
        with open_func(filepath, mode) as f:
            for line_num, line in enumerate(f, 1):
                line = line.strip()
                if not line:
                    continue
                line_count += 1
                try:
                    data = orjson.loads(line)
                    fen_key = data['fen']  # This includes move history
                    moves = data['moves']
                    
                    # Store with full key (including move history)
                    self.common_moves[fen_key] = moves
                    
                    # Also store by FEN only for fallback
                    # Parse the JSON key to extract move history and FEN
                    try:
                        parsed_key = orjson.loads(fen_key)
                        if isinstance(parsed_key, list) and len(parsed_key) == 2:
                            move_history, fen_only = parsed_key
                            
                            # Merge moves if this FEN already exists
                            if fen_only in self.fen_only_moves:
                                for move, count in moves.items():
                                    self.fen_only_moves[fen_only][move] = \
                                        self.fen_only_moves[fen_only].get(move, 0) + count
                            else:
                                self.fen_only_moves[fen_only] = moves.copy()
                    except orjson.JSONDecodeError:
                        # If it's not JSON, treat it as a plain FEN
                        self.fen_only_moves[fen_key] = moves
                        
                except Exception as e:
                    logger.error(f"Error parsing line {line_num} in {filepath}: {e}")
        
        load_time = (time.time() - start_time) * 1000
        logger.info(f"Loaded {line_count:,} positions from {filepath.name} in {load_time:.0f}ms")

    @staticmethod
    def strip_fen_clocks(fen: str) -> str:
        """Strip move clocks from FEN string.

        Removes the halfmove clock and fullmove number from the end of a FEN string.

        Args:
            fen: Full FEN string (e.g., "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")

        Returns:
            FEN without move clocks (e.g., "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -")
        """
        parts = fen.split()
        if len(parts) >= 4:
            # Keep only position, active color, castling rights, and en passant
            return ' '.join(parts[:4])
        return fen

    def load_positions_full(self, filepath: Path):
        """Load FEN-only positions from common_positions_full.jsonl.gz."""
        if not filepath.exists():
            logger.warning(f"Positions file not found: {filepath}")
            return
            
        # Open file with gzip if it's a .gz file
        open_func = gzip.open if filepath.suffix == '.gz' else open
        mode = 'rt' if filepath.suffix == '.gz' else 'r'
        
        start_time = time.time()
        count = 0
        with open_func(filepath, mode) as f:
            for line_num, line in enumerate(f, 1):
                line = line.strip()
                if not line:
                    continue
                try:
                    data = orjson.loads(line)
                    fen = data['fen']
                    moves = data['moves']
                    
                    # Store the position
                    self.fen_positions_full[fen] = moves
                    count += 1
                    
                except Exception as e:
                    logger.error(f"Error parsing line {line_num} in {filepath}: {e}")
        
        load_time = (time.time() - start_time) * 1000
        logger.info(f"Loaded {count:,} positions from {filepath.name} in {load_time:.0f}ms")
    
    @staticmethod
    def convert_san_to_uci(san_moves: list[str]) -> list[str] | None:
        """Convert a list of SAN moves to UCI notation.
        
        Args:
            san_moves: List of moves in SAN notation (e.g., ["e4", "e5", "Nf3"])
            
        Returns:
            List of moves in UCI notation (e.g., ["e2e4", "e7e5", "g1f3"]) or None if conversion fails.
        """
        try:
            board = chess.Board()
            uci_moves = []
            for san_move in san_moves:
                move = board.push_san(san_move)
                uci_moves.append(move.uci())
            return uci_moves
        except Exception:
            return None
    
    def get_moves(self, fen: str, move_history: list[str] | None = None, move_format: str = "uci") -> tuple[dict[str, int] | None, str]:
        """Get move distribution for a given FEN position.
        
        First tries with move history, then falls back to FEN only.
        
        Args:
            fen: The FEN position to look up
            move_history: Optional list of moves leading to this position
            move_format: Format of move_history - "uci" or "san" (default: "uci")
            
        Returns:
            A tuple of (moves_dict, strategy_used).
        """
        # Try with move history first
        if move_history is not None:
            # Convert SAN to UCI if needed
            if move_format == "san":
                uci_history = self.convert_san_to_uci(move_history)
                if uci_history is None:
                    # Conversion failed, skip to FEN-only lookup
                    move_history = None
                else:
                    move_history = uci_history
            
            if move_history is not None:
                # Create the key in the same format as stored (compact JSON)
                key_with_history = json.dumps([move_history, fen], separators=(',', ':'))
                if key_with_history in self.common_moves:
                    return self.common_moves[key_with_history], "FEN+moves"
        
        # Fallback to FEN only from common_positions_full.jsonl.gz
        stripped_fen = self.strip_fen_clocks(fen)
        if stripped_fen in self.fen_positions_full:
            return self.fen_positions_full[stripped_fen], "FEN only (positions_full)"
        
        # Fallback to FEN only from common_moves.jsonl.gz
        if fen in self.fen_only_moves:
            return self.fen_only_moves[fen], "FEN only"
        
        # Final fallback - try the key directly
        moves = self.common_moves.get(fen)
        if moves:
            return moves, "direct FEN"
        
        return None, "not found"
    
    def sample_move(self, fen: str, move_history: list[str] | None = None, move_format: str = "uci") -> tuple[str | None, str, dict[str, Any] | None]:
        """Sample a move probabilistically from the distribution.
        
        Args:
            fen: The FEN position to look up
            move_history: Optional list of moves leading to this position
            move_format: Format of move_history - "uci" or "san" (default: "uci")
        
        Returns:
            A tuple of (move, strategy_used, info_dict).
        """
        moves, strategy = self.get_moves(fen, move_history, move_format)
        if not moves:
            return None, strategy, None
        
        # Convert to lists for weighted random selection
        move_list = list(moves.keys())
        weights = list(moves.values())
        
        # Sample according to the distribution
        move = random.choices(move_list, weights=weights, k=1)[0]
        
        # Calculate probability and other info
        total_count = sum(weights)
        move_count = moves[move]
        probability = move_count / total_count if total_count > 0 else 0.0
        
        info = {
            'probability': probability,
            'move_count': move_count,
            'total_count': total_count,
            'num_moves': len(moves)
        }
        
        return move, strategy, info


def find_common_moves_file(search_paths: list[Path] | None = None) -> Path | None:
    """Find the common moves file in standard locations.
    
    Args:
        search_paths: List of paths to search. If None, uses default paths.
        
    Returns:
        Path to the file if found, None otherwise.
    """
    if search_paths is None:
        # Default search paths, prefer gzipped files
        search_paths = [
            Path("common_moves.jsonl.gz"),
            Path("common_moves.jsonl"),
            Path("backend/common_moves.jsonl.gz"),
            Path("backend/common_moves.jsonl"),
            Path("../common_moves.jsonl.gz"),
            Path("../common_moves.jsonl"),
            Path("../Training/common_moves.jsonl.gz"),
            Path("../Training/common_moves.jsonl"),
        ]
    
    for path in search_paths:
        if path.exists():
            return path
    
    return None


def load_common_moves_db(search_paths: list[Path] | None = None) -> CommonMovesDB:
    """Load the common moves database from file(s).

    Args:
        search_paths: List of paths to load. If None, uses default search paths to find a single file.

    Returns:
        CommonMovesDB instance (empty if no file found).
    """
    db = CommonMovesDB()

    # If search_paths provided, load all of them
    if search_paths is not None:
        if not search_paths:
            logger.warning("Empty search_paths provided, no common moves will be loaded")
            return db

        logger.info(f"Loading common moves from {len(search_paths)} file(s)...")
        total_start = time.time()

        for path in search_paths:
            if path.exists():
                db.load_from_file(path)
            else:
                logger.warning(f"Common moves file not found: {path}")

        total_time = (time.time() - total_start) * 1000
        logger.info(f"Loaded {len(db.common_moves):,} positions with move history in {total_time:.0f}ms")
        logger.info(f"Loaded {len(db.fen_only_moves):,} unique positions (FEN only)")

        # Also try to load common_positions_full.jsonl.gz
        positions_full_path = Path("common_positions_full.jsonl.gz")
        if not positions_full_path.exists():
            positions_full_path = Path("common_positions_full.jsonl")

        if positions_full_path.exists():
            db.load_positions_full(positions_full_path)
        else:
            logger.debug("common_positions_full.jsonl.gz not found, FEN-only fallback will use primary database")

        return db

    # Otherwise, use default search behavior (single file)
    path = find_common_moves_file(search_paths)

    if path:
        logger.info(f"Loading common moves from: {path}")
        db.load_from_file(path)
        logger.info(f"Loaded {len(db.common_moves):,} positions with move history")
        logger.info(f"Loaded {len(db.fen_only_moves):,} unique positions (FEN only)")

        # Also try to load common_positions_full.jsonl.gz
        positions_full_path = Path("common_positions_full.jsonl.gz")
        if not positions_full_path.exists():
            positions_full_path = Path("common_positions_full.jsonl")

        if positions_full_path.exists():
            db.load_positions_full(positions_full_path)
        else:
            logger.info("common_positions_full.jsonl.gz not found, FEN-only fallback will use primary database")

        return db
    else:
        logger.warning("No common_moves file found, will use model predictions only")
        return db