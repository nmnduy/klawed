# Explore Subagent

The Explore subagent is a specialized klawed mode optimized for web research and documentation lookup.

## Overview

When `KLAWED_EXPLORE_MODE=1` is set, klawed operates in Explore mode with:
- **Web Search**: DuckDuckGo search via web_browse_agent (requires building the Go binary)
- **Web Browsing**: Playwright-based page reading and navigation (requires building the Go binary)
- **Documentation Lookup**: Context7 integration for library documentation (no binary needed)
- **Local File Access**: Standard Glob, Grep, Read tools

**Note:** The `web_search` and `web_read` tools require the `web_browse_agent` Go binary to be built. If the binary is not found, only the Context7 tools (`context7_search`, `context7_docs`) and standard file tools will be available.

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `KLAWED_EXPLORE_MODE` | `0` | Enable Explore subagent mode |
| `KLAWED_EXPLORE_HEADLESS` | `1` | Run browser in headless mode |
| `KLAWED_WEB_BROWSE_AGENT_PATH` | `vendors/web_browse_agent/web_browse_agent` | Path to web_browse_agent binary |
| `CONTEXT7_API_KEY` | - | API key for Context7 (optional, for higher rate limits) |

## Tools Available in Explore Mode

### Web Research Tools (require web_browse_agent binary)
- `web_search` - Search the web using DuckDuckGo, returns structured results
- `web_read` - Navigate to URL and extract page content with citation

These tools are only registered if the `web_browse_agent` binary is found at runtime.

### Documentation Tools (Context7 API - no binary needed)
- `context7_search` - Search for library documentation
- `context7_docs` - Fetch documentation for a specific library

These tools use the Context7 HTTP API directly and are always available in explore mode.

### Standard Tools (always available)
- `Glob` - Find files by pattern
- `Grep` - Search file contents
- `Read` - Read file contents
- `Bash` - Execute shell commands

## Usage

### Starting an Explore Subagent

From the main klawed instance:
```json
{
  "tool": "Subagent",
  "parameters": {
    "prompt": "Research how to implement JWT authentication in Go"
  }
}
```

With environment variables set:
```bash
KLAWED_SUBAGENT_ENV_VARS="KLAWED_EXPLORE_MODE=1" klawed
```

Or run directly in explore mode:
```bash
KLAWED_EXPLORE_MODE=1 klawed "Research how to implement JWT authentication in Go"
```

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Main klawed                             │
│                                                              │
│  ┌─────────────────────────────────────────────────────────┐│
│  │                    Subagent Tool                        ││
│  │  KLAWED_SUBAGENT_ENV_VARS="KLAWED_EXPLORE_MODE=1"      ││
│  └─────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                   Explore Subagent                           │
│                   (KLAWED_EXPLORE_MODE=1)                   │
│                                                              │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────────────┐│
│  │ web_search   │ │ context7_*   │ │ Glob/Grep/Read       ││
│  │ web_read     │ │              │ │                      ││
│  └──────────────┘ └──────────────┘ └──────────────────────┘│
│         │                │                                  │
│         ▼                ▼                                  │
│  ┌──────────────┐ ┌──────────────┐                         │
│  │web_browse_   │ │ HTTP Client  │                         │
│  │agent binary  │ │ (Context7)   │                         │
│  └──────────────┘ └──────────────┘                         │
└─────────────────────────────────────────────────────────────┘
```

## Building

### Build klawed with Explore support

```bash
cd klawed
make
```

### Build web_browse_agent

```bash
cd vendors/web_browse_agent
go build -o web_browse_agent ./cmd/agent
```

### Install Playwright browsers (first time only)

```bash
go run github.com/playwright-community/playwright-go/cmd/playwright@latest install
```

## Best Practices

### Context Management
- Browser output can be verbose; prefer `web_read` for extracting main content
- Limit search results to 10-15 items
- Use Context7 for library-specific documentation queries

### Source Quality
- Prefer official documentation over blog posts
- Use `context7_docs` for up-to-date API documentation
- Cross-reference multiple sources for accuracy

### Error Handling
- If DuckDuckGo shows CAPTCHA, try different search terms
- If Context7 rate-limits, wait and retry or provide API key
- Check web_browse_agent logs for browser issues

## Implementation Files

- `src/explore_tools.c` - C implementation of explore tools
- `src/explore_tools.h` - Header file
- `vendors/web_browse_agent/` - Go web browser agent
- `vendors/context7/` - Context7 MCP server (reference)
