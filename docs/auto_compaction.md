# Auto-Compaction

## Overview

Auto-compaction is a context management system that prevents the conversation history from exceeding the model's token limit by automatically storing older messages in long-term memory (memvid) and replacing them with a summary notice. This allows Klawed to maintain indefinitely long conversations while keeping the active context window manageable.

## How It Works

When enabled, auto-compaction monitors the conversation token usage and triggers when a configurable threshold is reached:

1. **Trigger**: At 60% of model token limit (default), compaction is triggered
2. **Store**: Older messages are stored in memvid as searchable long-term memory
3. **Extract**: Tool usage statistics and token metrics are collected
4. **Replace**: Compacted messages are replaced with a single system message containing:
   - Number of messages compacted
   - Session ID
   - Message range that was compacted
   - Tool usage summary
   - Token usage statistics (before/after, freed)
   - Context usage percentage
5. **Continue**: Recent messages (default: last 20) remain in active context
6. **Retrieve**: AI can use `MemorySearch` tool to retrieve relevant past context

## Token Tracking

Auto-compaction uses **accurate token counts** from the API provider's usage data, not message counts:

- Primary source: Token usage from actual API calls (stored in `token_usage` table)
- Fallback: Estimated tokens based on message content (~4 chars per token)
- Model limits: Retrieved from model database or configurable via environment variable
- Default model limit: 125,000 tokens (when model is unknown)

## Requirements

- **Memvid library**: Must be compiled with memvid support (`HAVE_MEMVID`)
- **Auto-compact flag**: Enable via `--auto-compact` flag or `KLAWED_AUTO_COMPACT=1` environment variable

If memvid is not available, auto-compaction will be automatically disabled even if the flag is set.

## Configuration

### Command Line

```bash
# Enable auto-compaction
./build/klawed --auto-compact "your prompt"
```

### Environment Variables

```bash
# Enable/disable auto-compaction
export KLAWED_AUTO_COMPACT=1              # Enable (1/true/yes)

# Configure thresholds
export KLAWED_COMPACT_THRESHOLD=60        # Trigger at 60% of model token limit (default: 60)
export KLAWED_COMPACT_KEEP_RECENT=20      # Keep last 20 messages (default: 20)
export KLAWED_CONTEXT_LIMIT=125000  # Override model token limit (default: 125000)
```

### Configuration Parameters

- **`KLAWED_COMPACT_THRESHOLD`**: Percentage of model token limit at which to trigger compaction
  - Default: 60 (triggers at 75,000 tokens for 125k limit)
  - Range: 1-100
  - Lower values = more aggressive compaction
  - Example: 80% threshold on 125k model = triggers at 100,000 tokens

- **`KLAWED_COMPACT_KEEP_RECENT`**: Number of recent messages to keep after compaction
  - Default: 20
  - Minimum: 1 (always keeps system message)
  - These messages remain in the active context window

- **`KLAWED_CONTEXT_LIMIT`**: Override the model's token limit
  - Default: 125000 (125k tokens)
  - If not set, uses model database to look up limit based on model name
  - Use this for custom models or to enforce stricter limits

## Implementation Details

### Message Storage

Compacted messages are stored in memvid with the following structure:

- **Entity**: `session.{session_id}` or `conversation.history` (if no session)
- **Slot**: `msg_{index}` where index is the message position
- **Value**: `{role}: {content}` (e.g., "user: Write a function to...")
- **Kind**: `event` (memvid memory type)

### Compaction Notice Format

The system message injected after compaction contains:

```
## Context Compaction Notice
{N} earlier messages have been stored in memory. Use MemorySearch to retrieve relevant past context if needed.

Session: {session_id}
Messages compacted: {start}-{end}
Tools used: Read (X), Write (Y), Edit (Z), Bash (W)
Tokens: {before} → {after} (freed ~{compacted} tokens)
Context usage: {before_percent}% → {after_percent}% of {limit} token limit
```

