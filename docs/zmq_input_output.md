# ZMQ Input/Output Message Format

This document describes the JSON message format for ZeroMQ communication in Klawed.

## Overview

Klawed uses a simple JSON message format for both input (requests) and output (responses) when communicating via ZeroMQ sockets using the PAIR socket pattern (exclusive peer-to-peer communication). The format is designed to be minimal and self-describing.

## Message Structure

All messages are JSON objects with at least a `messageType` field that indicates the type of message.

### Common Fields

- `messageType` (string, required): Type of message ("TEXT", "ERROR", "TOOL", or "TOOL_RESULT")
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

Klawed's ZMQ implementation is simplified and focuses on basic peer-to-peer communication.

### Socket Configuration

The implementation uses ZMQ_PAIR sockets with the following settings:
- **LINGER**: 1000ms (1 second) for clean shutdown
- **TCP keepalive**: Enabled with idle=60s, interval=5s, count=3
- **Receive timeout**: Configurable via function parameter (default: infinite for daemon mode)
- **Buffer size**: 65536 bytes (configurable via `ZMQ_BUFFER_SIZE` in code)

### Environment Variables

Only basic environment variables are supported:

- `KLAWED_ZMQ_ENDPOINT`: ZMQ endpoint (e.g., "tcp://127.0.0.1:5555" or "ipc:///tmp/klawed.sock")
- `KLAWED_ZMQ_MODE`: ZMQ mode ("daemon")

**Note**: Previous versions supported many configuration options (heartbeat, reconnection, message queues, etc.), but these have been removed in the simplified implementation. The `src/zmq_config.h` file contains legacy configuration constants that are no longer used.

## Implementation Details

### Code Location
- Main implementation: `src/zmq_socket.c` - Simplified ZMQ socket handling
- Header: `src/zmq_socket.h` - Interface definitions
- Example client: `examples/zmq_client.c` - Simple interactive client

### Message Type Constants
- `"TEXT"`: Text processing request and successful response
- `"ERROR"`: Error response
- `"TOOL"`: Tool execution request (sent before tool execution)
- `"TOOL_RESULT"`: Tool execution result (sent after tool execution)

### Buffer Sizes
- Default buffer size: `ZMQ_BUFFER_SIZE` (65536 bytes, defined in code)
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
9. **Handle connection failures gracefully** - the simplified implementation doesn't include automatic reconnection
10. **Monitor connection state** - implement basic keepalive in your client if needed
11. **Clean up resources** - ensure proper cleanup of ZMQ contexts and sockets
12. **Reuse connections** when possible for performance
13. **Handle multiple messages per request** - The daemon may send multiple messages (TEXT, TOOL, TOOL_RESULT) for a single user request. Clients should keep receiving messages until the conversation is complete. The example client (`examples/zmq_client.c`) has been updated to handle this by receiving multiple messages with a timeout between messages.
