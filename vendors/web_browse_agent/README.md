# Web Browse Agent

An AI-powered agent for web browsing and UI testing, written in Go. This agent uses an agentic loop pattern to accomplish complex tasks through recursive tool calls, combining browser automation with LLM reasoning.

## Features

- **Agentic Loop Pattern** - Recursive tool execution with automatic retry and error handling (up to 50 iterations)
- **Browser Automation** - Full Playwright integration for web interaction
- **Multi-LLM Support** - Works with OpenAI GPT-4 and Anthropic Claude models
- **Core Tools** - File read/write operations and bash command execution
- **18 Browser Tools** - Comprehensive web automation capabilities
- **Flexible Selectors** - Multiple selector strategies for element targeting
- **Tab Management** - Full multi-tab browser session support
- **Screenshot Capture** - Visual documentation of browser state

## Requirements

- **Go 1.22+**
- **Playwright** - Installed automatically on first run
- **API Key** - One of the following:
  - OpenAI API key, or
  - Anthropic API key

## Installation

```bash
# Clone the repository
git clone https://github.com/puter/web-browse-agent.git
cd web-browse-agent

# Build the agent
go build -o web-browse-agent ./cmd/agent

# Install Playwright browsers (first time only)
go run github.com/playwright-community/playwright-go/cmd/playwright@latest install
```

## Usage

### Command Line Mode

Execute a single task:

```bash
# Using default provider (OpenAI)
./web-browse-agent "go to google.com and search for playwright"

# With verbose output
./web-browse-agent -v "navigate to github.com and find the trending repositories"
```

### Interactive Mode

Start an interactive session for multiple tasks:

```bash
./web-browse-agent --interactive

# Or with shorthand
./web-browse-agent -i
```

In interactive mode:
- Type your commands at the `>` prompt
- Use `reset` to clear conversation history
- Use `exit` or `quit` to exit

### With Different LLM Providers

```bash
# Using OpenAI (default)
export OPENAI_API_KEY="your-api-key"
./web-browse-agent "test the login form"

# Using Anthropic Claude
export ANTHROPIC_API_KEY="your-api-key"
./web-browse-agent --provider anthropic "test the login form"

# Or set provider via environment variable
export LLM_PROVIDER=anthropic
./web-browse-agent "test the login form"
```

### With Headless Browser

```bash
# Run without visible browser window
./web-browse-agent --headless "take a screenshot of example.com"

# Or via environment variable
export BROWSER_HEADLESS=true
./web-browse-agent "take a screenshot of example.com"
```

### Without Browser Tools

```bash
# Run with only file and bash tools (no browser)
./web-browse-agent --no-browser "list files in the current directory"
```

## Environment Variables

### LLM Configuration

| Variable | Description | Default |
|----------|-------------|---------|
| `LLM_PROVIDER` | LLM provider to use (`openai` or `anthropic`) | `openai` |
| `OPENAI_API_KEY` | OpenAI API key (required for OpenAI) | - |
| `OPENAI_MODEL` | OpenAI model to use | `gpt-4o` |
| `ANTHROPIC_API_KEY` | Anthropic API key (required for Anthropic) | - |
| `ANTHROPIC_MODEL` | Anthropic model to use | `claude-sonnet-4-20250514` |

### Browser Configuration

| Variable | Description | Default |
|----------|-------------|---------|
| `BROWSER_TYPE` | Browser engine (`CHROMIUM`, `FIREFOX`, `WEBKIT`) | `CHROMIUM` |
| `BROWSER_HEADLESS` | Run browser without UI (`true`/`false`) | `false` |
| `BROWSER_VIEWPORT_WIDTH` | Browser viewport width in pixels | `1280` |
| `BROWSER_VIEWPORT_HEIGHT` | Browser viewport height in pixels | `720` |
| `BROWSER_ACTION_TIMEOUT` | Timeout for actions in milliseconds | `5000` |
| `BROWSER_NAVIGATION_TIMEOUT` | Timeout for page navigation in milliseconds | `30000` |
| `BROWSER_OUTPUT_DIR` | Directory for screenshots and outputs | `.` (current) |
| `BROWSER_USER_DATA_DIR` | Path to user data directory for persistent sessions | - |

## Available Tools

### Core Tools

| Tool | Description |
|------|-------------|
| `read_file` | Read contents of a file from the filesystem |
| `write_file` | Write content to a file on the filesystem |
| `bash` | Execute shell commands and return output |

### Browser Navigation

| Tool | Description |
|------|-------------|
| `browser_navigate` | Navigate to a URL in the browser |
| `browser_navigate_back` | Go back to the previous page in browser history |

### Browser Interaction

| Tool | Description |
|------|-------------|
| `browser_click` | Click on an element in the page |
| `browser_fill` | Fill an input field with text (clears first) |
| `browser_type` | Type text character by character (simulates keystrokes) |
| `browser_press_key` | Press a key or key combination (e.g., Enter, Tab, Ctrl+c) |
| `browser_hover` | Hover over an element to trigger hover states |
| `browser_select_option` | Select an option from a dropdown by value, label, or index |

### Page Inspection

| Tool | Description |
|------|-------------|
| `browser_snapshot` | Get accessibility tree snapshot of the current page |
| `browser_screenshot` | Take a screenshot (full page, viewport, or element) |
| `browser_evaluate` | Execute JavaScript code in the browser context |

