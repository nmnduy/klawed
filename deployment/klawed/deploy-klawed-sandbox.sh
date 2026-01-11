#!/bin/bash
#
# deploy-klawed-sandbox.sh - Build (locally) and deploy klawed sandbox image to production VPS
#
# This script builds the klawed-sandbox Docker image on the CURRENT HOST
# (no remote build host) and transfers it to filesurf-0 (production VPS).
#
# Usage:
#   ./deploy-klawed-sandbox.sh [options]
#
# Options:
#   --tag TAG         Docker tag to deploy (default: latest)
#   --build           Build locally before deploying
#   --update-latest   Also tag the deployed image as :latest on prod
#   --src DIR         Path to klawed source (default: $HOME/git/klawed)
#   --dry-run         Show what would be done without executing
#   -h, --help        Show this help
#
# Prerequisites:
#   - Docker installed locally (for build) and on filesurf-0
#   - SSH access to filesurf-0 with permission to run docker (sudo ok)
#   - klawed source checked out locally (default: ~/git/klawed)

set -euo pipefail

# Configuration (overridable via args)
PROD_HOST="filesurf-0"
IMAGE_NAME="klawed-sandbox"
TAG="latest"
DO_BUILD=false
DRY_RUN=false
UPDATE_LATEST=false
KLAWED_SRC_DIR="${HOME}/git/klawed"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $*"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $*"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }

run_cmd() {
    if [[ "${DRY_RUN}" == "true" ]]; then
        echo -e "${YELLOW}[DRY-RUN]${NC} $*"
    else
        eval "$@"
    fi
}

usage() {
    head -25 "$0" | tail -19 | sed 's/^#//' | sed 's/^ //'
    exit 0
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --tag)
            TAG="$2"; shift 2 ;;
        --build)
            DO_BUILD=true; shift ;;
        --update-latest)
            UPDATE_LATEST=true; shift ;;
        --src)
            KLAWED_SRC_DIR="$2"; shift 2 ;;
        --dry-run)
            DRY_RUN=true; shift ;;
        -h|--help)
            usage ;;
        *)
            log_error "Unknown option: $1"; usage ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOCKERFILE="${SCRIPT_DIR}/Dockerfile.sandbox"

log_info "Deploying ${IMAGE_NAME}:${TAG} to ${PROD_HOST} (building locally)..."
log_info "Klawed source: ${KLAWED_SRC_DIR}"

if [[ ! -d "${KLAWED_SRC_DIR}" ]]; then
    log_error "Klawed source directory not found: ${KLAWED_SRC_DIR}"
    exit 1
fi
if [[ ! -f "${DOCKERFILE}" ]]; then
    log_error "Dockerfile.sandbox not found at ${DOCKERFILE}"
    exit 1
fi

# Step 1: Build if requested
if [[ "${DO_BUILD}" == "true" ]]; then
    log_info "Building image locally..."
    BUILD_CMD="docker build -f ${DOCKERFILE} -t ${IMAGE_NAME}:${TAG} ${KLAWED_SRC_DIR}"
    run_cmd "${BUILD_CMD}"
fi

# Step 2: Verify image exists locally
log_info "Checking local image..."
if ! docker images -q "${IMAGE_NAME}:${TAG}" | grep -q .; then
    log_error "Image ${IMAGE_NAME}:${TAG} not found locally!"
    log_error "Run with --build to build first."
    exit 1
fi

IMAGE_ID=$(docker images -q "${IMAGE_NAME}:${TAG}")
IMAGE_SIZE=$(docker images "${IMAGE_NAME}:${TAG}" --format '{{.Size}}')
log_info "Local Image ID: ${IMAGE_ID}"
log_info "Local Image size: ${IMAGE_SIZE}"

# Step 3: Check if same image already exists on production
log_info "Checking existing image on ${PROD_HOST}..."
PROD_IMAGE_ID=$(ssh "${PROD_HOST}" "sudo docker images -q ${IMAGE_NAME}:${TAG}" 2>/dev/null || echo "")

