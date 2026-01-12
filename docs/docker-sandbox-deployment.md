# Klawed Sandbox Docker Deployment Guide

## Overview

The klawed sandbox container is designed to run the klawed AI agent in an isolated environment with proper security constraints. This document covers deployment requirements, troubleshooting, and best practices.

## Security Model

The container runs as a non-root user (`agent`, UID 1000) with the following restrictions:

- `--security-opt=no-new-privileges`: Prevents privilege escalation
- `--cap-drop=ALL`: Drops all Linux capabilities
- `--user 1000:1000`: Runs as non-root user
- `--network=bridge`: Isolated network namespace
- `--tmpfs /tmp`: Temporary filesystem with `noexec`, `nosuid`
- Resource limits: 2GB RAM, 2 CPUs, 512 process limit

## Volume Mounting Requirements

### Critical: Correct Permissions

The mounted workspace directory **must** be owned by UID 1000 (the `agent` user inside the container). Without proper permissions, the container will fail to start.

**Before mounting:**
```bash
# On the host system
chown -R 1000:1000 /path/to/workspace
chmod -R u+rwX /path/to/workspace
```

### SELinux Context (`:Z` flag)

When using the `:Z` flag for SELinux relabeling:
```bash
-v /path/to/workspace:/workspace:Z
```

The `:Z` flag tells Docker/Podman to relabel the volume for exclusive use by this container. This is necessary on SELinux-enabled systems but does NOT change ownership - you must still set correct ownership before mounting.

### Validation at Startup

The entrypoint script (`docker-entrypoint.sh`) validates write permissions before starting klawed:

1. Tests write access with a temporary file
2. Creates required `.klawed/` subdirectories
3. Provides clear error messages if permissions are incorrect

**Example error output:**
```
ERROR: Cannot write to /workspace directory
The mounted volume must be writable by UID 1000 (agent user)
Fix permissions on the host before mounting:
  chown -R 1000:1000 /path/to/workspace
```

## Directory Structure

The container expects the following directory structure in `/workspace`:

```
/workspace/
├── .klawed/
│   ├── logs/              # Application logs
│   ├── mcp/               # MCP server logs
│   ├── api_calls.db       # SQLite database for API call history
│   └── memory.mv2         # Memvid memory file (if enabled)
├── klawed_messages.db     # SQLite queue for messaging (when using --sqlite-queue)
└── [user files]           # User's workspace files
```

All subdirectories under `.klawed/` are created automatically by the entrypoint script.

## Running the Container

### Basic Usage

```bash
podman run -d \
  --name klawed-session \
  --security-opt=no-new-privileges \
  --cap-drop=ALL \
  --user 1000:1000 \
  --network=bridge \
  --tmpfs /tmp:rw,noexec,nosuid,size=1g \
  --memory=2g \
  --cpus=2 \
  --pids-limit=512 \
  --rm \
  -v /path/to/workspace:/workspace:Z \
  -w /workspace \
  --env-file ./env.conf \
  -e LD_LIBRARY_PATH=/usr/local/lib \
  -e TERM=dumb \
  -e HOME=/workspace \
  klawed-sandbox:0.12.6 \
  --sqlite-queue /workspace/klawed_messages.db
```

### Environment Variables

Required environment variables (typically provided via `--env-file`):

- `OPENAI_API_KEY`: OpenAI API key
- `OPENAI_MODEL`: Model to use (e.g., `gpt-4`)
- `OPENAI_API_BASE`: Custom API base URL (optional)

Optional klawed configuration:

- `KLAWED_LOG_LEVEL`: Logging level (DEBUG/INFO/WARN/ERROR)
- `KLAWED_DB_PATH`: Custom database path (default: `.klawed/api_calls.db`)
- `KLAWED_BASH_TIMEOUT`: Bash command timeout in seconds (default: 30)
- `KLAWED_MCP_ENABLED`: Enable MCP (Model Context Protocol) support
- `DISABLE_PROMPT_CACHING`: Disable prompt caching if needed

See `KLAWED.md` for complete configuration options.

## Common Issues and Troubleshooting

### Container Dies Immediately with "Permission denied"

**Symptom:**
```
mkdir: cannot create directory '/workspace/.klawed/mcp': Permission denied
Container died unexpectedly
```

