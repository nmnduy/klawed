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
- `--headless` - Run browser without visible UI (default: true)
- `--headless=false` - Run browser with visible UI (requires X server, see below)
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

## Common Errors and Solutions

### Error 1: "Cannot read properties of null"

**Full Error:**
```
Error: command failed: failed to evaluate: playwright: TypeError: Cannot read properties of null (reading 'innerText')
```

**Cause:**
- CSS selector didn't match any element on the page
- `querySelector()` returned `null`
- Attempted to access property on `null` value

**Solution:**

```bash
# ❌ BAD - throws error if element doesn't exist
web_browse_agent --session s eval "document.querySelector('.container').innerText"

# ✅ GOOD - use optional chaining (returns undefined if null)
web_browse_agent --session s eval "document.querySelector('.container')?.innerText"

# ✅ BETTER - check existence first
web_browse_agent --session s eval "document.querySelector('.container')?.innerText || 'Element not found'"

# ✅ BEST - use guaranteed-to-exist selector
web_browse_agent --session s eval "document.body.innerText"
```

**Testing Selectors:**
```bash
# Check if element exists before trying to access it
web_browse_agent --session s eval "!!document.querySelector('.container')"
# Returns: {"value": true} or {"value": false}
```

---

### Error 2: "failed to unmarshal session"

**Full Error:**
```
Error: failed to get session: failed to load session: failed to unmarshal session: unexpected end of JSON input
```

**Cause:**
- Browser session file became corrupted
- Usually happens after JavaScript errors or rapid navigation
- Session state was not saved properly

**Solution:**

```bash
# Clean up corrupted session
web_browse_agent --session mysession end-session

# Start fresh (next command will create new session)
web_browse_agent --session mysession open https://example.com
```

**Prevention:**
- Always use `wait-for` after navigation before running `eval`
- Handle JavaScript errors gracefully in eval statements
- Use `ping` to verify session health before operations

```bash
# Check session health
web_browse_agent --session mysession ping

# Get session status
web_browse_agent --session mysession session-info
```

---

### Error 3: Empty or Unexpected Output from `eval`

**Problem:**
- `eval` returns empty string or outdated content
- JavaScript runs before page fully loads

**Solution:**

```bash
# ❌ BAD - eval runs before content loads
web_browse_agent --session s open https://example.com
web_browse_agent --session s eval "document.body.innerText"  # May be empty!

# ✅ GOOD - wait for body element
web_browse_agent --session s open https://example.com
web_browse_agent --session s wait-for "body"
web_browse_agent --session s eval "document.body.innerText"

# ✅ BETTER - wait for specific content element
web_browse_agent --session s open https://example.com
web_browse_agent --session s wait-for "#main-content"
web_browse_agent --session s eval "document.querySelector('#main-content').innerText"

# ✅ BEST - wait for navigation completion
web_browse_agent --session s open https://example.com
web_browse_agent --session s wait-for --wait-type navigation
web_browse_agent --session s eval "document.body.innerText"
```

---

## Best Practices & Lessons Learned

### 1. Always Use Safe Selectors

```bash
# Selector Safety Hierarchy (safest to least safe)

# Level 1: Always exists
document.body.innerText
document.documentElement.outerHTML
document.title

# Level 2: Safe with optional chaining
document.querySelector('.container')?.innerText
document.querySelector('#main')?.textContent || 'Not found'

# Level 3: Requires existence check
if (document.querySelector('.item')) {
  document.querySelector('.item').innerText
}

# Level 4: Dangerous - will throw if element doesn't exist
document.querySelector('.container').innerText  // ❌ DON'T USE
```

---

### 2. Verify Session Health

```bash
# Check session before critical operations
if web_browse_agent --session s ping &>/dev/null; then
    web_browse_agent --session s eval "document.body.innerText"
else
    echo "Session unhealthy, restarting..."
    web_browse_agent --session s end-session
    web_browse_agent --session s open https://example.com
fi
```

---

### 3. Wait Properly After Navigation

