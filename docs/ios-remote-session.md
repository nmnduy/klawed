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

Following the modular package architecture used by **scarf** (ScarfCore +
ScarfDesign + ScarfIOS) and **ETOS-LLM-Studio** (ETOSCore + app targets):

```
KlawedRemote/
├── Packages/
│   ├── KlawedCore/                    # Models + networking (no UI deps)
│   │   ├── Package.swift
│   │   └── Sources/KlawedCore/
│   │       ├── Models/
│   │       │   ├── ServerConnection.swift   # Host, port, API key, WS state
│   │       │   ├── Session.swift            # Session ID, title, metadata
│   │       │   ├── Message.swift            # Role, contentType enum, timestamp
│   │       │   ├── ToolCall.swift           # Tool name, params, result
│   │       │   ├── TodoItem.swift           # Content, status
│   │       │   ├── GoalState.swift          # Goal text, status, judge verdict
│   │       │   └── ServerEvent.swift        # WS event enum (parsed from JSON)
│   │       ├── Transport/
│   │       │   ├── KlawedTransport.swift    # Protocol: fetch, send, connect
│   │       │   ├── KlawedRESTClient.swift   # URLSession REST implementation
│   │       │   └── KlawedWebSocket.swift    # URLSessionWebSocket implementation
│   │       └── Services/
│   │           └── SessionCacheService.swift # Local SQLite cache (GRDB.swift)
│   │
│   └── KlawedDesign/                  # Reusable UI components (no biz logic)
│       ├── Package.swift
│       └── Sources/KlawedDesign/
│           ├── Colors.swift                # Color tokens from klawed themes
│           ├── Typography.swift            # Font styles
│           ├── MessageBubble.swift         # Container: routes to sub-views
│           ├── TextBubbleContent.swift     # Plain text message
│           ├── ToolCallCard.swift          # Collapsible tool call with icon
│           ├── ToolResultCard.swift        # Tool output (truncatable)
│           ├── ReasoningDisclosure.swift   # Thinking/reasoning expandable
│           ├── StreamingTextBubble.swift   # Animated streaming text
│           ├── MessageInputBar.swift       # Text field + send button
│           ├── ConnectionBadge.swift       # Connected/disconnected indicator
│           ├── TypingIndicator.swift       # "AI is thinking..." animation
│           └── StatusBanner.swift          # Persistent status bar
│
├── KlawedRemote.xcodeproj
├── KlawedRemote/                      # iOS app target
│   ├── KlawedApp.swift                # @main entry, wires DI
│   ├── Coordinators/
│   │   └── KlawedCoordinator.swift    # Cross-tab nav, pending actions
│   ├── ViewModels/
│   │   ├── ConnectionViewModel.swift  # Connect/disconnect, QR scan
│   │   ├── SessionListViewModel.swift # List sessions, switch
│   │   ├── ConversationViewModel.swift# Messages, send, streaming
│   │   └── TodoViewModel.swift        # TODO list, real-time updates
│   ├── Views/
│   │   ├── Connection/
│   │   │   ├── ConnectionView.swift       # QR scan + manual entry
│   │   │   └── ServerQRCodeView.swift     # QR scanner
│   │   ├── Sessions/
│   │   │   ├── SessionListView.swift      # Session browser
│   │   │   └── SessionRowView.swift       # Single session row
│   │   ├── Conversation/
│   │   │   └── ConversationView.swift     # Main chat (uses KlawedDesign)
│   │   ├── Sidebar/
│   │   │   ├── SidebarView.swift          # iPad sidebar
│   │   │   └── FileBrowserView.swift      # Workspace file tree
│   │   └── Settings/
│   │       └── SettingsView.swift         # Appearance, about
│   ├── Extensions/
│   │   ├── ShareExtension/               # "Send to Klawed"
│   │   └── Widget/                        # Home screen widget
│   └── Resources/
│       └── Assets.xcassets                # App icon, colors
│
└── KlawedRemoteTests/
    ├── KlawedCoreTests/                   # Transport + model tests
    └── KlawedRemoteUITests/               # UI tests
```

**Dependency flow:** `App → KlawedDesign → KlawedCore` (never reverse).

