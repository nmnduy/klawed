#!/bin/bash
set -e

# Check if we can write to the workspace
if ! touch /workspace/.write_test 2>/dev/null; then
    echo "ERROR: Cannot write to /workspace directory" >&2
    echo "The mounted volume must be writable by UID 1000 (agent user)" >&2
    echo "Fix permissions on the host before mounting:" >&2
    echo "  chown -R 1000:1000 /path/to/workspace" >&2
    exit 1
fi
rm -f /workspace/.write_test

# Ensure klawed directories exist in the workspace
# This is necessary because the workspace is mounted as a volume
# which overlays any directories created at build time
mkdir -p /workspace/.klawed/logs
mkdir -p /workspace/.klawed/mcp

# Execute klawed with all passed arguments
exec /usr/local/bin/klawed "$@"
