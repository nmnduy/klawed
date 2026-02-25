# KLAWED.md

Project instructions for Klawed when working with this codebase.

## Guidelines for This Document

**Purpose**: High-level executive overview and table of contents for the codebase.

**Keep it minimal:**
- Directional, not prescriptive - point to where things are, don't duplicate documentation
- Table of contents > detailed specs - link to source files and docs, don't copy them
- Executive summary > implementation details - what and where, not how
- Updates should be rare and only for structural/architectural changes

**Full documentation lives in**: Source code comments, `docs/*.md`, and individual README files.

## Quick Navigation

**Current tasks**: `./todo.md`
**Main implementation**: `zig/main.zig` (entry point, CLI argument parsing)

**Core Systems:**
- **API providers**: `zig/providers/openai.zig`, `zig/providers/anthropic.zig`, `zig/providers/bedrock.zig`, `zig/providers/deepseek.zig`
- **Tools**: Built-in tools in `zig/tools/`, dynamic tool definitions from JSON via `KLAWED_DYNAMIC_TOOLS` env or `.klawed/dynamic_tools.json`
- **Subagent**: `zig/subagent_manager.zig`, `docs/subagent.md` (task delegation with fresh context, supports per-subagent provider selection)
- **Explore Subagent**: `zig/explore_tools.zig`, `docs/explore-subagent.md` (web research mode with web_browse_agent)
- **MCP**: `zig/mcp.zig`, `docs/mcp.md` (external tool servers)
- **WebSocket mode**: `zig/websocket.zig` (RFC 6455 framing), `zig/websocket_mode.zig` (daemon runner), `docs/websocket.md` (ephemeral IPC with interrupt support)
- **TODO system**: `zig/tools/todo.zig`
- **TUI**: `zig/tui.zig`, `zig/tui/`, `docs/keyboard-shortcuts.md`
- **Memory system**: `zig/memory_db.zig` (SQLite-based persistent memory with FTS5)
- **Memory injection**: `zig/context/memory_injection.zig` (automatic context injection before each API request)
- **Auto-compaction**: `zig/compaction.zig`, `docs/auto_compaction.md` (automatic context management with API token tracking)

**Vendors:**
- **web_browse_agent**: `tools/web_browse_agent/` - Go-based web browser agent with Playwright

**Data & State:**
- **Database/Persistence**: `zig/persistence.zig`, `zig/sqlite_queue.zig`, `docs/sqlite-queue.md`
- **Token usage database**: `zig/token_usage_db.zig` (separate SQLite file for token tracking)
- **HTTP client**: `zig/http_client.zig`
- **Session management**: `zig/session.zig`
- **History**: `zig/history_file.zig`
- **Migration system**: `zig/migrations.zig`
- **Retry logic**: `zig/retry_logic.zig`

**User Interfaces:**
- **Color themes**: `zig/tui/colorscheme.zig`, `zig/tui/builtin_themes.zig`, `docs/COLOR_THEMES.md`
- **Token usage tracking**: `zig/token_usage_db.zig`, `docs/token-usage.md` (stored in separate `token_usage.db`)
- **Window management**: `zig/tui/window_manager.zig`, `docs/window-management-refactor.md`
- **Voice mode**: `docs/voice-mode.md`
- **Chat input**: `zig/tui/input.zig`
- **File search (Ctrl+F)**: `zig/tui/file_search.zig` (fuzzy file finder popup)
- **Streaming**: `docs/streaming.md` (real-time response display)

**Tests**: `zig/tests/test_*.zig` (unit tests for all major components)
**Build**: `build.zig` (Zig build system), `Makefile` (thin shim)
**Docker**: `Dockerfile.sandbox`, `docs/docker-web-browser.md`, `docs/docker-sandbox-deployment.md`

## Project Overview

Pure Zig implementation of a coding agent using AI APIs (OpenAI, Anthropic, AWS Bedrock).
Migrated from C to Zig in v2.0.0-zig. See `docs/zig-migration-plan.md` for migration history.

**Stack:**
- Zig 0.12.1
- libcurl (HTTP), sqlite3, openssl (via `@cImport` shims)
- 8 core tools implemented (including Subagent for task delegation)
- Prompt caching enabled by default
- Bash command timeout protection (configurable via `KLAWED_BASH_TIMEOUT`)
- Real-time streaming support (SSE) for Anthropic API

## Zig Coding Standards

