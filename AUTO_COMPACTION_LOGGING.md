# AUTO_COMPACTION Event Logging and Metrics

## Overview
This document describes the implementation of logging and metrics for auto compaction events from the klawed agent.

## What is AUTO_COMPACTION?
When klawed is started with the `--auto-compact` flag (or `KLAWED_AUTO_COMPACT=1` environment variable), it automatically compacts conversation context when token usage reaches a threshold. This moves older messages to long-term memory (memvid) to free up context space.

According to the SQLite queue specification (`/home/fandalf/git/klawed/docs/sqlite-queue.md`), klawed sends an `AUTO_COMPACTION` message with the following structure:

```json
{
  "messageType": "AUTO_COMPACTION",
  "messagesCompacted": 45,
  "tokensBefore": 92450,
  "tokensAfter": 28760,
  "tokensFreed": 63690,
  "usageBeforePct": 73.9,
  "usageAfterPct": 23.0,
  "content": "Context compaction: 45 messages stored to memory. Tokens: 92450 → 28760 (freed ~63690 tokens). Usage: 73.9% → 23.0%."
}
```

## Implementation

### 1. Constants Added

#### ChatConstants.java
- Added `MESSAGE_TYPE_AUTO_COMPACTION = "AUTO_COMPACTION"` for WebSocket/SQLite queue messages
- Added `DB_MESSAGE_TYPE_AUTO_COMPACTION = "auto_compaction"` for database message type

#### SQLiteQueueConstants.java
- Added `MESSAGE_TYPE_AUTO_COMPACTION = "AUTO_COMPACTION"` for SQLite queue communication

### 2. Message Handling

#### SQLiteQueueClient.java
- Added handler for `AUTO_COMPACTION` messages in the `receiveMessages()` method
- Extracts compaction metrics: messages compacted, tokens before/after, usage percentages
- Logs detailed info with session ID and user ID
- Calls MetricsService to record the event
- Saves message to database (but marked for non-forwarding)

**Log Format:**
```
INFO: AUTO_COMPACTION event for session <sessionId> (user <userId>): <n> messages compacted, tokens: <before> → <after> (freed ~<freed>), usage: <before%>% → <after%>%
```

#### ChatMessagePollingService.java
- Added handler to skip forwarding `AUTO_COMPACTION` messages to WebSocket clients
- Returns `null` for AUTO_COMPACTION messages to prevent user notification
- Marks messages as sent without displaying them

### 3. Prometheus Metrics

#### MetricsService.java
Added two metrics:

1. **Global Counter**: `filesurf_auto_compaction_events`
   - Description: "Total number of auto compaction events"
   - Tags: `application=filesurf`

2. **Per-User Counter**: `filesurf_auto_compaction_events_by_user`
   - Description: "Auto compaction events per user"
   - Tags: `application=filesurf`, `user_id=<userId>`

**Method:**
```java
public void incrementAutoCompactionEvents(String userId)
```

### 4. Configuration Updates

#### SQLiteQueueClient.Config
- Added `userId` field
- Added `metricsService` field
- Added `withUserId(String userId)` method
- Added `withMetricsService(MetricsService metricsService)` method

#### SQLiteQueueClientPool.java
- Injected `MetricsService`
- Updated `getOrCreateClient(String sessionId, String userId)` to accept userId
- Backward compatible `getOrCreateClient(String sessionId)` calls new method with `null`
- Passes userId and MetricsService to SQLiteQueueClient.Config

#### SQLiteQueuePollingService.java
- Updated `pollSessionQueue()` to pass userId to `getOrCreateClient()`

#### ChatMessagePollingService.java
- Updated `sendMessageToKlawed()` to pass userId to `getOrCreateClient()`

### 5. KlawedSocketMessage.java
- Added handler in `toSimpleString()` for AUTO_COMPACTION messages
- Returns human-readable content or `[AUTO COMPACTION]` fallback

## Usage

### Viewing Logs
Auto compaction events are logged at INFO level in the application logs:

```bash
tail -f logs/application.log | grep AUTO_COMPACTION
```

### Querying Prometheus Metrics

**Total auto compaction events:**
```promql
filesurf_auto_compaction_events_total
```

**Auto compaction events by user:**
```promql
filesurf_auto_compaction_events_by_user_total{user_id="user-123"}
```

**Rate of auto compaction events (per minute):**
```promql
rate(filesurf_auto_compaction_events_total[5m]) * 60
```

**Users with most auto compactions:**
```promql
topk(10, filesurf_auto_compaction_events_by_user_total)
```

## Benefits

1. **Visibility**: Operators can see when and how often context compaction occurs
2. **User Tracking**: Metrics tagged with user ID help identify power users or potential issues
3. **Debugging**: Detailed logs help troubleshoot memory/context issues
4. **Monitoring**: Prometheus metrics enable alerting and dashboards
5. **Non-intrusive**: Users don't see compaction events (logged only, not displayed in UI)

## Future Enhancements

Potential improvements:
- Add dashboard showing compaction frequency per user
- Alert if compaction happens too frequently (may indicate context issues)
- Track token savings over time
- Correlate compaction events with session duration/message count
