# WebSocket Communication Mode

This document describes the WebSocket communication mode for Klawed, which
enables ephemeral IPC between Klawed and external clients over a standard
WebSocket connection.

## Overview

WebSocket mode is like the [SQLite queue mode](sqlite-queue.md) but with no
storage required. All messages are ephemeral — they exist only in memory and
in-flight over the TCP connection. When the connection closes, nothing is
retained.

**When to use WebSocket mode over SQLite queue mode:**

| Concern | SQLite queue | WebSocket |
|---------|-------------|-----------|
| Persistence | Messages survive restarts | Ephemeral only |
| Latency | ~300 ms poll interval | Push delivery (sub-ms) |
| Dependencies | SQLite file on shared disk | TCP network socket |
| Multi-process | Any number of processes share the DB | Single client per connection |
| Storage | Grows until pruned | Zero |

**Ideal for:**
- Browser-based UIs (native browser WebSocket API)
- Real-time streaming of tool events
- Containers where a shared filesystem is unavailable
- Any client that can speak HTTP/1.1 → WebSocket upgrade

## Starting WebSocket Daemon Mode

### Command Line

```bash
# Listen on default port 9999 (all interfaces)
./zig-out/bin/klawed --websocket :9999

# Listen on specific host and port
./zig-out/bin/klawed --websocket 127.0.0.1:8080

# Short flag
./zig-out/bin/klawed -w :9999
```

### Environment Variables

```bash
export KLAWED_WS_PORT=9999
./zig-out/bin/klawed
```

## Configuration

| Environment Variable | Default | Description |
|---------------------|---------|-------------|
| `KLAWED_WS_HOST` | `0.0.0.0` | Bind host |
| `KLAWED_WS_PORT` | `9999` | Bind port (also enables WS mode when set) |
| `KLAWED_WS_SENDER` | `klawed` | Sender name used in JSON messages |
| `KLAWED_WS_MAX_MSG_SIZE` | `4194304` | Max inbound message size (4 MiB) |
| `KLAWED_WS_MAX_QUEUE` | `1000` | Outbound message queue capacity |

## Wire Protocol

Standard RFC 6455 WebSocket over TCP. No TLS built-in — use a reverse proxy
(nginx, caddy, etc.) if TLS is required.

**Framing:** Standard WebSocket text frames.  
**Encoding:** UTF-8 JSON.  
**Masking:** Clients MUST mask frames (per RFC 6455); server frames are unmasked.

## Message Format

