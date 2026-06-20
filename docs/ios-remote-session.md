# iOS Remote Session — System Design

## Overview

This document describes the architecture for an iOS app that can interact with a
running klawed session remotely. The system has three tiers:

1. **Bridge Server** — embedded HTTP/WebSocket server inside klawed
2. **Secure Gateway** — authentication, TLS termination, remote access
3. **iOS App** — native Swift/SwiftUI client

```
┌──────────────────┐     ┌──────────────────┐     ┌──────────────────┐
│   iOS App        │     │  Secure Gateway   │     │  klawed + Bridge │
│  (SwiftUI)       │◄───►│  (nginx/caddy)    │◄───►│  (C, embedded)   │
│                  │ WSS │  TLS + Auth       │ WS  │  HTTP/WS on      │
│  REST + WS       │     │  + Reverse Proxy  │     │  localhost:PORT   │
└──────────────────┘     └──────────────────┘     └──────────────────┘
```

### Why not reuse the existing UDS mode?

Klawed already has a [Unix Domain Socket daemon mode](unix-socket.md) with a
length-prefixed JSON protocol. However:

- UDS is **local-only** — iOS can't open Unix sockets
- The protocol is **synchronous request-response** — no streaming, no session
  multiplexing, no real-time tool call visibility
- No **session enumeration** — a remote client needs to list/switch sessions

We'll build a richer protocol on top of the existing internal infrastructure
(ConversationState, TUIMessageQueue, session management, API providers).

---

## 1. Bridge Server (Embedded in klawed)

### 1.1 Startup

```
# New CLI flags:
./build/klawed --serve 127.0.0.1:9099
./build/klawed --serve 0.0.0.0:9099 --serve-tls-cert cert.pem --serve-tls-key key.pem
./build/klawed --serve 127.0.0.1:9099 --serve-api-key "sk-abc123"

# Env vars (alternative):
KLAWED_SERVE_ADDR=127.0.0.1:9099
KLAWED_SERVE_TLS_CERT=/path/to/cert.pem
KLAWED_SERVE_TLS_KEY=/path/to/key.pem
KLAWED_SERVE_API_KEY=sk-abc123
```

The bridge server starts in a **dedicated pthread** alongside the TUI thread, AI
worker thread, and other klawed subsystems. It binds to `localhost` by default
for safety.

### 1.2 HTTP Library Choice

| Library        | Size   | License | Notes                              |
|----------------|--------|---------|-------------------------------------|
| **mongoose**   | 2 files| MIT     | Single .h + .c, WebSocket built-in |
| civetweb       | ~10 files | MIT | Fork of mongoose, more features    |
| libmicrohttpd  | system | LGPL    | GNU, external dep                  |
| custom + llhttp| 3 files| MIT     | Minimal, but more work             |

**Recommendation: mongoose** — single-header/single-file C library with HTTP,
WebSocket, TLS (OpenSSL/mbedTLS), and digest auth built in. Matches klawed's
"minimal dependencies" philosophy. Can be vendored as `vendor/mongoose.c` +
`vendor/mongoose.h`.

Connection to klawed's build system:

```makefile
# In Makefile
SERVER_SRC = src/bridge_server.c src/bridge_api.c src/bridge_ws.c
SERVER_OBJ = $(SERVER_SRC:.c=.o)
VENDOR_SRC = vendor/mongoose.c

klawed: ... $(SERVER_OBJ) $(VENDOR_SRC:.c=.o)
	$(CC) ... -o $@
```

### 1.3 Thread Architecture

```
Main Thread                Worker Thread              Bridge Thread
    │                           │                          │
    │── TUI event loop          │── AI API calls           │── mongoose event loop
    │── Input handling          │── Tool execution         │── HTTP request routing
    │── TUIMessageQueue (read)  │── TUIMessageQueue(write) │── WebSocket connections
    │                           │                          │── REST endpoint handlers
    │◄─────── shared state ─────►◄─────── shared state ───►│
    │  ConversationState        │                          │
    │  ConversationState.mutex  │                          │
    │  SubagentManager          │                          │
    │  TodoList                 │                          │
    │  GoalState                │                          │
```

All three threads share `ConversationState` protected by `conv_mutex`. The
bridge thread must lock `conv_mutex` when reading/writing conversation data.

