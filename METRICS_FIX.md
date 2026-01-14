# Metrics Fix: Session vs WebSocket Connection Tracking

## Problem
The `filesurf_active_chat_sessions` gauge shows 388 active sessions when there are only 2 active WebSocket connections. This is because:

1. The recent commit (132a23c) removed `decrementChatSessions()` from WebSocket `onClose()` to keep sessions alive for reconnection
2. But the metric `activeChatSessions` is incremented on every WebSocket open and never decremented
3. Result: The gauge keeps growing and never goes down

## Root Cause Analysis
The code conflates two different concepts:
- **WebSocket Connection**: A single client connection (can reconnect multiple times)
- **Chat Session**: A logical user session that persists across reconnections

The current code:
```java
// On WebSocket open:
metricsService.incrementWebSocketConnections();  // ✅ Correct
metricsService.incrementChatSessions();          // ❌ Wrong - increments every connect

// On WebSocket close:
metricsService.decrementWebSocketConnections();  // ✅ Correct
// metricsService.decrementChatSessions();       // ❌ Removed (intentionally)
```

## Proposed Solution

### Option 1: Rename Metric to Match Behavior (Recommended)
The `activeChatSessions` metric is actually tracking "total sessions ever started" not "currently active sessions". We should:

1. Keep the counter `filesurf_chat_sessions_started_total` (already correct)
2. Remove or fix the gauge `filesurf_active_chat_sessions` 
3. Add a new metric that tracks actual active sessions (from database or in-memory tracking)

### Option 2: Track Sessions Properly
Implement proper session lifecycle tracking:
1. Only increment `activeChatSessions` when a NEW session is created (not on reconnect)
2. Decrement when session is concluded via:
   - `/conclude` endpoint
   - Idle timeout (10 minutes)
   - Explicit deactivation

### Option 3: Query Database for Active Sessions (Best)
Replace the gauge implementation to query actual active sessions:
```java
// Instead of maintaining a counter, query the database
Gauge.builder("filesurf_active_chat_sessions", this, MetricsService::getActiveChatSessionsFromDb)
    .description("Number of currently active chat sessions")
    .tag("application", "filesurf")
    .register(meterRegistry);
```

## Recommended Fix
Use Option 3 - query the database for active sessions. This is the most accurate approach.

## Implementation

### Step 1: Fix the gauge to query database
Modify `MetricsService.java`:
```java
private int getActiveChatSessionsFromDb() {
    try {
        return sqliteManager.execute(conn -> {
            try (var stmt = conn.createStatement();
                 var rs = stmt.executeQuery("SELECT COUNT(*) FROM chat_session WHERE is_active = 1")) {
                if (rs.next()) {
                    return rs.getInt(1);
                }
            }
            return 0;
        });
    } catch (Exception e) {
        LOGGER.warning("Failed to get active chat sessions count: " + e.getMessage());
        return 0;
    }
}
```

### Step 2: Remove increment/decrement calls
Remove the misleading increment/decrement methods and calls since the gauge will be dynamic.

### Step 3: Ensure database is initialized
The `filesurf.db` database appears to be empty on production. Ensure schema initialization runs correctly.

## Grafana Dashboard Fixes
For immediate relief, update Grafana queries to use:
- `filesurf_active_websocket_connections` - for current connections (this is accurate)
- `rate(filesurf_chat_sessions_started_total[5m])` - for session creation rate
