#!/bin/bash
# Setup Podman for local development on macOS
# This allows testing the containerized klawed setup locally before deploying to production

set -e

echo "=== Podman Development Setup ==="
echo ""

# Check if running on macOS
if [[ "$(uname)" != "Darwin" ]]; then
    echo "This script is designed for macOS. For Linux, install podman via your package manager."
    exit 1
fi

# Step 1: Check/Install Podman
echo "Step 1: Checking Podman installation..."
if command -v podman &> /dev/null; then
    echo "   ✓ Podman is already installed: $(podman --version)"
else
    echo "   Installing Podman via Homebrew..."
    brew install podman
    echo "   ✓ Podman installed: $(podman --version)"
fi

# Step 2: Initialize Podman machine (macOS requires a VM)
echo ""
echo "Step 2: Checking Podman machine..."
if podman machine list 2>/dev/null | grep -q "Currently running"; then
    echo "   ✓ Podman machine is already running"
else
    echo "   Checking if machine exists..."
    if podman machine list 2>/dev/null | grep -q "podman-machine-default"; then
        echo "   Starting existing Podman machine..."
        podman machine start
    else
        echo "   Initializing new Podman machine..."
        # Use 4GB RAM and 2 CPUs for the VM
        podman machine init --cpus 2 --memory 4096 --disk-size 20
        podman machine start
    fi
    echo "   ✓ Podman machine started"
fi

# Step 3: Verify Podman is working
echo ""
echo "Step 3: Verifying Podman..."
podman info --format "{{.Host.OS}}" > /dev/null
echo "   ✓ Podman is working"

# Step 4: Build klawed-sandbox image locally
echo ""
echo "Step 4: Building klawed-sandbox image..."
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Check if we have the klawed source
KLAWED_SOURCE="${KLAWED_SOURCE:-$HOME/git/klawed}"
if [[ ! -d "$KLAWED_SOURCE" ]]; then
    echo "   ERROR: klawed source not found at $KLAWED_SOURCE"
    echo "   Set KLAWED_SOURCE environment variable to the klawed repo path"
    echo "   Or clone it: git clone <klawed-repo-url> ~/git/klawed"
    exit 1
fi

echo "   Using klawed source: $KLAWED_SOURCE"
echo "   Building image (this may take a few minutes)..."

# Copy Dockerfile to klawed source and build
cp "$PROJECT_DIR/deployment/klawed/Dockerfile.sandbox" "$KLAWED_SOURCE/Dockerfile.sandbox"
cd "$KLAWED_SOURCE"
podman build -f Dockerfile.sandbox -t klawed-sandbox:latest .

echo "   ✓ klawed-sandbox image built"

# Step 5: Verify the image
echo ""
echo "Step 5: Verifying klawed-sandbox image..."
podman run --rm klawed-sandbox:latest --version
echo "   ✓ Image verified"

# Step 6: Show next steps
echo ""
echo "=== Setup Complete ==="
echo ""
echo "To run FileSurf with Podman sandbox mode:"
echo ""
echo "  # Option 1: Set environment variable"
echo "  export SANDBOX_PODMAN_ENABLED=true"
echo "  mvn quarkus:dev"
echo ""
echo "  # Option 2: Use the convenience script"
echo "  ./scripts/run-with-podman.sh"
echo ""
echo "To disable Podman mode (run klawed directly):"
echo "  unset SANDBOX_PODMAN_ENABLED"
echo "  # or"
echo "  export SANDBOX_PODMAN_ENABLED=false"
echo ""
