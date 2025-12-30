# ZMQ Input/Output Message Format

This document describes the JSON message format for ZeroMQ communication in Klawed.

## Overview

Klawed uses a JSON message format for both input (requests) and output (responses) when communicating via ZeroMQ sockets using the PAIR socket pattern (exclusive peer-to-peer communication). The implementation includes:

1. **Message ID/ACK system** for reliable message delivery
2. **Background polling thread** for receiving messages
3. **Thread pool** for asynchronous tool execution
4. **Pending message queue** with automatic retry mechanism
5. **Duplicate message detection** to prevent processing the same message multiple times

The format is designed to be self-describing and reliable.

## Message Structure

All messages are JSON objects with at least a `messageType` field that indicates the type of message.

### Common Fields

- `messageType` (string, required): Type of message ("TEXT", "ERROR", "TOOL", "TOOL_RESULT", "API_CALL", or "ACK")
- `content` (string, optional): Primary content/message text
- `messageId` (string, optional): Unique message identifier for reliable delivery (32-character hex string)

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
  "content": "AI-generated response text",
  "messageId": "a1b2c3d4e5f678901234567890123456"
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
  },
  "messageId": "a1b2c3d4e5f678901234567890123456"
}
```

**Fields:**
- `messageType`: Always "TOOL"
- `toolName`: Name of the tool that will be executed
- `toolId`: ID of the tool call (matches the tool call ID from the AI)
- `toolParameters`: JSON object containing the tool input parameters (can be null)
- `messageId`: Unique message identifier (optional but recommended)

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
  "isError": false,
  "messageId": "a1b2c3d4e5f678901234567890123456"
}
```

**Fields:**
- `messageType`: Always "TOOL_RESULT"
- `toolName`: Name of the tool that was executed
- `toolId`: ID of the tool call (matches the tool call ID from the AI)
- `toolOutput`: JSON object containing the tool execution results
- `isError`: Boolean indicating if the tool execution resulted in an error
- `messageId`: Unique message identifier (optional but recommended)

### 4. API_CALL Message

API call in progress (sent before making an API call to indicate waiting time):

```json
{
  "messageType": "API_CALL",
  "timestamp": 1735579200,
  "timestampMs": 1735579200123,
  "estimatedDurationMs": 5000,
  "model": "gpt-4",
  "provider": "openai",
  "messageId": "a1b2c3d4e5f678901234567890123456"
}
```

**Fields:**
- `messageType`: Always "API_CALL"
- `timestamp`: Unix timestamp (seconds)
- `timestampMs`: Unix timestamp with milliseconds (optional, more precise)
- `estimatedDurationMs`: Estimated duration of the API call in milliseconds (optional)
- `model`: AI model being used (optional)
- `provider`: API provider (e.g., "openai", "anthropic", "bedrock") (optional)
- `messageId`: Unique message identifier (optional but recommended)

### 5. Error Response

Error messages for various failure conditions.

```json
{
  "messageType": "ERROR",
  "content": "Error description",
  "messageId": "a1b2c3d4e5f678901234567890123456"
}
```

### 5. ACK Response

Acknowledgment message for reliable delivery.

```json
{
  "messageType": "ACK",
  "messageId": "a1b2c3d4e5f678901234567890123456"
}
```

**Fields:**
- `messageType`: Always "ACK"
- `messageId`: ID of the message being acknowledged

## Message Flow Examples

### Simple Text Processing with ACK

**Client sends (with message ID):**
```json
{
  "messageType": "TEXT",
  "content": "What is 2+2?",
  "messageId": "client_query_001"
}
```

**Klawed responds with ACK and answer:**
```json
{
  "messageType": "ACK",
  "messageId": "client_query_001"
}
```

```json
{
  "messageType": "TEXT",
  "content": "2+2 equals 4.",
  "messageId": "klawed_response_001"
}
```

**Client sends ACK:**
```json
{
  "messageType": "ACK",
  "messageId": "klawed_response_001"
}
```

### Interactive Processing with Tool Calls and ACK

**Client sends (with message ID):**
```json
{
  "messageType": "TEXT",
  "content": "Read the file README.md and tell me what it says",
  "messageId": "client_msg_001"
}
```

**Klawed responds with ACK and multiple messages:**

1. ACK for client message:
```json
{
  "messageType": "ACK",
  "messageId": "client_msg_001"
}
```

2. TOOL message before execution (with message ID):
```json
{
  "messageType": "TOOL",
  "toolName": "Read",
  "toolId": "tool_call_abc123",
  "toolParameters": {
    "file_path": "README.md"
  },
  "messageId": "klawed_msg_001"
}
```

