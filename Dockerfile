# klawed Docker Image - AI coding agent with comprehensive toolset
# Compatible with Docker and Podman
# Build: docker build -t klawed:latest .
# Run: docker run -it --rm -e OPENAI_API_KEY=$OPENAI_API_KEY klawed:latest

FROM debian:12-slim

LABEL maintainer="klawed project"
LABEL description="klawed AI coding agent with memvid support and comprehensive development tools"
LABEL org.opencontainers.image.source="https://github.com/klawed/klawed"

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# Install system dependencies and development tools
RUN apt-get update && apt-get install -y --no-install-recommends \
    # Build essentials
    build-essential \
    gcc \
    clang \
    make \
    cmake \
    pkg-config \
    git \
    ca-certificates \
    \
    # klawed C dependencies
    libcurl4-openssl-dev \
    libcjson-dev \
    libsqlite3-dev \
    libssl-dev \
    libbsd-dev \
    libncursesw5-dev \
    \
    # Rust and Cargo (for memvid-ffi)
    curl \
    \
    # Python3 runtime and pip
    python3 \
    python3-pip \
    \
    # Node.js and npm
    nodejs \
    npm \
    \
    # OCR tools
    tesseract-ocr \
    tesseract-ocr-eng \
    \
    # PDF tools
    poppler-utils \
    ghostscript \
    \
    # LaTeX full distribution
    texlive-full \
    \
    # JSON/HTML processing
    jq \
    sqlite3 \
    \
    # Archive utilities
    zip \
    unzip \
    \
    # Additional utilities
    curl \
    wget \
    file \
    && rm -rf /var/lib/apt/lists/*

# Install Rust and Cargo using rustup (more reliable than apt)
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --default-toolchain stable
ENV PATH="/root/.cargo/bin:${PATH}"

# Note: htmq removed as it's not available in crates.io and not used by klawed

# Set working directory
WORKDIR /build

# Copy the entire project source
COPY . .

# Build memvid-ffi Rust library first
RUN cd vendor/memvid-ffi && \
    cargo build --release && \
    ls -lh target/release/libmemvid_ffi.a && \
    ls -lh target/release/libmemvid_ffi.so

# Build klawed with memvid support enabled (without ZMQ to avoid format string issues)
RUN make clean && \
    make MEMVID=1 ZMQ=0 && \
    ls -lh build/klawed

# Install klawed and memvid shared library to system paths
RUN cp build/klawed /usr/local/bin/klawed && \
    chmod +x /usr/local/bin/klawed && \
    cp vendor/memvid-ffi/target/release/libmemvid_ffi.so /usr/local/lib/ && \
    ldconfig

# Create working directory for user code
RUN mkdir -p /workspace && \
    chmod 777 /workspace

# Clean up build artifacts to reduce image size
RUN cargo cache --autoclean || true && \
    rm -rf /build && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

# Set working directory to workspace
WORKDIR /workspace

# Environment variables
ENV KLAWED_LOG_LEVEL=INFO
ENV KLAWED_LOG_PATH=/workspace/.klawed/logs/klawed.log
ENV KLAWED_DB_PATH=/workspace/.klawed/api_calls.db
ENV KLAWED_MEMORY_PATH=/workspace/.klawed/memory.mv2

# Create .klawed directory structure
RUN mkdir -p /workspace/.klawed/logs

# Verify installations
RUN echo "=== Installed Tools ===" && \
    echo "Python: $(python3 --version)" && \
    echo "Rust: $(rustc --version)" && \
    echo "Cargo: $(cargo --version)" && \
    echo "Node.js: $(node --version)" && \
    echo "npm: $(npm --version)" && \
    echo "Tesseract: $(tesseract --version | head -1)" && \
    echo "pdfinfo: $(pdfinfo -v 2>&1 | head -1)" && \
    echo "gs: $(gs --version)" && \
    echo "latex: $(latex --version | head -1)" && \
    echo "jq: $(jq --version)" && \
    echo "sqlite3: $(sqlite3 --version)" && \
    echo "curl: $(curl --version | head -1)" && \
    echo "klawed: $(klawed --version 2>&1 || echo 'installed')" && \
    echo "======================"

# Health check to verify klawed can run
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD klawed --help > /dev/null 2>&1 || exit 1

# Set entrypoint to klawed
ENTRYPOINT ["/usr/local/bin/klawed"]

# Default command shows help
CMD ["--help"]