**Key principles:**
- All fallible functions return `!T` error unions — never silently ignore errors
- Every allocating function takes an `allocator: std.mem.Allocator` parameter
- Use `defer` and `errdefer` for cleanup — no manual free() sequencing
- Strings are `[]const u8` slices (not null-terminated); use `std.mem.sliceTo(ptr, 0)` for C interop
- Use `std.ArrayList(u8)` as a string builder; `std.ArrayList(T)` for dynamic arrays
- Use `union(enum)` for variants (replaces C `int type + union` patterns)
- Use comptime for platform differences (`builtin.os.tag == .linux`)

**Testing requirements:**
- Run `zig build test` — all tests must pass
- Run `zig fmt zig/` (or `make fmt-whitespace`) after code changes
- Zero test failures required before committing

**Build flags:**
- Release: `zig build` (ReleaseSafe by default)
- Debug: `zig build debug`
- Tests: `zig build test`

## Building and Testing

**Quick start:**
```bash
make check-deps   # Verify dependencies (zig, libcurl, sqlite3)
make              # Build: output to zig-out/bin/klawed
make test         # Run unit tests (zig/tests/ directory)
```

`make test` (equivalently `zig build test`) can take a while. Increase the bash timeout if needed.

**Running:**
```bash
export OPENAI_API_KEY="your-api-key"
./zig-out/bin/klawed "your prompt"
```

**Test locations:**
- `zig/tests/test_edit.zig` - Edit tool tests
- `zig/tests/test_todo.zig` - TODO list system tests
- `zig/tests/test_base64.zig`, `test_config.zig`, `test_data_dir.zig`, etc.
- All tests run via `zig/tests.zig` as the root test file

## Configuration

**Configuration files:** 
- Global: `~/.klawed/config.json` - User-wide settings shared across all projects
- Local: `.klawed/config.json` - Project-specific settings (overrides global)
- See `docs/llm-provider-configuration.md` for detailed documentation and examples.

**Environment variables:**
- **API**: `OPENAI_API_KEY` (required), `OPENAI_MODEL`, `OPENAI_API_BASE`
- **OpenAI Authentication**: `OPENAI_AUTH_HEADER` - Custom auth header template (e.g., "x-api-key: %s" or "Authorization: Bearer %s")
- **Extra Headers**: `OPENAI_EXTRA_HEADERS` - Comma-separated list of additional headers (e.g., "anthropic-version: 2023-06-01, User-Agent: my-app")
- **Caching**: `DISABLE_PROMPT_CACHING=1` to disable
- **Data Directory**: `KLAWED_DATA_DIR` - Base directory for all klawed data files (default: `.klawed`). Individual paths can still be overridden by their specific env vars.
- **Logging**: `KLAWED_LOG_LEVEL` (DEBUG/INFO/WARN/ERROR), `KLAWED_LOG_PATH`
- **Database**: `KLAWED_DB_PATH` for API call history (SQLite)
- **Token Usage Database**: `KLAWED_TOKEN_USAGE_DB_PATH` - Path for token usage tracking (default: `.klawed/token_usage.db`)
- **Diagnostics**: `KLAWED_NO_STORAGE` - Set to 1 to disable SQLite database and history file. Useful for debugging TUI hangs on certain platforms (e.g., Mac Apple Silicon).
- **Database Rotation**:
  - `KLAWED_DB_MAX_DAYS` - Keep records for N days (default: 30, 0=unlimited)
  - `KLAWED_DB_MAX_RECORDS` - Keep last N records (default: 1000, 0=unlimited)
  - `KLAWED_DB_MAX_SIZE_MB` - Max database size in MB (default: 100, 0=unlimited)
  - `KLAWED_DB_AUTO_ROTATE` - Enable auto-rotation (default: 1, set to 0 to disable)
  - `KLAWED_TOKEN_USAGE_DB_MAX_DAYS` - Keep token records for N days (default: 30)
  - `KLAWED_TOKEN_USAGE_DB_MAX_RECORDS` - Keep last N token records (default: 5000)
