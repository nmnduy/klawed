# Socket Interface Specification

## Overview

Klawed uses Unix domain sockets for IPC communication. The socket interface allows external applications to interact with Klawed by sending prompts and receiving responses/tool execution results.

## Socket Creation

Klawed creates a Unix domain socket at the specified path:
```bash
./build/klawed --socket /path/to/socket
```

The socket accepts connections and reads JSON-formatted messages line by line (newline-separated).

## Message Format

All messages sent TO the socket should be:
```
{ "messageType": "prompt", "content": "Your prompt here" }
```

All messages sent FROM the socket follow this JSON schema:

```json
{
  "messageType": "string",
  "content": "object or string",
  "timestamp": "ISO8601 string (optional)",
  "sessionId": "string (optional)"
}
```

## Message Types

### 1. `apiResponse`
Sent when a complete API response is available (non-streaming mode).

**When**: When streaming is disabled (`KLAWED_ENABLE_STREAMING=0` or not set)
**Content**: Full OpenAI/Anthropic API response object

Example:
```json
{
  "messageType": "apiResponse",
  "content": {
    "id": "msg_123456",
    "type": "message",
    "role": "assistant",
    "content": [{
      "type": "text",
      "text": "Hello! How can I help you?"
    }],
    "model": "claude-3-5-sonnet-20241022",
    "stop_reason": "end_turn"
  }
}
```

### 2. `streamingEvent`
Sent during streaming mode (`KLAWED_ENABLE_STREAMING=1`).

**When**: For each Server-Sent Event (SSE) chunk received from the AI provider
**Content**: SSE event with type and data

Example:
```json
{
  "messageType": "streamingEvent",
  "content": {
    "type": "content_block_delta",
    "data": {
      "type": "text_delta",
      "text": "Hello"
    }
  }
}
```

Available event types:
- `message_start` - Conversation started
- `content_block_start` - New content block beginning
- `content_block_delta` - Text chunk (streaming)
- `content_block_stop` - Content block ended
- `message_delta` - Message metadata updates
- `message_stop` - Message completed
- `error` - Error occurred
- `ping` - Keepalive
- `openai_chunk` - OpenAI streaming chunk
- `openai_done` - OpenAI stream complete

### 3. `toolCall`
Sent when the AI requests tool execution.

**When**: API response contains tool calls that need to be executed
**Content**: Array of tool calls with IDs and parameters

Example:
```json
{
  "messageType": "toolCall",
  "content": {
    "tools": [
      {
        "id": "toolu_123",
        "name": "Bash",
        "parameters": {
          "command": "ls -la"
        }
      }
    ]
  }
}
```

### 4. `toolResult`
Sent when a tool execution completes.

**When**: Tool execution finished (either success or error)
**Content**: Tool execution result

Example:
```json
{
  "messageType": "toolResult",
  "content": {
    "toolCallId": "toolu_123",
    "toolName": "Bash",
    "result": {
      "exitCode": 0,
      "output": "total 8\ndrwxr-xr-x   ..."
    }
  }
}
```

### 5. `error`
Sent when an error occurs during processing.

**When**: API call fails, tool execution fails, or other errors
**Content**: Error message

Example:
```json
{
  "messageType": "error",
  "content": "Failed to execute tool: Permission denied"
}
```

### 6. `finalResponse`
Sent after all tool calls are processed and final response is ready.

**When**: Multi-turn conversation complete
**Content**: Final assistant response

Example:
```json
{
  "messageType": "finalResponse",
  "content": "I've completed all the requested tool executions. Here's a summary of the changes..."
}
```

## Complete API Response Format (Non-Streaming)

When streaming is disabled, Klawed sends the complete API response:

```json
{
  "id": "msg_123456",
  "type": "message",
  "role": "assistant",
  "content": [
    {
      "type": "text",
      "text": "Response text here..."
    }
  ],
  "model": "claude-3-5-sonnet-20241022",
  "stop_reason": "end_turn",
  "usage": {
    "input_tokens": 123,
    "output_tokens": 456,
    "total_tokens": 579
  }
}
```

## Streaming Event Format

Each SSE event is wrapped in a standardized format:

```json
{
  "type": "event_type",
  "data": {
    // Event-specific data
  }
}
```

**Example: Text Delta**
```json
{
  "type": "content_block_delta",
  "data": {
    "type": "text_delta",
    "text": "Hello "
  }
}
```

**Example: Tool Use Start**
```json
{
  "type": "content_block_start",
  "data": {
    "type": "tool_use",
    "tool_use_id": "toolu_abc123",
    "name": "Bash",
    "input": {}
  }
}
```

**Example: Stop Reason**
```json
{
  "type": "message_delta",
  "data": {
    "delta": {
      "stop_reason": "end_turn",
      "stop_sequence": null
    },
    "usage": {
      "output_tokens": 156
    }
  }
}
```

## Tool Execution Flow

1. **`toolCall`** → Client receives tool calls from AI
2. **Client executes tools** (locally or via callback)
3. **`toolResult`** → Client sends results back via socket
4. **Process continues** → Klawed feeds results to AI

## Socket Interaction Example

### Client Code (Python Example)
```python
import json
import socket
import sys

def call_klawed(prompt: str, socket_path: str):
    # Connect to socket
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(socket_path)
    
    # Send prompt
    message = {
        "messageType": "prompt",
        "content": prompt
    }
    sock.sendall(json.dumps(message).encode() + b'\n')
    
    # Read responses
    buffer = b""
    while True:
        data = sock.recv(4096)
        if not data:
            break
        
        buffer += data
        while b'\n' in buffer:
            line, buffer = buffer.split(b'\n', 1)
            if line:
                response = json.loads(line.decode())
                handle_response(response)
    
    sock.close()

def handle_response(response: dict):
    msg_type = response.get("messageType")
    content = response.get("content", {})
    
    if msg_type == "streamingEvent":
        event_type = content.get("type")
        event_data = content.get("data", {})
        
        if event_type == "content_block_delta":
            text_delta = event_data.get("text", "")
            sys.stdout.write(text_delta)
            sys.stdout.flush()
            
    elif msg_type == "apiResponse":
        print(f"\nFull response: {content}")
        
    elif msg_type == "toolCall":
        tools = content.get("tools", [])
        for tool in tools:
            print(f"Tool needed: {tool['name']}")
            # Execute tool here
            
    elif msg_type == "error":
        print(f"Error: {content}")
        
    elif msg_type == "finalResponse":
        print(f"Final: {content}")

if __name__ == "__main__":
    call_klawed("List files in current directory", "/tmp/klawed.sock")
```

## Notes

1. **Newline-separated**: All messages end with `\n`
2. **JSON format**: Both sending and receiving uses JSON
3. **Non-blocking I/O**: Socket is non-blocking but client should handle backpressure
4. **Error handling**: Send `{"messageType": "error", "content": "error message"}` for errors
5. **Tool results**: Return `{"messageType": "toolResult", "content": {...}}` for tool execution

## Migration from Current Format

The current format sends:
- Raw API responses (when not streaming)
- SSE events via `uds_send_event()` with `{"type": "...", "data": {...}}`

The new format standardizes everything under `messageType`/`content` structure for consistency.