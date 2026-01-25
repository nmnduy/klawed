# Container Management Architecture

## Overview
FileSurf runs Klawed AI agents inside isolated Podman containers. Container lifecycle is managed by `KlawedSandboxService`, which combines session tracking with container management.

## Service Responsibilities

**`KlawedSandboxService`** - Single source of truth for container and session lifecycle
- Manages session registration/unregistration (in `sessions.db`)
- Starts/stops containers via Podman CLI
- Scheduled lifecycle management (runs every 10 seconds via single-threaded executor)
- Prevents catch-up runner behavior by serializing operations
- Handles conversation seeding from chat history
- Manages graceful shutdown of all containers
- Container health checks (`isContainerRunning`)
- Tracks metrics (containers started/stopped/reused)

**Related Services:**
- `KlawedShutdownService` - Handles SIGTERM/SIGINT signals and emergency cleanup
- `KlawedDbCleanupService` - Daily cleanup of old klawed message DB files (60+ days old)
- `MetricsService` - Exposes container counts and lifecycle metrics

## Container Lifecycle Flow

```
User connects → SessionManager.registerSession()
                     ↓
         KlawedSandboxService.manageContainerLifecycle()
         (scheduled every 10 seconds)
                     ↓
         ┌─────────────────────┐
         │ Check if container  │
         │ is already running  │
         └──────────┬──────────┘
                    ↓
         Container running → Metrics increment "reused"
         Container not running → startContainerForSession()
                    ↓
         Seed conversation from chat history
                    ↓
         Podman CLI "podman run" with SQLite queue mode
                    ↓
         Container running with klawed agent
                    ↓
User disconnects → SessionManager.unregisterSession()
                     ↓
         (30s grace period via disconnected_at timestamp)
                     ↓
         KlawedSandboxService stops container after 90s inactivity
```

## Session Tracking (sessions.db)

Sessions are tracked in `data/sessions.db` (or `/var/lib/filesurf/data/sessions.db` in production):

```sql
CREATE TABLE sessions (
    session_id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL,
    email TEXT,                    -- Client identity (email address)
    registered_at INTEGER NOT NULL,
    last_active_at INTEGER NOT NULL,
    disconnected_at INTEGER        -- NULL when connected, timestamp when disconnected
);
```

**Session states:**
- **Active**: `disconnected_at IS NULL` - WebSocket is connected
- **Disconnected**: `disconnected_at IS NOT NULL` - WebSocket disconnected
- **Inactive**: Disconnected for >90 seconds (container will be stopped)
- **Idle**: Active but no activity for >30 minutes (container won't auto-start)

## Container Configuration

Containers are configured via `application.properties`:

```properties
# Podman sandbox settings
sandbox.podman.enabled=true
sandbox.podman.image=klawed-sandbox:1.0.0
sandbox.podman.memory=2g
sandbox.podman.cpus=2
sandbox.podman.pids-limit=512

# Timeouts
klawed.sandbox.inactivity-timeout=90s     # Stop after 90s of disconnection
klawed.sandbox.idle-timeout=30m           # Don't auto-start for idle sessions
```

**Container specs:**
- Detached mode with `--rm` (auto-removed on exit)
- `--userns=keep-id` for UID mapping
- `--network=bridge` for network access
- `--tmpfs /tmp:rw,size=1g` for temporary storage
- Memory and CPU limits via config
- Mounts workspace directory (`/workspace`)
- Mounts klawed messages directory (`/tmp/klawed-messages`)

## Scheduled Lifecycle Management

`KlawedSandboxService.manageContainerLifecycle()` runs every 10 seconds via a single-threaded executor:

- Uses `ExecutorService.newSingleThreadExecutor()` to serialize operations
- Prevents Quarkus catch-up runner behavior (missed executions won't pile up)
- All lifecycle operations are serialized, preventing overlapping container checks

**Execution flow:**
1. **Get active sessions** - Sessions where `disconnected_at IS NULL`
2. **Check idle sessions** - Sessions with no activity for >30 minutes
3. **Ensure containers exist** - Start containers for active, non-idle sessions
4. **List running containers** - Get all `klawed-*` containers from Podman
5. **Stop orphaned containers** - Containers where session is inactive/idle/disconnected

## Conversation Seeding

When a container starts, previous chat messages are seeded into the SQLite queue:

- Retrieves last 100 messages from chat history
- Inserts TEXT messages with `sent=1` flag
- Allows klawed agent to have conversation context
- Handles client↔agent message translation

## Key Design Decisions

1. **Simpler architecture** - No separate container tracking database; sessions.db handles both session and container state
2. **Scheduled management** - 10-second polling loop instead of event-driven container lifecycle
3. **Healthy containers are reused** - If running, don't restart; just write to SQLite queue
4. **`--rm` flag** - Auto-removes containers on exit
5. **Graceful shutdown** - 90-second timeout after disconnect before stopping container
6. **No ':latest' tags** - Prevents version confusion in production
7. **Idle session handling** - Active sessions with no activity won't auto-start containers

## Grace Periods and Timeouts

| Scenario | Timeout | Behavior |
|----------|---------|----------|
| Disconnected | 90 seconds | Container stopped after disconnected_at + 90s |
| Idle (connected but no activity) | 30 minutes | Container won't auto-start |
| Application shutdown | Immediate | All containers stopped gracefully |

## Message Queue Lifecycle

Klawed message databases are stored in `data/klawed-messages/`:
- Created per session when container starts
- Cleaned up daily (3 AM) if older than 60 days
- Named: `klawed_messages_{sessionId}.db`
