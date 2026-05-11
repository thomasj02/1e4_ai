#!/bin/bash

# Start the FastAPI application with uvicorn
# Bind to 0.0.0.0 to accept connections from outside the container
# Use PORT environment variable if set, otherwise default to 8000

PORT=${PORT:-8000}
echo "Starting server on port $PORT"

python -m uvicorn main:app --host 0.0.0.0 --port $PORT