```bash
# Pattern 1: Wait for specific content (RECOMMENDED)
web_browse_agent --session s open https://example.com
web_browse_agent --session s wait-for "#content-loaded"  # Wait for actual content
web_browse_agent --session s eval "document.querySelector('#content-loaded').innerText"

# Pattern 2: Wait for body (basic)
web_browse_agent --session s open https://example.com
web_browse_agent --session s wait-for "body"
web_browse_agent --session s eval "document.body.innerText"

# Pattern 3: Wait for navigation event (comprehensive)
web_browse_agent --session s open https://example.com
web_browse_agent --session s wait-for --wait-type navigation
web_browse_agent --session s eval "document.body.innerText"
```

---

### 4. Handle Errors Gracefully in Scripts

```bash
#!/bin/bash
# Robust web scraping script

SESSION="scraper"

# Function to clean up on exit
cleanup() {
    web_browse_agent --session "$SESSION" end-session 2>/dev/null
}
trap cleanup EXIT

# Navigate with error handling
if ! web_browse_agent --session "$SESSION" open https://example.com; then
    echo "Failed to open URL"
    exit 1
fi

# Wait for content
if ! web_browse_agent --session "$SESSION" wait-for "body"; then
    echo "Content didn't load in time"
    exit 1
fi

# Extract with safe selector
CONTENT=$(web_browse_agent --session "$SESSION" --json eval \
    "document.body?.innerText || 'No content found'" 2>&1)

if [ $? -ne 0 ]; then
    echo "Evaluation failed: $CONTENT"
    exit 1
fi

echo "$CONTENT" | jq -r '.value'
```

---

### 5. Test Selectors Before Complex Operations

```bash
# Step 1: Test if element exists
EXISTS=$(web_browse_agent --session s --json eval \
    "!!document.querySelector('.target-class')")

if [ "$(echo "$EXISTS" | jq -r '.value')" = "true" ]; then
    # Step 2: Extract content safely
    web_browse_agent --session s eval "document.querySelector('.target-class').innerText"
else
    echo "Element .target-class not found on page"
fi
```

---

### 6. Use Consistent Workflow Pattern

This pattern avoids most common errors:

```bash
#!/bin/bash
# Reliable web content extraction pattern

SESSION="docs"
URL="https://example.com/documentation"

# 1. Start session and navigate
web_browse_agent --session "$SESSION" open "$URL"

# 2. Wait for content (use specific selector when possible)
web_browse_agent --session "$SESSION" wait-for "body"

# 3. Extract with safest selector possible
CONTENT=$(web_browse_agent --session "$SESSION" --json eval \
    "document.body.innerText.substring(0, 8000)")

# 4. Parse JSON output
echo "$CONTENT" | jq -r '.value'

# 5. Clean up when done
web_browse_agent --session "$SESSION" end-session
```

---

### 7. Debug with Verbose Output

```bash
# Add --verbose flag to see what's happening
web_browse_agent --session debug --verbose open https://example.com
web_browse_agent --session debug --verbose wait-for "body"
web_browse_agent --session debug --verbose eval "document.title"
```

---

### 8. Common Pitfall: Session Timeout

Sessions have a default 5-minute idle timeout. For long-running operations:

```bash
# Set longer timeout
export WEB_AGENT_IDLE_TIMEOUT=1800  # 30 minutes

# Or keep session alive with periodic ping
while true; do
    web_browse_agent --session long-lived ping
    sleep 240  # Ping every 4 minutes
done &
PING_PID=$!

# Your long operations here...

# Stop keepalive
kill $PING_PID
```

---

## Tips

1. **Use JSON output** for programmatic parsing: `--json`
2. **Reuse sessions** to maintain state across multiple operations
3. **Use `wait-for`** after navigation to ensure content is loaded
4. **Use `eval`** for complex data extraction that simple commands can't handle
5. **Set viewport** before screenshots for consistent dimensions
6. **Set DISPLAY** when using `--headless=false` to see the browser window
7. **Always use safe selectors** - prefer `document.body` or optional chaining (`?.`)
8. **Check session health** with `ping` before critical operations
9. **Clean up sessions** with `end-session` when encountering corruption errors
10. **Test selectors first** with `!!document.querySelector()` before extraction