**Client sends ACK for TOOL message:**
```json
{
  "messageType": "ACK",
  "messageId": "klawed_msg_001"
}
```

3. TOOL_RESULT message after execution (with message ID):
```json
{
  "messageType": "TOOL_RESULT",
  "toolName": "Read",
  "toolId": "tool_call_abc123",
  "toolOutput": {
    "content": "# My Project\n\nThis is a sample README file...",
    "file_path": "README.md"
  },
  "isError": false,
  "messageId": "klawed_msg_002"
}
```

**Client sends ACK for TOOL_RESULT:**
```json
{
  "messageType": "ACK",
  "messageId": "klawed_msg_002"
}
```

4. TEXT response with AI analysis (with message ID):
```json
{
  "messageType": "TEXT",
  "content": "The README.md file contains: '# My Project\\n\\nThis is a sample README file...'",
  "messageId": "klawed_msg_003"
}
```

**Client sends ACK for final response:**
```json
{
  "messageType": "ACK",
  "messageId": "klawed_msg_003"
}
```

**Note:** If Klawed doesn't receive an ACK within the timeout period (default: 3 seconds), it will automatically resend the message. This continues up to the maximum retry count (default: 5).

## Reliable Delivery System

Klawed implements a reliable message delivery system with the following features:

### Message ID Generation
- Each message can have a unique `messageId` field (32-character hex string)
- Generated using timestamp + partial content hash + random salt
- Based on FNV-1a hash algorithm with additional mixing
- Implemented in `zmq_socket.c` with `zmq_generate_message_id()` function

### ACK Mechanism
- **ACK**: Positive acknowledgment - message received and processed successfully
- Clients should send ACK for successfully processed messages
- Klawed will automatically resend unacknowledged messages

### Pending Message Queue
- Messages with `messageId` are tracked in a pending queue
- Default maximum pending messages: 50
- Default ACK timeout: 3000ms (3 seconds)
- Default maximum retries: 5
- Automatic cleanup of acknowledged messages
- Automatic resend of timed-out messages

### Duplicate Message Detection
- Recently seen message IDs are tracked (up to 1000 messages)
- Default TTL for seen messages: 30 seconds
- Duplicate messages are acknowledged but not processed

## Daemon Architecture

The ZMQ daemon uses a multi-threaded architecture:

### 1. Background Polling Thread
- Continuously polls the ZMQ socket for incoming messages
- Processes ACK messages immediately (removes from pending queue)
- Queues other messages for main thread processing
- Handles termination messages to stop the daemon

### 2. Main Thread
- Processes messages from the background thread queue
- Handles AI API calls and conversation management
- Manages the pending message queue and retry logic
- Coordinates tool execution via thread pool

### 3. Thread Pool (Optional)
- Executes tools asynchronously to prevent blocking
- Default: 4 threads, max 50 queued tasks
- Can be disabled via `KLAWED_ZMQ_DISABLE_THREAD_POOL=1`
- Each tool execution sends TOOL and TOOL_RESULT messages

## Connection Management

Klawed's ZMQ implementation provides reliable peer-to-peer communication.

### Socket Configuration

The implementation uses ZMQ_PAIR sockets with the following settings:
- **LINGER**: 1000ms (1 second) for clean shutdown
- **TCP keepalive**: Enabled with idle=60s, interval=5s, count=3
- **Receive timeout**: Configurable via function parameter (default: infinite for daemon mode)
- **Buffer size**: 65536 bytes (configurable via `ZMQ_BUFFER_SIZE` in code)

### Environment Variables

- `KLAWED_ZMQ_ENDPOINT`: ZMQ endpoint (e.g., "tcp://127.0.0.1:5555" or "ipc:///tmp/klawed.sock")
- `KLAWED_ZMQ_MODE`: ZMQ mode ("daemon")
- `KLAWED_ZMQ_DISABLE_THREAD_POOL`: Set to "1" to disable thread pool for tool execution

### Configuration Constants (in code)

The following constants can be modified in `src/zmq_socket.c`:

- `DEFAULT_MAX_PENDING`: Maximum pending messages (default: 50)
- `DEFAULT_ACK_TIMEOUT_MS`: ACK timeout in milliseconds (default: 3000)
- `DEFAULT_MAX_RETRIES`: Maximum retry attempts (default: 5)
- `ZMQ_BUFFER_SIZE`: Message buffer size (default: 65536)
- `MESSAGE_ID_HEX_LENGTH`: Message ID string length (default: 33)
- `HASH_SAMPLE_SIZE`: Characters sampled for message ID hash (default: 256)
- `MAX_SEEN_MESSAGES`: Maximum number of messages to track for duplicate detection (default: 1000)
- `SEEN_MESSAGE_TTL_MS`: Time-to-live for seen messages in milliseconds (default: 30000)

