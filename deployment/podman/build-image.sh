#!/bin/bash
#
# Build the klawed-sandbox container image
#
# Usage:
#   ./build-image.sh [path/to/klawed]
#
# Arguments:
#   path/to/klawed  - Optional path to the klawed binary
#                     Default: looks in current directory, then ~/bin/klawed
#
# Examples:
#   ./build-image.sh                        # Use default klawed location
#   ./build-image.sh /opt/klawed/klawed     # Use specific binary
#   ./build-image.sh ~/downloads/klawed     # Use downloaded binary

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_NAME="klawed-sandbox"
IMAGE_TAG="latest"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Find the klawed binary
find_klawed_binary() {
    local binary_path=""
    
    # Check if argument provided
    if [[ -n "$1" ]]; then
        binary_path="$1"
    # Check current directory
    elif [[ -f "./klawed" ]]; then
        binary_path="./klawed"
    # Check ~/bin
    elif [[ -f "$HOME/bin/klawed" ]]; then
        binary_path="$HOME/bin/klawed"
    # Check /usr/local/bin
    elif [[ -f "/usr/local/bin/klawed" ]]; then
        binary_path="/usr/local/bin/klawed"
    fi
    
    echo "$binary_path"
}

# Validate the binary
validate_binary() {
    local binary_path="$1"
    
    if [[ ! -f "$binary_path" ]]; then
        log_error "klawed binary not found at: $binary_path"
        return 1
    fi
    
    if [[ ! -x "$binary_path" ]]; then
        log_warn "Binary is not executable, will set permissions during build"
    fi
    
    # Check if it's actually an executable (not a text file, etc.)
    if file "$binary_path" | grep -qE "(executable|binary|ELF)"; then
        return 0
    else
        log_warn "File may not be a valid executable: $(file "$binary_path")"
        read -p "Continue anyway? [y/N] " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            return 1
        fi
    fi
    
    return 0
}

# Main build function
build_image() {
    local klawed_path="$1"
    
    log_info "Building image: ${IMAGE_NAME}:${IMAGE_TAG}"
    log_info "Using klawed binary: $klawed_path"
    
    # Create temporary build context
    local build_context=$(mktemp -d)
    trap "rm -rf $build_context" EXIT
    
    # Copy Dockerfile and klawed binary to build context
    cp "$SCRIPT_DIR/Dockerfile" "$build_context/"
    cp "$klawed_path" "$build_context/klawed"
    chmod +x "$build_context/klawed"
    
    log_info "Build context prepared at: $build_context"
    
    # Build the image
    cd "$build_context"
    podman build \
        --tag "${IMAGE_NAME}:${IMAGE_TAG}" \
        --label "build.date=$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
        --label "build.host=$(hostname)" \
        .
    
    local exit_code=$?
    
    if [[ $exit_code -eq 0 ]]; then
        log_info "Successfully built ${IMAGE_NAME}:${IMAGE_TAG}"
        log_info ""
        log_info "To run the container:"
        log_info "  podman run --rm -it ${IMAGE_NAME}:${IMAGE_TAG}"
        log_info ""
        log_info "To run with a mounted workspace:"
        log_info "  podman run --rm -it -v \$(pwd):/workspace ${IMAGE_NAME}:${IMAGE_TAG}"
    else
        log_error "Build failed with exit code: $exit_code"
    fi
    
    return $exit_code
}

# Main entry point
main() {
    log_info "Klawed Sandbox Image Builder"
    log_info "=============================="
    
    # Check if podman is available
    if ! command -v podman &> /dev/null; then
        log_error "podman is not installed or not in PATH"
        exit 1
    fi
    
    # Find klawed binary
    local klawed_path
    klawed_path=$(find_klawed_binary "$1")
    
    if [[ -z "$klawed_path" ]]; then
        log_error "Could not find klawed binary"
        log_error "Please provide path as argument: ./build-image.sh /path/to/klawed"
        exit 1
    fi
    
    # Validate the binary
    if ! validate_binary "$klawed_path"; then
        exit 1
    fi
    
    # Build the image
    build_image "$klawed_path"
}

main "$@"