if [[ "${IMAGE_ID}" == "${PROD_IMAGE_ID}" ]] && [[ -n "${PROD_IMAGE_ID}" ]]; then
    log_warn "Same image already exists on ${PROD_HOST} (ID: ${IMAGE_ID})"
    read -p "Transfer anyway? [y/N] " -n 1 -r; echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        log_info "Skipping transfer."
        exit 0
    fi
fi

# Step 4: Transfer image using docker save | ssh | docker load
log_info "Transferring image from local host to ${PROD_HOST}..."
log_info "This may take a minute depending on network speed..."

# Use pigz for faster compression if available, fallback to gzip
COMPRESS_CMD="gzip"
DECOMPRESS_CMD="gunzip"
if command -v pigz &>/dev/null; then
    COMPRESS_CMD="pigz"
    log_info "Using pigz locally for faster compression"
fi
if ssh "${PROD_HOST}" "which pigz" &>/dev/null; then
    DECOMPRESS_CMD="pigz -d"
fi

TRANSFER_CMD="docker save ${IMAGE_NAME}:${TAG} | ${COMPRESS_CMD} | ssh ${PROD_HOST} '${DECOMPRESS_CMD} | sudo docker load'"

if [[ "${DRY_RUN}" == "true" ]]; then
    log_info "[DRY-RUN] Would execute: ${TRANSFER_CMD}"
else
    START_TIME=$(date +%s)
    eval "${TRANSFER_CMD}"
    END_TIME=$(date +%s)
    DURATION=$((END_TIME - START_TIME))
    log_info "Transfer completed in ${DURATION} seconds"
fi

# Step 5: Verify deployment
log_info "Verifying deployment on ${PROD_HOST}..."

# Check image exists
if ! ssh "${PROD_HOST}" "sudo docker images -q ${IMAGE_NAME}:${TAG}" | grep -q .; then
    log_error "Image not found on ${PROD_HOST} after transfer!"
    exit 1
fi

# Test the image
log_info "Testing klawed in container..."
VERSION=$(ssh "${PROD_HOST}" "sudo docker run --rm ${IMAGE_NAME}:${TAG} --version" 2>&1)
log_info "Version: ${VERSION}"

# Verify memvid
MEMVID_CHECK=$(ssh "${PROD_HOST}" "sudo docker run --rm --entrypoint bash ${IMAGE_NAME}:${TAG} -c 'ldd /usr/local/bin/klawed | grep memvid'" || echo "NOT FOUND")
if [[ "${MEMVID_CHECK}" == *"libmemvid"* ]]; then
    log_success "Memvid library verified"
else
    log_error "WARNING: Memvid library not found!"
fi

# Get final image size on prod
PROD_IMAGE_SIZE=$(ssh "${PROD_HOST}" "sudo docker images ${IMAGE_NAME}:${TAG} --format '{{.Size}}'")

# Step 6: Update latest tag if requested
if [[ "${UPDATE_LATEST}" == "true" ]] && [[ "${TAG}" != "latest" ]]; then
    log_info "Updating :latest tag on prod to point to :${TAG}..."
    if [[ "${DRY_RUN}" == "true" ]]; then
        log_info "[DRY-RUN] Would execute: sudo docker tag ${IMAGE_NAME}:${TAG} ${IMAGE_NAME}:latest"
    else
        ssh "${PROD_HOST}" "sudo docker tag ${IMAGE_NAME}:${TAG} ${IMAGE_NAME}:latest"
        log_success "Updated ${IMAGE_NAME}:latest -> ${IMAGE_NAME}:${TAG}"
    fi
fi

log_success "Deployment completed successfully!"
log_info ""
log_info "Summary:"
log_info "  Image: ${IMAGE_NAME}:${TAG}"
log_info "  Version: ${VERSION}"
log_info "  Size: ${PROD_IMAGE_SIZE}"
log_info ""
log_info "The image is now available on ${PROD_HOST}."
log_info "FileSurf can spawn containers using this image for agent sandboxing."
