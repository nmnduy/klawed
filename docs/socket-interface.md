# Socket Interface Specification

## Overview

Klawed uses Unix domain sockets for IPC communication. The socket interface allows external applications to interact with Klawed by sending prompts and receiving assistant text responses.

## Socket Creation

Klawed creates a Unix domain socket at the specified path:
```bash
./build/klawed --socket /path/to/socket
```

The socket accepts connections and reads JSON-formatted messages line by line (newline-separated).

## Message Format

All messages sent TO the socket should be:
```json
{ "messageType": "prompt", "content": "Your prompt here" }
```

All messages sent FROM the socket follow this simplified JSON schema:

```json
{
  "messageType": "TEXT",
  "content": "Assistant response text"
}
```

## Message Types

### `TEXT` (only message type sent)
Sent when assistant text content is available.

**When**: When the AI provider returns text content (either in streaming or non-streaming mode)
**Content**: Assistant response text as a string

Example:
```json
{
  "messageType": "TEXT",
  "content": "Hello! How can I help you?"
}
```

**Note**: In the simplified socket interface:
- Only `TEXT` messages are sent (no `apiResponse`, `toolCall`, `toolResult`, `streamingEvent`, `error`, or `finalResponse`)
- Tool calls are executed internally but not reported to the socket
- Errors are logged internally but not sent to the socket
- Streaming text deltas are sent as individual `TEXT` messages as they arrive
- Non-streaming text responses are sent as a single `TEXT` message

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
    content = response.get("content", "")
    
    if msg_type == "TEXT":
        # Print assistant text response
        sys.stdout.write(content)
        sys.stdout.flush()

if __name__ == "__main__":
    call_klawed("List files in current directory", "/tmp/klawed.sock")
```

## Notes

1. **Newline-separated**: All messages end with `\n`
2. **JSON format**: Both sending and receiving uses JSON
3. **Non-blocking I/O**: Socket is non-blocking but client should handle backpressure
4. **Simplified interface**: Only `TEXT` messages are sent (no errors, tool calls, or other message types)
5. **Tool execution**: Tools are executed internally but not reported to socket

## Simplified Interface

The simplified socket interface:
- Only sends `TEXT` messages with assistant content
- Does not send errors, tool calls, tool results, or streaming events
- Extracts text from both streaming and non-streaming responses
- Executes tools internally without notifying the socket client
