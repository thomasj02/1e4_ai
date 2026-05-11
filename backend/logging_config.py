"""Logging configuration for the ChessMimic backend."""
import logging
import logging.handlers
import os
from pathlib import Path


def setup_logging():
    """Configure logging for the entire application."""
    # Create logs directory if it doesn't exist
    log_dir = Path("logs")
    log_dir.mkdir(exist_ok=True)
    
    # Configure root logger
    root_logger = logging.getLogger()
    root_logger.setLevel(logging.DEBUG)
    
    # Remove any existing handlers
    root_logger.handlers.clear()
    
    # Create formatters
    detailed_formatter = logging.Formatter(
        '%(asctime)s - %(name)s - %(levelname)s: %(message)s',
        datefmt='%Y-%m-%d %H:%M:%S'
    )
    
    # Console handler - INFO and above (or DEBUG if env var set)
    console_handler = logging.StreamHandler()
    console_level = logging.DEBUG if os.getenv('CHESSMIMIC_DEBUG', '').lower() in ['true', '1', 'yes'] else logging.INFO
    console_handler.setLevel(console_level)
    console_handler.setFormatter(detailed_formatter)
    root_logger.addHandler(console_handler)
    
    # File handler - DEBUG and above
    file_handler = logging.handlers.RotatingFileHandler(
        log_dir / "chessmimic.log",
        maxBytes=10 * 1024 * 1024,  # 10MB
        backupCount=5
    )
    file_handler.setLevel(logging.DEBUG)
    file_handler.setFormatter(detailed_formatter)
    root_logger.addHandler(file_handler)
    
    # Error file handler - ERROR and above
    error_handler = logging.handlers.RotatingFileHandler(
        log_dir / "chessmimic_errors.log",
        maxBytes=5 * 1024 * 1024,  # 5MB
        backupCount=3
    )
    error_handler.setLevel(logging.ERROR)
    error_handler.setFormatter(detailed_formatter)
    root_logger.addHandler(error_handler)
    
    # Suppress noisy loggers
    logging.getLogger("urllib3").setLevel(logging.WARNING)
    logging.getLogger("httpx").setLevel(logging.WARNING)
    
    return root_logger


def get_logger(name: str) -> logging.Logger:
    """Get a logger with the specified name."""
    return logging.getLogger(name)