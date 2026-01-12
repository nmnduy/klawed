#!/bin/bash
set -e

# Ensure klawed directories exist in the workspace
# This is necessary because the workspace is mounted as a volume
# which overlays any directories created at build time
mkdir -p /workspace/.klawed/logs
mkdir -p /workspace/.klawed/mcp

# Execute klawed with all passed arguments
exec /usr/local/bin/klawed "$@"
