# Metrics Fix - Active Chat Sessions

## Problem Summary
The Grafana dashboard shows 388 active chat sessions when there are only 2 active WebSocket connections. This happened because:

1. Recent commit (132a23c "stop containers when idled") changed session lifecycle to keep agents alive after WebSocket close (for reconnections)
2. But the `filesurf_active_chat_sessions` gauge was still being incremented on every WebSocket open
3. The corresponding `decrementChatSessions()` call was intentionally removed from WebSocket close
4. Result: The gauge kept growing and never decreased

## Solution Implemented

Changed the `filesurf_active_chat_sessions` gauge from a **counter-based approach** to a **dynamic query approach**:

### Before (Counter-Based)
```java
// Maintained an AtomicInteger that was incremented/decremented
private AtomicInteger activeChatSessions;
Gauge.builder("filesurf_active_chat_sessions", activeChatSessions, AtomicInteger::get)
    .register(meterRegistry);

// On WebSocket open:
activeChatSessions.incrementAndGet();  // ❌ Increments every time

// On WebSocket close:
// activeChatSessions.decrementAndGet(); // ❌ Removed (intentionally)
```

### After (Dynamic Query)
```java
// Query the actual state from KlawedAgentManager
Gauge.builder("filesurf_active_chat_sessions", this, MetricsService::getActiveChatSessionsCount)
    .register(meterRegistry);

private double getActiveChatSessionsCount() {
    List<String> activeSessions = klawedAgentManager.getActiveSessions();
    return activeSessions != null ? activeSessions.size() : 0;
}
```

## Changes Made

### 1. `MetricsService.java`
- **Removed**: `activeChatSessions` AtomicInteger field
- **Added**: `klawedAgentManager` injection
- **Added**: `getActiveChatSessionsCount()` method that queries agent manager
- **Modified**: `incrementChatSessions()` to only increment the counter (not the gauge)
- **Removed**: `decrementChatSessions()` method (no longer needed)
- **Modified**: `getActiveChatSessions()` to call `getActiveChatSessionsCount()`
- **Added**: Import for `java.util.List`

### 2. Metrics Exposed
- `filesurf_chat_sessions_started_total` (counter) - Total sessions ever started ✅ Accurate
- `filesurf_active_chat_sessions` (gauge) - **Now queries actual agent count** ✅ Fixed
- `filesurf_active_websocket_connections` (gauge) - Current WebSocket connections ✅ Always accurate

## Testing

To verify the fix:

```bash
# 1. Build and deploy
mvn clean package -DskipTests
./deployment/deploy-rsync.sh

# 2. Check metrics on production
ssh pie-01 'curl -s http://filesurf-0:9090/metrics | grep filesurf_active_chat_sessions'

# Expected output:
# filesurf_active_chat_sessions{application="filesurf"} 2.0
# (should match the number of active agents, not accumulate)
```

## Benefits

1. **Accurate Metrics**: Gauge always reflects actual running agents
2. **Self-Healing**: No risk of counter drift - always queries authoritative source
3. **Simpler Code**: No need to track increment/decrement calls across multiple code paths
4. **Resilient**: Survives app restarts without losing accuracy

## Grafana Dashboard Recommendations

For better monitoring, use these queries:

1. **Current Active Sessions**: `filesurf_active_chat_sessions`
   - Now accurately shows sessions with running agents

2. **Current WebSocket Connections**: `filesurf_active_websocket_connections`
   - Shows live WebSocket connections (can be higher during reconnects)

3. **Session Creation Rate**: `rate(filesurf_chat_sessions_started_total[5m])`
   - Shows how many new sessions are being created over time

4. **Container Count**: `filesurf_klawed_containers_active`
   - Shows actual running containers (should match active sessions in sandbox mode)

## Related Files
- `src/main/java/com/filesurf/service/MetricsService.java`
- `src/main/java/com/filesurf/websocket/FileChatWebSocket.java` (no changes needed)
- `src/main/java/com/filesurf/service/AgentShutdownJobService.java` (idle cleanup runs every 5 min)
