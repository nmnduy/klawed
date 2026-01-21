# Web Browse Agent Output Capture - Debug Notes

## Summary

Investigated the issue where web_browse_agent's stdout/stderr output was not visible in the TUI during interactive mode.

## Current Implementation (After Commits 69eea04 and 85bb91d)

### Output Capture (✓ Working)
1. The shell command includes `2>&1` redirection to merge stderr into stdout
2. `popen()` captures the merged output stream
3. Output is read via `fread()` and stored in a buffer
4. ANSI escape sequences are stripped to prevent terminal corruption

### TUI Display (✓ Implemented, ? Status Unknown)
1. `tool_emit_line()` is called to display output line-by-line
2. Messages are posted to the TUI message queue (`g_active_tool_queue`)
3. The TUI's message handler (`TUI_MSG_ADD_LINE`) processes and displays them
4. Output is limited to 50 lines with a truncation indicator

## Enhancements Added (This Session)

### Diagnostic Logging
Added `LOG_DEBUG` and `LOG_INFO` calls to trace:
- Whether `g_active_tool_queue` is set (NULL check)
- Output length and exit code
- Each line being emitted to TUI
- Truncation events

### Visual Improvements
- Added `[web_browse_agent] Output:` header before displaying captured output
- Clear visual markers for errors and exit codes

## How to Verify the Fix

### 1. Check Logs
Run klawed with debug logging enabled:
```bash
export KLAWED_LOG_LEVEL=DEBUG
export KLAWED_LOG_PATH=.klawed/logs/debug.log
./build/klawed
```

In the log, look for:
```
web_browse_agent: command=<command>, g_active_tool_queue=<address>
web_browse_agent: received output length=<n>, exit_code=<code>
web_browse_agent: emitting output to TUI (g_active_tool_queue=<address>)
web_browse_agent: emitting line 1: <content>
...
```

### 2. What to Look For

**If `g_active_tool_queue` is NULL:**
- This means the tool is not running in the correct thread context
- Output won't be displayed in TUI (but will be in tool result JSON)
- Check `src/interactive/response_processor.c` - tool thread should set this

**If `g_active_tool_queue` is set but output isn't visible:**
- Check if messages are being posted successfully (LOG_WARN for failures)
- Check TUI message queue capacity (might be full)
- Check if TUI is processing messages from the queue
- Check auto-scroll behavior - output might be off-screen

**If output length is 0:**
- The web_browse_agent itself isn't producing output
- Check if the agent binary exists and is executable
- Check timeout settings (WEB_AGENT_TIMEOUT = 120 seconds)

### 3. Test Case

Create a simple test to verify stderr capture:
```bash
# In explore mode
KLAWED_EXPLORE_MODE=1 ./build/klawed

# Try a web_browse_agent command that generates both stdout and stderr
# The tool should display both in the TUI conversation window
```

## Code Flow

```
tool_web_browse_agent()
  ├─> tool_emit_line("", "[web_browse_agent] session=...")  // Command info
  │
  ├─> execute_web_agent_session()
  │     ├─> Build command with "2>&1" redirection
  │     ├─> popen() + fread() to capture merged stdout+stderr
  │     └─> strip_ansi_escapes() for TUI safety
  │
  ├─> tool_emit_line("", "[web_browse_agent] Output:")  // Header
  │
  ├─> For each line in output:
  │     └─> tool_emit_line(" ", line)  // Indented output
  │
  └─> tool_emit_line("", "[web_browse_agent] exit_code=N")  // If non-zero
```

## Known Constraints

1. **Parent process stderr redirection**: The parent's stderr is redirected to `/dev/null` when `g_active_tool_queue` is set. This is to prevent the parent process from corrupting the TUI, NOT to prevent capturing subprocess stderr.

2. **Subprocess stderr capture**: The `2>&1` in the command string happens INSIDE the shell spawned by `popen()`, so it correctly captures the subprocess's stderr regardless of parent redirection.

3. **Buffer size**: Output is limited to MAX_WEB_OUTPUT (100KB)

4. **Line limit**: TUI display is limited to 50 lines (configurable)

## Next Steps

1. Run with debug logging to identify the issue
2. If `g_active_tool_queue` is NULL, investigate thread context
3. If output is empty, check web_browse_agent execution
4. If output is captured but not visible, check TUI message processing