The bridge thread also subscribes to the **TUIMessageQueue** — when a streaming
response arrives from the AI worker, the bridge thread reads `TUI_MSG_STREAM_*`
messages and forwards deltas over WebSocket to connected iOS clients.

### 1.4 REST API

All endpoints are prefixed with `/api/v1/`. JSON request/response bodies.

#### Health & Info

```
GET /api/v1/health
→ 200 { "status": "ok", "version": "0.3.0", "uptime_seconds": 1234 }

GET /api/v1/info
→ 200 {
    "model": "claude-sonnet-4-20250514",
    "working_dir": "/Users/puter/git/myproject",
    "provider": "anthropic",
    "plan_mode": false,
    "streaming_enabled": true
  }
```

#### Session Management

```
GET /api/v1/sessions?limit=20
→ 200 {
    "sessions": [
      {
        "id": "abc123...",
        "title": "Fix the login bug",
        "created_at": "2026-06-15T10:30:00Z",
        "message_count": 42,
        "model": "claude-sonnet-4-20250514"
      }
    ]
  }

GET /api/v1/sessions/current
→ 200 { "id": "abc123...", "title": "...", ... }

POST /api/v1/sessions/switch
Body: { "session_id": "abc123..." }
→ 200 { "switched": true, "session": { ... } }
```

#### Conversation Messages

```
GET /api/v1/conversation?after_index=0&limit=50
→ 200 {
    "messages": [
      {
        "index": 0,
        "role": "user",
        "content": "fix the login bug",
        "timestamp": "2026-06-15T10:30:00Z"
      },
      {
        "index": 1,
        "role": "assistant",
        "content": "I'll investigate...",
        "reasoning": "...",         // may be null
        "tool_calls": [
          {
            "id": "toolu_01...",
            "name": "Grep",
            "params": { "pattern": "login", "path": "src/" }
          }
        ],
        "timestamp": "2026-06-15T10:30:05Z"
      },
      {
        "index": 2,
        "role": "tool",
        "tool_call_id": "toolu_01...",
        "tool_name": "Grep",
        "content": "src/auth.ts:42: function login() {",
        "is_error": false,
        "timestamp": "2026-06-15T10:30:08Z"
      }
    ],
    "total_count": 42,
    "has_more": true
  }
```

#### Send Message

```
POST /api/v1/conversation/send
Body: { "content": "what did you find?" }
→ 202 { "accepted": true, "message_index": 4 }
```

This enqueues the message via the existing AI instruction queue
(`enqueue_instruction`). The response is delivered asynchronously via WebSocket.

#### Tool Approval (future)

```
POST /api/v1/approval/respond
Body: { "request_id": "approval_001", "approved": true }
→ 200 { "ok": true }
```

#### Subagent Monitoring

```
GET /api/v1/subagents
→ 200 {
    "subagents": [
      {
        "pid": 12345,
        "prompt": "Run all unit tests",
        "started_at": "2026-06-15T10:35:00Z",
        "status": "running",
        "log_tail": "...last 500 chars..."
      }
    ]
  }
```

#### File Browsing

```
GET /api/v1/files?path=src/&limit=50
→ 200 {
    "path": "src/",
    "entries": [
      { "name": "auth.ts", "type": "file", "size": 2048 },
      { "name": "components/", "type": "directory" }
    ]
  }

GET /api/v1/files/read?path=src/auth.ts&start_line=1&end_line=50
→ 200 {
    "path": "src/auth.ts",
    "content": "import { ... }\n\nfunction login()...",
    "total_lines": 200
  }
```

#### TODO List

```
GET /api/v1/todos
→ 200 {
    "todos": [
      { "content": "Fix login bug", "status": "in_progress" },
      { "content": "Write tests", "status": "pending" },
      { "content": "Update docs", "status": "completed" }
    ]
  }
```

#### Goal (Ralph Mode)

```
GET /api/v1/goal
→ 200 {
    "has_goal": true,
    "goal": "Implement user authentication system",
    "status": "in_progress",
    "judge_verdict": "continue",
    "last_judge_at": "2026-06-15T10:40:00Z"
  }
```

### 1.5 WebSocket Protocol

Clients connect to `ws://host:9099/api/v1/ws?api_key=sk-abc123`. After a
successful upgrade, the protocol is JSON-framed text messages in both directions.

#### Client → Server

