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
- **Tools**: Built-in tools in `src/klawed.c`, common utilities in `src/tool_utils.c`
- **Subagent**: `src/subagent_manager.c`, `docs/subagent.md` (task delegation with fresh context)
- **Explore Subagent**: `src/explore_tools.c`, `docs/explore-subagent.md` (web research mode with Context7 + web_browse_agent)
- **MCP**: `src/mcp.c`, `src/mcp.h`, `docs/mcp.md` (external tool servers)
- **TODO system**: `src/todo.c`, `src/todo.h`
- **TUI & Normal Mode**: `src/tui.c`, `src/tui.h`, `docs/keyboard-shortcuts.md`
- **Arena allocator**: `src/arena.h` (region-based memory management, single-header library)
- **Memvid integration**: `src/memvid.c`, `src/memvid.h`, `docs/memvid.md` (persistent memory)
- **Memory injection**: `src/context/memory_injection.c`, `src/context/memory_injection.h` (automatic context injection before each API request)
- **Auto-compaction**: `src/compaction.c`, `src/compaction.h`, `docs/auto_compaction.md` (automatic context management with API token tracking)

**Vendors:**
- **web_browse_agent**: `vendors/web_browse_agent/` - Go-based web browser agent with Playwright
- **context7**: `vendors/context7/` - Context7 MCP server (reference for API)

**Data & State:**
- **Database/Persistence**: `src/persistence.c`, `src/sqlite_queue.c`, `docs/sqlite-queue.md`
- **HTTP client**: `src/http_client.c`, `src/http_client.h`
- **Session management**: `src/session.c`, `src/session.h`
- **History**: `src/history_file.c`, `src/history_file.h`
- **Migration system**: `src/migrations.c`, `src/migrations.h`
- **Retry logic**: `src/retry_logic.c`, `src/retry_logic.h`

**User Interfaces:**
- **Color themes**: `src/colorscheme.h`, `src/builtin_themes.c`, `docs/COLOR_THEMES.md`
- **Token usage tracking**: `src/persistence.c`, `docs/token-usage.md`
- **Window management**: `src/window_manager.c`, `src/window_manager.h`, `docs/window-management-refactor.md`
- **Voice mode**: `src/voice_input.c`, `src/voice_stub.c`, `docs/voice-mode.md`
- **Chat input**: `src/ncurses_input.c`, `src/ncurses_input.h`
- **File search (Ctrl+F)**: `src/file_search.c`, `src/file_search.h` (fuzzy file finder popup)
- **Streaming**: `docs/streaming.md` (real-time response display)

**Network & Communication:**
- **ZMQ socket**: `src/zmq_socket.c`, `src/zmq_socket.h`, `docs/zmq_input_output.md`
- **Unix domain socket**: `src/uds_socket.c`, `src/uds_socket.h` (synchronous peer-to-peer IPC)

**Tests**: `tests/test_*.c` (unit tests for all major components)
**Build**: `Makefile`

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

## Building and Testing

**Note for macOS users:** If you encounter errors like "invalid option -- 2" or "invalid option -- 0" when running `make`, you may have an old version of make (3.81) that has issues parsing paths with underscores. Install a newer version with `brew install make` and use `gmake` instead of `make`, or use the provided wrapper script `./make_wrapper.sh`.

**Quick start:**
```bash
make check-deps   # Verify dependencies (libcurl, cJSON, pthread)
make              # Build: output to build/klawed
make test         # Run unit tests (tests/ directory)
```

**With ZMQ support:**
```bash
make ZMQ=1        # Build with ZeroMQ socket support (requires libzmq)
make ZMQ=0        # Explicitly disable ZMQ support
# Or let it auto-detect (default):
make              # Will auto-detect libzmq if available
```

