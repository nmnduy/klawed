#!/bin/bash
#
# deploy-klawed-sandbox.sh - Deploy klawed sandbox image to production VPS
#
# This script transfers the klawed-sandbox Docker image from fandalf (build host)
# to filesurf-0 (production VPS).
#
# Usage:
#   ./deploy-klawed-sandbox.sh [options]
#
# Options:
#   --tag TAG         Docker tag to deploy (default: latest)
#   --build           Build before deploying
#   --update-latest   Also tag the deployed image as :latest
#   --dry-run         Show what would be done without executing
#   -h, --help        Show this help
#
# Prerequisites:
#   - SSH access to both fandalf and filesurf-0
#   - Docker installed on both hosts
#   - Image already built on fandalf (or use --build)

set -euo pipefail

# Configuration
BUILD_HOST="fandalf"
PROD_HOST="filesurf-0"
IMAGE_NAME="klawed-sandbox"
TAG="latest"
DO_BUILD=false
DRY_RUN=false
UPDATE_LATEST=false

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

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

run_cmd() {
    if [[ "${DRY_RUN}" == "true" ]]; then
        echo -e "${YELLOW}[DRY-RUN]${NC} $*"
    else
        eval "$@"
    fi
}

usage() {
    head -20 "$0" | tail -16 | sed 's/^#//' | sed 's/^ //'
    exit 0
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --tag)
            TAG="$2"
            shift 2
            ;;
        --build)
            DO_BUILD=true
            shift
            ;;
        --update-latest)
            UPDATE_LATEST=true
            shift
            ;;
        --dry-run)
            DRY_RUN=true
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

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

log_info "Deploying ${IMAGE_NAME}:${TAG} to ${PROD_HOST}..."

# Step 1: Build if requested
if [[ "${DO_BUILD}" == "true" ]]; then
    log_info "Building image first..."
    run_cmd "${SCRIPT_DIR}/build-klawed-sandbox.sh --tag ${TAG}"
fi

# Step 2: Verify image exists on build host
log_info "Checking image on ${BUILD_HOST}..."
if ! ssh "${BUILD_HOST}" "docker images -q ${IMAGE_NAME}:${TAG}" | grep -q .; then
    log_error "Image ${IMAGE_NAME}:${TAG} not found on ${BUILD_HOST}!"
    log_error "Run with --build to build first, or run build-klawed-sandbox.sh"
    exit 1
fi

# Get image details
IMAGE_ID=$(ssh "${BUILD_HOST}" "docker images -q ${IMAGE_NAME}:${TAG}")
IMAGE_SIZE=$(ssh "${BUILD_HOST}" "docker images ${IMAGE_NAME}:${TAG} --format '{{.Size}}'")
log_info "Image ID: ${IMAGE_ID}"
log_info "Image size: ${IMAGE_SIZE}"

# Step 3: Check if same image already exists on production
log_info "Checking existing image on ${PROD_HOST}..."
PROD_IMAGE_ID=$(ssh "${PROD_HOST}" "docker images -q ${IMAGE_NAME}:${TAG}" 2>/dev/null || echo "")

if [[ "${IMAGE_ID}" == "${PROD_IMAGE_ID}" ]] && [[ -n "${PROD_IMAGE_ID}" ]]; then
    log_warn "Same image already exists on ${PROD_HOST} (ID: ${IMAGE_ID})"
    read -p "Transfer anyway? [y/N] " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        log_info "Skipping transfer."
        exit 0
    fi
fi

# Step 4: Transfer image using docker save | ssh | docker load
log_info "Transferring image from ${BUILD_HOST} to ${PROD_HOST}..."
log_info "This may take a minute depending on network speed..."

# Use pigz for faster compression if available, fallback to gzip
COMPRESS_CMD="gzip"
DECOMPRESS_CMD="gunzip"
if ssh "${BUILD_HOST}" "which pigz" &>/dev/null; then
    COMPRESS_CMD="pigz"
    log_info "Using pigz for faster compression"
fi
if ssh "${PROD_HOST}" "which pigz" &>/dev/null; then
    DECOMPRESS_CMD="pigz -d"
fi

# Transfer: save on build host -> compress -> transfer -> decompress -> load on prod
TRANSFER_CMD="ssh ${BUILD_HOST} 'docker save ${IMAGE_NAME}:${TAG} | ${COMPRESS_CMD}' | ssh ${PROD_HOST} '${DECOMPRESS_CMD} | docker load'"

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
if ! ssh "${PROD_HOST}" "docker images -q ${IMAGE_NAME}:${TAG}" | grep -q .; then
    log_error "Image not found on ${PROD_HOST} after transfer!"
    exit 1
fi

# Test the image
log_info "Testing klawed in container..."
VERSION=$(ssh "${PROD_HOST}" "docker run --rm ${IMAGE_NAME}:${TAG} --version" 2>&1)
log_info "Version: ${VERSION}"

# Verify memvid
MEMVID_CHECK=$(ssh "${PROD_HOST}" "docker run --rm --entrypoint bash ${IMAGE_NAME}:${TAG} -c 'ldd /usr/local/bin/klawed | grep memvid'" || echo "NOT FOUND")
if [[ "${MEMVID_CHECK}" == *"libmemvid"* ]]; then
    log_success "Memvid library verified"
else
    log_error "WARNING: Memvid library not found!"
fi

# Get final image size on prod
PROD_IMAGE_SIZE=$(ssh "${PROD_HOST}" "docker images ${IMAGE_NAME}:${TAG} --format '{{.Size}}'")

# Step 6: Update latest tag if requested
if [[ "${UPDATE_LATEST}" == "true" ]] && [[ "${TAG}" != "latest" ]]; then
    log_info "Updating :latest tag to point to :${TAG}..."
    if [[ "${DRY_RUN}" == "true" ]]; then
        log_info "[DRY-RUN] Would execute: docker tag ${IMAGE_NAME}:${TAG} ${IMAGE_NAME}:latest"
    else
        ssh "${PROD_HOST}" "docker tag ${IMAGE_NAME}:${TAG} ${IMAGE_NAME}:latest"
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
