# Web Browse Agent

Version: `1.4.0`

A sessionful browser automation CLI for persistent web browsing sessions. This tool provides a REPL-style interface where you start a browser session and send commands one at a time while maintaining session state.

## Features

- **Persistent Sessions** - Browser sessions maintain state (tabs, cookies, navigation history) across commands
- **Persistent Storage** - Optional persistent browser data (cookies, localStorage, sessionStorage) via `WEB_AGENT_PERSISTENT_STORAGE` env var
- **One Command at a Time** - Stateless CLI that connects to session state
- **Auto-Cleanup** - Driver process monitors parent PID and auto-terminates when klawed exits
- **Full Browser Automation** - Powered by Playwright with support for all major browsers
- **JSON Output** - Machine-readable output with `--json` flag
- **Command Discovery** - Use `commands` subcommand to list available browser commands
- **Stealth Mode** - Built-in Cloudflare/bot-detection bypass for datacenter environments (on by default)

## Requirements

- **Go 1.22+**
- **Playwright** - Installed via `make install-deps`

## Installation

### Local/Desktop Environment

```bash
# Build the agent
cd tools/web_browse_agent
make build

# Install Playwright browsers (first time only)
make install-deps
```

### Datacenter / Container Environment

In datacenter environments (Docker, Kubernetes, CI/CD), system libraries required by
Chromium are often missing. Use the datacenter install target:

```bash
cd tools/web_browse_agent

# Full setup: build + Playwright browsers + system libs + wrapper script
make install-datacenter
```

This will:
1. Build the binary
2. Download and install Playwright's Chromium browser
3. Download Debian system libraries (libglib2, libnss3, libx11, etc.) to `bin/chromium_libs/`
4. Create a wrapper script `bin/web_browse_agent` that sets `LD_LIBRARY_PATH` automatically

The wrapper replaces the binary in-place, so you can use `bin/web_browse_agent` directly.

#### Manual library installation

If you only need the system libraries (e.g. you already have Go and Playwright installed):

```bash
./install-system-libs.sh /path/to/libs
export LD_LIBRARY_PATH="/path/to/libs/usr/lib/x86_64-linux-gnu:..."
```

## Stealth Mode (Cloudflare Bypass)

Stealth mode is **enabled by default** and patches several browser fingerprinting vectors
that bot detection systems (Cloudflare, DataDome, etc.) use to identify headless browsers:

| Detection Vector | Fix Applied |
|---|---|
| `navigator.webdriver` present | Deleted from Navigator prototype |
| `HeadlessChrome` in User-Agent | Replaced with regular `Chrome` UA string |
| `HeadlessChrome` in `sec-ch-ua` header | Overridden with `"Google Chrome"` brand list |
| `window.chrome` missing | Restored with full chrome runtime object |
| Plugins array empty | Spoofed with 3 standard Chrome plugins |
| `navigator.languages` shows `en-US@posix` | Fixed to `['en-US', 'en']` |
| Permissions return `denied` | Fixed for notifications |
| Small viewport | Set to 1920x1080 |
| Timezone missing | Set to `America/New_York` |

To disable stealth mode (e.g. for debugging):
```bash
WEB_AGENT_STEALTH=0 web_browse_agent --session test open https://example.com
```


## Usage

### Basic Usage

```bash
# Open a URL in a session
web_browse_agent --session my-session open https://example.com

# List tabs in the session
web_browse_agent --session my-session list-tabs

# Evaluate JavaScript on the page
web_browse_agent --session my-session eval "document.title"

# Take a screenshot
web_browse_agent --session my-session screenshot

# Upload a file to a file input
web_browse_agent --session my-session upload-file "input[type=file]" /path/to/file.pdf

# Upload multiple files
web_browse_agent --session my-session upload-file "#file-input" /path/to/file1.pdf /path/to/file2.jpg

# Get available commands
web_browse_agent commands

# End a session
web_browse_agent --session my-session end-session
```

### Async Navigation

The `open` command is async by design - it returns immediately after the browser commits the navigation (receives HTTP response headers) but doesn't wait for all resources to load. This enables REPL-style usage without blocking.

To wait for the page to fully load after opening:
```bash
web_browse_agent --session my-session open https://example.com
web_browse_agent --session my-session wait-for --wait-type navigation
```

### Global Flags

- `--session <id>` or `-s <id>`: Session ID (required for most commands)
- `--json`: Machine-readable JSON output
- `--timeout <sec>`: Per-command timeout in seconds (default: 30)
- `--headless/--no-headless`: Run browser in headless mode (default: headless)
- `--verbose` or `-v`: Enable verbose output
- `--proxy <url>`: HTTP/SOCKS proxy URL, e.g. `http://host:8080` or `socks5://host:1080` (overrides `WEB_AGENT_PROXY`)

## Available Commands

### Browser Control
- `open <url>` - Navigate to URL (async - returns immediately after navigation starts)
- `list-tabs` - List all tabs
- `switch-tab <id>` - Switch to tab by ID
- `close-tab <id>` - Close tab by ID

