Below is a concrete design for a “sessionful, one-command-at-a-time” CLI around `vendors/web_browse_agent/`. Think of it as a persistent RePL that you dip in and out of via a `--session <id>` flag.

## Goals
- Allow issuing one CLI command at a time while maintaining session state (tabs, cookies, navigation history).
- Let agents explore available commandsdynamically (discoverable help/describe).
- Keep it transport-agnostic so we can later expose the same model over MCP or sockets if desired.

## Mental model
- A **session** is a long-lived browser context keyed by `session_id`.
- The **CLI** is stateless per invocation; it connects to the session store, executes a single command, prints the result, and exits.
- Multiple invocations against the same `session_id` reuse tabs/cookies/storage.

## Proposed CLI shape
```
web-agent --session <id> <command> [args]

# Examples
web-agent --session abc123 open https://example.com
web-agent --session abc123 list-tabs
web-agent --session abc123 switch-tab 2
web-agent --session abc123 eval \"document.title\"
web-agent --session abc123 click \"text=Login\"
web-agent --session abc123 screenshot --path out.png
web-agent --session abc123 describe-commands
web-agent --session abc123 close-tab 2
web-agent --session abc123 end-session
```

### Global flags
- `--session <id>`: required. Creates if missing.
- `--json`: machine-readable output (stable schema).
- `--timeout <sec>`: per-command timeout.
- `--headless/--no-headless`: override default.
- `--verbose`: include debug traces.

### Core commands (initial set)
- `open <url>`
- `list-tabs`
- `switch-tab <index|id>`
- `close-tab <index|id>`
- `screenshot [--path <file>] [--full-page]`
- `html [--outer]` (get DOM)
- `eval <js>`
- `click <selector>` (Playwright-style: css= / text= / xpath= etc.)
- `type <selector> <text> [--delay <ms>]`
- `press <key>`
- `wait-for <selector> [--timeout <ms>]`
- `set-viewport <w> <h>`
- `cookies [--set <json>] [--clear]`
- `describe-commands` (returns list + short schema; enables “explore endpoints”)
- `session-info` (state summary: tabs, URL, user agent)
- `end-session` (teardown)
- `help` show how to use the RePL

### Session lifecycle
- Sessions persisted in a small registry (e.g., `~/.web-agent/sessions/`) storing:
  - Playwright context handle (PID/socket/IPC handle)
  - Metadata: created_at, last_used, headless flag, active tab id.
- Garbage collection policy: configurable idle TTL; `end-session` for explicit cleanup.

### Output contract
- Default: human-readable text.
- `--json`: `{ \"ok\": true/false, \"command\": \"...\", \"session\": \"abc123\", \"data\":{...}, \"error\": \"...\", \"logs\": [...] }`
- Include `available_commands` payload for `describe-commands`.

### Discovery / “explore endpoints”
- `describe-commands` returns:
  - Command names, args, brief descriptions, and capability tags (read/write/nav/snapshot).
  - Example payload:
    ```
    {
      \"commands\": [
        { \"name\": \"open\", \"args\": [\"url\"], \"desc\": \"Navigate to URL\", \"tags\": [\"nav\"] },
        { \"name\": \"list-tabs\", \"args\": [], \"desc\": \"List tabs\", \"tags\": [\"read\"] },
        ...
      ]
    }
  ```
- Optional `describe-command <name>` for detailed schema/validation hints.

### Execution flow (per invocation)
1. Parse flags (`--session`, `--json`, etc.).
2. Attach to or create session: load session registry; spin up Playwright context if new.
3. Dispatch command to session’s driver process.
4. Return result; if driver died, surface error and suggest `end-session` or recreate.

### Concurrency & isolation
- One driver process per session (or a pool keyed by session).
- Lock per session to serialize commands (simple file lock or mutex).
- Support multiple sessions in parallel (different IDs).

### Errors & resilience
- Detect stale/broken sessions; on error, return actionable message and set `ok:false`.
- Timeouts per command (propagate Playwright timeouts).
- Validation errors: exit code 2; runtime errors: exit code 1; success: 0.

### Minimal implementation steps
1. **Session registry**: simple JSON file per session with metadata; lockfile-based.
2. **Driver**: small daemon process started on first command; maintains Playwright context and tabs; communicates over a local IPC (Unix domain socket).
3. **CLI frontend**: thin wrapper that sends one request to the driver and prints result.
4. **Commands**: implement the initial set above.
5. **Describe**: hardcoded table for v1; later,introspect dynamically.
6. **Cleanup**: TTL sweeper + explicit `end-session`.
