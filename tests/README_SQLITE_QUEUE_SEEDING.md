# SQLite Queue Seeding Test Suite

## Overview

The `test_sqlite_queue_seeding.c` test suite provides comprehensive unit tests for the `sqlite_queue_seed_conversation()` function in `src/sqlite_queue.c`. This function is critical for restoring conversation history from the SQLite database when starting a daemon or resuming a session.

## What Does the Function Do?

The `sqlite_queue_seed_conversation()` function:

1. Loads messages marked as `sent=1` from the SQLite database
2. Handles three message types: `TEXT`, `TOOL`, and `TOOL_RESULT`
3. Pairs `TOOL` messages (assistant tool call requests) with their corresponding `TOOL_RESULT` messages
4. Injects synthetic error results for interrupted tool calls (when a `TOOL` has no matching `TOOL_RESULT`)
5. Ignores orphaned `TOOL_RESULT` messages (results without matching tool calls)
6. Maintains conversation integrity for LLM API requirements

## Why Is This Important?

The bug fix that prompted these tests addressed a critical issue where TOOL and TOOL_RESULT messages weren't being properly paired when seeding conversation history. This caused LLM API errors because:

- Tool calls must be followed by their results in the conversation
- The LLM expects every tool call to have a corresponding result
- Orphaned tool results (without a preceding call) are invalid

## Test Coverage

The test suite includes 9 test scenarios with 32 total assertions:

### 1. Basic TEXT Message Seeding (`test_basic_text_messages`)
- **Purpose**: Verify that simple user and assistant text messages are loaded correctly
- **Tests**: 5 assertions
- **Validates**: Message count, roles (user/assistant), content types

### 2. TOOL + TOOL_RESULT Pairing (`test_tool_result_pairing`)
- **Purpose**: Ensure TOOL messages are properly paired with their TOOL_RESULT
- **Tests**: 5 assertions
- **Validates**: Correct pairing, tool IDs match, result is not an error

### 3. Orphaned TOOL_RESULT Ignored (`test_orphaned_tool_result`)
- **Purpose**: Verify that TOOL_RESULT without matching TOOL is silently ignored
- **Tests**: 2 assertions
- **Validates**: Orphaned results don't appear in conversation

### 4. Synthetic Error Injection (`test_synthetic_error_injection`)
- **Purpose**: Test that interrupted tool calls get synthetic error results
- **Tests**: 5 assertions
- **Validates**: Error result created, error flag set, error message present

### 5. User Message Interrupts Tool (`test_user_message_interrupts_tool`)
- **Purpose**: Verify synthetic error injection when user sends message before tool completes
- **Tests**: 4 assertions
- **Validates**: Message ordering, synthetic error between tool and user message

### 6. Multiple Tool Calls (`test_multiple_tool_calls`)
- **Purpose**: Test correct handling of multiple sequential tool calls
- **Tests**: 4 assertions
- **Validates**: All tool results present, correct IDs, proper ordering

### 7. Mixed Tool Completion (`test_mixed_tool_completion`)
- **Purpose**: Test scenarios with both successful and interrupted tools
- **Tests**: 3 assertions
- **Validates**: Successful results preserved, synthetic errors for interrupted tools

### 8. Empty Database (`test_empty_database`)
- **Purpose**: Verify graceful handling of empty database
- **Tests**: 2 assertions
- **Validates**: Zero messages seeded, no errors

### 9. Only Assistant Messages (`test_only_assistant_messages`)
- **Purpose**: Test loading conversation with only assistant responses
- **Tests**: 2 assertions
- **Validates**: Assistant messages loaded correctly

## Message Format

The tests use JSON message formats as stored in the database:

### TEXT Message
```json
{
  "messageType": "TEXT",
  "content": "Hello, world!"
}
```

### TOOL Message (Assistant Request)
```json
{
  "messageType": "TOOL",
  "toolName": "Read",
  "toolId": "call_123",
  "toolParameters": {
    "file_path": "test.txt"
  }
}
```

### TOOL_RESULT Message
```json
{
  "messageType": "TOOL_RESULT",
  "toolName": "Read",
  "toolId": "call_123",
  "toolOutput": {
    "result": "File contents here"
  },
  "isError": false
}
```

## Running the Tests

```bash
# Run just the seeding tests
make test-sqlite-queue-seeding

# Run all tests (includes this test suite)
make test
```

## Implementation Notes

### Database Schema
The tests create a temporary SQLite database at `/tmp/test_sqlite_queue_seeding.db` with the standard messages table:
- `id`: Auto-incrementing primary key
- `sender`: Message sender (e.g., "klawed", "client")
- `receiver`: Message receiver
- `message`: JSON-formatted message content
- `sent`: Flag (0=pending, 1=delivered/seeded)
- `created_at`: Unix timestamp

### Test Utilities
The test suite provides helper functions:
- `create_text_message()`: Generate TEXT message JSON
- `create_tool_message()`: Generate TOOL message JSON
- `create_tool_result_message()`: Generate TOOL_RESULT message JSON
- `insert_message()`: Insert message into test database

### Code Changes for Testing
To enable testing, the `#ifndef TEST_BUILD` wrapper around `sqlite_queue_seed_conversation()` was removed in `src/sqlite_queue.c`. This allows the function to be available in both production and test builds.

## Expected Output

When all tests pass, you should see:

```
=== SQLite Queue Seeding Test Suite ===

SQLite Queue: Restored 2 message(s) from conversation history
✓ test_basic_text_messages: seed returned positive count
✓ test_basic_text_messages: message count
[... more test output ...]

=== Test Summary ===
Total tests: 32
Passed: 32

✓ All tests passed!
```

## Maintenance

When modifying the `sqlite_queue_seed_conversation()` function:

1. Run the test suite to ensure no regressions
2. Add new tests for new behavior
3. Update message format examples if schema changes
4. Verify both production build (`make`) and test build (`make test`) succeed

## Related Files

- `src/sqlite_queue.c` - Implementation of seeding function
- `src/sqlite_queue.h` - Public API declarations
- `src/klawed_internal.h` - Internal types (ConversationState, InternalMessage, etc.)
- `Makefile` - Build configuration for tests
