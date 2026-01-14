# Container Management Architecture

## Overview
FileSurf runs Klawed AI agents inside isolated Podman containers. The container lifecycle is managed through a layered architecture with clear separation of concerns.

## Class Responsibilities

**`PodmanSandboxService`** - Single source of truth for container lifecycle
- Starts/stops/kills containers via Podman CLI
- Manages container health checks (`isContainerRunning`)
- Handles orphan cleanup and startup recovery
- Maintains consistency between Podman state and database
- All container operations go through this service

**`ContainerTrackingService`** - Database layer (internal to container management)
- Persists container state to SQLite (`data/containers.db`)
- Provides claim-based locking for race condition prevention
- Tracks container history for debugging/auditing
- Should only be called by `PodmanSandboxService` (not directly by other services)

**Consumer Services** (use `PodmanSandboxService` public methods only):
- `KlawedAgentManager` - Manages agent instances, calls `startContainer`/`stopContainer`
- `AgentShutdownJobService` - Handles delayed shutdown jobs, calls `stopContainerBySession`/`killContainerBySession`
- `ContainerLivenessMonitor` - Periodic health checks, calls `isContainerRunning`, `cleanupOrphanedContainers`
- `MetricsService` - Exposes container counts, calls `countRunningContainersFromPodman`

## Container Lifecycle Flow

```
User connects → KlawedAgentManager.startAgentForSession()
                     ↓
         PodmanSandboxService.startContainer()
                     ↓
    ┌────────────────┴────────────────┐
    ↓                                 ↓
ContainerTrackingService        Podman CLI
.tryClaimContainerStart()       "podman run"
(DB lock to prevent race)
    └────────────────┬────────────────┘
                     ↓
         Container running, tracked in DB
                     ↓
User disconnects → AgentShutdownJobService schedules shutdown
                     ↓
          (30s grace period for reconnection)
                     ↓
         PodmanSandboxService.stopContainerBySession()
                     ↓
    ┌────────────────┴────────────────┐
    ↓                                 ↓
ContainerTrackingService        Podman CLI
.recordContainerStop()          "podman stop/rm"
```

## Database-Based Locking (Race Condition Prevention)

When starting a container, `PodmanSandboxService` uses `ContainerTrackingService.tryClaimContainerStart()` which:

1. Atomically checks if a container is already `running` or `starting` for the session
2. If not, marks the session as `starting` in the database (claim)
3. Returns one of: `CLAIMED`, `ALREADY_RUNNING`, or `ALREADY_STARTING`

This prevents race conditions where multiple WebSocket connections might try to start containers for the same session simultaneously.

### Reusing Healthy Containers

If a container is already running and healthy (verified via `podman inspect`), the system reuses it rather than stopping/restarting:
- Database says "running" + Podman confirms running → Return existing container
- This allows the klawed agent to continue processing; new messages written to the SQLite queue will be picked up automatically

### Handling Stale Database State

If database says "running" but Podman shows container is gone:
1. Mark container as "died" in database
2. Proceed with starting a new container

## Key Design Decisions

1. **Database is source of truth** for tracking, but Podman is verified on startup and periodically
2. **Healthy containers are reused** - if running, don't restart; just write to SQLite queue
3. **`--rm` flag** auto-removes containers on exit, reducing cleanup burden
4. **Orphan detection** compares Podman `ps` output against database records
5. **14-day retention** for historical container records (configurable)
6. **Stale 'starting' cleanup** on app startup handles crash recovery
