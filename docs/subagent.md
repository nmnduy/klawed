# Subagent Tool

## Overview

The Subagent tool allows klawed to spawn a new instance of itself with a fresh context to work on delegated tasks. This is useful for:

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

1. **Execution**: Spawns a new klawed process with the given prompt (non-blocking)
2. **Logging**: All stdout and stderr output is written to a timestamped log file in `.klawed/subagent/`
3. **Return value**: Returns immediately with PID and log file path
4. **Monitoring**: Use `CheckSubagentProgress` to monitor progress by reading the log
5. **Interruption**: Use `InterruptSubagent` only if the subagent is clearly stuck or task is no longer needed

## Output Structure

### Subagent Tool Response
```json
{
  "pid": 12345,
  "log_file": "/path/to/.klawed/subagent/subagent_20231208_123456_1234.log",
  "timeout_seconds": 300,
  "message": "Subagent started with PID 12345. Log file: /path/to/log... Use 'CheckSubagentProgress' tool to monitor progress or 'InterruptSubagent' to stop it."
}
```

### CheckSubagentProgress Tool Response
```json
{
  "pid": 12345,
  "is_running": true,
  "log_file": "/path/to/.klawed/subagent/subagent_20231208_123456_1234.log",
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

### Patience is Key

When delegating a task to a subagent:

1. **Give adequate time** - Complex tasks need time to complete. Check progress at reasonable intervals (every 1-2 minutes for complex tasks) rather than repeatedly polling.

2. **The subagent reports its status** - The subagent will log its progress and either:
   - Complete the task successfully
   - Stop and report that it cannot proceed with clear information about why
   - Ask for further instructions if blocked

3. **Only interrupt when necessary** - Use `InterruptSubagent` only when:
   - The subagent has been given adequate time to work
   - There is clear evidence of being stuck (no progress for extended period)
   - Repeated errors that prevent forward progress
   - The task is no longer needed

4. **Avoid premature interruption** - The subagent is designed to be stable and will communicate its status. Frequent interruption disrupts the subagent's ability to complete its work.

### Example Orchestrator Workflow

```bash
# 1. Start subagent with clear task
Subagent: "Analyze all Python files and create a summary report"
→ Returns: { "pid": 12345, "log_file": "/path/to/log" }

# 2. Wait for meaningful progress (complex tasks take time)
Sleep: 60 seconds
CheckSubagentProgress: { "pid": 12345, "log_file": "/path/to/log", "tail_lines": 20 }
→ Returns: { "is_running": true, "tail_output": "Found 50 Python files. Analyzing...", "total_lines": 100 }

# 3. Wait again for continued progress
Sleep: 60 seconds
CheckSubagentProgress: { "pid": 12345, "log_file": "/path/to/log", "tail_lines": 20 }
→ Returns: { "is_running": true, "tail_output": "Analyzed 30/50 files. Creating report...", "total_lines": 200 }

# 4. Subagent completes - review results
Sleep: 60 seconds
CheckSubagentProgress: { "pid": 12345, "log_file": "/path/to/log", "tail_lines": 50 }
→ Returns: { "is_running": false, "exit_code": 0, "tail_output": "Report created: summary.txt\nAnalyzed 50 files." }

# OR: Subagent reports it cannot proceed
→ Returns: { "is_running": false, "exit_code": 1, "tail_output": "Error: Cannot access file X. Please provide access or alternative approach." }

# 5. Only interrupt if truly stuck (no progress for several checks with adequate wait time)
# This should be rare - the subagent will report its status
```

### Signs a Subagent Needs Attention

Rather than "stuck", look for these signals that may require intervention:

- **Subagent reports it cannot proceed** - Clear error message about what's blocking
- **No new lines added after extended wait** - Several checks with no activity
- **Subagent explicitly asks for guidance** - Look for questions or requests for clarification in the log

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

### Orchestrator Guidelines

1. **Set clear, complete prompts** - Include all necessary context in the initial prompt
2. **Wait for meaningful intervals** - Check progress every 1-2 minutes for complex tasks
3. **Trust the subagent to report** - It will communicate progress and blockers
4. **Avoid micromanagement** - Frequent interruption disrupts the subagent's workflow
5. **Provide guidance when restarting** - If you do need to interrupt and restart, be specific about what changed

### Reading Subagent Output

The master agent should follow these guidelines:

1. **Start with the tail** - The returned `tail_output` typically contains the summary
2. **Check exit code** - `exit_code == 0` indicates success, non-zero means the subagent stopped with an issue
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

**Location**: `.klawed/subagent/subagent_YYYYMMDD_HHMMSS_PID.log`

**Format**: Timestamped filename with process ID for uniqueness

**Retention**: Log files accumulate over time. Consider periodic cleanup:
```bash
# Remove logs older than 7 days
find .klawed/subagent/ -name "*.log" -mtime +7 -delete
```

## Limitations

1. **No streaming output** - The master agent must poll for progress
2. **No context sharing** - Subagent starts with clean context (this is by design)
3. **Resource usage** - Each subagent is a full klawed instance with API calls
4. **Zombie processes** - If orchestrator crashes, subagent processes may become zombies (use `InterruptSubagent` or system tools to clean up)

## Implementation Details

**Location**: `src/klawed.c` - `tool_subagent()`, `tool_check_subagent_progress()`, `tool_interrupt_subagent()` functions

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
- Check tail_output for the subagent's report on why it couldn't proceed
- Use Grep to search log for "error", "failed", or "cannot"
- Read the full log file to understand what happened
- The subagent should have reported its status clearly

**Problem**: "Subagent seems slow"
- Complex tasks take time - wait for meaningful intervals before checking
- Review total_lines to see if the subagent is actively working
- Only consider intervention if there's no activity after several minutes
