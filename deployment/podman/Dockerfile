# Klawed Agent Sandbox Container
# Minimal Debian-based image for running the klawed AI agent binary
#
# Build: podman build -t klawed-sandbox:latest .
# Run:   podman run --rm -it klawed-sandbox:latest

FROM debian:bookworm-slim

LABEL maintainer="FileSurf Team"
LABEL description="Sandbox container for running the klawed AI agent"
LABEL version="1.0"

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    # Network utilities
    curl \
    wget \
    ca-certificates \
    # Version control
    git \
    openssh-client \
    # Python runtime
    python3 \
    python3-pip \
    python3-venv \
    # JSON processing
    jq \
    # Archive utilities
    unzip \
    zip \
    # Clean up apt cache to reduce image size
    && rm -rf /var/lib/apt/lists/* \
    && apt-get clean

# Create non-root user 'agent' with UID 1000
RUN useradd -m -u 1000 -s /bin/bash agent

# Copy the klawed binary
COPY klawed /usr/local/bin/klawed
RUN chmod +x /usr/local/bin/klawed

# Create workspace directory and set ownership
RUN mkdir -p /workspace && chown agent:agent /workspace

# Set working directory
WORKDIR /workspace

# Switch to non-root user
USER agent

# Configure git for the agent user (prevents warnings)
RUN git config --global init.defaultBranch main \
    && git config --global user.email "agent@sandbox.local" \
    && git config --global user.name "Klawed Agent"

# Entry point is the klawed binary
ENTRYPOINT ["/usr/local/bin/klawed"]