```jsonc
// Subscribe to real-time updates for current session
{ "type": "subscribe", "channels": ["conversation", "status", "todos"] }

// Unsubscribe
{ "type": "unsubscribe", "channels": ["todos"] }

// Send a user message (alternative to REST POST)
{ "type": "send_message", "content": "look at the auth module" }

// Interrupt the current AI generation
{ "type": "interrupt" }

// Ping to keep connection alive
{ "type": "ping" }
```

#### Server → Client

```jsonc
// AI is thinking / tool is executing
{ "type": "status", "state": "thinking", "detail": "Analyzing auth.ts" }
{ "type": "status", "state": "executing", "detail": "Running: grep -r login src/" }
{ "type": "status", "state": "idle" }

// Streaming text delta from assistant
{ "type": "stream_start", "message_index": 5, "role": "assistant" }
{ "type": "stream_delta", "delta": "I found the issue in" }
{ "type": "stream_delta", "delta": " src/auth.ts at line 42." }
{ "type": "stream_end", "message_index": 5 }

// Reasoning/thinking content (for models that expose it)
{ "type": "reasoning_start", "message_index": 5 }
{ "type": "reasoning_delta", "delta": "Let me trace through the login flow..." }
{ "type": "reasoning_end", "message_index": 5 }

// Tool call initiated
{
  "type": "tool_call",
  "request_id": "call_01",
  "tool_name": "Grep",
  "params": { "pattern": "login", "path": "src/" }
}

// Tool result
{
  "type": "tool_result",
  "request_id": "call_01",
  "tool_name": "Grep",
  "output": "src/auth.ts:42: function login() {",
  "is_error": false
}

// Message added to conversation (non-streaming, or final state after stream)
{
  "type": "message_added",
  "message": { "index": 5, "role": "assistant", ... }
}

// TODO list changed
{
  "type": "todos_updated",
  "todos": [ ... ]
}

// Connection confirmation
{ "type": "subscribed", "channels": ["conversation", "status", "todos"] }

// Error
{ "type": "error", "code": "auth_failed", "message": "Invalid API key" }

// Pong
{ "type": "pong" }
```

### 1.6 Internal Implementation (C)

New source files:

```
src/bridge_server.h       — Public API (start/stop server)
src/bridge_server.c       — Mongoose init, event loop thread
src/bridge_api.c          — REST endpoint handlers
src/bridge_ws.c           — WebSocket connection management
src/bridge_ws.h           — WebSocket types and functions
src/bridge_protocol.c     — JSON message serialization/deserialization
src/bridge_protocol.h     — Protocol constants and types
vendor/mongoose.c         — Vendored HTTP/WS library
vendor/mongoose.h         — Vendored header
```

Key internal bridges (connecting the server to klawed internals):

```
bridge_server.c
  ├── fn_handler_health()         → reads version.h
  ├── fn_handler_sessions()       → calls session_get_list(), session_get_metadata()
  ├── fn_handler_switch_session() → calls session_load_from_db()
  ├── fn_handler_conversation()   → reads ConversationState.messages[]
  ├── fn_handler_send_message()   → calls enqueue_instruction()
  ├── fn_handler_files()          → calls readdir() / fopen()
  ├── fn_handler_todos()          → reads ConversationState.todo_list
  ├── fn_handler_goal()           → reads ConversationState.goal

bridge_ws.c
  ├── tui_msg_bridge_loop()       → reads TUIMessageQueue, translates to WS frames
  ├── ws_on_message()             → handles subscribe/send_message/interrupt
  └── ws_broadcast()              → sends JSON to all subscribed connections
```

**Thread safety:** All handlers that touch `ConversationState` must:

```c
conversation_state_lock(state);
// ... read/write state ...
conversation_state_unlock(state);
```

**Streaming bridge:** The bridge thread polls the `TUIMessageQueue` in a loop:

```c
// In bridge_ws.c — runs in the bridge thread
static void tui_msg_bridge_loop(void *arg) {
    struct BridgeState *bs = arg;
    while (!bs->shutdown) {
        TUIMessage msg = {0};
        if (poll_tui_message(bs->tui_queue, &msg) == 1) {
            switch (msg.type) {
            case TUI_MSG_STREAM_START:
                ws_broadcast_json(bs, "{ \"type\": \"stream_start\", ... }");
                break;
            case TUI_MSG_STREAM_APPEND:
                ws_broadcast_json(bs, "{ \"type\": \"stream_delta\", ... }");
                break;
            case TUI_MSG_STREAM_END:
                ws_broadcast_json(bs, "{ \"type\": \"stream_end\", ... }");
                break;
            case TUI_MSG_TODO_UPDATE:
                ws_broadcast_json(bs, serialize_todos(bs->state->todo_list));
                break;
            // ... other message types ...
            }
            free(msg.text);
        }
        usleep(10000); // 10ms poll interval
    }
}
```