KlawedCore has zero SwiftUI imports — it can be tested without a simulator.
KlawedDesign depends on KlawedCore for model types but contains no business
logic. The app target wires everything together and provides the concrete
transport implementation via environment injection.

### 3.3 Key Screens and UX Flow

```
  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐     ┌──────────────────────┐
  │  Servers     │     │  Connect     │     │  Sessions    │     │  Conversation        │
  │              │     │              │     │              │     │                      │
  │ myserver    │────►│ [QR Scan]   │────►│ Fix login   │────►│ ╭──────────────────╮  │
  │ dev-box     │     │              │     │ Add tests   │     │ │ I found the bug │  │
  │              │     │  or          │     │ Refactor    │     │ │ in auth.ts:42   │  │
  │ [+ Add]     │     │ [Manual]     │     │             │     │ ╰──────────────────╯  │
  │              │     │  host:port   │     │ [+ New]     │     │                      │
  └──────────────┘     │  api key     │     │             │     │ ┌─────────────────┐  │
                       └──────────────┘     └──────────────┘     │ │ 🔧 Grep: login  │  │
                                                                  │ │ src/auth.ts:42  │  │
                                                                  │ └─────────────────┘  │
                                                                  │                      │
                                                                  │ [_____________] [▶] │
                                                                  └──────────────────────┘
```

