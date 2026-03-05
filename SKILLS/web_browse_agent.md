# Skill: Web Browser Automation

Use the `web_browse_agent` command-line tool to control a persistent browser session for web automation tasks.

## Quick Start

```bash
# Open a URL
web_browse_agent --session mysession open https://example.com

# Get page content
web_browse_agent --session mysession html

# Take a screenshot (use --json to get base64 data)
web_browse_agent --session mysession --json screenshot

# End the session when done
web_browse_agent --session mysession end-session
```

## Command Pattern

All commands follow this pattern:

```bash
web_browse_agent --session <session-id> [--headless] [--json] <command> [args...]
```

- `--session <id>` - Session ID to use (required for most commands)
- `--headless` - Run browser without visible UI (default: true)
- `--headless=false` - Run browser with visible UI (requires X server, see below)
- `--json` - Output in JSON format for machine parsing
- `--timeout <sec>` - Per-command timeout in seconds (default: 30)

## Available Commands

### Browser Navigation
| Command | Arguments | Description |
|---------|-----------|-------------|
| `open` | `<url>` | Navigate to URL (async - returns when HTTP headers received) |
| `list-tabs` | - | List all open tabs |
| `switch-tab` | `<tab-id>` | Switch to a specific tab |
| `close-tab` | `<tab-id>` | Close a specific tab |

### Page Interaction
| Command | Arguments | Description |
|---------|-----------|-------------|
| `click` | `<selector>` | Click an element (CSS or Playwright selector) |
| `type` | `<selector> <text>` | Type text into an element (clears first) |
| `upload-file` | `<selector> <path...>` | Upload file(s) to a file input element |
| `wait-for` | `<selector>` | Wait for element to appear (CSS/Playwright selector only) |
| `eval` | `<javascript>` | Execute JavaScript and return result |

### Page Inspection
| Command | Arguments | Description |
|---------|-----------|-------------|
| `html` | - | Get the full page HTML |
| `screenshot` | - | Take screenshot — use `--json` to get base64 PNG data |

### Browser Configuration
| Command | Arguments | Description |
|---------|-----------|-------------|
| `set-viewport` | `<width> <height>` | Set browser viewport size |
| `cookies` | - | Get current cookies (read-only) |

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
web_browse_agent --session scrape open https://news.ycombinator.com

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

# Wait for the form to be ready
web_browse_agent --session login wait-for "#username"

# Fill in credentials
web_browse_agent --session login type "#username" "myuser"
web_browse_agent --session login type "#password" "mypass"

# Submit form
web_browse_agent --session login click "#submit"

# Wait for post-login element (page load signal)
web_browse_agent --session login wait-for "#dashboard"
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

# Navigate and wait for page to be ready
web_browse_agent --session capture open https://example.com
web_browse_agent --session capture wait-for "body"

# Capture screenshot — must use --json to get base64 image data
web_browse_agent --session capture --json screenshot
# Without --json, you only get a human-readable summary line, not the image data
```

**Decoding the screenshot:**
```bash
# Save PNG directly using Python
web_browse_agent --session capture --json screenshot \
  | python3 -c "import sys,json,base64; d=json.load(sys.stdin); open('shot.png','wb').write(base64.b64decode(d['data']))"

# Or use jq + base64 (Linux)
web_browse_agent --session capture --json screenshot \
  | jq -r '.data' | base64 -d > screenshot.png
```

### JavaScript Evaluation

```bash
# Get page title
web_browse_agent --session test eval "document.title"

# Get all links (result is always in .value field)
web_browse_agent --session test --json eval "Array.from(document.links).map(l => l.href)"

# Check element existence
web_browse_agent --session test eval "!!document.querySelector('#my-element')"

# Scroll to bottom
web_browse_agent --session test eval "window.scrollTo(0, document.body.scrollHeight)"

# Set a cookie via JavaScript (no native set-cookie command exists)
web_browse_agent --session test eval "document.cookie = 'name=value; path=/'"
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

## Async Navigation and Waiting for Load

The `open` command returns as soon as the HTTP response headers are received — it does **not** wait for the page to fully render. There is **no** `wait-for --wait-type navigation` flag; that syntax does not exist.

**Correct pattern** — wait for a specific element that signals the page is ready:

```bash
web_browse_agent --session s open https://example.com
web_browse_agent --session s wait-for "#main-content"
```

**Fallback** — wait for `body` if you just need the DOM to exist:

```bash
web_browse_agent --session s open https://example.com
web_browse_agent --session s wait-for "body"
```

`wait-for` only accepts CSS/Playwright selectors. It does **not** accept numeric timeouts or `navigation` as arguments.

## Cookies

`cookies` is **read-only** — it returns the current cookies for the page. There is no `set-cookie` command.

To inject cookies, use `eval` to set them via JavaScript:

```bash
# Read cookies
web_browse_agent --session s --json cookies

# Set a cookie via JavaScript
web_browse_agent --session s eval "document.cookie = 'session=abc123; path=/'"
```

Note: JavaScript-set cookies only work for non-`HttpOnly` cookies. To inject `HttpOnly` cookies you need to set them server-side (e.g. via a request that returns a `Set-Cookie` header).

## eval Result Format

`eval` always returns `{"value": <result>}`. The result is the JavaScript return value, serialised as JSON:

```bash
web_browse_agent --session s --json eval "document.title"
# → {"value":"Example Domain"}

web_browse_agent --session s --json eval "[1,2,3]"
# → {"value":[1,2,3]}

web_browse_agent --session s --json eval "43"
# → {"value":43}
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
| `DISPLAY` | X server display for non-headless mode | Not set |
| `WEB_AGENT_PERSISTENT_STORAGE` | Enable persistent browser storage | `false` |
| `WEB_AGENT_IDLE_TIMEOUT` | Idle timeout in seconds | `300` (5 min) |

## Building from Source

If the binary isn't available:

```bash
cd tools/web_browse_agent
make build
make install-deps  # First time: installs Playwright browsers
```

## Running in Non-Headless Mode (Visible Browser)

To see the browser window, you must:

1. **Have an X server running** (desktop environment or Xvfb)
2. **Set the DISPLAY environment variable**
3. **Use `--headless=false`**

```bash
# Check for available X displays
ls /tmp/.X11-unix/

# Find your display (look for :0 or :1)
who

# Run with visible browser
DISPLAY=:1 web_browse_agent --session test --headless=false open https://example.com

# Or export DISPLAY for the session
export DISPLAY=:1
web_browse_agent --session test --headless=false open https://example.com
```

**Common error without DISPLAY:**
```
Looks like you launched a headed browser without having a XServer running.
Set either 'headless: true' or use 'xvfb-run <your-playwright-app>' before running Playwright.
```

**Alternative: Use xvfb-run** (virtual framebuffer, no visible window but runs headed mode):
```bash
xvfb-run web_browse_agent --session test --headless=false open https://example.com
```

## Tips

1. **Always use `--json`** for programmatic parsing — especially for `screenshot` (required to get image data) and `eval`
2. **Reuse sessions** to maintain state across multiple operations
3. **Use `wait-for <selector>`** after `open` to ensure the page is ready — wait for a specific element, not `navigation`
4. **Use `eval`** for complex data extraction and for setting cookies
5. **Set viewport** before screenshots for consistent dimensions
6. **Set DISPLAY** when using `--headless=false` to see the browser window
