# Web Agent

A sessionful web browsing CLI with persistent browser sessions, inspired by the design in `repl-web-agent.md`.

## Overview

Web Agent is a CLI tool that provides persistent browser sessions for web automation. Each session maintains browser state (tabs, cookies, navigation history) across invocations, allowing you to issue one command at a time while maintaining session state.

## Features

- **Persistent Sessions**: Browser sessions are persisted in `~/.web-agent/sessions/`
- **One Command at a Time**: Stateless CLI that connects to session state
- **Full Browser Automation**: Powered by Playwright with support for all major browsers
- **Session Management**: Create, list, and cleanup sessions
- **JSON Output**: Machine-readable output with `--json` flag
- **Command Discovery**: `describe-commands` to explore available commands

## Installation

### Prerequisites

- Go 1.22 or later
- Playwright browsers (installed automatically on first run)

### Build from Source

```bash
# Clone the repository
git clone <repository-url>
cd web-agent

# Install dependencies
make install-deps

# Build the binary
make build

# The binary will be available at bin/web-agent
```

## Usage

### Basic Usage

```bash
# Create a session and open a URL
web-agent --session my-session open https://example.com

# List tabs in the session
web-agent --session my-session list-tabs

# Evaluate JavaScript on the page
web-agent --session my-session eval "document.title"

# Take a screenshot
web-agent --session my-session screenshot --path screenshot.png

# Get session information
web-agent --session my-session session-info

# Describe available commands
web-agent --session my-session describe-commands

# End a session
web-agent --session my-session end-session
```

### Global Flags

- `--session <id>`: Session ID (required, creates if missing)
- `--json`: Machine-readable JSON output
- `--timeout <sec>`: Per-command timeout in seconds (default: 30)
- `--headless/--no-headless`: Run browser in headless mode (default: headless)
- `--verbose`: Enable verbose output

## Available Commands

### Browser Control
- `open <url>` - Navigate to URL
- `list-tabs` - List all tabs
- `switch-tab <id>` - Switch to tab by ID
- `close-tab <id>` - Close tab by ID

### Page Interaction
- `eval <js>` - Evaluate JavaScript
- `click <selector>` - Click element (Playwright-style selectors)
- `type <selector> <text>` - Type text into element
- `wait-for <selector>` - Wait for element to appear

### Page Inspection
- `screenshot [--path <file>]` - Take screenshot
- `html` - Get page HTML

### Browser Configuration
- `set-viewport <width> <height>` - Set viewport size
- `cookies` - Get/set cookies

### Session Management
- `session-info` - Get session information
- `describe-commands` - List available commands
- `end-session` - End the session

## Architecture

Web Agent follows a client-driver architecture:

1. **CLI Client**: Thin wrapper that parses commands and communicates with driver
2. **Driver Process**: Manages Playwright browser instance and executes commands
3. **IPC Communication**: Unix domain sockets for client-driver communication
4. **Session Registry**: JSON-based persistence in `~/.web-agent/sessions/`

## Development

### Project Structure

```
web-agent/
├── cmd/web-agent/main.go      # CLI entry point
├── internal/
│   ├── commands/              # Command execution layer
│   ├── session/              # Session management
│   └── registry/             # Session registry
├── pkg/
│   ├── browser/              # Browser driver and context
│   └── ipc/                  # IPC communication
├── go.mod                    # Go module definition
└── Makefile                  # Build system
```

### Building and Testing

```bash
# Install dependencies
make install-deps

# Build the project
make build

# Run tests
make test

# Format code
make fmt

# Run vet checks
make vet
```

### Adding New Commands

1. Add command to `pkg/ipc/protocol.go` (CommandType and structures)
2. Implement handler in `pkg/browser/commands.go`
3. Register handler in `pkg/browser/driver.go`
4. Add parsing logic in `internal/commands/execute.go`
5. Update `describe-commands` response

## Session Persistence

Sessions are stored in `~/.web-agent/sessions/<session-id>.json` with metadata:
- Session ID and creation timestamp
- Last used timestamp
- Driver process information (PID, socket path)
- Browser configuration (headless mode, active tab)

Sessions are automatically cleaned up after 7 days of inactivity (configurable).

## License

[License information to be added]

## Acknowledgments

- Built with [Playwright for Go](https://github.com/playwright-community/playwright-go)
- Inspired by the design in `repl-web-agent.md`