Identical to the [SQLite queue mode message format](sqlite-queue.md#message-format).
All messages are JSON objects with a `messageType` field.

### Inbound (Client → Klawed)

| `messageType` | Description |
|--------------|-------------|
| `TEXT` | Text prompt to process |
| `TRIGGER_COMPACT` | Request context compaction |
| `INTERRUPT` | **Abort the current AI turn immediately** |

#### TEXT

```json
{"messageType": "TEXT", "content": "Your prompt here"}
```

#### TRIGGER_COMPACT

```json
{"messageType": "TRIGGER_COMPACT"}
```

#### INTERRUPT

Sends a signal to abort the currently running AI turn. The worker will stop
after the next tool completes (or immediately if waiting for an API response).

```json
{"messageType": "INTERRUPT"}
```

Klawed will send an `END_AI_TURN` (or `ERROR`) in response to acknowledge the
interruption, then become ready for a new request.

### Outbound (Klawed → Client)

| `messageType` | Description |
|--------------|-------------|
| `TEXT` | AI-generated response text |
| `TOOL` | Tool execution starting |
| `TOOL_RESULT` | Tool execution complete |
| `API_CALL` | API request in flight |
| `END_AI_TURN` | AI turn finished, ready for next message |
| `ERROR` | Error occurred |
| `AUTO_COMPACTION` | Context was automatically compacted |

See [SQLite queue docs](sqlite-queue.md#output-messages-klawed--client) for
full field descriptions of each type.

## Connection Model

- Klawed acts as the **server** (binds and listens)
- Your application acts as the **client** (connects via HTTP upgrade)
- **Single client at a time** — klawed handles one connection fully before
  accepting the next
- On disconnect, klawed loops back to accept the next client with a fresh
  conversation state

## Communication Flow

```
Client                          Klawed
  |                               |
  |-- HTTP Upgrade request -----> |
  |<-- 101 Switching Protocols -- |
  |                               |
  |-- {"messageType":"TEXT"} ---> |  (user prompt)
  |<-- {"messageType":"API_CALL"} |  (AI thinking)
  |<-- {"messageType":"TOOL"}  -- |  (tool starting)
  |<-- {"messageType":"TOOL_RESULT"} |  (tool done)
  |<-- {"messageType":"TEXT"}  -- |  (AI response)
  |<-- {"messageType":"END_AI_TURN"} |  (ready for next)
  |                               |
  |-- {"messageType":"TEXT"} ---> |  (next prompt)
  |   ...                         |
  |                               |
  |-- {"messageType":"INTERRUPT"} |  (abort current turn)
  |<-- {"messageType":"END_AI_TURN"} |  (acknowledged)
  |                               |
  |-- WebSocket close frame ----> |
  |<-- WebSocket close frame ---- |
```

## Interrupt Mode

Sending `{"messageType":"INTERRUPT"}` at any time during an active AI turn
will signal the worker to abort:

- **During tool execution:** the current tool is allowed to finish, then the
  turn is aborted. An `END_AI_TURN` is sent.
- **During API call:** the in-flight HTTP request is cancelled if possible,
  otherwise waits for the response then aborts. An `END_AI_TURN` is sent.
- **Between turns:** the interrupt flag is cleared and ignored; a fresh
  request can be sent immediately.

The interrupt is **non-destructive to the conversation** — the partially
completed turn is discarded, but prior turns remain in the context window.
The client can send a new `TEXT` message immediately after receiving
`END_AI_TURN`.

## Client Examples

### JavaScript / Browser

```javascript
const ws = new WebSocket("ws://localhost:9999");

ws.onopen = () => {
  ws.send(JSON.stringify({
    messageType: "TEXT",
    content: "List files in the current directory"
  }));
};

ws.onmessage = (event) => {
  const msg = JSON.parse(event.data);

  switch (msg.messageType) {
    case "TEXT":
      console.log("AI:", msg.content);
      break;
    case "TOOL":
      console.log(`Tool starting: ${msg.toolName}`);
      break;
    case "TOOL_RESULT":
      console.log(`Tool done: ${msg.toolName}, error=${msg.isError}`);
      break;
    case "API_CALL":
      console.log("Waiting for AI response...");
      break;
    case "END_AI_TURN":
      console.log("AI turn complete, ready for next message");
      break;
    case "ERROR":
      console.error("Error:", msg.content);
      break;
    case "AUTO_COMPACTION":
      console.log(`Context compacted: ${msg.tokensFreed} tokens freed`);
      break;
  }
};

ws.onerror = (err) => console.error("WS error:", err);

// Send interrupt while AI is working:
// ws.send(JSON.stringify({ messageType: "INTERRUPT" }));
```

### Python

```python
import asyncio
import json
import websockets

async def klawed_chat(prompt: str, host="localhost", port=9999):
    uri = f"ws://{host}:{port}"
    async with websockets.connect(uri) as ws:
        # Send prompt
        await ws.send(json.dumps({"messageType": "TEXT", "content": prompt}))

        # Collect responses until END_AI_TURN
        async for raw in ws:
            msg = json.loads(raw)
            t = msg.get("messageType")

            if t == "TEXT":
                print(f"AI: {msg['content']}")
            elif t == "TOOL":
                print(f"  → tool: {msg['toolName']}")
            elif t == "TOOL_RESULT":
                status = "ERR" if msg.get("isError") else "OK"
                print(f"  ← {msg['toolName']} [{status}]")
            elif t == "API_CALL":
                print("  [API call in flight...]")
            elif t == "END_AI_TURN":
                break
            elif t == "ERROR":
                print(f"Error: {msg['content']}")
                break
            elif t == "AUTO_COMPACTION":
                freed = msg.get("tokensFreed", 0)
                print(f"  [context compacted, {freed} tokens freed]")

asyncio.run(klawed_chat("What files are in the current directory?"))
```

### Python with Interrupt

```python
import asyncio
import json
import websockets

async def demo_interrupt():
    uri = "ws://localhost:9999"
    async with websockets.connect(uri) as ws:
        # Start a long-running task
        await ws.send(json.dumps({
            "messageType": "TEXT",
            "content": "Read every file in this project and summarise each one"
        }))

        # Interrupt after seeing 3 tool calls
        tool_count = 0
        async for raw in ws:
            msg = json.loads(raw)
            t = msg.get("messageType")
            print(t, msg.get("toolName", ""))

            if t == "TOOL":
                tool_count += 1
                if tool_count >= 3:
                    print("Interrupting!")
                    await ws.send(json.dumps({"messageType": "INTERRUPT"}))

            elif t == "END_AI_TURN":
                print("Done (or interrupted)")
                break

asyncio.run(demo_interrupt())
```

### Node.js

```javascript
import WebSocket from "ws";

function klawedChat(prompt, host = "localhost", port = 9999) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(`ws://${host}:${port}`);
    const responses = [];

    ws.on("open", () => {
      ws.send(JSON.stringify({ messageType: "TEXT", content: prompt }));
    });

    ws.on("message", (data) => {
      const msg = JSON.parse(data.toString());
      switch (msg.messageType) {
        case "TEXT":
          responses.push(msg.content);
          break;
        case "END_AI_TURN":
          ws.close();
          resolve(responses.join("\n"));
          break;
        case "ERROR":
          ws.close();
          reject(new Error(msg.content));
          break;
      }
    });

    ws.on("error", reject);
  });
}

const result = await klawedChat("Explain what build.zig does");
console.log(result);
```

## Security Considerations

- **No authentication built-in.** Bind to `127.0.0.1` for local-only use, or
  place behind an authenticated reverse proxy.
- **No TLS built-in.** Use nginx/caddy with `wss://` termination if traffic
  crosses untrusted networks.
- **Single client.** Only one WebSocket connection is active at a time; a
  second client must wait until the first disconnects.

## See Also

- [SQLite Queue Mode](sqlite-queue.md) — durable, poll-based alternative
- [Unix Domain Socket Mode](unix-socket.md) — low-latency local IPC
- [Auto-compaction](auto_compaction.md) — context management
