# web_browse_agent tool

Direct access to the Playwright-based `web_browse_agent` binary without enabling Explore mode. Use it when you need fine-grained browser control or when Explore mode is off, as long as the binary is available.

## Availability
- Tool name: `web_browse_agent`
- Registered in the default tool list whenever the binary path is set and executable.
- Does **not** require `KLAWED_EXPLORE_MODE=1`.

## Prerequisites
- Build or provide the binary:
  - Default path: `vendors/web_browse_agent/web_browse_agent`
  - Or set `KLAWED_WEB_BROWSE_AGENT_PATH=/absolute/path/to/web_browse_agent`
- Optional: ensure Playwright dependencies are installed for the binary (see vendor docs).

## Headless control
- Headless is **on by default**.
- Overrides:
  - `KLAWED_WEB_BROWSE_AGENT_HEADLESS=0` (or `false`/`no`) to run with a visible browser
  - Falls back to `KLAWED_EXPLORE_HEADLESS` if the dedicated var is unset

## Parameters
- `args` (string, optional): Raw arguments passed directly to the binary, e.g.
  - `"browser_navigate https://example.com"`
  - `"browser_navigate https://example.com && get_page_content"`
- `prompt` (string, optional): High-level prompt; when provided, the tool wraps it using the built-in prompt runner.
- You must provide **either** `prompt` **or** `args`. If both are provided, raw `args` are used (prompt is ignored).

## Behavior
- Timeout: 120s enforced via `timeout` wrapper in the tool.
- Headless flag is appended automatically based on the env vars above.
- Output: Captured stdout/stderr up to 100KB; returned as `output` with `exit_code`.

## Logging
- Uses the standard Klawed logger. Logs go to `.klawed/logs/klawed.log` by default (configurable via `KLAWED_LOG_PATH`; level via `KLAWED_LOG_LEVEL`).

## Examples

### Prompt mode (wrapped)
```
{
  "function": "web_browse_agent",
  "arguments": {
    "prompt": "Open https://example.com and extract the main body text."
  }
}
```

### Raw args mode
```
{
  "function": "web_browse_agent",
  "arguments": {
    "args": "browser_navigate https://example.com && get_page_content"
  }
}
```

### Non-headless run
```
KLAWED_WEB_BROWSE_AGENT_HEADLESS=0 ./build/klawed "..."
```
