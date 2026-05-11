import sys
import os
from pathlib import Path

# Add the parent directory to sys.path to allow importing from backend
backend_dir = Path(__file__).parent.parent
sys.path.insert(0, str(backend_dir))