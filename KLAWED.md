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
**Main implementation**: `src/klawed.c` (core agent loop, API calls)

**Core Systems:**
- **API providers**: `src/openai_provider.c`, `src/anthropic_provider.c`, `src/bedrock_provider.c`, `src/aws_bedrock.c`
- **Tools**: Built-in tools in `src/klawed.c`, common utilities in `src/tool_utils.c`, dynamic tool definitions from JSON via `KLAWED_DYNAMIC_TOOLS` env or `.klawed/dynamic_tools.json`
- **Subagent**: `src/subagent_manager.c`, `docs/subagent.md` (task delegation with fresh context, supports per-subagent provider selection)
- **Explore Subagent**: `src/explore_tools.c`, `docs/explore-subagent.md` (web research mode with web_browse_agent)
- **MCP**: `src/mcp.c`, `src/mcp.h`, `docs/mcp.md` (external tool servers)
- **TODO system**: `src/todo.c`, `src/todo.h`
- **TUI & Normal Mode**: `src/tui.c`, `src/tui.h`, `docs/keyboard-shortcuts.md`
- **Arena allocator**: `src/arena.h` (region-based memory management, single-header library)
- **Memory system**: `src/memory_db.c`, `src/memory_db.h` (SQLite-based persistent memory with FTS5)
- **Memory injection**: `src/context/memory_injection.c`, `src/context/memory_injection.h` (automatic context injection before each API request)
- **Auto-compaction**: `src/compaction.c`, `src/compaction.h`, `docs/auto_compaction.md` (automatic context management with API token tracking)
- **Perpetual mode**: `src/perpetual/`, `docs/perpetual-mode.md` (stateless+persistent mode: fresh context each request, growing `perpetual.md` log, root LLM orchestrates via Bash+Subagent tools)

**Vendors:**
- **web_browse_agent**: `tools/web_browse_agent/` - Go-based web browser agent with Playwright

**Data & State:**
- **Database/Persistence**: `src/persistence.c`, `src/sqlite_queue.c`, `docs/sqlite-queue.md`
- **Token usage database**: `src/token_usage_db.c`, `src/token_usage_db.h` (separate SQLite file for token tracking)
- **HTTP client**: `src/http_client.c`, `src/http_client.h`
- **Session management**: `src/session.c`, `src/session.h`
- **History**: `src/history_file.c`, `src/history_file.h`
- **Migration system**: `src/migrations.c`, `src/migrations.h`, `src/token_usage_db_migrations.c`
- **Retry logic**: `src/retry_logic.c`, `src/retry_logic.h`

**User Interfaces:**
- **Color themes**: `src/colorscheme.h`, `src/builtin_themes.c`, `docs/COLOR_THEMES.md`
- **Token usage tracking**: `src/token_usage_db.c`, `docs/token-usage.md` (stored in separate `token_usage.db`)
- **Window management**: `src/window_manager.c`, `src/window_manager.h`, `docs/window-management-refactor.md`
- **Voice mode**: `src/voice_input.c`, `src/voice_stub.c`, `docs/voice-mode.md`
- **Chat input**: `src/ncurses_input.c`, `src/ncurses_input.h`
- **File search (Ctrl+F)**: `src/file_search.c`, `src/file_search.h` (fuzzy file finder popup)
- **Streaming**: `docs/streaming.md` (real-time response display)

**Tests**: `tests/test_*.c` (unit tests for all major components)
**Build**: `Makefile`
**Docker**: `Dockerfile.sandbox`, `docs/DOCKER_QUICKSTART.md`, `docs/docker-web-browser.md`, `docs/docker-sandbox-deployment.md`

## Project Overview

Pure C implementation of a coding agent using AI APIs (OpenAI, Anthropic, AWS Bedrock).

**Stack:**
- C11 + POSIX
- libcurl (HTTP), cJSON (parsing), pthread, ncurses (TUI)
- 8 core tools implemented (including Subagent for task delegation)
- Prompt caching enabled by default
- Bash command timeout protection (configurable via `KLAWED_BASH_TIMEOUT`)
- Real-time streaming support (SSE) for Anthropic API

## C Coding Standards

**Key principles:**
- Initialize all pointers to NULL
- Zero-initialize structs with `= {0}`
- Check all malloc/calloc returns

**libbsd for safer C code:**

libbsd is a newly introduced to this codebase. We will slowly transition to it. New code should use this library. It is to replace risky libc functions and add overflow-safe allocation and secure memory wiping.
- **Install**: `apt install libbsd-dev` and link with `-lbsd`
- **Functions to use**:
  - `strlcpy(dst, src, size)` - safe string copy with guaranteed NUL termination
  - `strlcat(dst, src, size)` - safe concatenation
  - `strnlen(buf, max)` - bounded length check
  - `reallocarray(ptr, nmemb, size)` - detects integer overflow during allocations and returns NULL safely
  - `explicit_bzero(buf, len)` - securely erase memory without optimizer removal
  - `arc4random()` / `arc4random_buf()` - strong randomness with no manual seeding
