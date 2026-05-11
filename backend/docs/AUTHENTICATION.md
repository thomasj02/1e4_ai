# Backend Authentication

This document describes how authentication is implemented in the ChessMimic backend API.

## Overview

The backend uses JWT (JSON Web Token) authentication to verify that requests are coming from authenticated users. JWTs are issued by Clerk on the frontend and verified by the backend using Clerk's public keys.

## How It Works

1. **Frontend Authentication**: Users authenticate through Clerk on the frontend
2. **Token Generation**: Clerk issues a JWT token when requested by the frontend
3. **API Request**: Frontend includes the JWT in the `Authorization: Bearer <token>` header
4. **Token Verification**: Backend verifies the JWT using Clerk's public key
5. **Access Control**: Authenticated requests proceed, unauthenticated requests get 401

## JWT Details

### Token Format
- **Algorithm**: RS256 (RSA signature with SHA-256)
- **Issuer**: `https://<your-clerk-instance>.clerk.accounts.dev`
- **Expiration**: 60 seconds (default)
- **Claims**:
  - `sub`: User ID
  - `exp`: Expiration timestamp
  - `iat`: Issued at timestamp
  - `iss`: Issuer URL
  - `azp`: Authorized party (your frontend URL)

### Verification Process
1. Extract token from `Authorization: Bearer <token>` header
2. Fetch Clerk's JWKS (JSON Web Key Set) from `/.well-known/jwks.json`
3. Verify token signature using the appropriate public key
4. Validate standard claims (exp, iss, azp)
5. Return user information if valid

## Implementation

### Dependencies
```python
# Required packages
PyJWT==2.8.0        # JWT encoding/decoding
cryptography==42.0.5  # RSA key handling
httpx==0.27.0       # HTTP client for JWKS
```

### Environment Variables
```env
# Clerk configuration
CLERK_ISSUER_URL=https://your-instance.clerk.accounts.dev
CLERK_FRONTEND_URL=http://localhost:3000
CLERK_ALLOWED_AUTHORIZED_PARTIES=http://localhost:3000
```

Alternatively, the backend can derive the issuer from
`NEXT_PUBLIC_CLERK_PUBLISHABLE_KEY`. There is no project-specific fallback
issuer in the public release.

### Protected Endpoints
All chess-related endpoints require authentication:
- `POST /get_move` - Get AI move
- `POST /evaluate_position` - Evaluate board position

### Error Responses
- `401 Unauthorized` - Missing or invalid token
- `403 Forbidden` - Valid token but insufficient permissions (future use)

## Testing

### With Authentication Enabled
```bash
# Get token from frontend
const token = await getToken()

# Make authenticated request
curl -X POST http://localhost:8000/get_move \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"fen": "...", "clock_data": {...}}'
```

### With Authentication Disabled
For development/testing, set:
```env
DISABLE_AUTH=true
```

## Security Considerations

1. **Token Expiration**: Tokens expire after 60 seconds, frontend should refresh
2. **JWKS Caching**: Public keys are cached for 5 minutes to reduce API calls
3. **HTTPS**: In production, always use HTTPS to prevent token interception
4. **CORS**: Configure `BACKEND_CORS_ORIGINS` to only allow your frontend domain
5. **Rate Limiting**: Consider adding rate limiting per user (future enhancement)
