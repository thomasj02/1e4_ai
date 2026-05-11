"""Model registry for discovering and mapping chess models by rating range."""

from pathlib import Path
from dataclasses import dataclass
from logging_config import get_logger

logger = get_logger(__name__)


@dataclass
class ModelFiles:
    """Represents the files needed for a model."""
    model_path: Path
    scalers_path: Path
    extra_files: dict[str, Path]  # For clock_buckets.json, common_moves files, etc.

    def all_exist(self) -> bool:
        """Check if all required files exist."""
        if not self.model_path.exists():
            return False
        if not self.scalers_path.exists():
            return False
        for path in self.extra_files.values():
            if not path.exists():
                return False
        return True


@dataclass
class RatingRange:
    """Represents a rating range for a model."""
    min_rating: int
    max_rating: int
    directory_name: str

    def contains(self, rating: int) -> bool:
        """Check if this range contains the given rating."""
        return self.min_rating <= rating < self.max_rating

    def distance_to(self, rating: int) -> int:
        """Calculate distance from rating to this range (0 if inside range)."""
        if self.contains(rating):
            return 0
        if rating < self.min_rating:
            return self.min_rating - rating
        return rating - self.max_rating + 1  # +1 because max is exclusive

    def midpoint(self) -> float:
        """Get the midpoint of this rating range."""
        return (self.min_rating + self.max_rating) / 2