### Page Interaction
- `eval <js>` - Evaluate JavaScript
- `click <selector>` - Click element (CSS/Playwright selectors)
- `type <selector> <text>` - Type text into element
- `upload-file <selector> <file_path...>` - Upload file(s) to a file input element
- `wait-for <selector>` - Wait for element to appear (also supports `wait_type=navigation` to wait for page load after `open`)

### Page Inspection
- `screenshot` - Take screenshot (returns base64)
- `html` - Get page HTML

### Browser Configuration
- `set-viewport <width> <height>` - Set viewport size
- `cookies` - Get/set cookies

### Session Management
- `session-info` - Get session information
- `describe-commands` - List available commands with details
- `end-session` - End the session
- `ping` - Check if session is alive
- `commands` - Show available commands (no session required)

## Klawed Integration

The `web_browse_agent` tool in klawed uses this binary:

```json
{
  "command": "open",
  "args": ["https://example.com"],
  "session": "klawed"
}
```

The tool will:
1. Start a browser driver process if not already running
2. Send the command to the session
3. Return JSON results
4. Auto-cleanup when idle or when klawed exits

## Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `WEB_AGENT_PERSISTENT_STORAGE` | Enable persistent browser storage (cookies, localStorage, etc.) | `false` |
| `WEB_AGENT_IDLE_TIMEOUT` | Idle timeout in seconds (0 to disable) | `300` (5 min) |
| `WEB_AGENT_PROXY` | HTTP/SOCKS proxy URL for all browser traffic, e.g. `http://host:8080` or `socks5://host:1080` | (none) |
| `BROWSER_HEADLESS` | Run browser without UI | `true` |
| `BROWSER_VIEWPORT_WIDTH` | Browser viewport width | `1280` |
| `BROWSER_VIEWPORT_HEIGHT` | Browser viewport height | `720` |
| `BROWSER_ACTION_TIMEOUT` | Timeout for actions (ms) | `5000` |
| `BROWSER_NAVIGATION_TIMEOUT` | Timeout for navigation (ms) | `30000` |

## Persistent Browser Storage

By default, browser sessions use ephemeral storage - cookies, localStorage, sessionStorage, and other browser data are lost when the driver shuts down. To enable persistent storage across sessions:

```bash
# Enable persistent storage for a session
WEB_AGENT_PERSISTENT_STORAGE=true web_browse_agent --session my-session open https://example.com

# Browser data (cookies, localStorage, etc.) is stored in ~/.web-agent/sessions/<session-id>/user-data/
# Data persists even after the driver shuts down due to idle timeout

# When you restart the session, all stored data is restored
WEB_AGENT_PERSISTENT_STORAGE=true web_browse_agent --session my-session open https://example.com
```

**Note**: When persistent storage is enabled:
- Browser data is stored in `~/.web-agent/sessions/<session-id>/user-data/`
- Cookies, localStorage, sessionStorage, IndexedDB, and other browser data persist across driver restarts
- Each session has its own isolated user data directory
- To clear persistent data, delete the session directory or use a new session ID

## Idle Timeout

Browser sessions automatically shut down after a period of inactivity to clean up resources. The default idle timeout is 5 minutes.

- Sessions are kept alive by any command (open, eval, click, ping, etc.)
- After the idle timeout expires with no commands, the driver process exits
- The next command will automatically start a new driver

To configure:
```bash
# Set idle timeout to 10 minutes
WEB_AGENT_IDLE_TIMEOUT=600 web_browse_agent --session my-session open https://example.com

# Disable idle timeout (session runs until end-session or process killed)
WEB_AGENT_IDLE_TIMEOUT=0 web_browse_agent --session my-session open https://example.com
```

## Architecture

Web Agent uses a client-driver architecture:

1. **CLI Client** - Thin wrapper that parses commands and communicates with driver
2. **Driver Process** - Manages Playwright browser instance and executes commands
3. **IPC Communication** - Unix domain sockets for client-driver communication
4. **Session Registry** - JSON-based persistence in `~/.web-agent/sessions/`
5. **Parent PID Monitoring** - Driver auto-terminates when parent process dies

## Development

### Project Structure

```
tools/web_browse_agent/
├── cmd/web_browse_agent/main.go   # CLI entry point
├── internal/
│   ├── commands/                  # Command execution layer
│   └── session/                   # Session management
├── pkg/
│   ├── browser/                   # Browser driver and handlers
│   ├── ipc/                       # IPC communication
│   └── version/                   # Version info
├── go.mod
└── Makefile
```

### Building and Testing

```bash
make build       # Build the binary
make test        # Run tests
make fmt         # Format code
make vet         # Run go vet
make dev         # Full dev workflow
```

## Breaking Changes from v0.x

This is a complete rewrite from the agentic "fire and forget" style to REPL-style:

- **v0.x**: Ran an agentic loop with LLM to accomplish tasks
- **v1.x**: Sends individual commands to a persistent browser session

The new architecture:
- Uses less context (no 18+ tool definitions flooded to LLM)
- Provides better control over browser state
- Auto-cleans up when klawed exits
- Uses `help` command for discovering available operations

## License

Part of the klawed project.