**With Memvid support (persistent memory):**
```bash
make MEMVID=1     # Build with memvid support (requires memvid-ffi library)
make MEMVID=0     # Explicitly disable memvid support
# Or let it auto-detect (default):
make              # Will auto-detect if libmemvid_ffi is available
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

**Environment variables:**
- **API**: `OPENAI_API_KEY` (required), `OPENAI_MODEL`, `OPENAI_API_BASE`
- **OpenAI Authentication**: `OPENAI_AUTH_HEADER` - Custom auth header template (e.g., "x-api-key: %s" or "Authorization: Bearer %s")
- **Extra Headers**: `OPENAI_EXTRA_HEADERS` - Comma-separated list of additional headers (e.g., "anthropic-version: 2023-06-01, User-Agent: my-app")
- **Caching**: `DISABLE_PROMPT_CACHING=1` to disable
- **Logging**: `KLAWED_LOG_LEVEL` (DEBUG/INFO/WARN/ERROR), `KLAWED_LOG_PATH`
- **Database**: `KLAWED_DB_PATH` for API call history (SQLite)
- **Database Rotation**:
  - `KLAWED_DB_MAX_DAYS` - Keep records for N days (default: 30, 0=unlimited)
  - `KLAWED_DB_MAX_RECORDS` - Keep last N records (default: 1000, 0=unlimited)
  - `KLAWED_DB_MAX_SIZE_MB` - Max database size in MB (default: 100, 0=unlimited)
  - `KLAWED_DB_AUTO_ROTATE` - Enable auto-rotation (default: 1, set to 0 to disable)
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
- **API Limits**:
  - `KLAWED_MAX_TOKENS` - Maximum tokens for completion (default: 16384)
- **Theme**: `KLAWED_THEME` pointing to Kitty .conf file
- **MCP**: `KLAWED_MCP_ENABLED=1` to enable (disabled by default), `KLAWED_MCP_CONFIG` for config path
  - `KLAWED_MCP_INIT_TIMEOUT` - Timeout for MCP server initialization in seconds (default: 10, 0=no timeout, overrides config file)
  - `KLAWED_MCP_REQUEST_TIMEOUT` - Timeout for MCP server requests in seconds (default: 30, 0=no timeout, overrides config file)
- **ZMQ Socket**: `KLAWED_ZMQ_ENDPOINT` - ZMQ endpoint (e.g., "tcp://127.0.0.1:5555" or "ipc:///tmp/klawed.sock")
  - `KLAWED_ZMQ_MODE` - ZMQ mode ("daemon")
- **Unix Socket**: `KLAWED_UNIX_SOCKET_PATH` - Unix domain socket path (e.g., "/tmp/klawed.sock")
  - `KLAWED_UNIX_SOCKET_RETRIES` - Max reconnection attempts (default: 5)
  - `KLAWED_UNIX_SOCKET_TIMEOUT` - Timeout for operations in seconds (default: 30)
- **Memory**: `KLAWED_MEMORY_PATH` for custom memory file location (default: `.klawed/memory.mv2`)
- **Auto-compaction**: `KLAWED_AUTO_COMPACT` - Enable automatic context compaction (1/true/yes, requires memvid)
  - `KLAWED_COMPACT_THRESHOLD` - Trigger compaction at this % of model token limit (default: 60)
  - `KLAWED_COMPACT_KEEP_RECENT` - Keep this many recent messages after compaction (default: 20)
  - `KLAWED_CONTEXT_LIMIT` - Override model token limit (default: 125000)
- **Explore Mode**: `KLAWED_EXPLORE_MODE` - Enable explore subagent mode (1/true/yes)
  - `KLAWED_EXPLORE_HEADLESS` - Run browser in headless mode (default: 1)
  - `KLAWED_WEB_BROWSE_AGENT_PATH` - Path to web_browse_agent binary (default: vendors/web_browse_agent/web_browse_agent)
  - `CONTEXT7_API_KEY` - API key for Context7 (optional, for higher rate limits)

**Defaults:**
- Logs: `./.klawed/logs/klawed.log` (project-local)
- Database: `./.klawed/api_calls.db` (project-local)
- Prompt caching: Enabled
- Max tokens: 16384 (configurable via `KLAWED_MAX_TOKENS`)
- Token usage tracking: Enabled (stores in `token_usage` table)
- Memory file: `./.klawed/memory.mv2` (project-local, when memvid enabled)

## Development

If you're going to test the built binary, make sure it is run with 'timeout'. This is to avoid deadlock because the program runs in a loop waiting for user input.

You are encouraged to commit at the end of your completed coding task if changes were made.

Git commit conventions:
- When AI commits changes, use all lowercase for the main commit message (subject line)
- Keep commit bodies concise - don't write overly long commit messages
- Do not stage any markdown files. I will read them and decide whether to commit them.