### Tab Management

| Tool | Description |
|------|-------------|
| `browser_tabs_list` | List all open browser tabs with IDs, URLs, and titles |
| `browser_tab_new` | Open a new browser tab, optionally navigating to a URL |
| `browser_tab_select` | Switch to a specific browser tab by ID |
| `browser_tab_close` | Close a browser tab by ID |

### Wait Operations

| Tool | Description |
|------|-------------|
| `browser_wait` | Wait for element state (visible, hidden, attached, detached) or time |

## Element Selector Formats

The browser tools support multiple selector strategies for targeting elements:

| Format | Example | Description |
|--------|---------|-------------|
| CSS Selector | `div.class`, `#id`, `button[type="submit"]` | Standard CSS selectors (default) |
| `testid:xxx` | `testid:login-button` | Select by `data-testid` attribute |
| `role:xxx` | `role:button` | Select by ARIA role |
| `role:xxx:text` | `role:button:Submit` | Select by ARIA role with accessible name |
| `text:xxx` | `text:Click me` | Select by text content |
| `label:xxx` | `label:Email` | Select by associated form label |
| `placeholder:xxx` | `placeholder:Enter email` | Select by input placeholder text |

### Selector Examples

```bash
# CSS selectors
./web-browse-agent "click on the element with selector '#submit-btn'"
./web-browse-agent "fill the input '.email-field' with 'test@example.com'"

# Using data-testid
./web-browse-agent "click on 'testid:login-button'"

# Using ARIA roles
./web-browse-agent "click on 'role:button:Sign In'"

# Using text content
./web-browse-agent "click on 'text:Learn More'"

# Using form labels
./web-browse-agent "fill 'label:Password' with 'secret123'"
```

## Project Structure

```
web-browse-agent/
├── cmd/
│   └── agent/
│       └── main.go           # CLI entry point with cobra commands
├── internal/
│   ├── agent/
│   │   └── agent.go          # Agentic loop implementation
│   ├── browser/
│   │   ├── config.go         # Browser configuration from environment
│   │   ├── context.go        # Browser context and tab management
│   │   ├── registry.go       # Browser tool registration
│   │   └── tools.go          # 16 browser tool implementations
│   ├── llm/
│   │   ├── client.go         # LLM client interface
│   │   ├── factory.go        # Client factory for provider selection
│   │   ├── openai.go         # OpenAI API implementation
│   │   └── anthropic.go      # Anthropic Claude API implementation
│   └── tool/
│       ├── tool.go           # Tool interface definition
│       ├── registry.go       # Tool registry for management
│       ├── read_file.go      # File read tool
│       ├── write_file.go     # File write tool
│       └── bash.go           # Bash execution tool
├── pkg/                      # Public packages (if any)
├── go.mod                    # Go module definition
├── go.sum                    # Dependency checksums
└── README.md                 # This file
```

## Architecture

### Agentic Loop

The agent follows a recursive loop pattern:

1. **User Input** → System receives task description
2. **LLM Call** → Send conversation history and available tools to LLM
3. **Response Processing** → Check for tool calls or completion
4. **Tool Execution** → Execute requested tools and capture results
5. **Result Integration** → Add tool results to conversation
6. **Iterate** → Return to step 2 until task complete or max iterations

### Tool Interface

All tools implement the `Tool` interface:

```go
type Tool interface {
    Name() string
    Description() string
    ParametersSchema() map[string]interface{}
    Execute(params map[string]interface{}) (string, error)
}
```

### LLM Client Interface

LLM providers implement the `Client` interface:

```go
type Client interface {
    Chat(messages []Message, tools []ToolDefinition) (*Response, error)
    GetModel() string
}
```

## Examples

### Web Scraping

```bash
./web-browse-agent "go to news.ycombinator.com and get the titles of the top 5 stories"
```

### Form Testing

```bash
./web-browse-agent "navigate to the login page, fill in test credentials, and verify the error message"
```

### Screenshot Documentation

```bash
./web-browse-agent --headless "take full page screenshots of example.com and save them"
```

### Multi-Tab Operations

```bash
./web-browse-agent -i
> open google.com in a new tab
> open github.com in another tab
> list all open tabs
> switch to the first tab
```

## Troubleshooting

### Playwright Not Found

```bash
# Install Playwright browsers
go run github.com/playwright-community/playwright-go/cmd/playwright@latest install
```

### API Key Errors

Ensure your API key is properly exported:

```bash
export OPENAI_API_KEY="sk-..."
# or
export ANTHROPIC_API_KEY="sk-ant-..."
```

### Browser Timeout Issues

Increase timeout values:

```bash
export BROWSER_ACTION_TIMEOUT=10000
export BROWSER_NAVIGATION_TIMEOUT=60000
```

### Headless Mode Issues

Some sites may behave differently in headless mode. Try with visible browser:

```bash
export BROWSER_HEADLESS=false
```

## License

MIT License

## Credits

This project is a Go port inspired by Java-based AI agents for UI testing. It leverages:

- [Playwright for Go](https://github.com/playwright-community/playwright-go) - Browser automation
- [Cobra](https://github.com/spf13/cobra) - CLI framework
- OpenAI and Anthropic APIs for LLM capabilities
