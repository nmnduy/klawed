# Model Switching Unit Tests

These tests verify that klawed can successfully switch between different LLM providers at any point during a conversation, in both interactive and SQLite queue modes.

## Providers Tested

Based on the actual providers found in `real_api_calls.db`:

| Provider | Model | API Base | Usage in DB |
|----------|-------|----------|-------------|
| Kimi Coding | kimi-for-coding | api.kimi.com | 582 calls |
| Z.AI/GLM | glm-4.6 | api.z.ai | 1 call |
| OpenAI/GPT | gpt-5.4 | chatgpt.com | 1 call |
| Anthropic Claude | claude-opus-4 | api.anthropic.com | 1 call |

## Test Files

### 1. `test_model_switch_interactive.c`
Tests for interactive (in-memory) mode.

**Test Cases:**
- **Switch at empty conversation** - Verifies provider can be set before any messages
- **Switch after user message** - Ensures switching works after initial user input
- **Switch after assistant response** - Tests switching mid-conversation
- **Switch with tool calls** - Verifies tool call/result pairs are preserved across switches
- **Switch with parallel tools** - Tests multiple simultaneous tool calls
- **Rapid switching** - Stress test with 12 rapid provider switches
- **Switch after error result** - Ensures error flags are preserved
- **Provider API URLs** - Verifies correct API endpoints for each provider

**Results:** 67 tests, all passing

### 2. `test_model_switch_sqlite_queue.c`
Tests for SQLite queue mode (persistent IPC).

**Test Cases:**
- **Queue save/restore** - Verifies conversation serialization/deserialization
- **Switch after queue restore** - Tests provider switching after loading from queue
- **Multiple queue operations** - Tests save/restore cycles with switches
- **Tool ordering preserved** - Verifies tool call/result sequence through queue
- **All real providers** - Tests each provider from the actual database

**Results:** 33 tests, all passing

## Key Verifications

### 1. Message Format Preservation
The internal message format (`InternalMessage`) is vendor-agnostic:
```c
typedef struct {
    MessageRole role;           // MSG_USER, MSG_ASSISTANT, MSG_SYSTEM
    InternalContent *contents;  // Array of content blocks
    int content_count;
} InternalMessage;
```

When switching providers, messages don't need conversion - they remain in the internal format until sent to the new provider's API.

### 2. Tool Call/Result Integrity
Tests verify that:
- Tool calls have matching results
- Tool IDs are preserved across switches
- Error flags remain intact
- Multiple parallel tool calls maintain ordering

### 3. Bedrock Converse API Compatibility
Special handling for AWS Bedrock Converse API:
- All tool results merged into single user message
- First message must be from user
- ToolUse/ToolResult ID matching preserved

### 4. Queue Mode Persistence
SQLite queue mode tests verify:
- Complete conversation state survives serialization
- Provider can be changed after loading from queue
- Tool ordering is maintained through save/restore cycles

## Running the Tests

```bash
# Interactive mode tests
make test-model-switch-interactive

# SQLite queue mode tests
make test-model-switch-sqlite-queue

# All tests
make test
```

## Implementation Notes

### Provider Abstraction
The `Provider` struct provides a common interface:
```c
typedef struct Provider {
    const char *name;
    void *config;
    void (*call_api)(struct Provider *self, struct ConversationState *state, ApiCallResult *result);
    void (*cleanup)(struct Provider *self);
} Provider;
```

### Switching Logic
Provider switching follows this pattern:
1. Clean up old provider (free resources)
2. Initialize new provider from config
3. Update state->provider pointer
4. Preserve all messages (they're vendor-agnostic)

### Thread Safety
All conversation state modifications use `pthread_mutex_lock/unlock` to ensure thread safety during provider switches.

## Edge Cases Handled

1. **Empty conversation** - Provider can be set before any messages
2. **Interrupted tools** - Synthetic results injected for missing tool responses
3. **Error results** - Error flags preserved across switches
4. **Rapid switching** - No resource leaks or state corruption
5. **Queue persistence** - Full state survives SQLite serialization