**Cause:** The mounted volume is not owned by UID 1000.

**Solution:**
```bash
# On the host
chown -R 1000:1000 /path/to/workspace
chmod -R u+rwX /path/to/workspace
```

### Cannot Create Files in Workspace

**Symptom:** Klawed fails to create files, write logs, or save data.

**Cause:** Volume mounted read-only or incorrect permissions.

**Solution:**
1. Verify the mount is read-write (no `:ro` flag)
2. Check ownership: `ls -la /path/to/workspace`
3. Ensure parent directory is accessible

### MCP Servers Fail to Start

**Symptom:** MCP tools unavailable, warnings in logs about `.klawed/mcp` directory.

**Cause:** MCP requires write access to `.klawed/mcp/` for server logs.

**Solution:** Verify `.klawed/mcp/` exists and is writable by UID 1000.

### SELinux Denials

**Symptom:** Permission errors despite correct ownership.

**Cause:** SELinux policy blocking access.

**Solution:**
1. Use `:Z` flag when mounting: `-v /path:/workspace:Z`
2. Or temporarily test with: `setenforce 0` (not recommended for production)
3. Check denials: `ausearch -m avc -ts recent`

## Building the Sandbox Image

### Prerequisites

- Docker or Podman
- Build platform: Linux x86_64 (same architecture as deployment target)

### Build Command

```bash
docker build -f Dockerfile.sandbox -t klawed-sandbox:0.12.6 .
```

### Transfer to Production

```bash
# Save image to tar
docker save klawed-sandbox:0.12.6 | gzip > klawed-sandbox-0.12.6.tar.gz

# Transfer to remote host
scp klawed-sandbox-0.12.6.tar.gz user@remote-host:/tmp/

# Load on remote host
ssh user@remote-host 'gunzip -c /tmp/klawed-sandbox-0.12.6.tar.gz | docker load'
```

Or directly over SSH:
```bash
docker save klawed-sandbox:0.12.6 | ssh user@remote-host 'docker load'
```

## Integration with FileSurf

When integrating with Java application (FileSurf):

### Volume Setup

```java
// In PodmanSandboxService or similar
String workspacePath = String.format(
    "/path/to/persistent/user-%s", userId);

// CRITICAL: Ensure correct ownership before starting container
Process chownProcess = new ProcessBuilder(
    "chown", "-R", "1000:1000", workspacePath
).start();
chownProcess.waitFor();

// Now safe to start container
```

### Error Handling

Catch and handle container startup failures gracefully:

```java
try {
    startContainer();
} catch (IOException e) {
    if (e.getMessage().contains("Permission denied")) {
        // Attempt to fix permissions and retry
        fixPermissions(workspacePath);
        startContainer();
    } else {
        throw e;
    }
}
```

### Logging

Container logs are available via:
```bash
podman logs <container-id>
```

Application logs are in `/workspace/.klawed/logs/klawed.log` (mapped to host).

## Best Practices

1. **Pre-create workspace directories**: Create and set permissions before mounting
2. **Monitor disk usage**: Set `KLAWED_DB_MAX_SIZE_MB` to limit database growth
3. **Rotate logs**: Implement log rotation for `.klawed/logs/`
4. **Test permissions**: Always test with a minimal container before production
5. **Use health checks**: Implement container health checks to detect issues early
6. **Set resource limits**: Always specify `--memory`, `--cpus`, `--pids-limit`
7. **Clean up**: Use `--rm` flag to auto-remove stopped containers

## Security Considerations

1. **Never run as root**: Always use `--user 1000:1000`
2. **Drop capabilities**: Always use `--cap-drop=ALL`
3. **Restrict network**: Use `--network=bridge` or custom networks, never `host`
4. **Limit resources**: Set hard limits on memory, CPU, processes
5. **No privilege escalation**: Always use `--security-opt=no-new-privileges`
6. **Read-only root**: Consider `--read-only` with tmpfs for `/tmp`
7. **Audit logs**: Enable SELinux auditing in production

## References

- Main documentation: `KLAWED.md`
- Dockerfile: `Dockerfile.sandbox`
- Entrypoint script: `docker-entrypoint.sh`
- MCP documentation: `docs/mcp.md`
