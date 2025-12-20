# ZMQ Input/Output Message Format

This document describes the JSON message format for ZeroMQ communication in Klawed.

## Overview

Klawed uses a simple JSON message format for both input (requests) and output (responses) when communicating via ZeroMQ sockets. The format is designed to be minimal and self-describing.

## Message Structure

All messages are JSON objects with at least a `messageType` field that indicates the type of message.

### Common Fields

- `messageType` (string, required): Type of message ("TEXT" or "ERROR")
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
- `messageType`: Always "TEXT" for successful responses
- `content`: The AI-generated response text

**Example:**
```json
{
  "messageType": "TEXT",
  "content": "Here's a hello world program in C:\n\n```c\n#include <stdio.h>\n\nint main() {\n    printf(\"Hello, world!\\n\");\n    return 0;\n}\n```"
}
```

### 2. Error Response

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

### Successful Text Processing

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

### Python Client Example

```python
import zmq
import json

context = zmq.Context()
socket = context.socket(zmq.REQ)
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
