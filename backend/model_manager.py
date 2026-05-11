"""Model manager for loading and accessing chess models dynamically."""

from pathlib import Path
from model_registry import ModelRegistry, RatingRange, ModelFiles
from model_inference import ModelMovePredictor
from clock_inference import ClockPredictor
from winner_inference import WinnerPredictor
from logging_config import get_logger
import time

logger = get_logger(__name__)


class ModelManager:
    """Manager for loading and accessing chess models by rating."""

    def __init__(self, models_base_path: Path | str = "models"):
        """Initialize the model manager.

        Args:
            models_base_path: Base path to the models directory
        """
        self.registry = ModelRegistry(models_base_path)

        # Dictionaries to store loaded models, keyed by min_rating
        self.move_models: dict[int, ModelMovePredictor] = {}
        self.clock_models: dict[int, ClockPredictor] = {}
        self.winner_models: dict[int, WinnerPredictor] = {}

        # Cache mapping ratings to model keys for faster lookup
        self._move_model_cache: dict[int, int] = {}
        self._clock_model_cache: dict[int, int] = {}
        self._winner_model_cache: dict[int, int] = {}

        self._load_all_models()

    def _load_all_models(self):
        """Load all available models at startup."""
        logger.info("Loading all available models...")
        total_start = time.time()

        # Load move models
        move_start = time.time()
        for min_rating, (rating_range, model_files) in self.registry.move_models.items():
            try:
                start = time.time()
                predictor = ModelMovePredictor(
                    model_files.model_path,
                    model_files.scalers_path
                )
                load_time = (time.time() - start) * 1000
                self.move_models[min_rating] = predictor
                logger.info(f"Loaded move model {rating_range.min_rating}-{rating_range.max_rating} in {load_time:.0f}ms")
            except Exception as e:
                logger.error(f"Failed to load move model {rating_range.min_rating}-{rating_range.max_rating}: {e}", exc_info=True)

        move_time = (time.time() - move_start) * 1000
        logger.info(f"Loaded {len(self.move_models)} move models in {move_time:.0f}ms")

        # Load clock models
        clock_start = time.time()
        for min_rating, (rating_range, model_files) in self.registry.clock_models.items():
            try:
                start = time.time()
                predictor = ClockPredictor(
                    model_files.model_path,
                    model_files.scalers_path,
                    model_files.extra_files["clock_buckets"]
                )
                load_time = (time.time() - start) * 1000
                self.clock_models[min_rating] = predictor
                logger.info(f"Loaded clock model {rating_range.min_rating}-{rating_range.max_rating} in {load_time:.0f}ms")
            except Exception as e:
                logger.error(f"Failed to load clock model {rating_range.min_rating}-{rating_range.max_rating}: {e}", exc_info=True)

        clock_time = (time.time() - clock_start) * 1000
        logger.info(f"Loaded {len(self.clock_models)} clock models in {clock_time:.0f}ms")

        # Load winner models
        winner_start = time.time()
        for min_rating, (rating_range, model_files) in self.registry.winner_models.items():
            try:
                start = time.time()
                predictor = WinnerPredictor(
                    model_files.model_path,
                    model_files.scalers_path
                )
                load_time = (time.time() - start) * 1000
                self.winner_models[min_rating] = predictor
                logger.info(f"Loaded winner model {rating_range.min_rating}-{rating_range.max_rating} in {load_time:.0f}ms")
            except Exception as e:
                logger.error(f"Failed to load winner model {rating_range.min_rating}-{rating_range.max_rating}: {e}", exc_info=True)

        winner_time = (time.time() - winner_start) * 1000
        logger.info(f"Loaded {len(self.winner_models)} winner models in {winner_time:.0f}ms")

        total_time = (time.time() - total_start) * 1000
        logger.info(f"Total model loading time: {total_time:.0f}ms")

    def get_move_model(self, rating: int) -> ModelMovePredictor | None:
        """Get the best move model for the given rating.

        Args:
            rating: Player rating

        Returns:
            ModelMovePredictor instance or None if no models available
        """
        # Check cache first
        if rating in self._move_model_cache:
            model_key = self._move_model_cache[rating]
            return self.move_models.get(model_key)

        # Find best model using registry
        result = self.registry.get_best_model(rating, "move")
        if result is None:
            logger.warning(f"No move model available for rating {rating}")
            return None

        rating_range, _ = result
        model = self.move_models.get(rating_range.min_rating)

        # Cache the result
        self._move_model_cache[rating] = rating_range.min_rating

        if model and rating_range.distance_to(rating) > 0:
            logger.debug(f"Using move model {rating_range.min_rating}-{rating_range.max_rating} for rating {rating}")

        return model

    def get_clock_model(self, rating: int) -> ClockPredictor | None:
        """Get the best clock model for the given rating.

        Args:
            rating: Player rating

        Returns:
            ClockPredictor instance or None if no models available
        """
        # Check cache first
        if rating in self._clock_model_cache:
            model_key = self._clock_model_cache[rating]
            return self.clock_models.get(model_key)

        # Find best model using registry
        result = self.registry.get_best_model(rating, "clock")
        if result is None:
            logger.warning(f"No clock model available for rating {rating}")
            return None

        rating_range, _ = result
        model = self.clock_models.get(rating_range.min_rating)

        # Cache the result
        self._clock_model_cache[rating] = rating_range.min_rating

        if model and rating_range.distance_to(rating) > 0:
            logger.debug(f"Using clock model {rating_range.min_rating}-{rating_range.max_rating} for rating {rating}")

        return model

    def get_winner_model(self, rating: int) -> WinnerPredictor | None:
        """Get the best winner model for the given rating.

        Args:
            rating: Player rating (uses average of white/black ratings if both provided)

        Returns:
            WinnerPredictor instance or None if no models available
        """
        # Check cache first
        if rating in self._winner_model_cache:
            model_key = self._winner_model_cache[rating]
            return self.winner_models.get(model_key)

        # Find best model using registry
        result = self.registry.get_best_model(rating, "winner")
        if result is None:
            logger.warning(f"No winner model available for rating {rating}")
            return None

        rating_range, _ = result
        model = self.winner_models.get(rating_range.min_rating)

        # Cache the result
        self._winner_model_cache[rating] = rating_range.min_rating

        if model and rating_range.distance_to(rating) > 0:
            logger.debug(f"Using winner model {rating_range.min_rating}-{rating_range.max_rating} for rating {rating}")

        return model

    def get_model_info(self) -> dict:
        """Get information about loaded models.

        Returns:
            Dictionary with model counts and rating ranges
        """
        move_ranges = [
            f"{rr.min_rating}-{rr.max_rating}"
            for _, (rr, _) in sorted(self.registry.move_models.items())
        ]
        clock_ranges = [
            f"{rr.min_rating}-{rr.max_rating}"
            for _, (rr, _) in sorted(self.registry.clock_models.items())
        ]
        winner_ranges = [
            f"{rr.min_rating}-{rr.max_rating}"
            for _, (rr, _) in sorted(self.registry.winner_models.items())
        ]

        return {
            "move_models": {
                "count": len(self.move_models),
                "ranges": move_ranges
            },
            "clock_models": {
                "count": len(self.clock_models),
                "ranges": clock_ranges
            },
            "winner_models": {
                "count": len(self.winner_models),
                "ranges": winner_ranges
            }
        }
