# Web Browse Agent

Version: `1.0.0`

A sessionful browser automation CLI for persistent web browsing sessions. This tool provides a REPL-style interface where you start a browser session and send commands one at a time while maintaining session state.

## Features

- **Persistent Sessions** - Browser sessions maintain state (tabs, cookies, navigation history) across commands
- **One Command at a Time** - Stateless CLI that connects to session state
- **Auto-Cleanup** - Driver process monitors parent PID and auto-terminates when klawed exits
- **Full Browser Automation** - Powered by Playwright with support for all major browsers
- **JSON Output** - Machine-readable output with `--json` flag
- **Command Discovery** - Use `commands` subcommand to list available browser commands

## Requirements

- **Go 1.22+**
- **Playwright** - Installed automatically on first run

## Installation

```bash
# Build the agent
cd tools/web_browse_agent
make build

# Install Playwright browsers (first time only)
make install-deps
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

# Get available commands
web_browse_agent commands

# End a session
web_browse_agent --session my-session end-session
```

### Global Flags

- `--session <id>` or `-s <id>`: Session ID (required for most commands)
- `--json`: Machine-readable JSON output
- `--timeout <sec>`: Per-command timeout in seconds (default: 30)
- `--headless/--no-headless`: Run browser in headless mode (default: headless)
- `--verbose` or `-v`: Enable verbose output

## Available Commands

### Browser Control
- `open <url>` - Navigate to URL
- `list-tabs` - List all tabs
- `switch-tab <id>` - Switch to tab by ID
- `close-tab <id>` - Close tab by ID

### Page Interaction
- `eval <js>` - Evaluate JavaScript
- `click <selector>` - Click element (CSS/Playwright selectors)
- `type <selector> <text>` - Type text into element
- `wait-for <selector>` - Wait for element to appear

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
4. Auto-cleanup when klawed exits

## Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `BROWSER_HEADLESS` | Run browser without UI | `true` |
| `BROWSER_VIEWPORT_WIDTH` | Browser viewport width | `1280` |
| `BROWSER_VIEWPORT_HEIGHT` | Browser viewport height | `720` |
| `BROWSER_ACTION_TIMEOUT` | Timeout for actions (ms) | `5000` |
| `BROWSER_NAVIGATION_TIMEOUT` | Timeout for navigation (ms) | `30000` |

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
