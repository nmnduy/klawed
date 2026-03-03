# Klawed Browser Controller

A Chrome/Chromium extension + Go native messaging host that lets the Klawed AI agent control a real browser that's already open on your machine.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│  klawed (C agent)                                               │
│                                                                 │
│  uses BrowserControl dynamic tool                               │
│  (loads from klawed_dynamic_tools.json)                         │
└───────────────────────┬─────────────────────────────────────────┘
                        │  JSON over Unix socket
                        │  /tmp/klawed-browser.sock
                        ▼
┌─────────────────────────────────────────────────────────────────┐
│  Go Native Messaging Host  (host/main.go)                       │
│                                                                 │
│  • Listens on Unix socket for klawed commands                   │
│  • Speaks Chrome Native Messaging Protocol on stdin/stdout      │
│  • Bridges: klawed ↔ Chrome extension (request/response)        │
└───────────────────────┬─────────────────────────────────────────┘
                        │  4-byte LE length-prefixed JSON
                        │  (Chrome Native Messaging Protocol)
                        ▼
┌─────────────────────────────────────────────────────────────────┐
│  Chrome Extension  (extension/)                                 │
│                                                                 │
│  background.js  — service worker, command router                │
│  content.js     — injected into pages for DOM access            │
│  popup.html/js  — status UI, quick actions                      │
└───────────────────────┬─────────────────────────────────────────┘
                        │  Chrome APIs
                        │  (tabs, scripting, captureVisibleTab…)
                        ▼
┌─────────────────────────────────────────────────────────────────┐
│  Real Chrome / Chromium browser                                 │
│  (the browser you already have open)                            │
└─────────────────────────────────────────────────────────────────┘
```

### Why Go?

The native messaging host is written in Go (stdlib only, no dependencies) because:
- Go produces a single statically-linkable binary — no runtime deps
- Go's concurrency model (goroutines + channels) maps naturally to the async message routing
- The host needs to run two concurrent I/O loops (Chrome stdin/stdout + Unix socket accept loop) with a shared pending-request map
- Go handles the binary Native Messaging framing cleanly with `encoding/binary`

### Message Flow

1. klawed calls `BrowserControl` tool with `{"command": "navigate", "params": {"url": "https://..."}}`
2. `browser_ctl.py` connects to the Unix socket and sends the JSON + newline
3. Go host receives the line, assigns a unique ID, forwards to Chrome as `{"id": "abc123", "command": "navigate", "params": {...}}`
4. Chrome extension's `background.js` receives the message, executes the Chrome API, sends back `{"id": "abc123", "result": {"success": true}}`
5. Go host's `chromeReader` goroutine receives the response, looks up `pending["abc123"]`, sends to the waiting channel
6. Go host writes the JSON response + newline back to the klawed socket connection
7. `browser_ctl.py` reads the response line and prints it

## Requirements

- Chrome or Chromium browser
- Go 1.21+ (`go build` — no external dependencies)
- Python 3.6+ (for `browser_ctl.py` client script)

## Installation

### Step 1: Build the Go host

```bash
cd chrome_extension_native_messaging/host
make build
```

This produces `klawed_browser_controller` binary.

### Step 2: Load the extension in Chrome

1. Open Chrome → `chrome://extensions/`
2. Enable **Developer mode** (toggle, top-right)
3. Click **Load unpacked**
4. Select the `chrome_extension_native_messaging/extension/` directory
5. Note the **Extension ID** shown (32 lowercase letters, e.g. `abcdefghijklmnopqrstuvwxyzabcdef`)

### Step 3: Install the native host

```bash
cd chrome_extension_native_messaging/host
make install EXTENSION_ID=abcdefghijklmnopqrstuvwxyzabcdef
```

This:
- Installs the binary to `~/.local/bin/klawed_browser_controller`
- Writes the native messaging manifest to `~/.config/google-chrome/NativeMessagingHosts/`
- Also installs for Chromium if it exists

### Step 4: Connect the extension

Click the 🤖 extension icon in the Chrome toolbar and click **Connect**. The dot should turn green.

### Step 5: Configure klawed

Copy or link the dynamic tools config, and optionally set the socket path:

```bash
# Point klawed to the browser tools
export KLAWED_DYNAMIC_TOOLS=/path/to/klawed/chrome_extension_native_messaging/klawed_dynamic_tools.json

# The socket path (default: /tmp/klawed-browser.sock)
# export KLAWED_BROWSER_SOCKET=/tmp/klawed-browser.sock

# Point browser_ctl.py to the right socket (if customized)
# export KLAWED_BROWSER_SOCKET=/tmp/my-browser.sock
```

Or copy to the project directory:

```bash
cp chrome_extension_native_messaging/klawed_dynamic_tools.json .klawed/dynamic_tools.json
```

## Commands

