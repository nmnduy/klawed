# Auto-Compaction

## Overview

Auto-compaction is a context management system that prevents the conversation history from exceeding the model's token limit by automatically storing older messages in long-term memory (SQLite memory database) and replacing them with a summary notice. This allows Klawed to maintain indefinitely long conversations while keeping the active context window manageable.

## How It Works

When enabled, auto-compaction monitors the conversation token usage and triggers when a configurable threshold is reached:

1. **Trigger**: At 60% of model token limit (default), compaction is triggered
2. **Summarize**: AI generates a 250-400 word summary of the messages being compacted, capturing:
   - What was being worked on
   - Current goals/objectives
   - Task state/progress
3. **Store**: Older messages are stored in the SQLite memory database as searchable long-term memory
4. **Extract**: Tool usage statistics and token metrics are collected
5. **Replace**: Compacted messages are replaced with a single system message containing:
   - AI-generated summary of compacted content
   - Number of messages compacted
   - Session ID
   - Message range that was compacted
   - Tool usage summary
   - Token usage statistics (before/after, freed)
   - Context usage percentage
6. **Continue**: Recent messages (default: last 20) remain in active context
7. **Retrieve**: AI can use `MemorySearch` tool to retrieve relevant past context

## Token Tracking

Auto-compaction uses **accurate token counts** from the API provider's usage data, not message counts:

- Primary source: Token usage from actual API calls (stored in `token_usage` table)
- Fallback: Estimated tokens based on message content (~4 chars per token)
- Model limits: Retrieved from model database or configurable via environment variable
- Default model limit: 125,000 tokens (when model is unknown)

## Requirements

- **SQLite memory database**: Memory system uses SQLite3 with FTS5 (always available)
- **Auto-compact flag**: Enable via `--auto-compact` flag or `KLAWED_AUTO_COMPACT=1` environment variable

The memory database is always available since it uses SQLite3 with FTS5, which is built into most SQLite installations.

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
export KLAWED_COMPACT_THRESHOLD=75        # Trigger at 75% of model token limit (default: 75)
export KLAWED_COMPACT_KEEP_RECENT=100     # Keep last 100 messages (default: 100)
export KLAWED_CONTEXT_LIMIT=125000  # Override model token limit (default: 125000)
```

### Configuration Parameters

- **`KLAWED_COMPACT_THRESHOLD`**: Percentage of model token limit at which to trigger compaction
  - Default: 75 (triggers at 93,750 tokens for 125k limit)
  - Range: 1-100
  - Lower values = more aggressive compaction
  - Example: 80% threshold on 125k model = triggers at 100,000 tokens

- **`KLAWED_COMPACT_KEEP_RECENT`**: Number of recent messages to keep after compaction
  - Default: 100
  - Minimum: 1 (always keeps system message)
  - These messages remain in the active context window

- **`KLAWED_CONTEXT_LIMIT`**: Override the model's token limit
  - Default: 125000 (125k tokens)
  - If not set, uses model database to look up limit based on model name
  - Use this for custom models or to enforce stricter limits

## Implementation Details

### Message Storage

Compacted messages are stored in the SQLite memory database with the following structure:

- **Entity**: `session.{session_id}` or `conversation.history` (if no session)
- **Slot**: `msg_{index}` where index is the message position
- **Value**: `{role}: {content}` (e.g., "user: Write a function to...")
- **Kind**: `event` (memory database memory type)

### Compaction Notice Format

The system message injected after compaction contains an AI-generated summary and metadata:

```
## Context Compaction Notice

{N} earlier messages have been stored in memory. Use MemorySearch to retrieve relevant past context if needed.

### Summary of Compacted Context

{AI-generated summary covering:
- What was being worked on (summary of recent activity)
- Current goals/objectives
- Task state/progress}

---
**Session**: {session_id}
**Messages compacted**: {start}-{end}
**Tools used**: Read (X), Write (Y), Edit (Z), Bash (W)
**Tokens**: {before} → {after} (freed ~{compacted} tokens)
**Context usage**: {before_percent}% → {after_percent}% of {limit} token limit
```

Example:
```
## Context Compaction Notice

