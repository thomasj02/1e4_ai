"""
Authentication module for verifying Clerk JWT tokens.
"""
import base64
import os
import time
from typing import Any
from functools import lru_cache

import httpx
import jwt
from jwt.exceptions import InvalidTokenError, ExpiredSignatureError
from fastapi import Depends, HTTPException, status
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials


# Security scheme for Bearer tokens
security = HTTPBearer()

# Cache for JWKS (JSON Web Key Set)
_jwks_cache: dict[str, Any] | None = None
_jwks_cache_time: float = 0
JWKS_CACHE_DURATION = 300  # 5 minutes


def _env_bool(name: str, default: bool = False) -> bool:
    """Read a boolean environment variable."""
    value = os.getenv(name)
    if value is None:
        return default
    return value.strip().lower() in {"1", "true", "yes", "on"}


def _csv_env(name: str, default: list[str]) -> list[str]:
    """Read a comma-separated environment variable."""
    value = os.getenv(name)
    if not value:
        return default
    parsed = [item.strip().rstrip("/") for item in value.split(",") if item.strip()]
    return parsed or default


def _issuer_from_publishable_key(clerk_key: str) -> str | None:
    """Derive the Clerk issuer URL from a publishable key when possible."""
    if not clerk_key.startswith(("pk_test_", "pk_live_")):
        return None

    parts = clerk_key.split("_", 2)
    if len(parts) < 3:
        return None

    encoded = parts[2]
    padding = "=" * (-len(encoded) % 4)

    try:
        decoded = base64.urlsafe_b64decode(encoded + padding).decode("utf-8")
    except Exception:
        return None

    domain = decoded.rstrip("$").strip()
    if not domain:
        return None
    if domain.startswith(("http://", "https://")):
        return domain.rstrip("/")
    return f"https://{domain}".rstrip("/")


def _is_allowed_authorized_party(azp: str, allowed_origins: list[str]) -> bool:
    """Validate Clerk authorized party against configured frontend origins."""
    normalized = azp.rstrip("/")
    if normalized in allowed_origins:
        return True

    local_prefixes = (
        "http://localhost",
        "https://localhost",
        "http://127.0.0.1",
        "https://127.0.0.1",
    )
    return normalized.startswith(local_prefixes)


class AuthConfig:
    """Configuration for authentication."""
    
    def __init__(self):
        self.disable_auth = _env_bool("DISABLE_AUTH", False)
        self.frontend_url = (
            os.getenv("CLERK_FRONTEND_URL")
            or os.getenv("NEXT_PUBLIC_CLERK_AFTER_SIGN_IN_URL")
            or "http://localhost:3000"
        ).rstrip("/")
        self.allowed_authorized_parties = _csv_env(
            "CLERK_ALLOWED_AUTHORIZED_PARTIES",
            [self.frontend_url],
        )

        clerk_key = os.getenv("NEXT_PUBLIC_CLERK_PUBLISHABLE_KEY", "")
        self.issuer_url = (
            os.getenv("CLERK_ISSUER_URL", "").rstrip("/")
            or _issuer_from_publishable_key(clerk_key)
        )

        if not self.issuer_url:
            if self.disable_auth:
                self.issuer_url = "http://localhost"
            else:
                raise RuntimeError(
                    "CLERK_ISSUER_URL or NEXT_PUBLIC_CLERK_PUBLISHABLE_KEY is required "
                    "when DISABLE_AUTH is not true"
                )

        self.jwks_url = f"{self.issuer_url}/.well-known/jwks.json"


@lru_cache()
def get_auth_config() -> AuthConfig:
    """Get cached auth configuration."""
    return AuthConfig()


async def get_jwks() -> dict[str, Any]:
    """Fetch and cache JWKS from Clerk."""
    global _jwks_cache, _jwks_cache_time
    
    current_time = time.time()
    
    # Return cached JWKS if still valid
    if _jwks_cache and (current_time - _jwks_cache_time) < JWKS_CACHE_DURATION:
        return _jwks_cache
    
    # Fetch new JWKS
    config = get_auth_config()
    async with httpx.AsyncClient() as client:
        try:
            response = await client.get(config.jwks_url)
            response.raise_for_status()
            _jwks_cache = response.json()
            _jwks_cache_time = current_time
            return _jwks_cache
        except Exception as e:
            # If we have a cache, return it even if expired
            if _jwks_cache:
                return _jwks_cache
            raise HTTPException(
                status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
                detail=f"Failed to fetch JWKS: {str(e)}"
            )


def get_public_key(token: str, jwks: dict[str, Any]) -> Any:
    """Get the public key for verifying the token."""
    # Decode header without verification to get kid
    unverified_header = jwt.get_unverified_header(token)
    kid = unverified_header.get("kid")
    
    if not kid:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Token missing key ID"
        )
    
    # Find the key with matching kid
    for key in jwks.get("keys", []):
        if key.get("kid") == kid:
            # Convert JWK to PEM format for PyJWT
            return jwt.algorithms.RSAAlgorithm.from_jwk(key)
    
    raise HTTPException(
        status_code=status.HTTP_401_UNAUTHORIZED,
        detail="Public key not found"
    )


async def verify_token(credentials: HTTPAuthorizationCredentials = Depends(security)) -> dict[str, Any]:
    """
    Verify JWT token from Clerk.
    
    Returns the decoded token payload if valid.
    Raises HTTPException with 401 if invalid.
    """
    config = get_auth_config()
    
    # Skip verification if auth is disabled (for development/testing)
    if config.disable_auth:
        return {
            "sub": "test_user_id",
            "email": "test@example.com",
            "iss": config.issuer_url,
            "azp": config.frontend_url
        }
    
    token = credentials.credentials
    
    try:
        # Get JWKS
        jwks = await get_jwks()
        
        # Get public key
        public_key = get_public_key(token, jwks)
        
        # Verify and decode token
        payload = jwt.decode(
            token,
            public_key,
            algorithms=["RS256"],
            issuer=config.issuer_url,
            options={
                "verify_exp": True,
                "verify_iss": True,
                "verify_aud": False,  # Clerk doesn't always include aud
            }
        )
        
        # Additional validation for authorized party
        azp = payload.get("azp")
        if azp and not _is_allowed_authorized_party(azp, config.allowed_authorized_parties):
            raise HTTPException(
                status_code=status.HTTP_401_UNAUTHORIZED,
                detail="Invalid authorized party"
            )
        
        return payload
        
    except ExpiredSignatureError:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Token has expired",
            headers={"WWW-Authenticate": "Bearer"},
        )
    except InvalidTokenError as e:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail=f"Invalid token: {str(e)}",
            headers={"WWW-Authenticate": "Bearer"},
        )
    except Exception as e:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail=f"Authentication failed: {str(e)}",
            headers={"WWW-Authenticate": "Bearer"},
        )


# Dependency for protected routes
async def get_current_user(token_payload: dict = Depends(verify_token)) -> dict[str, Any]:
    """Get current user from token payload."""
    return {
        "user_id": token_payload.get("sub"),
        "email": token_payload.get("email"),
        "token_payload": token_payload
    }
