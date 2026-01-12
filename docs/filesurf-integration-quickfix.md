# FileSurf Integration Quick Fix Guide

## Problem Summary

The klawed sandbox container fails to start with "Permission denied" error when trying to create `.klawed/mcp` directory. This occurs because the mounted workspace volume has incorrect ownership.

## Root Cause

The container runs as non-root user (UID 1000) but the mounted volume is owned by a different user. When the entrypoint script tries to create directories, it fails with EACCES.

## Solution

### 1. Update klawed Repository

The following files have been updated in the klawed repository:

- **`docker-entrypoint.sh`**: Now validates write permissions before proceeding
- **`docs/docker-sandbox-deployment.md`**: Comprehensive deployment guide
- **`Dockerfile.sandbox`**: Updated documentation in header

Pull the latest changes:
```bash
cd /path/to/klawed
git pull origin master
```

### 2. Rebuild the Sandbox Image

```bash
cd /path/to/klawed
make docker-sandbox
```

This will build `klawed-sandbox:0.12.6` (or current version).

### 3. Transfer to Production

```bash
docker save klawed-sandbox:0.12.6 | ssh filesurf-0 'docker load'
```

### 4. Update Java Code (PodmanSandboxService)

Before starting each container, ensure the workspace has correct ownership:

```java
// In PodmanSandboxService.java or wherever containers are started

private void prepareWorkspace(String workspacePath) throws IOException {
    Path workspace = Paths.get(workspacePath);
    
    // Create workspace directory if it doesn't exist
    Files.createDirectories(workspace);
    
    // Set ownership to UID 1000 (agent user in container)
    // This requires the Java process to have sufficient privileges
    ProcessBuilder pb = new ProcessBuilder(
        "chown", "-R", "1000:1000", workspacePath
    );
    
    Process process = pb.start();
    try {
        int exitCode = process.waitFor();
        if (exitCode != 0) {
            String error = new String(process.getErrorStream().readAllBytes());
            throw new IOException("Failed to set workspace permissions: " + error);
        }
    } catch (InterruptedException e) {
        Thread.currentThread().interrupt();
        throw new IOException("Interrupted while setting permissions", e);
    }
}

// Call before starting container
public String startContainer(String sessionId, String userId) throws IOException {
    String workspacePath = String.format(
        "/home/fandalf/git/filesurf_v2/data/persistent/user-%s", 
        userId
    );
    
    // CRITICAL: Set permissions before mounting
    prepareWorkspace(workspacePath);
    
    // Now start container with volume mount
    // ... existing container start code ...
}
```

### 5. Alternative: Filesystem-Level Fix

If changing Java code is not immediately feasible, fix permissions at the filesystem level:

```bash
# On the host system (filesurf-0)
cd /home/fandalf/git/filesurf_v2/data/persistent

# Fix all existing user workspaces
for dir in user-*; do
    echo "Fixing permissions for $dir"
    chown -R 1000:1000 "$dir"
done

# Set up a cron job or systemd path unit to monitor new directories
```

### 6. Verify the Fix

Test with a single session:

```bash
# Create test workspace
mkdir -p /tmp/test-workspace
chown -R 1000:1000 /tmp/test-workspace

# Start test container
podman run -d \
  --name test-klawed \
  --user 1000:1000 \
  -v /tmp/test-workspace:/workspace:Z \
  -e OPENAI_API_KEY=$OPENAI_API_KEY \
  klawed-sandbox:0.12.6 \
  --sqlite-queue /workspace/test.db

# Check logs
podman logs test-klawed

# Should see:
# - No "Permission denied" errors
# - "Klawed initialized successfully" or similar

# Clean up
podman stop test-klawed
rm -rf /tmp/test-workspace
```

## Error Messages

### Before Fix
```
mkdir: cannot create directory '/workspace/.klawed/mcp': Permission denied
Container died unexpectedly
```

### After Fix (Good)
```
Klawed agent starting...
Initialized successfully
```

### After Fix (Permission Still Wrong)
```
ERROR: Cannot write to /workspace directory
The mounted volume must be writable by UID 1000 (agent user)
Fix permissions on the host before mounting:
  chown -R 1000:1000 /path/to/workspace
```

This explicit error message makes it clear what needs to be fixed.

## Production Deployment Checklist

- [ ] Pull latest klawed code with fixes
- [ ] Rebuild sandbox image: `make docker-sandbox`
- [ ] Transfer image to production: `docker save | ssh ... 'docker load'`
- [ ] Update Java code to set permissions OR fix existing workspace permissions
- [ ] Test with a single session
- [ ] Monitor logs for permission errors
- [ ] Deploy to production
- [ ] Monitor container startup success rate

## Rollback Plan

If issues arise:

1. Revert to previous container image:
   ```bash
   podman tag klawed-sandbox:0.12.5 klawed-sandbox:latest
   ```

2. Or disable MCP temporarily:
   ```bash
   # In container startup command, add:
   -e KLAWED_MCP_ENABLED=0
   ```

## References

- Full deployment guide: `docs/docker-sandbox-deployment.md`
- Entrypoint script: `docker-entrypoint.sh`
- Dockerfile: `Dockerfile.sandbox`
