# ZMQ Input/Output Message Format

This document describes the JSON message format for ZeroMQ communication in Klawed.

## Overview

Klawed uses a simple JSON message format for both input (requests) and output (responses) when communicating via ZeroMQ sockets using the PAIR socket pattern (exclusive peer-to-peer communication). The format is designed to be minimal and self-describing.

## Message Structure

All messages are JSON objects with at least a `messageType` field that indicates the type of message.

### Common Fields

- `messageType` (string, required): Type of message ("TEXT", "ERROR", "TOOL", "TOOL_RESULT", "HEARTBEAT_PING", or "HEARTBEAT_PONG")
- `content` (string, optional): Primary content/message text

## Input Messages (Client → Klawed)

### Text Processing Request

Send a text prompt to Klawed for AI processing.

```json
{
  "messageType": "TEXT",
  "content": "Your prompt here"
}
```

**Fields:**
- `messageType`: Must be "TEXT"
- `content`: The text prompt to process

## Output Messages (Klawed → Client)

### 1. Text Response

Response to a TEXT request with AI-generated content.

```json
{
  "messageType": "TEXT",
  "content": "AI-generated response text"
}
```

### 2. Tool Request Response

Sent when a tool is about to be executed during interactive processing.

```json
{
  "messageType": "TOOL",
  "toolName": "ToolName",
  "toolId": "tool_call_abc123",
  "toolParameters": {
    "param1": "value1",
    "param2": "value2"
  }
}
```

**Fields:**
- `messageType`: Always "TOOL"
- `toolName`: Name of the tool that will be executed
- `toolId`: ID of the tool call (matches the tool call ID from the AI)
- `toolParameters`: JSON object containing the tool input parameters (can be null)

### 3. Tool Result Response

Sent when a tool execution completes during interactive processing.

```json
{
  "messageType": "TOOL_RESULT",
  "toolName": "ToolName",
  "toolId": "tool_call_abc123",
  "toolOutput": {
    "result": "tool output data"
  },
  "isError": false
}
```

**Fields:**
- `messageType`: Always "TOOL_RESULT"
- `toolName`: Name of the tool that was executed
- `toolId`: ID of the tool call (matches the tool call ID from the AI)
- `toolOutput`: JSON object containing the tool execution results
- `isError`: Boolean indicating if the tool execution resulted in an error

### 4. Error Response

Error messages for various failure conditions.

```json
{
  "messageType": "ERROR",
  "content": "Error description"
}
```

### 5. Heartbeat Messages

For connection health monitoring:

**Ping:**
```json
{
  "messageType": "HEARTBEAT_PING",
  "timestamp": 1703456789
}
```

**Pong:**
```json
{
  "messageType": "HEARTBEAT_PONG",
  "pingTimestamp": 1703456789
}
```

## Message Flow Examples

### Simple Text Processing

**Client sends:**
```json
{
  "messageType": "TEXT",
  "content": "What is 2+2?"
}
```

**Klawed responds:**
```json
{
  "messageType": "TEXT",
  "content": "2+2 equals 4."
}
```

### Interactive Processing with Tool Calls

**Client sends:**
```json
{
  "messageType": "TEXT",
  "content": "Read the file README.md and tell me what it says"
}
```

**Klawed responds with multiple messages:**

1. TOOL message before execution:
```json
{
  "messageType": "TOOL",
  "toolName": "Read",
  "toolId": "tool_call_abc123",
  "toolParameters": {
    "file_path": "README.md"
  }
}
```

2. TOOL_RESULT message after execution:
```json
{
  "messageType": "TOOL_RESULT",
  "toolName": "Read",
  "toolId": "tool_call_abc123",
  "toolOutput": {
    "content": "# My Project\n\nThis is a sample README file...",
    "file_path": "README.md"
  },
  "isError": false
}
```

3. TEXT response with AI analysis:
```json
{
  "messageType": "TEXT",
  "content": "The README.md file contains: '# My Project\\n\\nThis is a sample README file...'"
}
```

## Connection Management

Klawed's ZMQ implementation includes robustness features for network reliability.

### Heartbeat Mechanism

Monitor connection health with ping/pong messages:

**Ping:**
```json
{
  "messageType": "HEARTBEAT_PING",
  "timestamp": 1703456789
}
```

**Pong:**
```json
{
  "messageType": "HEARTBEAT_PONG",
  "pingTimestamp": 1703456789
}
```

