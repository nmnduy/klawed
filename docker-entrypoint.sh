#!/bin/bash
set -e

# Ensure klawed directories exist in the workspace
mkdir -p /workspace/.klawed/logs /workspace/.klawed/mcp

# Execute klawed as root
exec /usr/local/bin/klawed "$@"
