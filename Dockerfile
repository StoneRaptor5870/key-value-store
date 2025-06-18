# Build stage
FROM gcc:latest AS builder

WORKDIR /app
COPY . .

# Build the application
RUN make clean && make

# Runtime stage
FROM debian:bookworm-slim

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

# Copy the built binary
COPY --from=builder /app/bin/kv-store /usr/local/bin/kv-store

# Create data directory
RUN mkdir -p /data

# Create startup script that uses PORT env var
RUN echo '#!/bin/bash\n\
PORT=${PORT:-8520}\n\
echo "Starting KV-Store on port $PORT"\n\
exec /usr/local/bin/kv-store -p "$PORT"' > /start.sh && chmod +x /start.sh

# Expose port (Render will override this)
EXPOSE $PORT

# Health check for Render
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
  CMD nc -z localhost ${PORT:-8520} || exit 1

CMD ["/start.sh"]