### Automatic Reconnection

When connections fail, Klawed can automatically reconnect with exponential backoff:
- Base interval: 1 second (configurable)
- Maximum interval: 30 seconds (configurable)
- Maximum attempts: 10 (configurable)

### Message Queue

Messages can be queued during disconnections to prevent loss:
- Configurable queue size (default: 100 messages)
- Queued messages sent when connection is restored

### Configuration

Environment variables for tuning:

**Heartbeat:**
- `KLAWED_ZMQ_HEARTBEAT_INTERVAL`: Ping interval in ms (default: 5000)
- `KLAWED_ZMQ_ENABLE_HEARTBEAT`: Enable heartbeat (default: false)

**Reconnection:**
- `KLAWED_ZMQ_RECONNECT_INTERVAL`: Base reconnect interval in ms (default: 1000)
- `KLAWED_ZMQ_MAX_RECONNECT_ATTEMPTS`: Max reconnect attempts (default: 10)
- `KLAWED_ZMQ_ENABLE_RECONNECT`: Enable auto-reconnect (default: false)

**Message Queue:**
- `KLAWED_ZMQ_SEND_QUEUE_SIZE`: Send queue capacity (default: 100)
- `KLAWED_ZMQ_RECEIVE_QUEUE_SIZE`: Receive queue capacity (default: 100)

**Timeouts:**
- `KLAWED_ZMQ_RECEIVE_TIMEOUT`: Receive timeout in ms (default: 30000)
- `KLAWED_ZMQ_SEND_TIMEOUT`: Send timeout in ms (default: 10000)
- `KLAWED_ZMQ_CONNECT_TIMEOUT`: Connect timeout in ms (default: 5000)

**Buffers:**
- `KLAWED_ZMQ_BUFFER_SIZE`: Buffer size in bytes (default: 65536)
- `KLAWED_ZMQ_MAX_MESSAGE_SIZE`: Maximum message size in bytes (default: 1048576 = 1MB)

## Implementation Details

### Code Location
- Input parsing: `src/zmq_socket.c` - `zmq_socket_process_message()` function
- Response generation: Same function, various response creation sections

### Message Type Constants
- `"TEXT"`: Text processing request and successful response
- `"ERROR"`: Error response
- `"TOOL"`: Tool execution request (sent before tool execution)
- `"TOOL_RESULT"`: Tool execution result (sent after tool execution)
- `"HEARTBEAT_PING"`: Connection health check ping
- `"HEARTBEAT_PONG"`: Connection health check pong response

### Buffer Sizes
- Input buffer: `ZMQ_BUFFER_SIZE` (65536 bytes, configurable via `KLAWED_ZMQ_BUFFER_SIZE`)
- Response buffer: Same size
- Maximum message size: `ZMQ_MAX_MESSAGE_SIZE` (1048576 bytes = 1MB, configurable via `KLAWED_ZMQ_MAX_MESSAGE_SIZE`)
- Messages larger than buffer will be truncated (with warning logged)

### Message Framing

ZeroMQ handles message framing internally:
- Each `zmq_send()` creates one discrete message
- Each `zmq_recv()` receives one complete message
- No explicit end-of-message markers needed
- Return value from `zmq_recv()` indicates exact message length

**Key Points:**
1. ZeroMQ guarantees message boundaries at the transport layer
2. Messages are self-contained JSON objects
3. Clients should validate JSON to ensure message completeness

### Error Handling
- JSON parse errors return immediate error response
- Missing required fields return error response
- AI API failures return error with details
- Network/timeout errors return -1 (no response sent)

## Best Practices

### Message Handling
1. **Check `messageType`** before processing responses
2. **Handle errors gracefully** - check for ERROR messageType
3. **Validate JSON** to ensure message completeness
4. **Use appropriate timeouts** for network operations

### Conversation Flow
5. **Track message sequences**: TOOL → TOOL_RESULT → TEXT for interactive processing
6. **Use timeout-based polling** to detect conversation completion
7. **Ensure buffers are large enough** (minimum 64KB recommended)
8. **If there is no more TOOL without a TOOL_RESULT, you can consider that the agent has completed its turn. This can be used as a signal for continuing to wait**

### Connection Management
9. **Implement reconnection logic** for network failures
10. **Use heartbeats** for connection health monitoring
11. **Queue messages during outages** for reliable delivery
12. **Reuse connections** when possible for performance