- **Tools**: 
  - `KLAWED_GREP_MAX_RESULTS` - Max grep results (default: 100)
  - `KLAWED_GREP_DISPLAY_LIMIT` - Max grep results to display in TUI (default: 20)
  - `KLAWED_GLOB_DISPLAY_LIMIT` - Max glob results to display in TUI (default: 10)
  - `KLAWED_BASH_TIMEOUT` - Timeout for bash commands in seconds (default: 30, 0=no timeout)
  - `KLAWED_SUBAGENT_LOG_LINE_MAX_CHARS` - Maximum characters per log line in CheckSubagentProgress (default: 12000)
  - `KLAWED_SUBAGENT_ENV_VARS` - Comma-separated list of KEY=VALUE pairs to set in subagent processes (e.g., "OPENAI_MODEL=gpt-4,DEBUG=1")
  - `KLAWED_TOOL_VERBOSE` - Verbose tool logging (0=off, 1=basic, 2=detailed, default: 0)
  - `KLAWED_IS_SUBAGENT` - Internal flag set automatically when running as a subagent (1/true/yes=subagent mode). Excludes Subagent, CheckSubagentProgress, and InterruptSubagent tools to prevent recursion
  - `KLAWED_DISABLE_TOOLS` - Comma-separated list of tool names to disable (e.g., "UploadImage,Subagent"). Disabled tools won't appear in tool definitions and will return an error if called
  - `KLAWED_ONESHOT_FORMAT` - Output format for one-shot mode: `human` (clean, human-readable, default) or `json`/`machine` (HTML+JSON for machine parsing)
  - `KLAWED_ONESHOT_STYLE` - Visual style for one-shot mode: `boxes` (Unicode box-drawing, default), `compact` (minimal single-line output), or `minimal` (ultra-minimal)
  - `KLAWED_LLM_PROVIDER` - Select which named LLM provider to use from configuration (e.g., "sonnet-4.5-bedrock")
  - `KLAWED_NARROW_SCREEN_THRESHOLD` - Screen width threshold (in characters) below which status text is hidden to make space for token count and scroll percentage (default: 80, 0=always hide status text)
- **API Limits**:
  - `KLAWED_MAX_TOKENS` - Maximum tokens for completion (default: 16384)
- **Theme**: `KLAWED_THEME` pointing to Kitty .conf file
- **MCP**: `KLAWED_MCP_ENABLED=1` to enable (disabled by default), `KLAWED_MCP_CONFIG` for config path
  - `KLAWED_MCP_INIT_TIMEOUT` - Timeout for MCP server initialization in seconds (default: 10, 0=no timeout, overrides config file)
  - `KLAWED_MCP_REQUEST_TIMEOUT` - Timeout for MCP server requests in seconds (default: 30, 0=no timeout, overrides config file)
- **WebSocket daemon**:
  - `KLAWED_WS_HOST` - Bind host (default: `0.0.0.0`)
  - `KLAWED_WS_PORT` - Bind port, also enables WS mode when set (default: `9999`)
  - `KLAWED_WS_SENDER` - Sender name in JSON messages (default: `klawed`)
  - `KLAWED_WS_MAX_MSG_SIZE` - Max inbound message size in bytes (default: `4194304`)
  - `KLAWED_WS_MAX_QUEUE` - Outbound message queue capacity (default: `1000`)
- **Memory**: `KLAWED_MEMORY_PATH` for custom memory database location (default: `.klawed/memory.db`)
- **Auto-compaction**: `KLAWED_AUTO_COMPACT` - Enable automatic context compaction (1/true/yes)
  - `KLAWED_COMPACT_THRESHOLD` - Trigger compaction at this % of model token limit (default: 75)
  - `KLAWED_COMPACT_KEEP_RECENT` - Keep this many recent messages after compaction (default: 100)
  - `KLAWED_CONTEXT_LIMIT` - Override model token limit (default: 125000)
- **Explore Mode**: `KLAWED_EXPLORE_MODE` - Enable explore subagent mode (1/true/yes)
  - `KLAWED_EXPLORE_HEADLESS` - Run browser in headless mode (default: 1)
  - `KLAWED_WEB_BROWSE_AGENT_PATH` - Path to web_browse_agent binary (default: tools/web_browse_agent/web_browse_agent)
  - `CONTEXT7_API_KEY` - API key for Context7 (optional, for higher rate limits)

**Defaults:**
- Logs: `./.klawed/logs/klawed.log` (project-local)
- Database: `./.klawed/api_calls.db` (project-local)
- Token usage database: `./.klawed/token_usage.db` (project-local, separate from API call logs)
- Prompt caching: Enabled
- Max tokens: 16384 (configurable via `KLAWED_MAX_TOKENS`)
- Token usage tracking: Enabled (stores in separate `token_usage.db`)
- Memory database: `./.klawed/memory.db` (project-local, SQLite with FTS5)

## Development

If you're going to test the built binary, make sure it is run with 'timeout'. This is to avoid deadlock because the program runs in a loop waiting for user input.

You are encouraged to commit at the end of your completed coding task if changes were made.

Git commit conventions:
- When AI commits changes, use all lowercase for the main commit message (subject line)
- Keep commit bodies concise - don't write overly long commit messages