### 1.7 API Key Management

On first `--serve` startup, if no `--serve-api-key` is provided:

1. Generate a random 32-byte key: `klawed_` + hex(arc4random_buf(16))
2. Display it in the TUI status bar: `🔑 API key: klawed_a1b2c3d4...`
3. Store in `~/.klawed/bridge_api_key` for persistence across restarts
4. iOS app scans a QR code or user manually enters the key

The API key is sent as a header:
```
Authorization: Bearer klawed_a1b2c3d4e5f6...
```

Or as a query param for WebSocket (browsers can't set WS headers):
```
ws://host:9099/api/v1/ws?api_key=klawed_a1b2c3d4...
```

---

## 2. Secure Gateway

The bridge server binds to `127.0.0.1` by default (local only). For remote
access from an iOS device, we need a secure gateway. Three options:

### 2.1 Option A: Direct TLS (Simplest)

Run klawed with TLS enabled, bind to `0.0.0.0`:

```bash
./build/klawed --serve 0.0.0.0:9099 \
  --serve-tls-cert /etc/letsencrypt/live/myhost/fullchain.pem \
  --serve-tls-key /etc/letsencrypt/live/myhost/privkey.pem \
  --serve-api-key "sk-abc123"
```

iOS connects directly via `wss://myhost.example.com:9099/api/v1/ws?api_key=sk-abc123`.

**Pros:** No extra infrastructure.
**Cons:** Port 9099 must be open; no DDoS protection; manual TLS cert management.

### 2.2 Option B: Cloudflare Tunnel (Recommended for Personal Use)

```
┌──────────┐     ┌──────────────┐     ┌──────────────────┐
│ iOS App  │────►│ cloudflared  │────►│ klawed :9099     │
│ (WSS)    │     │ (exit node)  │     │ (localhost)      │
└──────────┘     └──────────────┘     └──────────────────┘
```

```bash
# Install and run cloudflared
cloudflared tunnel --url http://localhost:9099
# → https://klawed-abc123.trycloudflare.com
```

**Pros:** Free, automatic TLS, DDoS protection, no open ports.
**Cons:** Depends on Cloudflare; trycloudflare subdomains are ephemeral (need
named tunnel for permanent URL).

### 2.3 Option C: Tailscale / WireGuard

iOS device and klawed host are on the same Tailscale network. No TLS needed
(WireGuard encrypts everything). iOS connects to `ws://100.x.y.z:9099`.

**Pros:** Zero config auth (Tailscale identity), encrypted by default.
**Cons:** Both devices need Tailscale installed.

### 2.4 Option D: nginx/caddy Reverse Proxy (Self-Hosted)

```nginx
# nginx config
server {
    listen 443 ssl;
    server_name klawed.example.com;

    ssl_certificate /etc/letsencrypt/live/klawed.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/klawed.example.com/privkey.pem;

    location / {
        proxy_pass http://127.0.0.1:9099;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
    }
}
```

**Pros:** Full control, standard DevOps setup.
**Cons:** Requires a domain, TLS cert, and nginx config.

### Recommendation

For **v1**, support **Option A** (direct TLS, opt-in) and document **Option B**
(Cloudflare Tunnel) as the recommended remote access method. This gives users
the simplest path: run `cloudflared tunnel` and get a public URL instantly.

---

## 3. iOS App Architecture

### 3.1 Technology Stack

| Layer            | Technology                          |
|------------------|-------------------------------------|
| UI               | SwiftUI (iOS 17+)                   |
| Networking       | URLSession (REST) + URLSessionWebSocket (WS) |
| State Management | `@Observable` (iOS 17+)             |
| Persistence      | SwiftData / UserDefaults            |
| Minimum Target   | iOS 17.0                            |

### 3.2 Project Structure

```
KlawedRemote/
├── KlawedRemoteApp.swift          // @main App entry
├── Models/
│   ├── ServerConnection.swift     // Host, port, API key, WS state
│   ├── Session.swift              // Session ID, title, metadata
│   ├── Message.swift              // Role, content, timestamp, tool calls
│   ├── ToolCall.swift             // Tool name, params, result
│   ├── TodoItem.swift             // Content, status
│   └── ServerInfo.swift           // Model, working dir, uptime
├── Networking/
│   ├── APIClient.swift            // REST calls via URLSession
│   ├── WebSocketClient.swift      // WS connection, auto-reconnect
│   └── Endpoint.swift             // URL builder, API versioning
├── ViewModels/
│   ├── ConnectionViewModel.swift  // Connect/disconnect, QR scan
│   ├── SessionListViewModel.swift // List sessions, switch
│   ├── ConversationViewModel.swift// Messages, send, streaming
│   ├── TodoViewModel.swift        // TODO list, real-time updates
│   └── GoalViewModel.swift        // Goal status, judge verdicts
├── Views/
│   ├── Connection/
│   │   ├── ConnectionView.swift       // QR scan + manual entry
│   │   └── ServerQRCodeView.swift     // QR scanner
│   ├── Sessions/
│   │   ├── SessionListView.swift      // Session browser
│   │   └── SessionRowView.swift       // Single session row
│   ├── Conversation/
│   │   ├── ConversationView.swift     // Main chat interface
│   │   ├── MessageBubbleView.swift    // User/assistant bubbles
│   │   ├── ToolCallCardView.swift     // Collapsible tool call
│   │   ├── ReasoningDisclosureView.swift // Thinking/reasoning
│   │   ├── StreamingText.swift        // Animated streaming text
│   │   └── MessageInputBar.swift      // Text input + send
│   ├── Sidebar/
│   │   ├── SidebarView.swift          // iPad sidebar
│   │   └── FileBrowserView.swift      // Workspace file tree
│   └── Settings/
│       └── SettingsView.swift         // Appearance, about
├── Resources/
│   └── Assets.xcassets                // App icon, colors
└── KlawedRemote.xcodeproj
```

### 3.3 Key Screens and UX Flow

```
  ┌──────────────┐     ┌──────────────┐     ┌──────────────────────┐
  │  Connect     │     │  Sessions    │     │  Conversation        │
  │              │     │              │     │                      │
  │ [QR Scan]   │────►│ Fix login   │────►│ ╭──────────────────╮  │
  │              │     │ Add tests   │     │ │ I found the bug │  │
  │  or          │     │ Refactor    │     │ │ in auth.ts:42   │  │
  │ [Manual]     │     │             │     │ ╰──────────────────╯  │
  │  host:port   │     │ [+ New]     │     │                      │
  │  api key     │     │             │     │ ┌─────────────────┐  │
  │              │     │             │     │ │ 🔧 Grep: login  │  │
  └──────────────┘     └──────────────┘     │ │ src/auth.ts:42  │  │
                                            │ └─────────────────┘  │
                                            │                      │
                                            │ [_____________] [▶] │
                                            └──────────────────────┘
```

**1. Connection Screen**
- QR code scanner (scan the TUI-displayed QR code)
- Manual entry: host, port, API key
- "Remember this server" toggle → stored in Keychain
- Connection status indicator (connecting/connected/error)

**2. Session List**
- Pull-to-refresh from server
- Session rows show title, date, message count, model
- Tap to switch session
- "+" to start a new session
- Pull-down to show file browser sidebar (iPad)

**3. Conversation View**
- Chat-style bubbles (user right, assistant left)
- Tool calls shown as collapsible cards with icon + name
- Streaming text animates character by character or chunk by chunk
- Reasoning/thinking content in a disclosure group (collapsed by default)
- Input bar at bottom with send button
- Typing indicator when AI is thinking
- Pull-down to show TODO list overlay
- Goal/Ralph status as a persistent thin banner at top

### 3.4 Data Flow

```
User taps Send
       │
       ▼
ConversationViewModel.sendMessage("text")
       │
       ├──► POST /api/v1/conversation/send  →  202 Accepted
       │
       ▼
WebSocket receives events:
       │
       ├── status: "thinking"      →  show spinner/typing indicator
       ├── tool_call: {...}        →  show tool card (collapsed)
       ├── stream_delta: "I am..." →  append to streaming bubble
       ├── tool_result: {...}      →  update tool card with result
       ├── stream_end              →  finalize bubble, mark complete
       ├── message_added           →  append to message array
       └── todos_updated           →  update TODO overlay
```

### 3.5 Key SwiftUI Components (Pseudocode)

#### WebSocketClient

```swift
@Observable
final class WebSocketClient {
    private var task: URLSessionWebSocketTask?
    private(set) var isConnected = false

    // Published event stream — used by ViewModels
    let events = AsyncStream<ServerEvent>.makeStream()

    func connect(to url: URL, apiKey: String) {
        var request = URLRequest(url: url)
        request.setValue("Bearer \(apiKey)", forHTTPHeaderField: "Authorization")
        task = URLSession.shared.webSocketTask(with: request)
        task?.resume()
        isConnected = true
        subscribe(channels: ["conversation", "status", "todos"])
        Task { await receiveLoop() }
    }

    private func receiveLoop() async {
        while let message = try? await task?.receive() {
            switch message {
            case .string(let text):
                if let data = text.data(using: .utf8),
                   let event = try? JSONDecoder().decode(ServerEvent.self, from: data) {
                    events.continuation.yield(event)
                }
            default: break
            }
        }
        isConnected = false
        // Auto-reconnect after delay
    }

    func send(_ message: ClientMessage) {
        let data = try! JSONEncoder().encode(message)
        let text = String(data: data, encoding: .utf8)!
        task?.send(.string(text))
    }
}
```

#### ConversationViewModel

```swift
@Observable
final class ConversationViewModel {
    private(set) var messages: [Message] = []
    private(set) var streamingText: String = ""
    private(set) var isStreaming = false
    private(set) var status: ServerStatus = .idle

    private let api: APIClient
    private let ws: WebSocketClient

    func loadMessages() async {
        messages = try await api.getConversation(afterIndex: messages.count)
    }

    func sendMessage(_ content: String) async {
        try await api.sendMessage(content)
        // Response arrives via WebSocket events
    }

    func handleEvent(_ event: ServerEvent) {
        switch event {
        case .status(let state, _):
            status = state
        case .streamStart(let idx):
            isStreaming = true
            streamingText = ""
        case .streamDelta(let delta):
            streamingText += delta
        case .streamEnd(let idx):
            // Convert streaming text to a message
            messages.append(Message(role: .assistant, content: streamingText, ...))
            streamingText = ""
            isStreaming = false
        case .toolCall(let call):
            // Add tool call to last assistant message
            break
        case .toolResult(let result):
            // Update tool call card
            break
        // ... other cases
        }
    }
}
```

#### ConversationView

```swift
struct ConversationView: View {
    @State var viewModel: ConversationViewModel
    @State private var inputText = ""

    var body: some View {
        VStack(spacing: 0) {
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 8) {
                        ForEach(viewModel.messages) { message in
                            MessageBubbleView(message: message)
                        }
                        if viewModel.isStreaming {
                            MessageBubbleView(
                                message: Message(
                                    role: .assistant,
                                    content: viewModel.streamingText,
                                    isStreaming: true
                                )
                            )
                            .id("streaming")
                        }
                    }
                    .padding()
                }
                .onChange(of: viewModel.streamingText) { _, _ in
                    withAnimation { proxy.scrollTo("streaming", anchor: .bottom) }
                }
            }

            if viewModel.status == .thinking {
                HStack {
                    ProgressView()
                    Text("Thinking...")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Spacer()
                }
                .padding(.horizontal)
            }

            MessageInputBar(text: $inputText) {
                Task {
                    let text = inputText
                    inputText = ""
                    await viewModel.sendMessage(text)
                }
            }
        }
        .task { await viewModel.loadMessages() }
    }
}
```

### 3.6 iPad-Specific Features

- **Sidebar**: Session list always visible on left
- **Split View**: File browser in secondary column alongside conversation
- **Keyboard Shortcuts**: ⌘N new session, ⌘R refresh, ⌘Enter send
- **Stage Manager**: Works in any window size

### 3.7 iOS-Specific Features

- **Widget**: "What's klawed doing?" — shows current status and last message
- **Siri Shortcuts**: "Ask klawed" — sends a one-shot prompt and reads response
- **Share Extension**: Select text/files in any app → "Send to klawed"
- **Push Notifications**: When a long-running subagent completes (requires
  APNs or a polling background task)
- **Handoff**: Start on iPhone, continue on iPad/Mac

### 3.8 Offline/Reconnection

- WebSocketClient auto-reconnects with exponential backoff (1s → 2s → 4s → ... → 30s max)
- On reconnect, client re-subscribes and fetches missed messages via
  `GET /api/v1/conversation?after_index=N`
- `isConnected` state drives a persistent connection-status bar

---

## 4. Security Considerations

### 4.1 Threat Model

| Threat                        | Mitigation                                       |
|-------------------------------|--------------------------------------------------|
| Unauthorized access           | API key auth (32-byte random, Bearer token)      |
| Eavesdropping on LAN          | TLS (either direct or via gateway)               |
| API key exposure in URLs      | Accept key via header (preferred) or query param |
| Malicious inputs              | Server validates all JSON; tool sandboxing (existing) |
| WebSocket hijacking           | TLS + API key per-connection validation          |
| Brute force API key           | Rate limiting (future); key entropy is 128 bits  |

### 4.2 API Key Storage (iOS)

- API keys stored in **iOS Keychain** (encrypted at rest)
- Never stored in UserDefaults or plaintext files
- Access requires device unlock (`.whenUnlocked`)

### 4.3 Best Practices for Users

```bash
# Generate a strong random API key
openssl rand -hex 16 | sed 's/^/klawed_/'

# Bind to localhost by default
./build/klawed --serve 127.0.0.1:9099 --serve-api-key "$KEY"

# For remote access, use Cloudflare Tunnel (no open ports)
cloudflared tunnel --url http://localhost:9099

# OR: use Tailscale for encrypted overlay network
tailscale up
./build/klawed --serve 100.x.y.z:9099 --serve-api-key "$KEY"
```

---

## 5. Implementation Plan

### Phase 1: Bridge Server Core (C)

| Step | Description                                              | Files                          |
|------|----------------------------------------------------------|--------------------------------|
| 1.1  | Vendor mongoose (single .h + .c)                         | `vendor/mongoose.{c,h}`        |
| 1.2  | Bridge server start/stop + health endpoint               | `src/bridge_server.{c,h}`      |
| 1.3  | API key generation, validation, storage                  | `src/bridge_server.c`          |
| 1.4  | Session list + switch REST endpoints                     | `src/bridge_api.c`             |
| 1.5  | Conversation read (GET messages)                         | `src/bridge_api.c`             |
| 1.6  | Send message (enqueue to AI queue)                       | `src/bridge_api.c`             |
| 1.7  | WebSocket accept + subscribe                             | `src/bridge_ws.{c,h}`          |
| 1.8  | TUI message queue → WS bridge (streaming, todos)         | `src/bridge_ws.c`              |
| 1.9  | CLI flags + env vars for server config                   | `src/klawed.c`                 |
| 1.10 | Unit tests for bridge server                             | `tests/test_bridge_server.c`   |

### Phase 2: iOS App (Swift)

| Step | Description                                              |
|------|----------------------------------------------------------|
| 2.1  | Xcode project setup, SwiftUI app shell                   |
| 2.2  | APIClient + WebSocketClient networking layer             |
| 2.3  | ConnectionView (QR scan + manual entry)                  |
| 2.4  | SessionListView + session switching                      |
| 2.5  | ConversationView with MessageBubbleView                  |
| 2.6  | Streaming text support                                   |
| 2.7  | ToolCallCardView (collapsible)                           |
| 2.8  | Reasoning/thinking disclosure group                      |
| 2.9  | TODO list overlay                                        |
| 2.10 | iPad sidebar + split view                                |
| 2.11 | Widget + Siri Shortcuts                                  |
| 2.12 | Share extension                                          |

### Phase 3: Polish

| Step | Description                                              |
|------|----------------------------------------------------------|
| 3.1  | Auto-reconnect with backoff                              |
| 3.2  | Message persistence (cache conversations locally)        |
| 3.3  | Dark mode, Dynamic Type, VoiceOver accessibility         |
| 3.4  | Push notification for subagent completion                |
| 3.5  | App Store submission                                     |

---

## 6. Alternatives Considered

### 6.1 VNC-style Screen Sharing

Stream the ncurses TUI as a terminal emulator in the iOS app.
- **Pros:** Zero server changes — just run klawed in a pty.
- **Cons:** Terrible UX on mobile (60-column terminal on 6" screen), no native
  iOS interactions, high bandwidth, no structured data access.

**Verdict:** Rejected. Native UI is essential for a good mobile experience.

### 6.2 GraphQL Instead of REST

- **Pros:** Single endpoint, query exactly what you need.
- **Cons:** Adds complexity (schema, resolvers), harder to implement in C.
  REST + WebSocket is simpler and sufficient.

**Verdict:** Rejected for v1. REST is simpler to implement in C.

### 6.3 gRPC Instead of REST+WS

- **Pros:** Strong typing, bidirectional streaming built-in.
- **Cons:** Requires protobuf compiler, complex C integration, iOS can do it
  but adds dependency weight.

**Verdict:** Rejected. JSON over HTTP/WS is universally supported and simpler.

### 6.4 Separate Bridge Daemon (Go/Rust)

A standalone process that speaks klawed's UDS protocol and exposes HTTP+WS.

- **Pros:** Clean separation, can use Go/Rust HTTP libraries.
- **Cons:** Two processes to manage, UDS protocol is too limited (no streaming,
  no session listing), would need to extend UDS protocol anyway.

**Verdict:** Rejected. Embedding in klawed gives direct access to all internal
state without serialization overhead.

---

## 7. Open Questions

1. **Should the bridge server support multiple concurrent sessions?**
   Currently klawed has one active session at a time. Supporting multiple
   concurrent sessions would require significant refactoring of
   `ConversationState`. For v1, one session at a time is acceptable.

2. **Should iOS support multiple server connections?**
   For v1, single server is fine. Power users with multiple klawed instances
   can switch manually.

3. **Tool approval on iOS?**
   Some tools (bash commands, file writes) might warrant user approval before
   execution. This requires a two-phase tool execution pipeline (request →
   approval → execute). Defer to v2.

4. **Image upload from iOS?**
   The bridge server already needs to handle `INTERNAL_IMAGE` content blocks.
   Add `POST /api/v1/conversation/send` with multipart form data + image.
   Defer to v2.

5. **End-to-end encryption?**
   TLS protects data in transit. For additional security, the bridge server
   and iOS app could encrypt message payloads with a shared secret. Not
   planned for v1.

---

## 8. Appendix: Wire Format Examples

### 8.1 Full Conversation Turn via WebSocket

```
CLIENT → { "type": "send_message", "content": "fix the login bug" }

SERVER → { "type": "status", "state": "thinking" }

SERVER → { "type": "reasoning_start", "message_index": 3 }
SERVER → { "type": "reasoning_delta", "delta": "I need to search for login-related code..." }
SERVER → { "type": "reasoning_delta", "delta": " Let me check auth.ts first." }
SERVER → { "type": "reasoning_end", "message_index": 3 }

SERVER → { "type": "tool_call", "request_id": "call_01", "tool_name": "Grep",
           "params": { "pattern": "login", "path": "src/" } }

SERVER → { "type": "status", "state": "executing", "detail": "Running: grep -r login src/" }

SERVER → { "type": "tool_result", "request_id": "call_01", "tool_name": "Grep",
           "output": "src/auth.ts:42: function login(username, password) {\nsrc/auth.ts:88:   login(user, pass);",
           "is_error": false }

SERVER → { "type": "status", "state": "thinking" }

SERVER → { "type": "stream_start", "message_index": 3 }
SERVER → { "type": "stream_delta", "delta": "I found the login" }
SERVER → { "type": "stream_delta", "delta": " function in `src/auth.ts`" }
SERVER → { "type": "stream_delta", "delta": " at line 42." }
SERVER → { "type": "stream_end", "message_index": 3 }

SERVER → { "type": "message_added", "message": {
    "index": 3, "role": "assistant",
    "content": "I found the login function in `src/auth.ts` at line 42.",
    "tool_calls": [{ "id": "call_01", "name": "Grep", "params": {...}, "result": {...} }]
}}

SERVER → { "type": "status", "state": "idle" }
```

### 8.2 Interrupt Flow

```
CLIENT → { "type": "interrupt" }

SERVER → { "type": "status", "state": "idle" }
SERVER → { "type": "message_added", "message": {
    "index": 3, "role": "assistant",
    "content": "[Interrupted by user]",
    "interrupted": true
}}
```
