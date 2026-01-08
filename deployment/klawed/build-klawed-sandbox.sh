#!/bin/bash
#
# build-klawed-sandbox.sh - Build klawed sandbox Docker image on fandalf
#
# This script is designed to run on the fandalf build host (same x86_64 arch as production)
# It builds a minimal klawed container with memvid support.
#
# Usage:
#   ./build-klawed-sandbox.sh [options]
#
# Options:
#   --tag TAG       Docker tag (default: latest)
#   --push          Push to registry after build (not implemented yet)
#   --no-cache      Build without Docker cache
#   -h, --help      Show this help
#
# Prerequisites:
#   - Docker installed on fandalf
#   - SSH access to fandalf (configured in ~/.ssh/config)
#   - klawed source code in ~/git/klawed on fandalf

set -euo pipefail

# Configuration
REMOTE_HOST="fandalf"
REMOTE_KLAWED_DIR="~/git/klawed"
IMAGE_NAME="klawed-sandbox"
TAG="latest"
NO_CACHE=""

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $*"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $*"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $*"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $*"
}

usage() {
    head -25 "$0" | tail -21 | sed 's/^#//' | sed 's/^ //'
    exit 0
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --tag)
            TAG="$2"
            shift 2
            ;;
        --no-cache)
            NO_CACHE="--no-cache"
            shift
            ;;
        --push)
            log_warn "Registry push not implemented yet"
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            log_error "Unknown option: $1"
            usage
            ;;
    esac
done

log_info "Building klawed-sandbox image on ${REMOTE_HOST}..."
log_info "Image: ${IMAGE_NAME}:${TAG}"

# Step 1: Update klawed source on remote from local machine
log_info "Updating klawed source code on ${REMOTE_HOST}..."
# First, ensure the remote has our local machine as a git remote
ssh "${REMOTE_HOST}" "cd ${REMOTE_KLAWED_DIR} && git remote add local_mac puter:/Users/puter/git/klawedspace 2>/dev/null || git remote set-url local_mac puter:/Users/puter/git/klawedspace" || true
ssh "${REMOTE_HOST}" "cd ${REMOTE_KLAWED_DIR} && git fetch local_mac && git merge local_mac/master --ff-only" || {
    log_warn "Git update failed (may have local changes or be up to date), continuing anyway..."
}

# Step 2: Copy optimized Dockerfile to remote if it doesn't exist or is different
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOCKERFILE="${SCRIPT_DIR}/Dockerfile.sandbox"

if [[ ! -f "${DOCKERFILE}" ]]; then
    log_error "Dockerfile.sandbox not found at ${DOCKERFILE}"
    exit 1
fi

log_info "Syncing Dockerfile.sandbox to ${REMOTE_HOST}..."
scp "${DOCKERFILE}" "${REMOTE_HOST}:${REMOTE_KLAWED_DIR}/Dockerfile.sandbox"

# Step 3: Build the Docker image on remote
log_info "Building Docker image (this may take a few minutes)..."

BUILD_CMD="cd ${REMOTE_KLAWED_DIR} && docker build ${NO_CACHE} -f Dockerfile.sandbox -t ${IMAGE_NAME}:${TAG} ."

if ! ssh "${REMOTE_HOST}" "${BUILD_CMD}"; then
    log_error "Docker build failed!"
    exit 1
fi

# Step 4: Verify the build
log_info "Verifying build..."
ssh "${REMOTE_HOST}" "docker run --rm ${IMAGE_NAME}:${TAG} --version"

# Get image size
IMAGE_SIZE=$(ssh "${REMOTE_HOST}" "docker images ${IMAGE_NAME}:${TAG} --format '{{.Size}}'")
log_info "Image size: ${IMAGE_SIZE}"

# Verify memvid library is present
log_info "Verifying memvid library..."
MEMVID_CHECK=$(ssh "${REMOTE_HOST}" "docker run --rm --entrypoint bash ${IMAGE_NAME}:${TAG} -c 'ldd /usr/local/bin/klawed | grep memvid'" || echo "NOT FOUND")
if [[ "${MEMVID_CHECK}" == *"libmemvid"* ]]; then
    log_success "Memvid library is correctly linked"
else
    log_error "Memvid library NOT found in container!"
    log_error "Check: ${MEMVID_CHECK}"
    exit 1
fi

log_success "Build completed successfully!"
log_info "Image: ${IMAGE_NAME}:${TAG} (${IMAGE_SIZE})"
log_info ""
log_info "To deploy to production, run:"
log_info "  ./deploy-klawed-sandbox.sh"