45 earlier messages have been stored in memory. Use MemorySearch to retrieve relevant past context if needed.

### Summary of Compacted Context

The user has been working on implementing a new authentication system for the web application. The main activities include:

1. **What was being worked on**: Refactoring the user authentication module in `src/auth/user_auth.c`. Created new functions for token validation and session management. Modified the database schema in `migrations/003_add_sessions.sql` to support refresh tokens.

2. **Current goals/objectives**: Complete the OAuth2 integration with Google and GitHub providers. The user wants to support both social login and traditional email/password authentication.

3. **Task state/progress**: Token generation and validation are complete. Session persistence is working. Still need to implement the OAuth callback handlers and update the frontend login form.

---
**Session**: 20260114-143022-a3f8
**Messages compacted**: 1-45
**Tools used**: Read (12), Write (8), Edit (15), Bash (10)
**Tokens**: 92450 → 28760 (freed ~63690 tokens)
**Context usage**: 73.9% → 23.0% of 125000 token limit
```

### Summarization

During compaction, an AI-generated summary (250-400 words) is created that captures:
- **What was being worked on**: Main activities and tasks performed
- **Current goals/objectives**: What the user is trying to accomplish
- **Task state/progress**: What has been completed and what remains

This summary is generated by making an additional API call before the messages are archived. If summarization fails, a fallback notice is used and the compaction still proceeds.

### Timing

Compaction is performed **before** each API call if the threshold is met. This ensures:
- The API call always has a valid context window
- No messages are lost during streaming responses
- The compaction notice is visible to the AI in the next response

## Source Files

- **Core logic**: `src/compaction.c`, `src/compaction.h`
- **Integration**: `src/klawed.c` (trigger logic in `call_api_with_retries()`)
- **Internal types**: `src/klawed_internal.h` (ConversationState changes)
- **Memory storage**: `src/memory_db.c`, `src/memory_db.h`
- **Tests**: `tests/test_compaction.c`, `tests/test_compaction_stubs.c`
- **Build**: `Makefile` (test target: `make test-compaction`)

## API Reference

### `compaction_init_config()`

```c
void compaction_init_config(CompactionConfig *config, int enabled);
```

Initialize compaction configuration from environment variables. Sets defaults and validates memory database availability.

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
1. Generates AI summary of messages being compacted (250-400 words)
2. Stores old messages to SQLite memory database
3. Collects tool usage statistics
4. Generates compaction notice with summary
5. Reorganizes message array
6. Updates state and config

Returns 0 on success, -1 on error.

### `compaction_generate_summary()`

```c
int compaction_generate_summary(ConversationState *state,
                                const InternalMessage *messages,
                                int message_count,
                                char *summary_out,
                                size_t summary_size);
```

Generates an AI summary of the messages being compacted. The summary includes:
- What was being worked on (activities and tasks)
- Current goals/objectives
- Task state/progress

Returns 0 on success, -1 on error (summary_out will be empty on error).

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

# Check version (memory system is always available)
./build/klawed --version
```

## Memory System Behavior

The SQLite memory database is always available:

1. Messages are stored with FTS5 full-text search indexing
2. `MemorySearch` can retrieve compacted messages by content
3. Entity-based queries work with the same database
4. Memory database path: `.klawed/memory.db` (customizable via `KLAWED_MEMORY_PATH`)

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
# - Config initialization
# - Environment variable parsing
# - Trigger threshold logic
# - Message storage and retrieval
# - Compaction notice generation
# - Memory cleanup
```

## Future Enhancements

Potential improvements for future versions:

1. **Structured extraction**: Parse file paths, function names from compacted tool calls
2. **Selective compaction**: Keep messages referencing specific files/topics
3. **Compaction history**: Track multiple compaction events per session
4. **Auto-retrieval**: Automatically search memory when context is relevant
5. **Summary caching**: Store summaries in memory database for faster retrieval
6. **Vector search**: Use sqlite-vector for semantic similarity-based retrieval

## Related Documentation

- [SQLite Memory System](memory_db.md) - Long-term memory storage with FTS5
- [Session Management](session.md) - Session ID generation and tracking