### Navigation
| Command | Params | Description |
|---------|--------|-------------|
| `navigate` | `{"url": "https://..."}` | Navigate active tab to URL |
| `navigateTab` | `{"tabId": N, "url": "..."}` | Navigate specific tab |
| `goBack` | — | History back |
| `goForward` | — | History forward |
| `reload` | — | Reload active tab |
| `newTab` | `{"url": "..."}` | Open new tab (optional URL) |
| `closeTab` | `{"tabId": N}` | Close a tab |
| `switchTab` | `{"tabId": N}` | Focus a tab |

### Tab Info
| Command | Params | Description |
|---------|--------|-------------|
| `listTabs` | — | All open tabs with id/url/title/active |
| `getActiveTab` | — | Active tab info |

### Page Content
| Command | Params | Description |
|---------|--------|-------------|
| `getPageInfo` | — | URL, title, scroll position, dimensions |
| `getPageSource` | — | Full HTML of active page |
| `getReadableText` | — | Cleaned readable text (scripts/styles removed) |

### DOM Interaction
| Command | Params | Description |
|---------|--------|-------------|
| `click` | `{"selector": "CSS"}` | Click element |
| `type` | `{"selector": "CSS", "text": "...", "clearFirst": true}` | Type into input |
| `getText` | `{"selector": "CSS"}` | Get innerText (omit selector for body) |
| `getHtml` | `{"selector": "CSS"}` | Get innerHTML |
| `getAttribute` | `{"selector": "CSS", "attribute": "href"}` | Get element attribute |
| `findElements` | `{"selector": "CSS"}` | List matching elements with metadata |
| `getLinks` | — | All `<a>` links on page |
| `getForms` | — | All forms with inputs |
| `fillForm` | `{"data": {"#sel": "val"}}` | Fill multiple form fields |
| `submitForm` | `{"selector": "form"}` | Submit a form |
| `pressKey` | `{"selector": "CSS", "key": "Enter"}` | Dispatch keyboard event |

### Scrolling
| Command | Params | Description |
|---------|--------|-------------|
| `scroll` | `{"x": 0, "y": 500}` | Scroll to absolute position |
| `scrollBy` | `{"dx": 0, "dy": 300}` | Scroll relative |
| `scrollToElement` | `{"selector": "CSS"}` | Scroll element into view |

### JavaScript
| Command | Params | Description |
|---------|--------|-------------|
| `evaluate` | `{"code": "document.title"}` | Execute JS and return result |
| `waitForElement` | `{"selector": "CSS", "timeout": 5000}` | Wait for element to appear |

### Capture
| Command | Params | Description |
|---------|--------|-------------|
| `screenshot` | — | Capture visible area as PNG data URL |

### Debug
| Command | Description |
|---------|-------------|
| `ping` | Connectivity check |
| `getInfo` | Host metadata + command list |

## Usage from klawed

Once configured, klawed can use the `BrowserControl` tool naturally:

```
navigate to https://github.com and find the trending repositories
```

```
go to https://google.com and search for "Go programming language"
```

```
take a screenshot of the current page
```

```
list all open tabs
```

## Manual Testing

Test the socket directly:

```bash
# Ping
echo '{"command":"ping"}' | python3 chrome_extension_native_messaging/host/browser_ctl.py '{"command":"ping"}'

# Navigate
python3 chrome_extension_native_messaging/host/browser_ctl.py '{"command":"navigate","params":{"url":"https://example.com"}}'

# Get page text
python3 chrome_extension_native_messaging/host/browser_ctl.py '{"command":"getReadableText"}'

# List tabs
python3 chrome_extension_native_messaging/host/browser_ctl.py '{"command":"listTabs"}'

# Screenshot
python3 chrome_extension_native_messaging/host/browser_ctl.py '{"command":"screenshot"}'
```

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `KLAWED_BROWSER_SOCKET` | `/tmp/klawed-browser.sock` | Unix socket path |
| `KLAWED_BROWSER_LOG` | `/tmp/klawed-browser-host.log` | Go host log file |

## Troubleshooting

### "Socket not found"
The Chrome extension is not connected. Click the 🤖 icon in Chrome and click **Connect**.

### "Failed to connect to native host" (in Chrome)
1. Verify the binary exists: `ls ~/.local/bin/klawed_browser_controller`
2. Check the manifest: `cat ~/.config/google-chrome/NativeMessagingHosts/com.klawed.browser_controller.json`
3. Verify the Extension ID in the manifest matches your extension
4. Check the log: `tail -f /tmp/klawed-browser-host.log`

### Extension ID mismatch
After reinstalling the extension, the ID changes. Re-run:
```bash
make install EXTENSION_ID=<new-id>
```
Then reload Chrome.

### Timeout errors
The Go host waits 30 seconds for a Chrome response. If you see timeouts:
- Check the Chrome extension is still connected (green dot)
- Check `tail -f /tmp/klawed-browser-host.log` for errors

## Security Notes

- The native host only accepts connections from Chrome extensions whose ID is listed in the manifest
- The Unix socket is created in `/tmp` with default permissions; set a custom path if you need tighter control
- `evaluate` executes arbitrary JavaScript in the active page — use with care
