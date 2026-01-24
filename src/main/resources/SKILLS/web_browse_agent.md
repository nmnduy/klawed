# Skill: Web Browser Automation

Use the `web_browse_agent` command-line tool to control a persistent browser session for web automation tasks.

## Quick Start

```bash
# Open a URL
web_browse_agent --session mysession open https://example.com

# Get page content
web_browse_agent --session mysession html

# Take a screenshot
web_browse_agent --session mysession screenshot

# End the session when done
web_browse_agent --session mysession end-session
```

## Command Pattern

All commands follow this pattern:

```bash
web_browse_agent --session <session-id> [--headless] [--json] <command> [args...]
```

- `--session <id>` - Session ID to use (required for most commands)
- `--headless` - Run browser without visible UI (default)
- `--json` - Output in JSON format for machine parsing
- `--timeout <sec>` - Per-command timeout in seconds (default: 30)

## Available Commands

### Browser Navigation
| Command | Arguments | Description |
|---------|-----------|-------------|
| `open` | `<url>` | Navigate to URL (async - returns immediately) |
| `list-tabs` | - | List all open tabs |
| `switch-tab` | `<tab-id>` | Switch to a specific tab |
| `close-tab` | `<tab-id>` | Close a specific tab |

### Page Interaction
| Command | Arguments | Description |
|---------|-----------|-------------|
| `click` | `<selector>` | Click an element (CSS or Playwright selector) |
| `type` | `<selector> <text>` | Type text into an element |
| `upload-file` | `<selector> <path...>` | Upload file(s) to a file input element |
| `wait-for` | `<selector>` | Wait for element to appear |
| `eval` | `<javascript>` | Execute JavaScript and return result |

### Page Inspection
| Command | Arguments | Description |
|---------|-----------|-------------|
| `html` | - | Get the full page HTML |
| `screenshot` | - | Take screenshot (returns base64 PNG) |

### Browser Configuration
| Command | Arguments | Description |
|---------|-----------|-------------|
| `set-viewport` | `<width> <height>` | Set browser viewport size |
| `cookies` | - | Get current cookies |

### Session Management
| Command | Arguments | Description |
|---------|-----------|-------------|
| `session-info` | - | Get session information |
| `end-session` | - | Close browser and end session |
| `ping` | - | Check if session is alive |
| `commands` | - | List available commands (no session required) |
| `describe-commands` | - | Detailed command descriptions |

## Usage Examples

### Web Scraping Workflow

```bash
# Start a session and navigate
web_browse_agent --session scrape --json open https://news.ycombinator.com

# Wait for content to load
web_browse_agent --session scrape wait-for ".athing"

# Extract data with JavaScript
web_browse_agent --session scrape --json eval "Array.from(document.querySelectorAll('.athing .titleline a')).slice(0, 5).map(a => ({title: a.textContent, url: a.href}))"

# Clean up
web_browse_agent --session scrape end-session
```

### Form Interaction

```bash
# Navigate to login page
web_browse_agent --session login open https://example.com/login

# Fill in credentials
web_browse_agent --session login type "#username" "myuser"
web_browse_agent --session login type "#password" "mypass"

# Submit form
web_browse_agent --session login click "#submit"

# Wait for navigation
web_browse_agent --session login wait-for --wait-type navigation
```

### File Upload

```bash
# Upload a single file to a file input
web_browse_agent --session upload upload-file "input[type=file]" /path/to/document.pdf

# Upload multiple files
web_browse_agent --session upload upload-file "#file-input" /path/to/file1.pdf /path/to/file2.jpg

# Common workflow: navigate, upload, submit
web_browse_agent --session upload open https://example.com/upload
web_browse_agent --session upload wait-for "input[type=file]"
web_browse_agent --session upload upload-file "input[type=file]" /tmp/myfile.pdf
web_browse_agent --session upload click "#submit-button"
```

### Taking Screenshots

```bash
# Set viewport for consistent screenshots
web_browse_agent --session capture set-viewport 1920 1080

# Navigate and capture
web_browse_agent --session capture open https://example.com
web_browse_agent --session capture wait-for "body"
web_browse_agent --session capture screenshot > screenshot.base64
```

### JavaScript Evaluation

```bash
# Get page title
web_browse_agent --session test eval "document.title"

# Get all links
web_browse_agent --session test --json eval "Array.from(document.links).map(l => l.href)"

# Check element existence
web_browse_agent --session test eval "!!document.querySelector('#my-element')"

# Scroll to bottom
web_browse_agent --session test eval "window.scrollTo(0, document.body.scrollHeight)"
```

## Selectors

The `click`, `type`, and `wait-for` commands accept CSS selectors or Playwright-specific selectors:

```bash
# CSS selectors
web_browse_agent --session s click "#submit-button"
web_browse_agent --session s click ".nav-item:first-child"
web_browse_agent --session s click "[data-testid='login']"

# Playwright text selector
web_browse_agent --session s click "text=Sign In"

# Playwright role selector
web_browse_agent --session s click "role=button[name='Submit']"
```

## Async Navigation Note

The `open` command returns immediately after navigation starts (when HTTP headers are received). To wait for full page load:

```bash
web_browse_agent --session s open https://example.com
web_browse_agent --session s wait-for --wait-type navigation
```

Or wait for a specific element that indicates the page is ready:

```bash
web_browse_agent --session s open https://example.com
web_browse_agent --session s wait-for "#main-content"
```

## Session Lifecycle

1. **First command creates session** - No explicit session creation needed
2. **Session persists across commands** - Cookies, tabs, and state are maintained
3. **Auto-cleanup on exit** - Sessions are cleaned up when the parent process exits
4. **Manual cleanup** - Use `end-session` to explicitly close

## Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `KLAWED_WEB_BROWSE_AGENT_PATH` | Path to web_browse_agent binary | Auto-detected |
| `KLAWED_EXPLORE_HEADLESS` | Run in headless mode | `1` (true) |

## Building from Source

If the binary isn't available:

```bash
cd tools/web_browse_agent
make build
make install-deps  # First time: installs Playwright browsers
```

## Tips

1. **Use JSON output** for programmatic parsing: `--json`
2. **Reuse sessions** to maintain state across multiple operations
3. **Use `wait-for`** after navigation to ensure content is loaded
4. **Use `eval`** for complex data extraction that simple commands can't handle
5. **Set viewport** before screenshots for consistent dimensions
