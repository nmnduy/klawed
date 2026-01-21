# Klawed Sandbox Docker Image Build & Deploy Pipeline

This document describes how to build and deploy the klawed AI agent Docker image for the FileSurf sandbox environment.

## Overview

- **Build Host**: `fandalf` (192.168.1.156) - Debian 12, x86_64
- **Production Host**: `filesurf-0` (100.65.242.128) - Ubuntu 24.04, x86_64
- **Image Name**: `klawed-sandbox`
- **Image Size**: ~250MB (minimal)

The klawed sandbox image provides an isolated environment for running AI agents with:
- klawed binary with memvid support
- Python 3
- Git and SSH
- Essential CLI tools (jq, curl, wget, zip/unzip)
- Non-root user (`agent`) for security

## Quick Start

### Build and Deploy in One Command

```bash
# Build on fandalf, transfer to production, DO NOT update :latest tag
./deployment/klawed/build-klawed-sandbox.sh --tag v0.19.2
./deployment/klawed/deploy-klawed-sandbox.sh --tag v0.19.2

# Then deploy FileSurf with this version
./deployment/deploy-jvm.sh --image-version 0.19.2
```

**Important:** We no longer use `:latest` tag in production. Always specify a version tag for reproducibility.

### Or Use the Convenience Workflow

```bash
# Build and deploy together (without updating :latest)
./deployment/klawed/deploy-klawed-sandbox.sh --build --tag v0.19.2

# Then deploy FileSurf with this version
./deployment/deploy-jvm.sh --image-version 0.19.2
```

**Note:** The `--update-latest` option is deprecated. Always use specific version tags in production.

## Pipeline Components

### Files

```
deployment/klawed/
├── Dockerfile.sandbox           # Multi-stage Dockerfile (optimized for size)
├── build-klawed-sandbox.sh      # Build script (runs on local machine, builds on fandalf)
├── deploy-klawed-sandbox.sh     # Deploy script (transfers image to production)
└── README.md                    # This file
```

### Dockerfile.sandbox

Multi-stage Dockerfile optimized for minimal image size:

1. **Builder Stage**:
   - Installs build dependencies (gcc, make, Rust, etc.)
   - Builds memvid-ffi Rust library
   - Builds klawed with memvid support (`MEMVID=1 ZMQ=0`)

2. **Runtime Stage**:
   - Only runtime dependencies (no compilers)
   - Copies klawed binary and libmemvid_ffi.so
   - Creates non-root `agent` user
   - ~250MB final size

### build-klawed-sandbox.sh

Builds the Docker image on fandalf:

```bash
./build-klawed-sandbox.sh [options]

Options:
  --tag TAG       Docker tag (default: latest)
  --no-cache      Build without Docker cache
  -h, --help      Show help
```

**What it does:**
1. Updates klawed source on fandalf from local machine
2. Syncs Dockerfile.sandbox to fandalf
3. Runs `docker build` on fandalf
4. Verifies the build (version, memvid linkage)

### deploy-klawed-sandbox.sh

Deploys the image from fandalf to filesurf-0:

```bash
./deploy-klawed-sandbox.sh [options]

Options:
  --tag TAG         Docker tag to deploy (default: latest)
  --build           Build before deploying
  --update-latest   Also tag the deployed image as :latest
  --dry-run         Show what would be done without executing
  -h, --help        Show help
```

**What it does:**
1. Optionally builds the image first (`--build`)
2. Checks image exists on build host
3. Transfers image: `docker save | pigz | ssh | docker load`
4. Verifies deployment (version, memvid linkage)
5. Optionally updates `:latest` tag (`--update-latest`)

## Example Workflows

### Regular Update

Build and deploy a new version:

```bash
# 1. Build with a version tag
./deployment/klawed/build-klawed-sandbox.sh --tag v0.19.2

# 2. Deploy to production
./deployment/klawed/deploy-klawed-sandbox.sh --tag v0.19.2

# 3. Update FileSurf to use this version
./deployment/deploy-jvm.sh --image-version 0.19.2
```

### Force Rebuild (No Cache)