Example:
```
## Context Compaction Notice
45 earlier messages have been stored in memory. Use MemorySearch to retrieve relevant past context if needed.

Session: 20260114-143022-a3f8
Messages compacted: 1-45
Tools used: Read (12), Write (8), Edit (15), Bash (10)
Tokens: 92450 → 28760 (freed ~63690 tokens)
Context usage: 73.9% → 23.0% of 125000 token limit
```

### Timing

Compaction is performed **before** each API call if the threshold is met. This ensures:
- The API call always has a valid context window
- No messages are lost during streaming responses
- The compaction notice is visible to the AI in the next response

## Source Files

- **Core logic**: `src/compaction.c`, `src/compaction.h`
- **Integration**: `src/klawed.c` (trigger logic in `call_api_with_retries()`)
- **Internal types**: `src/klawed_internal.h` (ConversationState changes)
- **Tests**: `tests/test_compaction.c`, `tests/test_compaction_stubs.c`
- **Build**: `Makefile` (test target: `make test-compaction`)

## API Reference

### `compaction_init_config()`

```c
void compaction_init_config(CompactionConfig *config, int enabled);
```

Initialize compaction configuration from environment variables. Sets defaults and validates memvid availability.

### `compaction_should_trigger()`

```c
int compaction_should_trigger(const ConversationState *state, const CompactionConfig *config);
```

Returns 1 if compaction should trigger based on current message count and threshold. Returns 0 if disabled or threshold not met.

### `compaction_perform()`

```c
int compaction_perform(ConversationState *state, CompactionConfig *config, const char *session_id);
```

Performs the compaction operation:
1. Stores old messages to memvid
2. Collects tool usage statistics
3. Generates compaction notice
4. Reorganizes message array
5. Updates state and config

Returns 0 on success, -1 on error.

## Example Usage

```bash
# Enable with defaults (trigger at 60% of 125k tokens = 75k tokens, keep 20 messages)
./build/klawed --auto-compact "help me refactor this codebase"

# Aggressive compaction (trigger at 40% of 125k = 50k tokens, keep 10 messages)
KLAWED_COMPACT_THRESHOLD=40 \
KLAWED_COMPACT_KEEP_RECENT=10 \
./build/klawed --auto-compact "long-running task"

# Custom token limit (trigger at 60% of 200k = 120k tokens)
KLAWED_CONTEXT_LIMIT=200000 \
./build/klawed --auto-compact "very long conversation"

# Check if it would work (requires memvid)
./build/klawed --auto-compact --version
# If memvid not available, warning will be logged
```

## Behavior Without Memvid

If auto-compact is enabled but memvid is not available:

1. A warning is logged: `"Auto-compact enabled but memvid not available. Compaction disabled."`
2. `config->enabled` is forced to 0
3. `compaction_should_trigger()` always returns 0
4. `compaction_perform()` logs warning and returns -1
5. Conversation continues normally without compaction

## Memory Safety

The implementation follows NASA C coding standards and project conventions:

- All pointers checked for NULL before use
- Message contents properly freed before reorganization
- Bounds checking on all array operations
- Safe string operations (`snprintf` with size limits)
- No memory leaks (verified with tests)

## Testing

```bash
# Run compaction test suite
make test-compaction

# Tests cover:
# - Config initialization with/without memvid
# - Environment variable parsing
# - Trigger threshold logic
# - Message storage and retrieval
# - Compaction notice generation
# - Memory cleanup
```

## Future Enhancements

Potential improvements for future versions:

1. **Structured extraction**: Parse file paths, function names from compacted tool calls
2. **Compression ratio**: Summarize compacted content with AI before storing
3. **Selective compaction**: Keep messages referencing specific files/topics
4. **Compaction history**: Track multiple compaction events per session
5. **Auto-retrieval**: Automatically search memory when context is relevant

## Related Documentation

- [Memvid Integration](memvid.md) - Long-term memory storage
- [Session Management](session.md) - Session ID generation and tracking
- [Database Rotation](database-rotation.md) - Related but different: rotates SQLite records
