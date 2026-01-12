#!/bin/bash
set -e

# Ensure klawed directories exist in the workspace with correct permissions
# This runs as root before dropping privileges
mkdir -p /workspace/.klawed/logs /workspace/.klawed/mcp
chown -R agent:agent /workspace/.klawed 2>/dev/null || true

# Also ensure the workspace root is writable by agent if possible
# (only affects files/dirs not owned by a different user)
chown agent:agent /workspace 2>/dev/null || true

# Drop privileges and execute klawed as the agent user
exec gosu agent /usr/local/bin/klawed "$@"