```bash
./deployment/klawed/build-klawed-sandbox.sh --tag v0.19.2 --no-cache
./deployment/klawed/deploy-klawed-sandbox.sh --tag v0.19.2
./deployment/deploy-jvm.sh --image-version 0.19.2
```

### Quick Deploy Only

If image already built on fandalf:

```bash
./deployment/klawed/deploy-klawed-sandbox.sh --tag v0.19.2
```

### Check Deployment (Dry Run)

```bash
./deployment/klawed/deploy-klawed-sandbox.sh --tag v0.19.2 --dry-run
```

## Verifying the Image

### On Build Host (fandalf)

```bash
ssh fandalf "docker images | grep klawed"
ssh fandalf "docker run --rm klawed-sandbox:latest --version"
```

### On Production (filesurf-0)

```bash
ssh filesurf-0 "docker images | grep klawed"
ssh filesurf-0 "docker run --rm klawed-sandbox:latest --version"

# Verify memvid library
ssh filesurf-0 'docker run --rm --entrypoint bash klawed-sandbox:latest -c "ldd /usr/local/bin/klawed | grep memvid"'
```

### Running klawed in Container

```bash
# Interactive session
ssh filesurf-0 'docker run -it --rm \
  -e OPENAI_API_KEY=$OPENAI_API_KEY \
  -v /path/to/workspace:/workspace \
  klawed-sandbox:latest'

# Non-interactive (with prompt)
ssh filesurf-0 'docker run --rm \
  -e OPENAI_API_KEY=$OPENAI_API_KEY \
  -v /path/to/workspace:/workspace \
  klawed-sandbox:latest -p "Hello, what can you do?"'
```

## Troubleshooting

### Build Fails

1. Check build logs on fandalf:
   ```bash
   ssh fandalf "docker build -f ~/git/klawed/Dockerfile.sandbox -t klawed-sandbox:test ~/git/klawed 2>&1 | tail -50"
   ```

2. Verify klawed source is up to date:
   ```bash
   ssh fandalf "cd ~/git/klawed && git log --oneline -3"
   ```

3. Check Rust/cargo availability:
   ```bash
   ssh fandalf "source ~/.cargo/env && cargo --version"
   ```

### Memvid Library Missing

If memvid is not linking properly:

1. Verify memvid-ffi built correctly:
   ```bash
   ssh fandalf "ls -la ~/git/klawed/vendor/memvid-ffi/target/release/libmemvid_ffi.so"
   ```

2. Check libmemvid in container:
   ```bash
   docker run --rm --entrypoint bash klawed-sandbox:latest -c "ls -la /usr/local/lib/"
   ```

3. Rebuild with `--no-cache`:
   ```bash
   ./build-klawed-sandbox.sh --tag test --no-cache
   ```

### Transfer Timeout

If image transfer is slow:

1. Check network connectivity between hosts
2. Ensure `pigz` is installed on both hosts for faster compression:
   ```bash
   ssh fandalf "sudo apt-get install -y pigz"
   ssh filesurf-0 "sudo apt-get install -y pigz"
   ```

### Container Fails to Start

1. Check Docker daemon:
   ```bash
   ssh filesurf-0 "systemctl status docker"
   ```

2. Check container logs:
   ```bash
   ssh filesurf-0 "docker logs <container_id>"
   ```

## Architecture Considerations

Both fandalf and filesurf-0 are x86_64 architecture, so no cross-compilation is needed. The image built on fandalf runs directly on filesurf-0.

If you need to support different architectures in the future:
1. Use Docker buildx for multi-arch builds
2. Or maintain separate build hosts per architecture

## Security Notes

- Container runs as non-root user `agent` (UID 1000)
- No privileged access required
- Workspace is mounted at `/workspace`
- API keys should be passed via environment variables, not baked into the image

## Image Size Optimization

The current image is ~250MB. Further optimization options:

1. **Use Alpine base** (not recommended due to glibc compatibility issues with klawed)
2. **Remove Python** if not needed (saves ~100MB)
3. **Static linking** of libmemvid (requires build changes)
4. **Use `--squash`** during build (requires experimental Docker features)
