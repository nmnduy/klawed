# ZMQ Input/Output Message Format

This document describes the JSON message format for ZeroMQ communication in Klawed.

## Overview

Klawed uses a JSON message format for both input (requests) and output (responses) when communicating via ZeroMQ sockets using the PAIR socket pattern (exclusive peer-to-peer communication). The implementation now includes:

1. **Message ID/ACK system** for reliable message delivery
2. **Time-sharing loop** for checking both user input and incoming messages
3. **Pending message queue** with automatic retry mechanism
4. **Enhanced error handling** with ACK/NACK messages

The format is designed to be self-describing and reliable.

## Message Structure

All messages are JSON objects with at least a `messageType` field that indicates the type of message.

### Common Fields

- `messageType` (string, required): Type of message ("TEXT", "ERROR", "TOOL", "TOOL_RESULT", "ACK", or "NACK")
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

### 4. Error Response

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

### 6. NACK Response

Negative acknowledgment (error in processing).

```json
{
  "messageType": "NACK",
  "messageId": "a1b2c3d4e5f678901234567890123456",
  "content": "Error reason for negative acknowledgment"
}
```

**Fields:**
- `messageType`: Always "NACK"
- `messageId`: ID of the message being negatively acknowledged
- `content`: Reason for the negative acknowledgment

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

Klawed now implements a reliable message delivery system with the following features:

### Message ID Generation
- Each message can have a unique `messageId` field (32-character hex string)
- Generated using timestamp + partial content hash + random salt
- Based on FNV-1a hash algorithm with additional mixing

### ACK/NACK Mechanism
- **ACK**: Positive acknowledgment - message received and processed successfully
- **NACK**: Negative acknowledgment - message received but processing failed
- Clients should send ACK for successfully processed messages
- Klawed will automatically resend unacknowledged messages

### Pending Message Queue
- Messages with `messageId` are tracked in a pending queue
- Default maximum pending messages: 50
- Default ACK timeout: 3000ms (3 seconds)
- Default maximum retries: 5
- Automatic cleanup of acknowledged messages

### Time-sharing Loop
The daemon mode now uses a time-sharing approach:
- Checks for user input for 100ms
- Checks for incoming messages for 100ms
- Alternates between input and message processing
- Allows responsive handling of both user interaction and ZMQ communication

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

### Configuration Constants (in code)

The following constants can be modified in `src/zmq_socket.c`:

- `DEFAULT_MAX_PENDING`: Maximum pending messages (default: 50)
- `DEFAULT_ACK_TIMEOUT_MS`: ACK timeout in milliseconds (default: 3000)
- `DEFAULT_MAX_RETRIES`: Maximum retry attempts (default: 5)
- `DEFAULT_TIMESLICE_MS`: Time-sharing interval in milliseconds (default: 100)
- `ZMQ_BUFFER_SIZE`: Message buffer size (default: 65536)
- `MESSAGE_ID_HEX_LENGTH`: Message ID string length (default: 33)
- `HASH_SAMPLE_SIZE`: Characters sampled for message ID hash (default: 256)

## Implementation Details

### Code Location
- Main implementation: `src/zmq_socket.c` - ZMQ socket handling with reliable delivery
- Header: `src/zmq_socket.h` - Interface definitions
- Message ID system: `src/zmq_message_id.h` - Message ID generation and tracking
- Client mode: Built into main `klawed` binary (use `--zmq-client <endpoint>`)
- Daemon mode: Built into main `klawed` binary (use `--zmq <endpoint>`)

### Message Type Constants
- `"TEXT"`: Text processing request and successful response
- `"ERROR"`: Error response
- `"TOOL"`: Tool execution request (sent before tool execution)
- `"TOOL_RESULT"`: Tool execution result (sent after tool execution)
- `"ACK"`: Acknowledgment message
- `"NACK"`: Negative acknowledgment message

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

### New API Functions

The following new functions have been added for reliable message delivery:

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
```

### Error Handling
- JSON parse errors return immediate error response
- Missing required fields return error response
- AI API failures return error with details
- Network/timeout errors return -1 (no response sent)
- Unacknowledged messages are automatically retried (up to 5 times)
- Messages exceeding retry limit are dropped with warning

## Best Practices

### Message Handling with Reliable Delivery
1. **Always include `messageId`** in messages for reliable delivery
2. **Send ACK for received messages** - Respond with ACK message for successful processing
3. **Send NACK for failed processing** - Use NACK with error reason when message processing fails
4. **Check `messageType`** before processing responses
5. **Handle errors gracefully** - check for ERROR and NACK message types
6. **Validate JSON** to ensure message completeness
7. **Use appropriate timeouts** for network operations

### Conversation Flow
8. **Track message sequences**: TOOL → TOOL_RESULT → TEXT for interactive processing
9. **Implement ACK handling** - Process ACK messages to confirm delivery
10. **Monitor pending messages** - Track messages waiting for acknowledgment
11. **Use time-sharing approach** - Alternate between user input and message processing
12. **Ensure buffers are large enough** (minimum 64KB recommended)
13. **If there is no more TOOL without a TOOL_RESULT, you can consider that the agent has completed its turn. This can be used as a signal for continuing to wait**

### Connection Management
14. **Handle connection failures gracefully** - Messages will be retried when connection is restored
15. **Monitor connection state** - The system includes TCP keepalive
16. **Clean up resources** - ensure proper cleanup of ZMQ contexts and sockets
17. **Reuse connections** when possible for performance
18. **Handle multiple messages per request** - The daemon may send multiple messages (TEXT, TOOL, TOOL_RESULT) for a single user request. Clients should keep receiving messages until the conversation is complete. The built-in client mode handles this by receiving multiple messages with a timeout between messages.
19. **Implement message deduplication** - Use `messageId` to detect and handle duplicate messages

### Performance Considerations
20. **Adjust pending queue size** based on expected message volume
21. **Tune ACK timeout** based on network latency
22. **Configure retry limits** based on reliability requirements
23. **Optimize time-sharing interval** for responsiveness vs CPU usage
