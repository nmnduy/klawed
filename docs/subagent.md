# Subagent Tool

## Overview

The Subagent tool allows claude-c to spawn a new instance of itself with a fresh context to work on delegated tasks. This is useful for:

1. **Context management** - Start with a clean slate without conversation history
2. **Task delegation** - Offload complex independent tasks to a separate agent
3. **Avoiding context limits** - Split large tasks across multiple contexts
4. **Parallel thinking** - Each subagent can explore different approaches

## Usage

### Starting a Subagent

```json
{
  "prompt": "Your task description here",
  "timeout": 300
}
```

**Parameters:**
- `prompt` (required, string) - The task description for the subagent
- `timeout` (optional, integer) - Timeout in seconds (default: 300, 0 = no timeout)

### Monitoring Subagent Progress

```json
{
  "pid": 12345,
  "log_file": "/path/to/log/file.log",
  "tail_lines": 50
}
```

**Parameters:**
- `pid` (optional, integer) - Process ID of the subagent (from Subagent tool response)
- `log_file` (optional, string) - Log file path (from Subagent tool response)
- `tail_lines` (optional, integer) - Number of lines to read from end of log (default: 50)

### Interrupting a Subagent

```json
{
  "pid": 12345
}
```

**Parameters:**
- `pid` (required, integer) - Process ID of the subagent to interrupt

## How It Works

1. **Execution**: Spawns a new claude-c process with the given prompt (non-blocking)
2. **Logging**: All stdout and stderr output is written to a timestamped log file in `.claude-c/subagent/`
3. **Return value**: Returns immediately with PID and log file path
4. **Monitoring**: Use `CheckSubagentProgress` to monitor progress by reading the log
5. **Interruption**: Use `InterruptSubagent` to stop a stuck subagent

## Output Structure

### Subagent Tool Response
```json
{
  "pid": 12345,
  "log_file": "/path/to/.claude-c/subagent/subagent_20231208_123456_1234.log",
  "timeout_seconds": 300,
  "message": "Subagent started with PID 12345. Log file: /path/to/log... Use 'CheckSubagentProgress' tool to monitor progress or 'InterruptSubagent' to stop it."
}
```

### CheckSubagentProgress Tool Response
```json
{
  "pid": 12345,
  "is_running": true,
  "log_file": "/path/to/.claude-c/subagent/subagent_20231208_123456_1234.log",
  "total_lines": 150,
  "tail_lines_returned": 50,
  "tail_output": "Last 50 lines of subagent output...",
  "summary": "Subagent with PID 12345 is still running. Log file: /path/to/log",
  "truncation_warning": "Optional warning if log was truncated"
}
```

### InterruptSubagent Tool Response
```json
{
  "pid": 12345,
  "killed": true,
  "message": "Sent SIGTERM to subagent with PID 12345. Process terminated gracefully."
}
```

## Monitoring Patterns

### Basic Monitoring Loop

When delegating a task to a subagent, the orchestrator should follow this pattern:

1. **Start subagent** - Get PID and log file
2. **Periodically check progress** - Use `CheckSubagentProgress` every 30-60 seconds
3. **Analyze log tail** - Look for signs of progress or stuckness
4. **Interrupt if stuck** - If no progress for several checks, use `InterruptSubagent`
5. **Restart with guidance** - Start a new subagent with specific guidance to overcome blocker

### Example Orchestrator Workflow

```bash
# 1. Start subagent
Subagent: "Analyze all Python files and create a summary report"
→ Returns: { "pid": 12345, "log_file": "/path/to/log" }

# 2. Wait and check progress
Sleep: 30 seconds
CheckSubagentProgress: { "pid": 12345, "log_file": "/path/to/log", "tail_lines": 20 }
→ Returns: { "is_running": true, "tail_output": "Found 10 Python files...", "total_lines": 50 }

# 3. Check again after more time
Sleep: 30 seconds
CheckSubagentProgress: { "pid": 12345, "log_file": "/path/to/log", "tail_lines": 20 }
→ Returns: { "is_running": true, "tail_output": "Still analyzing file complex_module.py...", "total_lines": 55 }

# 4. If stuck on same file for too long, interrupt and restart with guidance
InterruptSubagent: { "pid": 12345 }
→ Returns: { "killed": true }

# 5. Restart with specific guidance
Subagent: "Analyze all Python files and create a summary report. Skip complex_module.py for now as it seems to be causing issues. Focus on the other files first."
```

