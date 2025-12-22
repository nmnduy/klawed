# ZMQ Input/Output Message Format

This document describes the JSON message format for ZeroMQ communication in Klawed.

## Overview

Klawed uses a simple JSON message format for both input (requests) and output (responses) when communicating via ZeroMQ sockets using the PAIR socket pattern (exclusive peer-to-peer communication). The format is designed to be minimal and self-describing.

## Message Structure

All messages are JSON objects with at least a `messageType` field that indicates the type of message.

### Common Fields

- `messageType` (string, required): Type of message ("TEXT", "ERROR", "TOOL_RESULT", "USER_PROMPT", or "COMPLETED")
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

### 3. User Prompt Response

Sent when the AI needs additional information from the user (not currently used, reserved for future use).

```json
{
  "messageType": "USER_PROMPT",
  "content": "Please provide additional information"
}
```

**Fields:**
- `messageType`: Always "USER_PROMPT"
- `content`: Prompt text asking for user input

### 4. Completion Response

Sent when interactive processing is complete.

```json
{
  "messageType": "COMPLETED",
  "content": "Interactive processing completed successfully"
}
```

**Fields:**
- `messageType`: Always "COMPLETED"
- `content`: Status message

### 5. Error Response

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

3. Finally, when processing is complete:
```json
{
  "messageType": "COMPLETED",
  "content": "Interactive processing completed successfully"
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
    
    while True:
        response = socket.recv()
        response_data = json.loads(response.decode('utf-8'))
        msg_type = response_data.get("messageType")
        
        if msg_type == "TEXT":
            print(f"AI: {response_data.get('content')}")
        elif msg_type == "TOOL_RESULT":
            tool_name = response_data.get("toolName")
            is_error = response_data.get("isError", False)
            if is_error:
                print(f"Tool {tool_name} error: {response_data.get('toolOutput', {}).get('error', 'Unknown error')}")
            else:
                print(f"Tool {tool_name} executed successfully")
        elif msg_type == "COMPLETED":
            print(f"Completed: {response_data.get('content')}")
            break
        elif msg_type == "ERROR":
            print(f"Error: {response_data.get('content')}")
            break
        elif msg_type == "USER_PROMPT":
            # Not currently used, but could prompt user for input
            user_input = input(f"{response_data.get('content')} ")
            send_and_receive({
                "messageType": "TEXT",
                "content": user_input
            })
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
