# Docker Sandbox

The Docker sandbox provides an isolated environment for running Klawed with all dependencies pre-installed.

> **⚠️ Important for Production:** If you're deploying to production with mounted volumes, see the [Docker Sandbox Deployment Guide](docker-sandbox-deployment.md) for critical permission requirements and security best practices.

## Building

```bash
docker build -f Dockerfile.sandbox -t klawed-sandbox .
```

## Running

The container accepts all standard Klawed command-line arguments.

### Basic usage (interactive chat):

```bash
docker run -it --rm \
  -e OPENAI_API_KEY="your-api-key" \
  klawed-sandbox "your prompt here"
```

### SQLite Queue Mode:

```bash
# Mount a directory containing your queue database
# IMPORTANT: Directory must be owned by UID 1000
chown -R 1000:1000 /path/to/data
docker run -it --rm \
  -e OPENAI_API_KEY="your-api-key" \
  -v /path/to/data:/data:Z \
  klawed-sandbox --sqlite-queue /data/queue.db
```

### Unix Socket Mode:

```bash
# Mount a directory containing your Unix socket
chown -R 1000:1000 /path/to/sockets
docker run -it --rm \
  -e OPENAI_API_KEY="your-api-key" \
  -v /path/to/sockets:/sockets:Z \
  klawed-sandbox -u /sockets/klawed.sock
```

### Working with Files:

```bash
# Mount your project directory to /workspace
# IMPORTANT: Directory must be owned by UID 1000
chown -R 1000:1000 $(pwd)
docker run -it --rm \
  -e OPENAI_API_KEY="your-api-key" \
  -v $(pwd):/workspace:Z \
  klawed-sandbox "analyze this codebase"
```

## Environment Variables

All standard Klawed environment variables are supported:

- `OPENAI_API_KEY` - Required for API access
- `OPENAI_MODEL` - Model to use (default: gpt-4o)
- `KLAWED_LOG_LEVEL` - Logging level (DEBUG/INFO/WARN/ERROR)
- `KLAWED_DB_PATH` - Custom database path
- `KLAWED_BASH_TIMEOUT` - Bash command timeout in seconds
- See KLAWED.md for full list

Example with custom settings:

```bash
docker run -it --rm \
  -e OPENAI_API_KEY="your-api-key" \
  -e OPENAI_MODEL="gpt-4o-mini" \
  -e KLAWED_LOG_LEVEL="DEBUG" \
  -e KLAWED_BASH_TIMEOUT="60" \
  klawed-sandbox "your prompt"
```

## Pre-configured Setup

The sandbox includes:

- All dependencies (libcurl, cJSON, ncurses, etc.)
- Memvid FFI library for persistent memory
- Pre-created `.klawed/logs` directory structure
- Non-root `agent` user for security (UID 1000)
- Git configured with default settings

## Important Notes

- The working directory is `/workspace` by default
- Mount volumes to `/workspace` to persist data
- **The container runs as non-root user `agent` (UID 1000)**
- **Mounted volumes MUST be owned by UID 1000 or the container will fail**
- Use `:Z` flag for SELinux systems (e.g., `-v path:/workspace:Z`)
- Logs are written to `/workspace/.klawed/logs/klawed.log`

## Troubleshooting

### "Permission denied" errors

If you see:
```
mkdir: cannot create directory '/workspace/.klawed/mcp': Permission denied
Container died unexpectedly
```

Fix the ownership:
```bash
chown -R 1000:1000 /path/to/workspace
```

### Container fails to start

The entrypoint script validates write permissions and will show a clear error:
```
ERROR: Cannot write to /workspace directory
The mounted volume must be writable by UID 1000 (agent user)
```

Follow the suggested fix in the error message.

## See Also

- [Docker Sandbox Deployment Guide](docker-sandbox-deployment.md) - Production deployment, security, and integration
- [KLAWED.md](../KLAWED.md) - Full project documentation
- [FileSurf Integration Quick Fix](filesurf-integration-quickfix.md) - Integration guide for FileSurf team