class ModelRegistry:
    """Registry for discovering and accessing chess models."""

    def __init__(self, models_base_path: Path | str = "models"):
        """Initialize the model registry.

        Args:
            models_base_path: Base path to the models directory
        """
        self.models_base_path = Path(models_base_path)
        self.move_models: dict[int, tuple[RatingRange, ModelFiles]] = {}
        self.clock_models: dict[int, tuple[RatingRange, ModelFiles]] = {}
        self.winner_models: dict[int, tuple[RatingRange, ModelFiles]] = {}

        self._discover_models()

    def _parse_rating_range(self, dir_name: str) -> RatingRange | None:
        """Parse rating range from directory name.

        Args:
            dir_name: Directory name like "1800_1900_brier"

        Returns:
            RatingRange object or None if parsing fails
        """
        parts = dir_name.split('_')
        if len(parts) < 2:
            return None

        try:
            min_rating = int(parts[0])
            max_rating = int(parts[1])
            return RatingRange(min_rating, max_rating, dir_name)
        except ValueError:
            return None

    def _find_common_moves_file(self, model_dir: Path) -> Path | None:
        """Find common moves file in model directory.

        Tries multiple patterns:
        - common_moves.jsonl.gz
        - common_moves.txt.gz
        - common_moves.txt
        - common_moves.*.jsonl.gz (with timestamp)

        Args:
            model_dir: Model directory to search

        Returns:
            Path to common moves file or None if not found
        """
        # Try exact matches first
        for filename in ["common_moves.jsonl.gz", "common_moves.txt.gz", "common_moves.txt"]:
            path = model_dir / filename
            if path.exists():
                return path

        # Try glob patterns
        for pattern in ["common_moves.*.jsonl.gz", "common_moves.*.txt.gz"]:
            matches = list(model_dir.glob(pattern))
            if matches:
                return matches[0]  # Return first match

        return None

    def _discover_move_models(self):
        """Discover available move models."""
        move_model_dir = self.models_base_path / "move_model"
        if not move_model_dir.exists():
            logger.warning(f"Move model directory not found: {move_model_dir}")
            return

        for model_dir in move_model_dir.iterdir():
            if not model_dir.is_dir():
                continue

            rating_range = self._parse_rating_range(model_dir.name)
            if not rating_range:
                logger.debug(f"Skipping directory with invalid name: {model_dir.name}")
                continue

            model_path = model_dir / "model.ckpt"
            scalers_path = model_dir / "scalers.pkl"
            common_moves_path = self._find_common_moves_file(model_dir)

            if not model_path.exists() or not scalers_path.exists():
                logger.warning(f"Missing required files in {model_dir.name}")
                continue

            extra_files = {}
            if common_moves_path:
                extra_files["common_moves"] = common_moves_path
            else:
                logger.warning(f"No common_moves file found in {model_dir.name}")

            model_files = ModelFiles(model_path, scalers_path, extra_files)
            self.move_models[rating_range.min_rating] = (rating_range, model_files)
            logger.info(f"Discovered move model: {rating_range.min_rating}-{rating_range.max_rating}")

    def _discover_clock_models(self):
        """Discover available clock models."""
        clock_model_dir = self.models_base_path / "clock_model"
        if not clock_model_dir.exists():
            logger.warning(f"Clock model directory not found: {clock_model_dir}")
            return

        for model_dir in clock_model_dir.iterdir():
            if not model_dir.is_dir():
                continue

            rating_range = self._parse_rating_range(model_dir.name)
            if not rating_range:
                logger.debug(f"Skipping directory with invalid name: {model_dir.name}")
                continue

            model_path = model_dir / "model.ckpt"
            scalers_path = model_dir / "scalers.pkl"
            buckets_path = model_dir / "clock_buckets.json"

            if not model_path.exists() or not scalers_path.exists() or not buckets_path.exists():
                logger.warning(f"Missing required files in {model_dir.name}")
                continue

            model_files = ModelFiles(
                model_path,
                scalers_path,
                {"clock_buckets": buckets_path}
            )
            self.clock_models[rating_range.min_rating] = (rating_range, model_files)
            logger.info(f"Discovered clock model: {rating_range.min_rating}-{rating_range.max_rating}")

    def _discover_winner_models(self):
        """Discover available winner models."""
        winner_model_dir = self.models_base_path / "winner_model"
        if not winner_model_dir.exists():
            logger.warning(f"Winner model directory not found: {winner_model_dir}")
            return

        for model_dir in winner_model_dir.iterdir():
            if not model_dir.is_dir():
                continue

            rating_range = self._parse_rating_range(model_dir.name)
            if not rating_range:
                logger.debug(f"Skipping directory with invalid name: {model_dir.name}")
                continue

            model_path = model_dir / "model.ckpt"
            scalers_path = model_dir / "scalers.pkl"

            if not model_path.exists() or not scalers_path.exists():
                logger.warning(f"Missing required files in {model_dir.name}")
                continue

            model_files = ModelFiles(model_path, scalers_path, {})
            self.winner_models[rating_range.min_rating] = (rating_range, model_files)
            logger.info(f"Discovered winner model: {rating_range.min_rating}-{rating_range.max_rating}")

    def _discover_models(self):
        """Discover all available models."""
        logger.info(f"Discovering models in {self.models_base_path}")
        self._discover_move_models()
        self._discover_clock_models()
        self._discover_winner_models()

        logger.info(f"Model discovery complete: {len(self.move_models)} move models, "
                   f"{len(self.clock_models)} clock models, {len(self.winner_models)} winner models")

    def get_best_model(self, rating: int, model_type: str) -> tuple[RatingRange, ModelFiles] | None:
        """Get the best model for the given rating.

        Args:
            rating: Player rating
            model_type: Type of model ("move", "clock", or "winner")

        Returns:
            Tuple of (RatingRange, ModelFiles) for the best matching model, or None if no models available
        """
        if model_type == "move":
            models = self.move_models
        elif model_type == "clock":
            models = self.clock_models
        elif model_type == "winner":
            models = self.winner_models
        else:
            raise ValueError(f"Invalid model type: {model_type}")

        if not models:
            return None

        # Find model with minimum distance to the rating
        best_key = None
        best_distance = float('inf')

        for key, (rating_range, _) in models.items():
            distance = rating_range.distance_to(rating)
            if distance < best_distance:
                best_distance = distance
                best_key = key

        if best_key is None:
            return None

        rating_range, model_files = models[best_key]

        if best_distance > 0:
            logger.debug(f"No exact {model_type} model match for rating {rating}, "
                        f"using {rating_range.min_rating}-{rating_range.max_rating} "
                        f"(distance: {best_distance})")

        return rating_range, model_files

    def get_all_common_moves_files(self) -> list[Path]:
        """Get all common moves files from all move models.

        Returns:
            List of paths to common moves files
        """
        paths = []
        for _, model_files in self.move_models.values():
            if "common_moves" in model_files.extra_files:
                paths.append(model_files.extra_files["common_moves"])
        return paths
