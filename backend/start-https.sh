#!/bin/bash

# Start the FastAPI application with HTTPS support
# You need SSL certificates first (use Let's Encrypt)

PORT=${PORT:-8443}
SSL_DOMAIN=${SSL_DOMAIN:-}

if [ -z "$SSL_DOMAIN" ]; then
    echo "Error: set SSL_DOMAIN to the certificate domain before starting HTTPS."
    echo "Example: SSL_DOMAIN=example.com ./start-https.sh"
    exit 1
fi

SSL_CERT_DIR=${SSL_CERT_DIR:-/etc/letsencrypt/live/$SSL_DOMAIN}

echo "Starting HTTPS server on port $PORT for domain $SSL_DOMAIN"

# First, get certificates using certbot standalone:
# sudo certbot certonly --standalone -d your-domain.com

# Then run with SSL certificates
python -m uvicorn main:app \
    --host 0.0.0.0 \
    --port $PORT \
    --ssl-keyfile "$SSL_CERT_DIR/privkey.pem" \
    --ssl-certfile "$SSL_CERT_DIR/fullchain.pem"