- **Project rules**:
  - Never use `strcpy`, `strcat`, or unchecked `strlen`. Replace with `strlcpy`, `strlcat`, `strnlen`
  - For all resizable buffers and arrays, use `reallocarray` instead of `malloc(n*m)` or `realloc` to avoid overflow bugs
  - When wiping secrets (passwords, tokens), use `explicit_bzero`
  - Always check return values: `strlcpy`/`strlcat` return full source length; detect truncation when >= size. `reallocarray` returns NULL on overflow or OOM
  - **Recommended dynamic buffer pattern**:
    - Grow capacity using `reallocarray`
    - Append using `strlcpy(dst + len, src, cap - len)`
    - Update length safely

**Required compilation:**
- Flags: `-Wall -Wextra -Werror`
- Sanitizers: `-fsanitize=address,undefined` for testing
- Static analysis: `clang --analyze` or `gcc -fanalyzer`

**Testing requirements:**
- Zero warnings, zero leaks (Valgrind)
- All tests pass with sanitizers enabled
- See `Makefile` for test targets
- Run `make fmt-whitespace` once after making code changes.

## NASA C Coding Standards

1. **Simple control flow** - Avoid `goto`, `setjmp`/`longjmp`, and recursion where possible
2. **Bounded loops** - All loops should have reasonable upper bounds
3. **Minimize dynamic memory** - Use stack allocation when feasible
4. **Function length limit** - Keep functions under ~60 lines where possible
5. **Assertion density** - Add assertions for critical invariants
6. **Minimal scope** - Declare at smallest possible scope
7. **Return value & parameter checking** - Always check returns and validate parameters ✓
8. **Limited preprocessor** - Only header includes and simple macros ✓
9. **Careful pointer use** - Be mindful of pointer complexity
10. **Zero warnings** - Compile with all warnings, pass static analysis ✓

**Note:** These rules were discovered after the project has somewhat matured. You might see code that violates them. That's fine, we will incrementally refactor following the rules above. If it makes practical sense to use memory allocation, then use `src/arena.h`.

**Full details:** `docs/nasa_c_coding_standards.md`

## Thread-Safe TUI Updates

For code running in worker threads (especially streaming API responses), route all ncurses/TUI updates through the TUI message queue to avoid deadlocks:

- `post_tui_message()` - For adding lines, status updates, errors
- `post_tui_stream_start()` / `TUI_MSG_STREAM_START` - For opening streaming lines with color
- `TUI_MSG_STREAM_APPEND` - For appending incremental text

Direct ncurses calls from worker threads can cause deadlocks due to ncurses' internal locking. The message queue provides thread-safe communication between worker threads and the main TUI thread.

See: `src/message_queue.h`, `src/message_queue.c`

## Building and Testing

**Note for macOS users:** If you encounter errors like "invalid option -- 2" or "invalid option -- 0" when running `make`, you may have an old version of make (3.81) that has issues parsing paths with underscores. Install a newer version with `brew install make` and use `gmake` instead of `make`, or use the provided wrapper script `./make_wrapper.sh`.

**Quick start:**
```bash
make check-deps   # Verify dependencies (libcurl, cJSON, pthread)
make              # Build: output to build/klawed
make test         # Run unit tests (tests/ directory)
```

`make test` can take a while, most likely more than the default bash command timeout. So increase timeout value if required.

**Running:**
```bash
export OPENAI_API_KEY="your-api-key"
./build/klawed "your prompt"
```

**Test locations:**
- `tests/test_edit.c` - Edit tool tests (regex, replace_all)
- `tests/test_todo.c` - TODO list system tests
- Build system: Uses `-DTEST_BUILD` to expose internal functions

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
  - `KLAWED_PACMAN_MAX_CONTEXT` - Maximum context tokens for pacman thinking style (default: 200000). The distance from the first dot to pacman's position shows context usage as a ratio of this value.
- **API Limits**:
  - `KLAWED_MAX_TOKENS` - Maximum tokens for completion (default: 16384)
- **Theme**: `KLAWED_THEME` pointing to Kitty .conf file
- **MCP**: `KLAWED_MCP_ENABLED=1` to enable (disabled by default), `KLAWED_MCP_CONFIG` for config path
  - `KLAWED_MCP_INIT_TIMEOUT` - Timeout for MCP server initialization in seconds (default: 10, 0=no timeout, overrides config file)
  - `KLAWED_MCP_REQUEST_TIMEOUT` - Timeout for MCP server requests in seconds (default: 30, 0=no timeout, overrides config file)
- **Memory**: `KLAWED_MEMORY_PATH` for custom memory database location (default: `.klawed/memory.db`)
- **Auto-compaction**: `KLAWED_AUTO_COMPACT` - Enable automatic context compaction (1/true/yes)
  - `KLAWED_COMPACT_THRESHOLD` - Trigger compaction at this % of model token limit (default: 75)
  - `KLAWED_COMPACT_KEEP_RECENT` - Keep this many recent messages after compaction (default: 0 — rely entirely on compaction notice)
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
