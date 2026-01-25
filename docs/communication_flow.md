# Communication Flow & Polling Architecture

This document describes the end-to-end message flow between the client, FileSurf application, and klawed agents.

## Architecture Overview

```
┌─────────────┐     WebSocket      ┌─────────────────────┐     SQLite Queue      ┌─────────────┐
│   Client    │ ◄────────────────► │    FileSurf App     │ ◄───────────────────► │   klawed    │
│  (Browser)  │                    │                     │                       │   Agent     │
└─────────────┘                    └─────────────────────┘                       └─────────────┘
                                          │                                               │
                                          │ Polling                                      │
                                          ▼                                               │
                                 ┌─────────────────────┐                               │
                                 │   SQLite Database   │ ◄───────────────────────────────┘
                                 │   (messages.db)     │          Polls & saves
                                 └─────────────────────┘          messages to DB
```

## Message Flow: Client to klawed

### Steps:

1. **Client sends message** via WebSocket text frame
2. **FileChatWebSocket** receives the message
3. **FileChatWebSocket.sendMessageToKlawed()** is called
4. **SQLiteQueueClient.sendMessageFrom("client", "klawed", message)** writes to SQLite queue
5. **klawed agent** polls its queue, receives the message, processes it, and sends responses back

### Code Path:

```
Client (WebSocket)
  └─> FileChatWebSocket.incomingMessage()
        └─> chatMessagePollingService.sendMessageToKlawed(sessionId, userId, message)
              └─> sqliteQueueClient.sendMessageFrom("client", "klawed", message)
                    └─> Writes to klawed_messages_{sessionId}.db
                          └─> klawed polls and receives message
```

## Message Flow: klawed to Client

This is more complex due to the polling architecture:

### Steps:

1. **klawed agent** writes responses to SQLite queue (sender=klawed, receiver=client)
2. **SQLiteQueuePollingService** polls the queue every 1 second
3. **Received messages** are saved to the database with `sent = 0`
4. **ChatMessagePollingService** polls for unsent messages every 1 second
5. **Messages are sent** to the client via WebSocket
6. **On success**, message is marked as `sent = 1`

### Code Path:

```
klawed Agent
  └─> Writes response to klawed_messages_{sessionId}.db (sender=klawed, receiver=client)

SQLiteQueuePollingService.pollSQLiteQueues() [every 1s]
  └─> Polls all active session queues
        └─> Receives messages from klawed
              └─> fileChatService.createChatMessage() [saves to DB with sent=0]

ChatMessagePollingService.pollAndSendUnsentMessages() [every 1s]
  └─> fileChatService.findUnsentMessagesForSession(sessionId) [sent=0]
        └─> connection.sendText(jsonMessage) [sends to WebSocket client]
              └─> On success: fileChatService.markMessageAsSent() [sets sent=1]

Client (Browser)
  └─> Receives message via WebSocket
```

## Polling Services

### 1. SQLiteQueuePollingService

**Purpose**: Poll SQLite queues for messages FROM klawed agents

| Property | Value |
|----------|-------|
| Schedule | Every 1 second |
| Method | `pollSQLiteQueues()` |
| Database | `klawed_messages_{sessionId}.db` per session |

**Behavior**:
- Loops through all active sessions
- Polls each session's SQLite queue for messages from klawed
- Saves received messages to the main database
- Marks messages as received in the queue (acked)

### 2. ChatMessagePollingService

**Purpose**: Poll database for unsent messages TO clients

| Property | Value |
|----------|-------|
| Schedule | Every 1 second |
| Method | `pollAndSendUnsentMessages()` |
| Executor | Single-threaded (serializes all polling) |

**Behavior**:
- Only polls for sessions with active WebSocket connections
- Queries `messages` table for `sent = 0` records
- Sends messages to clients via WebSocket
- On WebSocket send success: marks message as `sent = 1`
- On failure: leaves `sent = 0` for retry on next cycle

**Key Design**:
- Single-threaded executor prevents race conditions
- Graceful shutdown (500ms) ensures in-flight sends complete
- Stale connections are cleaned up automatically

## Message States

Messages in the database have a `sent` flag:

| State | Meaning |
|-------|---------|
| `sent = 0` | Message received from klawed, awaiting delivery to client |
| `sent = 1` | Message successfully delivered to client |

**State Transitions**:

```
[sent=0] ──(send success)──► [sent=1]
     │
     └──(send failure)──► stays [sent=0] ──(retry on next poll)──►
```

## Race Condition Prevention

### The Problem (Historical)

Previously, two pollers could compete for the same messages:

1. **Catch-up poller**: Called immediately on WebSocket connect
2. **Scheduled poller**: Runs every 1 second

If both ran simultaneously, they could pick up the same unsent messages before either marked them as sent, resulting in duplicates.

### The Solution

**Single poller only**: The scheduled `pollAndSendUnsentMessages()` handles all message delivery. No catch-up poller on connection.

**Single-threaded executor**: All polling operations are serialized, preventing concurrent access to messages.

**Graceful shutdown**: 500ms window during shutdown allows in-flight WebSocket sends to complete before the executor terminates.

## Database Tables

### messages table

```sql
CREATE TABLE messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id TEXT NOT NULL,
    role TEXT NOT NULL,          -- 'agent' or 'user'
    sender TEXT NOT NULL,        -- 'user', 'agent', 'system'
    content TEXT,
    message_type TEXT,           -- 'text', 'status', 'error', 'api_call', 'tool', etc.
    sent BOOLEAN NOT NULL DEFAULT FALSE,
    created_at TIMESTAMP NOT NULL
);
```

### klawed_messages_{sessionId}.db (per session)

SQLite queue database for communication with klawed:
- **Sender**: "klawed" (messages from klawed to app)
- **Receiver**: "client" (app receiving from klawed)

## Error Handling

### WebSocket Send Failure

If `connection.sendText()` fails:
- Message stays with `sent = 0`
- Retry on next polling cycle
- Warning logged: `"Failed to send message ID: X, error: ..."`

### klawed Queue Polling Failure

If SQLite queue poll fails:
- Error logged, but other sessions continue
- No message loss (messages remain in queue)

### Application Shutdown

1. Stop accepting new polling tasks (`pollingActive = false`)
2. Clear connection map
3. 500ms grace period for in-flight callbacks
4. Shutdown executor gracefully (5s timeout)

## Sequence Diagram

```
Client                    FileSurf                     klawed
   │                         │                           │
   │───Message──────────────►│                           │
   │   (WebSocket)           │                           │
   │                         │───SQLite Queue───────────►│
   │                         │   (sendMessageFrom)       │
   │                         │                           │
   │                         │◄───SQLite Queue───────────│
   │                         │   (pollSQLiteQueues)      │
   │                         │                           │
   │                         │───Save to DB─────────────►│
   │                         │   (sent=0)                │
   │                         │                           │
   │                         │◄───Poll DB────────────────│
   │                         │   (pollAndSendUnsent)     │
   │                         │                           │
   │◄───Message──────────────│                           │
   │   (WebSocket)           │                           │
   │                         │───Mark sent=1────────────►│
   │                         │                           │
```

## Summary

| Direction | Mechanism | Polling Service |
|-----------|-----------|-----------------|
| Client → klawed | Direct SQLite queue write | None (synchronous) |
| klawed → Client | DB polling + WebSocket | `SQLiteQueuePollingService` + `ChatMessagePollingService` |

The dual-polling design ensures:
- Reliable message delivery (retries on failure)
- No message loss if client disconnects temporarily
- No duplicates (single-threaded, serialized polling)
- Graceful shutdown without lost messages