### Signs a Subagent is Stuck

- Same log message repeated multiple times
- No new lines added to log for several checks
- Log shows error messages or exceptions
- Process is running but CPU usage is low (inferred from lack of progress)

## Best Practices

### When to Use Subagent

✅ **Good use cases:**
- Complex multi-step tasks that can be isolated
- Tasks requiring extensive file analysis (large codebases)
- When you're approaching context limits
- Delegating independent research or exploration
- Tasks that would benefit from a fresh perspective

❌ **Bad use cases:**
- Simple one-liner tasks (use direct tools instead)
- Tasks requiring shared conversation history
- When immediate real-time feedback is needed
- Highly interactive workflows

### Reading Subagent Output

The master agent should follow these guidelines:

1. **Start with the tail** - The returned `tail_output` typically contains the summary
2. **Check exit code** - `exit_code == 0` indicates success
3. **Count lines first** - Use `total_lines` to assess log size before reading
4. **Use Grep for search** - Search the log file for specific content rather than reading it all
5. **Read strategically** - Use Read tool with line ranges if you need specific sections

### Example Pattern

```
Master: "Use Subagent to analyze all Python files and create a report"
  ↓
Subagent: Spawned with fresh context
  - Uses Glob to find all *.py files
  - Uses Read to analyze each file
  - Uses Write to create report.txt
  - Returns summary in tail_output
  ↓
Master: Receives tail showing "Report created with 42 files analyzed"
  - Can Read the report.txt file if needed
  - Can Grep the log for errors or specific details
```

## Log File Management

**Location**: `.claude-c/subagent/subagent_YYYYMMDD_HHMMSS_PID.log`

**Format**: Timestamped filename with process ID for uniqueness

**Retention**: Log files accumulate over time. Consider periodic cleanup:
```bash
# Remove logs older than 7 days
find .claude-c/subagent/ -name "*.log" -mtime +7 -delete
```

## Limitations

1. **No streaming output** - The master agent must poll for progress
2. **No context sharing** - Subagent starts with clean context (this is by design)
3. **Resource usage** - Each subagent is a full claude-c instance with API calls
4. **Zombie processes** - If orchestrator crashes, subagent processes may become zombies (use `InterruptSubagent` or system tools to clean up)

## Implementation Details

**Location**: `src/claude.c` - `tool_subagent()`, `tool_check_subagent_progress()`, `tool_interrupt_subagent()` functions

**Key features:**
- Uses `fork()` and `exec()` for non-blocking process execution
- Proper shell escaping for quotes, backslashes, dollar signs, and backticks
- Redirects both stdout and stderr to log file
- Process monitoring via `waitpid()` with `WNOHANG`
- Graceful shutdown with `SIGTERM` followed by `SIGKILL` if needed
- Returns log file path for further inspection

## Security Considerations

- The subagent inherits all environment variables (API keys, config)
- The subagent has full tool access (Write, Edit, Bash)
- Log files may contain sensitive information
- Shell escaping prevents command injection

## Troubleshooting

**Problem**: "Failed to read log file"
- Likely cause: Subagent failed to start or crashed immediately
- Check: Examine the exit code and error message
- Solution: Verify the prompt is valid and executable path is correct

**Problem**: "Subagent timed out"
- Exit code will be timeout-related
- Solution: Increase timeout parameter or simplify the task

**Problem**: "Output truncated"
- The tail_lines parameter limits returned content
- Solution: Use Read tool to access full log file, or increase tail_lines

**Problem**: "Task not completed"
- Check tail_output for errors or incomplete status
- Use Grep to search log for "error" or "failed"
- Read the full log file to understand what happened
