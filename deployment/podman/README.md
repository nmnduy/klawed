# Klawed Agent Sandbox - Podman Container

This directory contains the Dockerfile and scripts for building a sandboxed container image for running the klawed AI agent binary.

## Overview

The klawed-sandbox container provides an isolated environment for running AI agent tasks with:
- Network access for API calls and package downloads
- Common development tools (git, python3, curl, etc.)
- Non-root execution for security
- Minimal attack surface using Debian slim base

## Prerequisites

- **Podman** installed and configured
- **klawed binary** - The klawed agent executable

## Building the Image

### Quick Start

```bash
# Make the build script executable (first time only)
chmod +x build-image.sh

# Build with klawed binary in default location
./build-image.sh

# Or specify the klawed binary path explicitly
./build-image.sh /path/to/klawed
```

### Manual Build

If you prefer to build manually:

```bash
# Copy klawed binary to this directory
cp /path/to/klawed ./klawed

# Build the image
podman build -t klawed-sandbox:latest .

# Clean up
rm ./klawed
```

## Running the Container

### Basic Run

```bash
# Run interactively
podman run --rm -it klawed-sandbox:latest

# Pass arguments to klawed
podman run --rm -it klawed-sandbox:latest --help
```

### With Mounted Workspace

Mount a local directory as the container's workspace:

```bash
# Mount current directory
podman run --rm -it \
    -v $(pwd):/workspace:Z \
    klawed-sandbox:latest

# Mount specific directory (read-write)
podman run --rm -it \
    -v /path/to/project:/workspace:Z \
    klawed-sandbox:latest

# Mount as read-only (safer)
podman run --rm -it \
    -v /path/to/project:/workspace:ro,Z \
    klawed-sandbox:latest
```

### With Environment Variables

Pass configuration via environment variables:

```bash
podman run --rm -it \
    -e ANTHROPIC_API_KEY="your-api-key" \
    -e OPENAI_API_KEY="your-api-key" \
    klawed-sandbox:latest
```

### With Network Restrictions

```bash
# No network access (fully isolated)
podman run --rm -it \
    --network=none \
    klawed-sandbox:latest

# Custom network
podman run --rm -it \
    --network=my-custom-network \
    klawed-sandbox:latest
```

### Full Example

```bash
podman run --rm -it \
    --name klawed-agent \
    -v $(pwd):/workspace:Z \
    -e ANTHROPIC_API_KEY="${ANTHROPIC_API_KEY}" \
    --memory=4g \
    --cpus=2 \
    klawed-sandbox:latest \
    --config /workspace/.klawed.toml
```

## Testing the Container

### Verify the Image

```bash
# Check image was built
podman images klawed-sandbox

# Inspect image details
podman inspect klawed-sandbox:latest

# Check image size
podman images --format "{{.Repository}}:{{.Tag}} {{.Size}}" klawed-sandbox
```

### Test Container Environment

```bash
# Start a shell in the container (bypassing entrypoint)
podman run --rm -it --entrypoint /bin/bash klawed-sandbox:latest

# Inside the container, verify tools are installed:
which python3 git curl jq
python3 --version
git --version

# Check user
whoami  # Should output: agent
id      # Should show uid=1000(agent)

# Test network connectivity
curl -s https://api.github.com | jq '.current_user_url'
```

### Test klawed Binary

```bash
# Test klawed runs correctly
podman run --rm klawed-sandbox:latest --version

# Test with a simple task
podman run --rm -it klawed-sandbox:latest --help
```

## Security Features

### Non-Root Execution

The container runs as user `agent` (UID 1000), not as root. This provides:
- Reduced impact if the container is compromised
- Better compatibility with rootless Podman
- Prevents accidental system modifications

### Minimal Base Image

Using `debian:bookworm-slim`:
- Significantly smaller than full Debian (~75MB vs ~300MB)
- Fewer packages = smaller attack surface
- Regular security updates from Debian stable

### No Privilege Escalation

The container doesn't include:
- sudo or su commands
- SUID binaries
- Capability grants

### Resource Limits (Recommended)

When running in production, apply resource limits:

```bash
podman run --rm -it \
    --memory=4g \
    --memory-swap=4g \
    --cpus=2 \
    --pids-limit=256 \
    --tmpfs /tmp:size=512m \
    -v $(pwd):/workspace:Z \
    klawed-sandbox:latest
```

Note: We don't use `--read-only` because the agent needs to create and edit files in the workspace.

### Network Isolation Options

| Option | Command | Use Case |
|--------|---------|----------|
| Full network | (default) | General use, API calls needed |
| No network | `--network=none` | Maximum isolation, offline tasks |
| Host network | `--network=host` | Debug only, not recommended |

### SELinux/AppArmor

Podman integrates with SELinux (on RHEL/Fedora) and AppArmor (on Ubuntu/Debian) for additional mandatory access control. The `:Z` flag on volume mounts ensures proper SELinux labeling.

## Installed Tools

| Tool | Purpose |
|------|---------|
| curl, wget | HTTP requests and downloads |
| git | Version control, cloning repositories |
| python3, pip | Scripting, data analysis |
| jq | JSON processing |
| openssh-client | Git over SSH, remote connections |
| zip, unzip | Archive handling |
| ca-certificates | HTTPS/TLS support |

## Customization

### Adding Additional Tools

Modify the `Dockerfile` to add more packages:

```dockerfile
RUN apt-get update && apt-get install -y --no-install-recommends \
    your-package-here \
    && rm -rf /var/lib/apt/lists/*
```

### Pre-installed Python Packages

To include Python packages in the image:

```dockerfile
# Add after the USER agent line
RUN pip3 install --user --no-cache-dir \
    requests \
    pandas \
    numpy
```

### Custom Entrypoint

For debugging or custom workflows, override the entrypoint:

```bash
# Use bash instead of klawed
podman run --rm -it --entrypoint /bin/bash klawed-sandbox:latest

# Use a custom script
podman run --rm -it \
    -v ./my-script.sh:/entrypoint.sh:ro \
    --entrypoint /entrypoint.sh \
    klawed-sandbox:latest
```

## Troubleshooting

### "Permission denied" on mounted volumes

```bash
# Use :Z flag for SELinux relabeling
podman run -v $(pwd):/workspace:Z klawed-sandbox:latest

# Or run with --userns=keep-id for rootless podman
podman run --userns=keep-id -v $(pwd):/workspace klawed-sandbox:latest
```

### Container can't access network

```bash
# Check if running with --network=none
# Check host firewall rules
# Test from inside container:
podman run --rm -it --entrypoint /bin/bash klawed-sandbox:latest
curl -v https://api.anthropic.com
```

### klawed binary not found

Ensure the binary was copied during build:
```bash
# Check if binary exists in image
podman run --rm --entrypoint /bin/ls klawed-sandbox:latest -la /usr/local/bin/klawed
```

### Out of memory

Apply memory limits and check if the task requires more resources:
```bash
podman run --rm -it --memory=8g klawed-sandbox:latest
```

## License

See the main project LICENSE file.