**0. Server List** (adopted from scarf's `ServerListView`)
- Shows all previously connected klawed instances
- Each row: nickname, host:port, connection status dot
- Swipe to forget (with confirmation dialog)
- "+" to add a new server (opens Connection screen)
- Tap to connect and proceed to Session List

**1. Connection Screen**
- QR code scanner (scan the TUI-displayed QR code)
- Manual entry: host, port, API key, optional nickname
- "Remember this server" toggle → stored in Keychain
- Connection status indicator (connecting/connected/error)
- Test connection button before proceeding

**2. Session List**
- Pull-to-refresh from server
- Session rows show title, date, message count, model
- Tap to switch session and enter Conversation
- "+" to start a new session
- Long-press for session metadata/details
- Pull-down to show file browser sidebar (iPad)

**3. Conversation View**
- Chat-style bubbles (user right, assistant left)
- Tool calls shown as collapsible cards with icon + name
- Streaming text animates chunk by chunk via WebSocket deltas
- Reasoning/thinking content in a disclosure group (collapsed by default)
- Input bar at bottom with send button and slash-command autocomplete
- Typing indicator when AI is thinking (three-dot animation)
- Pull-down to show TODO list overlay
- Goal/Ralph status as a persistent thin banner at top
- Long-press message for copy/share/delete actions

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

### 3.5 Key SwiftUI Components

#### KlawedTransport Protocol (from scarf's ServerTransport pattern)

```swift
// In KlawedCore — no SwiftUI dependencies
protocol KlawedTransport: Sendable {
    /// Fetch list of sessions from the server
    func fetchSessions() async throws -> [Session]

    /// Switch the active session on the server
    func switchSession(_ id: String) async throws -> Session

    /// Fetch conversation messages after a given index
    func fetchMessages(after index: Int, limit: Int) async throws -> [Message]

    /// Send a user message to the server (returns 202 Accepted)
    func sendMessage(_ content: String) async throws

    /// Open a WebSocket connection for real-time events
    func connectWebSocket() -> AsyncStream<ServerEvent>

    /// Send an interrupt signal to stop current generation
    func interrupt() async throws

    /// Fetch current TODO list
    func fetchTodos() async throws -> [TodoItem]

    /// Fetch current goal state (Ralph mode)
    func fetchGoal() async throws -> GoalState?

    /// Test connection health
    func healthCheck() async throws -> ServerInfo
}
```

#### KlawedCoordinator (from scarf's ScarfGoCoordinator pattern)

```swift
// In KlawedRemote app target
@Observable @MainActor
final class KlawedCoordinator {
    var selectedTab: Tab = .conversation
    var pendingSessionID: String?     // Consumed once by ConversationViewModel
    var connectionState: ConnectionState = .disconnected

    enum Tab: Hashable { case conversation, sessions, files, settings }
    enum ConnectionState { case disconnected, connecting, connected, error(String) }

    /// Navigate to a specific session from anywhere in the app
    func navigateToSession(_ id: String) {
        pendingSessionID = id
        selectedTab = .conversation
    }
}

// Custom environment key
struct KlawedCoordinatorKey: EnvironmentKey {
    static let defaultValue = KlawedCoordinator()
}
extension EnvironmentValues {
    var klawedCoordinator: KlawedCoordinator {
        get { self[KlawedCoordinatorKey.self] }
        set { self[KlawedCoordinatorKey.self] = newValue }
    }
}
```

#### ConversationViewModel (ScenePhase-aware, uses transport protocol)

```swift
// In KlawedRemote app target
@Observable @MainActor
final class ConversationViewModel {
    private(set) var messages: [Message] = []
    private(set) var streamingText: String = ""
    private(set) var isStreaming = false
    private(set) var status: ServerStatus = .idle
    private(set) var todos: [TodoItem] = []
    private(set) var goal: GoalState?

    private let transport: any KlawedTransport
    private let cache: SessionCacheService

    init(transport: any KlawedTransport, cache: SessionCacheService) {
        self.transport = transport
        self.cache = cache
        // Start consuming WebSocket events
        Task { await consumeEvents() }
    }

    func loadMessages() async {
        // Show cache instantly, then refresh from server
        messages = await cache.getCachedMessages(sessionID: currentSessionID)
        do {
            let fresh = try await transport.fetchMessages(after: messages.count)
            messages.append(contentsOf: fresh)
            await cache.cacheMessages(fresh)
        } catch {
            // Already showing cached data; surface error subtly
        }
    }

    func sendMessage(_ content: String) async {
        let userMsg = Message(role: .user, content: content)
        messages.append(userMsg)
        try? await transport.sendMessage(content)
        // Response arrives via WebSocket events
    }

    private func consumeEvents() async {
        for await event in transport.connectWebSocket() {
            handleEvent(event)
        }
    }

    func handleEvent(_ event: ServerEvent) {
        switch event {
        case .status(let state, let detail):
            status = .init(state: state, detail: detail)
        case .streamStart:
            isStreaming = true; streamingText = ""
        case .streamDelta(let delta):
            streamingText += delta
        case .streamEnd:
            let msg = Message(role: .assistant, content: streamingText)
            messages.append(msg)
            streamingText = ""; isStreaming = false
        case .toolCall(let id, let name, let params):
            appendToolCall(toLastAssistant: id, name: name, params: params)
        case .toolResult(let id, let output, let isError):
            updateToolResult(id: id, output: output, isError: isError)
        case .reasoningDelta(let delta):
            appendReasoningDelta(delta)
        case .todosUpdated(let items):
            todos = items
        case .goalUpdated(let state):
            goal = state
        }
    }

    func handleScenePhase(_ phase: ScenePhase) {
        // Pause/resume WebSocket on background/foreground transitions
        // (adopted from scarf's scenePhaseTick pattern)
    }
}
```

#### ConversationView

```swift
struct ConversationView: View {
    @State var viewModel: ConversationViewModel
    @Environment(\.klawedCoordinator) private var coordinator
    @Environment(\.scenePhase) private var scenePhase
    @State private var inputText = ""

    var body: some View {
        VStack(spacing: 0) {
            // Goal/Ralph status banner (if active)
            if let goal = viewModel.goal {
                StatusBanner(goal: goal)
            }

            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 8) {
                        ForEach(viewModel.messages) { message in
                            MessageBubble(message: message)
                                .id(message.id)
                        }
                        if viewModel.isStreaming {
                            StreamingTextBubble(text: viewModel.streamingText)
                                .id("streaming")
                        }
                    }
                    .padding()
                }
                .onChange(of: viewModel.streamingText) { _, _ in
                    withAnimation { proxy.scrollTo("streaming", anchor: .bottom) }
                }
                .onChange(of: viewModel.messages.count) { _, _ in
                    if let last = viewModel.messages.last {
                        withAnimation { proxy.scrollTo(last.id, anchor: .bottom) }
                    }
                }
            }

            // Typing indicator
            if viewModel.status.isThinking {
                TypingIndicator()
            }

            MessageInputBar(text: $inputText) {
                let text = inputText
                inputText = ""
                Task { await viewModel.sendMessage(text) }
            }
        }
        .task { await viewModel.loadMessages() }
        // ScenePhase awareness (from scarf pattern):
        // view model pauses WS on .background, resumes on .active
        .onChange(of: scenePhase) { _, newPhase in
            viewModel.handleScenePhase(newPhase)
        }
        // Handle coordinator-directed session navigation
        .onAppear {
            if let id = coordinator.pendingSessionID {
                Task { await viewModel.switchToSession(id) }
                coordinator.pendingSessionID = nil
            }
        }
    }
}
```

The `KlawedCoordinator` is injected at the app root:

```swift
@main
struct KlawedApp: App {
    @State private var coordinator = KlawedCoordinator()

    var body: some Scene {
        WindowGroup {
            ServerListView()
                .environment(\.klawedCoordinator, coordinator)
                .environment(\.klawedTransport, /* configured transport */)
        }
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

---

## 9. Architecture Patterns from Well-Designed Open Source iOS Apps

We studied three well-architected open source iOS apps that are relevant to
our design. Here are the key patterns we're adopting.

### 9.1 Studied Projects

| Project | Stars | Relevance | Key Takeaways |
|---------|-------|-----------|---------------|
| [**scarf**](https://github.com/awizemann/scarf) | ⭐636 | iOS AI agent app connecting to remote Hermes agent over SSH | Transport abstraction, coordinator pattern, modular packages, Keychain auth |
| [**AICat**](https://github.com/Panl/AICat) | ⭐288 | Multi-platform SwiftUI ChatGPT client (iOS/iPadOS/Mac) | Clean ObservableObject pattern, SQLite via Blackbird, MarkdownUI for responses |
| [**ETOS-LLM-Studio**](https://github.com/Eric-Terminal/ETOS-LLM-Studio) | ⭐140 | Feature-rich iOS LLM client (OpenAI/Claude/Gemini + local GGUF) | Modular ChatBubble views, ETOSCore shared framework, MCP integration |

### 9.2 Adopted Architecture Patterns

#### 9.2.1 Transport Abstraction (from scarf)

Scarf defines a `ServerTransport` protocol with unified I/O primitives:
`readFile`, `writeFile`, `runProcess`, `streamLines`. Local and SSH
transports implement the same interface. All services and ViewModels
depend on the protocol, never on concrete transports.

**How we apply this:** Our `APIClient` + `WebSocketClient` in the iOS app
form an equivalent transport layer. The bridge server in klawed is the
"remote transport." The iOS app treats it as a generic "klawed transport"
— whether the server is local or remote is transparent above the transport
layer.

```
┌──────────────────────────────────┐
│  ConversationViewModel           │   ← depends on protocol, not concrete
│  SessionListViewModel            │
│  TodoViewModel                   │
└──────────────┬───────────────────┘
               │
┌──────────────▼───────────────────┐
│  KlawedTransport (protocol)      │   ← unified interface
│  - fetchSessions()               │
│  - fetchMessages(after:)         │
│  - sendMessage(_)                │
│  - connectWebSocket()            │
└──────┬────────────────┬──────────┘
       │                │
┌──────▼──────┐  ┌──────▼──────────┐
│ APIClient   │  │ WebSocketClient │   ← concrete implementations
│ (REST)      │  │ (real-time)     │
└─────────────┘  └─────────────────┘
```

#### 9.2.2 Coordinator Pattern (from scarf)

Scarf uses `ScarfGoCoordinator` — an `@Observable @MainActor` class injected
via `.environment()` — for cross-tab navigation and inter-view signaling.
It carries `selectedTab`, `pendingResumeSessionID`, and `pendingProjectChat`.

**How we apply this:**

```swift
@Observable @MainActor
final class KlawedCoordinator {
    var selectedTab: Tab = .conversation
    var pendingSessionID: String?   // Consumed by ConversationViewModel
    var connectionState: ConnectionState = .disconnected

    enum Tab { case conversation, sessions, files, settings }

    func navigateToSession(_ id: String) {
        pendingSessionID = id
        selectedTab = .conversation
    }
}
```

This replaces ad-hoc `@State` / `@Binding` chains between views. Any view
in the tree can read the coordinator via `@Environment(\.klawedCoordinator)`.

#### 9.2.3 Modular Package Structure (from scarf and ETOS)

Scarf splits into three Swift packages:
- **ScarfCore** — Models, services, transport, no UI dependencies
- **ScarfDesign** — Reusable UI components (colors, typography, controls)
- **ScarfIOS** — iOS-specific bindings (Citadel transport, Keychain)

ETOS-LLM-Studio splits into:
- **ETOSCore** — Shared models, adapters (OpenAI, Anthropic, Gemini), ChatService
- **ETOS LLM Studio iOS App** — iOS views
- **ETOS LLM Studio Watch App** — Watch views

**How we apply this:**

```
KlawedRemote/
├── Packages/
│   ├── KlawedCore/           # Models + networking (no UI)
│   │   ├── Models/
│   │   │   ├── Session.swift
│   │   │   ├── Message.swift
│   │   │   ├── ToolCall.swift
│   │   │   ├── TodoItem.swift
│   │   │   └── ServerEvent.swift
│   │   ├── Transport/
│   │   │   ├── KlawedTransport.swift    # Protocol
│   │   │   ├── KlawedRESTClient.swift   # REST implementation
│   │   │   └── KlawedWebSocket.swift    # WS implementation
│   │   └── Services/
│   │       └── SessionCacheService.swift # Local SQLite cache
│   │
│   └── KlawedDesign/         # Shared UI components (no business logic)
│       ├── Colors.swift
│       ├── Typography.swift
│       ├── MessageBubble.swift
│       ├── ToolCallCard.swift
│       ├── StreamingText.swift
│       └── ConnectionBadge.swift
│
├── KlawedRemote/             # iOS app target
│   ├── KlawedApp.swift
│   ├── Coordinators/
│   │   └── KlawedCoordinator.swift
│   ├── ViewModels/
│   │   ├── ConnectionViewModel.swift
│   │   ├── SessionListViewModel.swift
│   │   ├── ConversationViewModel.swift
│   │   └── TodoViewModel.swift
│   └── Views/
│       ├── Connection/ConnectionView.swift
│       ├── Sessions/SessionListView.swift
│       ├── Chat/ConversationView.swift
│       └── Settings/SettingsView.swift
```

Benefits:
- **KlawedCore** can be tested without UI or SwiftUI imports
- **KlawedDesign** can be previewed in isolation with mock data
- iOS and (future) macOS/watchOS targets share Core + Design
- Clear dependency direction: App → Design → Core (never reverse)

#### 9.2.4 Message Bubble Modularity (from ETOS-LLM-Studio)

ETOS has separate view files for every aspect of chat bubbles:
`ChatBubbleTextSupport.swift`, `ChatBubbleReasoningViews.swift`,
`ChatBubbleToolWidgetViews.swift`, `ChatBubbleToolResults.swift`,
`ChatBubbleAttachmentSupport.swift`, etc.

**How we apply this:** Each message content type gets its own view component:

```swift
// In KlawedDesign:
struct MessageBubble: View {
    let message: Message

    var body: some View {
        VStack(alignment: message.role == .user ? .trailing : .leading) {
            switch message.contentType {
            case .text:
                TextBubbleContent(text: message.content)
            case .toolCall(let tool):
                ToolCallCard(tool: tool)
            case .toolResult(let result):
                ToolResultCard(result: result)
            case .reasoning(let reasoning):
                ReasoningDisclosure(reasoning: reasoning)
            case .streaming(let delta):
                StreamingTextBubble(text: delta)
            }
        }
    }
}
```

Each sub-view is independently previewable, testable, and reusable across
different screens (e.g., `ToolCallCard` appears in both the conversation
view and a future "recent tool calls" sidebar).

#### 9.2.5 SQLite Local Cache (from AICat)

AICat uses **Blackbird** (a lightweight Swift SQLite ORM) to cache
conversations and messages locally. This enables:
- Instant app launch with cached data
- Offline browsing of past conversations
- Background sync when connection is restored

**How we apply this:** Add a `SessionCacheService` in KlawedCore that mirrors
the server's session/message data into a local SQLite database (using
GRDB.swift or Blackbird):

```swift
actor SessionCacheService {
    func cacheSession(_ session: Session) async { ... }
    func cacheMessage(_ message: Message, sessionID: String) async { ... }
    func getCachedSessions() async -> [Session] { ... }
    func getCachedMessages(sessionID: String, after index: Int) async -> [Message] { ... }
    func syncFromServer(_ transport: KlawedTransport) async { ... }
}
```

The `ConversationViewModel` reads from cache first for instant display,
then refreshes from the server.

#### 9.2.6 Environment-Based Dependency Injection (from scarf)

Scarf uses SwiftUI's `@Environment` for dependency injection, avoiding
singletons and enabling testability:

```swift
// Scarf's approach:
@Environment(\.serverContext) private var serverContext
@Environment(\.scarfGoCoordinator) private var coordinator
@Environment(\.hermesCapabilities) private var capabilities
```

**How we apply this:**

```swift
// Custom environment keys
struct KlawedTransportKey: EnvironmentKey {
    static let defaultValue: any KlawedTransport = NoopTransport()
}
struct KlawedCoordinatorKey: EnvironmentKey {
    static let defaultValue = KlawedCoordinator()
}

extension EnvironmentValues {
    var klawedTransport: any KlawedTransport {
        get { self[KlawedTransportKey.self] }
        set { self[KlawedTransportKey.self] = newValue }
    }
    var klawedCoordinator: KlawedCoordinator {
        get { self[KlawedCoordinatorKey.self] }
        set { self[KlawedCoordinatorKey.self] = newValue }
    }
}

// Usage in views:
struct ConversationView: View {
    @Environment(\.klawedTransport) private var transport
    @Environment(\.klawedCoordinator) private var coordinator
    // ...
}
```

#### 9.2.7 ScenePhase-Aware ViewModels (from scarf)

Scarf tracks `ScenePhase` transitions (`.active` / `.background`) and
propagates them to ViewModels even when they're on non-foreground tabs.
This ensures proper pause/resume of WebSocket connections and polling.

**How we apply this:**

```swift
@Observable @MainActor
final class ConversationViewModel {
    private(set) var isActive = true

    func handleScenePhase(_ phase: ScenePhase) {
        switch phase {
        case .active:
            isActive = true
            reconnectIfNeeded()
        case .background:
            isActive = false
            // Keep WS alive for background tasks, but pause UI updates
        case .inactive:
            break
        }
    }
}
```

### 9.3 Patterns We Chose NOT to Adopt

| Pattern | Source | Why Not |
|---------|--------|---------|
| SSH transport (Citadel) | scarf | Klawed uses HTTP/WS, not SSH exec channels. Simpler. |
| Blackbird ORM | AICat | GRDB.swift is more actively maintained and features FTS5 like klawed's memory DB. |
| ObservableObject (iOS 16) | AICat | We target iOS 17+ and use modern `@Observable` macro. |
| Separate Watch app | ETOS | Defer to v2. The companion Watch app is a nice-to-have. |
| Claude.ai OAuth in-app browser | ETOS | Defer to v2. We start with API-key-based auth. |

### 9.4 Design Refinements from Study

After reviewing these projects, we made the following improvements to our
original design (sections 1–8 above have been updated):

1. **Modular packages** (was: flat Xcode project) → KlawedCore + KlawedDesign
2. **Coordinator pattern** (was: ad-hoc @State/@Binding) → KlawedCoordinator
3. **Transport protocol** (was: hardcoded APIClient) → KlawedTransport protocol
4. **Local SQLite cache** (was: server-only data) → SessionCacheService
5. **ScenePhase awareness** (was: no lifecycle handling) → handleScenePhase()
6. **Environment DI** (was: singletons) → @Environment injection
7. **Message bubble modularity** (was: monolithic MessageView) → per-type sub-views
8. **Message model with contentType enum** (was: flat Message struct) →
   discriminated union of .text, .toolCall, .toolResult, .reasoning, .streaming
