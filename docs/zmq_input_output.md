# ZMQ Input/Output Message Format

This document describes the JSON message format for ZeroMQ communication in Klawed.

## Overview

Klawed uses a simple JSON message format for both input (requests) and output (responses) when communicating via ZeroMQ sockets using the PAIR socket pattern (exclusive peer-to-peer communication). The format is designed to be minimal and self-describing.

## Message Structure

All messages are JSON objects with at least a `messageType` field that indicates the type of message.

### Common Fields

- `messageType` (string, required): Type of message ("TEXT", "ERROR", "TOOL_RESULT", "HEARTBEAT_PING", or "HEARTBEAT_PONG")
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

**Example:**
```json
{
  "messageType": "TEXT",
  "content": "Write a hello world program in C"
}
```

## Output Messages (Klawed → Client)

### 1. Text Response

Response to a TEXT request with AI-generated content.

```json
{
  "messageType": "TEXT",
  "content": "AI-generated response text"
}
```

**Fields:**
- `messageType`: Always "TEXT" for text responses
- `content`: The AI-generated response text

**Example:**
```json
{
  "messageType": "TEXT",
  "content": "Here's a hello world program in C:\n\n```c\n#include <stdio.h>\n\nint main() {\n    printf(\"Hello, world!\\n\");\n    return 0;\n}\n```"
}
```

### 2. Tool Result Response

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

**Example (successful tool execution):**
```json
{
  "messageType": "TOOL_RESULT",
  "toolName": "Read",
  "toolId": "tool_call_abc123",
  "toolOutput": {
    "content": "File contents here...",
    "file_path": "/path/to/file.txt"
  },
  "isError": false
}
```

**Example (tool error):**
```json
{
  "messageType": "TOOL_RESULT",
  "toolName": "Read",
  "toolId": "tool_call_abc123",
  "toolOutput": {
    "error": "File not found: /path/to/nonexistent.txt"
  },
  "isError": true
}
```


### 4. Error Response

Error messages for various failure conditions.

```json
{
  "messageType": "ERROR",
  "content": "Error description"
}
```

**Common error types:**

**JSON parse error:**
```json
{
  "messageType": "ERROR",
  "content": "Invalid JSON"
}
```

**Invalid message format:**
```json
{
  "messageType": "ERROR",
  "content": "Invalid message format"
}
```

**AI inference error:**
```json
{
  "messageType": "ERROR",
  "content": "AI inference error"
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

1. First, the AI might decide to use a tool:
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

2. Then the AI processes the tool result and responds:
```json
{
  "messageType": "TEXT",
  "content": "The README.md file contains: '# My Project\\n\\nThis is a sample README file...'"
}
```



### Multi-turn Conversation

**First client message:**
```json
{
  "messageType": "TEXT",
  "content": "What's in the current directory?"
}
```

**Klawed responds (after possible tool calls):**
```json
{
  "messageType": "TEXT",
  "content": "The current directory contains: README.md, src/, tests/"
}
```

**Client sends follow-up (conversation context is maintained):**
```json
{
  "messageType": "TEXT",
  "content": "Now read the README.md file"
}
```

**Klawed responds with tool result and text response as above.**

### Error Case

**Client sends (malformed JSON):**
```json
{
  "messageType": "TEXT"
  // Missing content field
}
```

**Klawed responds:**
```json
{
  "messageType": "ERROR",
  "content": "Invalid message format"
}
```

## Connection Management and Robustness Features

Klawed's ZMQ implementation includes several robustness features to handle network failures and ensure reliable communication.

### Heartbeat Mechanism

Klawed implements a heartbeat system to monitor connection health:

**Heartbeat Ping (Client → Klawed or Klawed → Client):**
```json
{
  "messageType": "HEARTBEAT_PING",
  "timestamp": 1703456789
}
```

**Heartbeat Pong (Response to ping):**
```json
{
  "messageType": "HEARTBEAT_PONG",
  "timestamp": 1703456790,
  "pingTimestamp": 1703456789
}
```

**Features:**
- Automatic ping/pong exchange at configurable intervals
- Connection considered dead if no pong received within timeout
- Heartbeat messages don't interfere with normal message processing

### Automatic Reconnection

Klawed can automatically reconnect when connections fail:

**Exponential Backoff:**
- Base reconnect interval: 1 second (configurable)
- Maximum reconnect interval: 30 seconds (configurable)
- Formula: `interval = base * 2^(attempt-1)` capped at maximum
- Maximum attempts: 10 (configurable)

**Reconnection Process:**
1. Detect connection failure (send/receive error or heartbeat timeout)
2. Close existing socket
3. Wait using exponential backoff
4. Create new socket and reconnect
5. Retry queued messages if message queue is enabled

### Message Queue for Reliable Delivery

When enabled, Klawed can queue messages during disconnections:

**Features:**
- Configurable queue size (default: 100 messages)
- Messages automatically queued when connection is down
- Queued messages sent when connection is restored
- Prevents message loss during transient network issues

### Connection Testing

Clients can test connection health using the `zmq_socket_test_connection()` function:

```c
// C client example
int timeout_ms = 5000; // 5 second timeout
int result = zmq_socket_test_connection(ctx, timeout_ms);
if (result == 0) {
    printf("Connection test successful\n");
} else {
    printf("Connection test failed\n");
}
```

### Configuration Environment Variables

**Heartbeat Configuration:**
- `KLAWED_ZMQ_HEARTBEAT_INTERVAL`: Ping interval in ms (default: 5000)
- `KLAWED_ZMQ_HEARTBEAT_TIMEOUT`: Pong timeout in ms (default: 15000)
- `KLAWED_ZMQ_ENABLE_HEARTBEAT`: Enable heartbeat (default: false)

**Reconnection Configuration:**
- `KLAWED_ZMQ_RECONNECT_INTERVAL`: Base reconnect interval in ms (default: 1000)
- `KLAWED_ZMQ_MAX_RECONNECT_INTERVAL`: Max reconnect interval in ms (default: 30000)
- `KLAWED_ZMQ_MAX_RECONNECT_ATTEMPTS`: Max reconnect attempts (default: 10)
- `KLAWED_ZMQ_ENABLE_RECONNECT`: Enable auto-reconnect (default: false)

**Message Queue Configuration:**
- `KLAWED_ZMQ_SEND_QUEUE_SIZE`: Send queue capacity (default: 100)
- `KLAWED_ZMQ_RECEIVE_QUEUE_SIZE`: Receive queue capacity (default: 100)
- `KLAWED_ZMQ_ENABLE_MESSAGE_QUEUE`: Enable message queues (default: false)

**Timeout Configuration:**
- `KLAWED_ZMQ_RECEIVE_TIMEOUT`: Receive timeout in ms (default: 30000)
- `KLAWED_ZMQ_SEND_TIMEOUT`: Send timeout in ms (default: 10000)
- `KLAWED_ZMQ_CONNECT_TIMEOUT`: Connect timeout in ms (default: 5000)

### Client Reconnection Best Practices

1. **Always check send/receive return codes**
   ```python
   try:
       socket.send(message)
   except zmq.ZMQError as e:
       if e.errno == zmq.EAGAIN:
           # Timeout - implement retry logic
           pass
       else:
           # Other error - attempt reconnection
           socket.close()
           socket = context.socket(zmq.PAIR)
           socket.connect(endpoint)
   ```

2. **Implement application-level retry logic**
   ```python
   max_retries = 3
   for attempt in range(max_retries):
       try:
           response = socket.recv(timeout=5000)
           break
       except zmq.Again:
           if attempt == max_retries - 1:
               raise
           time.sleep(2 ** attempt)  # Exponential backoff
   ```

3. **Use connection testing for health checks**
   ```python
   def test_connection(socket, endpoint):
       ping_msg = json.dumps({
           "messageType": "HEARTBEAT_PING",
           "timestamp": int(time.time())
       })
       try:
           socket.send(ping_msg.encode(), zmq.DONTWAIT)
           response = socket.recv(timeout=5000)
           return True
       except:
           return False
   ```

4. **Handle graceful degradation**
   - Queue messages locally when connection is down
   - Implement circuit breaker pattern
   - Provide user feedback about connection status

### Error Recovery Flow

```
Client sends message
    ↓
[Connection healthy?] → No → [Queue enabled?] → No → Return error
    ↓ Yes                    ↓ Yes
Send message                Queue message
    ↓                        ↓
[Send success?] → No → [Reconnect enabled?] → No → Return error
    ↓ Yes                    ↓ Yes
Return success              Attempt reconnect
                                ↓
                            [Reconnect success?] → No → Return error
                                ↓ Yes
                            Send queued messages
                                ↓
                            Return success
```

## Implementation Details

### Code Location
- Input parsing: `src/zmq_socket.c` - `zmq_socket_process_message()` function
- Response generation: Same function, various response creation sections

### Message Type Constants
- `"TEXT"`: Text processing request and successful response
- `"ERROR"`: Error response

### Buffer Sizes
- Input buffer: `ZMQ_BUFFER_SIZE` (65536 bytes)
- Response buffer: Same size
- Messages larger than buffer will be truncated (with warning logged)

### Error Handling
- JSON parse errors return immediate error response
- Missing required fields return error response
- AI API failures return error with details
- Network/timeout errors return -1 (no response sent)

## Usage Examples

### Python Client Example (Simple)

```python
import zmq
import json

context = zmq.Context()
socket = context.socket(zmq.PAIR)
socket.connect("tcp://127.0.0.1:5555")

# Send text request
request = {
    "messageType": "TEXT",
    "content": "Write a function to calculate factorial"
}
socket.send(json.dumps(request).encode('utf-8'))

# Receive response
response = socket.recv()
response_data = json.loads(response.decode('utf-8'))

if response_data.get("messageType") == "TEXT":
    print("AI Response:", response_data.get("content"))
elif response_data.get("messageType") == "ERROR":
    print("Error:", response_data.get("content"))
```

### Python Client Example (Interactive with Tool Calls)

```python
import zmq
import json

context = zmq.Context()
socket = context.socket(zmq.PAIR)
socket.connect("tcp://127.0.0.1:5555")

def send_and_receive(request):
    """Send request and handle multiple response types"""
    socket.send(json.dumps(request).encode('utf-8'))
    
    # Set a timeout for receiving responses (5 seconds)
    socket.RCVTIMEO = 5000
    
    while True:
        try:
            response = socket.recv()
            response_data = json.loads(response.decode('utf-8'))
            msg_type = response_data.get("messageType")
            
            if msg_type == "TEXT":
                print(f"AI: {response_data.get('content')}")
                # After receiving a TEXT response, wait a bit more in case there are more messages
                # but reset timeout for next receive
                socket.RCVTIMEO = 2000  # Shorter timeout for subsequent messages
            elif msg_type == "TOOL_RESULT":
                tool_name = response_data.get("toolName")
                is_error = response_data.get("isError", False)
                if is_error:
                    print(f"Tool {tool_name} error: {response_data.get('toolOutput', {}).get('error', 'Unknown error')}")
                else:
                    print(f"Tool {tool_name} executed successfully")
                # Reset timeout for next message
                socket.RCVTIMEO = 2000
            elif msg_type == "ERROR":
                print(f"Error: {response_data.get('content')}")
                break
        except zmq.Again:
            # Timeout reached - no more messages
            print("Processing completed (timeout)")
            break

# Send initial request
send_and_receive({
    "messageType": "TEXT",
    "content": "Read the README.md file and summarize it"
})
```

## Best Practices

1. **Always check `messageType`** before processing responses
2. **Handle errors gracefully** - check for ERROR messageType
3. **Validate input** before sending to Klawed
4. **Use appropriate timeouts** for network operations
5. **Log message exchanges** for debugging complex interactions

## See Also

- [ZMQ Socket Implementation](../src/zmq_socket.c)
- [ZMQ Header File](../src/zmq_socket.h)
- [Main ZMQ Integration](../src/klawed.c) (lines ~8134-8143)
