#!/bin/bash
# Run FileSurf with Podman sandbox mode enabled
# This is useful for testing container isolation locally

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_DIR"

# Check if Podman is available
if ! command -v podman &> /dev/null; then
    echo "ERROR: Podman is not installed."
    echo "Run ./scripts/setup-podman-dev.sh to set up Podman"
    exit 1
fi

# Check if Podman machine is running (macOS)
if [[ "$(uname)" == "Darwin" ]]; then
    if ! podman machine list 2>/dev/null | grep -q "Currently running"; then
        echo "Starting Podman machine..."
        podman machine start
    fi
fi

# Check if klawed-sandbox image exists
if ! podman image exists klawed-sandbox:latest 2>/dev/null; then
    echo "ERROR: klawed-sandbox:latest image not found."
    echo "Run ./scripts/setup-podman-dev.sh to build the image"
    exit 1
fi

echo "Starting FileSurf with Podman sandbox mode..."
echo ""

# Run with Podman enabled
export SANDBOX_PODMAN_ENABLED=true
exec mvn quarkus:dev
