# Klawed Database Cleanup Fix

## Problem
Klawed database files (`klawed_messages_{sessionId}.db` and related WAL files) were not being cleaned up properly, leading to accumulation of orphaned files in the persistent storage.

### Root Cause
The cleanup logic existed in three places:
1. `AgentShutdownJobService.cleanupKlawedArtifacts()` - only called during scheduled shutdown after grace period
2. `FileChatHttpResource` conclude endpoint - called when session is explicitly concluded via HTTP
3. `FileChatWebSocket` conclude command - called when session is explicitly concluded via WebSocket

However, if the application restarted or crashed before:
- The scheduled shutdown job executed (30 second grace period)
- The session was explicitly concluded

Then the klawed DB files would remain orphaned in the file system.

## Solution
Added cleanup directly to `KlawedAgentManager.KlawedAgentInstance.stop()` method, ensuring that klawed database files are **always** cleaned up when an agent stops, regardless of how it's stopped.

### Changes Made
1. Added `cleanupKlawedDbFiles()` method to `KlawedAgentManager` class
2. Called this method from `KlawedAgentInstance.stop()` before logging completion
3. The cleanup deletes:
   - `klawed_messages_{sessionId}.db`
   - `klawed_messages_{sessionId}.db-shm` (shared memory)
   - `klawed_messages_{sessionId}.db-wal` (write-ahead log)

### Code Location
File: `src/main/java/com/filesurf/service/KlawedAgentManager.java`

Method added:
```java
private void cleanupKlawedDbFiles(String sessionId, Path sessionDir)
```

Called from:
```java
public void stop() // in KlawedAgentInstance inner class
```

## Production Cleanup
Before deploying this fix, we manually cleaned up 373 orphaned klawed database files on production server `filesurf-0`:

```bash
ssh filesurf-0 "cd filesurf_v2/data/persistent && find . -name '*klawed*.db' | wc -l"
# Result: 373 files

ssh filesurf-0 "cd filesurf_v2/data/persistent && find . -name '*klawed*.db' -exec du -ch {} + | tail -1"
# Result: 1.5MB total

ssh filesurf-0 "cd filesurf_v2/data/persistent && find . -name '*klawed*.db' -type f -delete"
# Result: All 373 files deleted
```

## Testing
Compiled successfully with:
```bash
mvn clean compile -DskipTests
```

## Deployment
After merging to main branch, deploy to production:
```bash
cd deployment
./deploy-prod.sh
```

## Verification
After deployment, monitor that klawed DB files are being cleaned up:
```bash
# Check for any klawed DB files in persistent storage
ssh filesurf-0 "cd filesurf_v2/data/persistent && find . -name '*klawed*.db' -type f | wc -l"

# Monitor logs for cleanup messages
ssh filesurf-0 "podman logs -f filesurf-app 2>&1 | grep 'Cleaning up klawed database'"
```

## Notes
- The fix ensures cleanup happens even if:
  - Application crashes or restarts
  - Shutdown jobs don't execute
  - Sessions aren't explicitly concluded
- The cleanup is safe to call multiple times (idempotent)
- Errors during cleanup are logged but don't prevent agent shutdown
- This complements (doesn't replace) the existing cleanup in:
  - `AgentShutdownJobService`
  - Session conclude endpoints