## Implementation Details

### Code Location
- Main implementation: `src/zmq_socket.c` - ZMQ socket handling with reliable delivery
- Header: `src/zmq_socket.h` - Interface definitions
- Thread pool: `src/zmq_thread_pool.c` - Asynchronous tool execution
- Client mode: `src/zmq_client.c` - ZMQ client implementation
- Daemon mode: Built into main `klawed` binary (use `--zmq <endpoint>`)
- Client mode: Built into main `klawed` binary (use `--zmq-client <endpoint>`)

### Message Type Constants
- `"TEXT"`: Text processing request and successful response
- `"ERROR"`: Error response
- `"TOOL"`: Tool execution request (sent before tool execution)
- `"TOOL_RESULT"`: Tool execution result (sent after tool execution)
- `"API_CALL"`: API call in progress (sent before making API calls)
- `"ACK"`: Acknowledgment message

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

### API Functions

The following functions are implemented for reliable message delivery:

```c
// Generate unique message ID
int zmq_generate_message_id(ZMQContext *ctx, const char *message, size_t message_len,
                           char *out_id, size_t out_id_size);

// Send message with ID and track for ACK
int zmq_socket_send_with_id(ZMQContext *ctx, const char *message, size_t message_len,
                           char *message_id_out, size_t message_id_out_size);

// Send ACK for received message
int zmq_send_ack(ZMQContext *ctx, const char *message_id);

// Process ACK message
int zmq_process_ack(ZMQContext *ctx, const char *message_id);

// Check and resend pending messages
int zmq_check_and_resend_pending(ZMQContext *ctx, int64_t current_time_ms);

// Clean up pending queue
void zmq_cleanup_pending_queue(ZMQContext *ctx);

// Run ZMQ daemon mode
int zmq_socket_daemon_mode(ZMQContext *ctx, struct ConversationState *state);

// Send tool request message
int zmq_send_tool_request(ZMQContext *ctx, const char *tool_name, const char *tool_id,
                          cJSON *tool_parameters);

// Send tool result message
int zmq_send_tool_result(ZMQContext *ctx, const char *tool_name, const char *tool_id,
                         cJSON *tool_output, int is_error);
```

### Error Handling
- JSON parse errors return immediate error response
- Missing required fields return error response
- AI API failures return error with details
- Network/timeout errors return -1 (no response sent)
- Unacknowledged messages are automatically retried (up to 5 times)
- Messages exceeding retry limit are dropped with warning
- Duplicate messages are acknowledged but not processed

## Best Practices

### Message Handling with Reliable Delivery
1. **Always include `messageId`** in messages for reliable delivery
2. **Send ACK for received messages** - Respond with ACK message for successful processing
3. **Check `messageType`** before processing responses
4. **Handle errors gracefully** - check for ERROR message type
5. **Validate JSON** to ensure message completeness
6. **Use appropriate timeouts** for network operations

### Conversation Flow
7. **Track message sequences**: TOOL → TOOL_RESULT → TEXT for interactive processing
8. **Implement ACK handling** - Process ACK messages to confirm delivery
9. **Monitor pending messages** - Track messages waiting for acknowledgment
10. **Ensure buffers are large enough** (minimum 64KB recommended)
11. **If there is no more TOOL without a TOOL_RESULT, you can consider that the agent has completed its turn. This can be used as a signal for continuing to wait**

### Connection Management
12. **Handle connection failures gracefully** - Messages will be retried when connection is restored
13. **Monitor connection state** - The system includes TCP keepalive
14. **Clean up resources** - ensure proper cleanup of ZMQ contexts and sockets
15. **Reuse connections** when possible for performance
16. **Handle multiple messages per request** - The daemon may send multiple messages (TEXT, TOOL, TOOL_RESULT) for a single user request. Clients should keep receiving messages until the conversation is complete. The built-in client mode handles this by receiving multiple messages with a timeout between messages.
17. **Implement message deduplication** - Use `messageId` to detect and handle duplicate messages

### Performance Considerations
18. **Adjust pending queue size** based on expected message volume
19. **Tune ACK timeout** based on network latency
20. **Configure retry limits** based on reliability requirements
21. **Enable/disable thread pool** based on tool execution requirements
22. **Monitor thread pool statistics** for performance tuning