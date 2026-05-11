"""
Supabase client initialization for the ChessMimic backend.
"""
import os
from functools import lru_cache
from pathlib import Path

from dotenv import load_dotenv
from supabase import create_client, Client

# Load .env file from backend directory
env_path = Path(__file__).parent / '.env'
load_dotenv(env_path)


class SupabaseConfig:
    """Configuration for Supabase connection."""

    def __init__(self):
        self.url = os.getenv("SUPABASE_URL")
        self.key = os.getenv("SUPABASE_KEY")

        if not self.url or not self.key:
            raise ValueError(
                "SUPABASE_URL and SUPABASE_KEY environment variables must be set. "
                "See backend/.env.example for required configuration."
            )


@lru_cache()
def get_supabase_config() -> SupabaseConfig:
    """Get cached Supabase configuration."""
    return SupabaseConfig()


@lru_cache()
def get_supabase_client() -> Client:
    """Get cached Supabase client instance."""
    config = get_supabase_config()
    return create_client(config.url, config.key)


def get_supabase() -> Client:
    """Dependency for FastAPI routes to get Supabase client."""
    return get_supabase_client()
