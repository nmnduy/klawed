# END_AI_TURN Event Implementation

## Summary

Added a new `END_AI_TURN` message type to the SQLite queue mode that signals when klawed has completed processing a request and is ready for the next instruction.

## Changes Made

### 1. Code Changes (`src/sqlite_queue.c`)

#### Added new helper function:
```c
static int sqlite_queue_send_end_ai_turn(SQLiteQueueContext *ctx, const char *receiver)
```

This function sends an `END_AI_TURN` message with just the message type (no content field).

#### Modified `sqlite_queue_process_interactive()`:
- Added call to `sqlite_queue_send_end_ai_turn()` after successful interactive processing completes
- Replaced the old comment about "No completion message" with the actual event emission

### 2. Documentation Changes (`docs/sqlite-queue.md`)

#### Updated Message Types table:
Added `END_AI_TURN` to the list of message types with description "AI turn completed, waiting for further instruction"

#### Added END_AI_TURN Message documentation:
- Full message format specification
- When the message is sent (after all tools executed, final text sent, ready for next message)
- Use cases for clients (update UI, hide loading indicators, enable input, trigger post-processing)

#### Updated Completion Detection section:
- Now recommends using `END_AI_TURN` as the primary way to detect completion
- Kept the tool tracking approach as an alternative for advanced use cases

#### Updated Message Flow Examples:
- Simple request-response: Added `END_AI_TURN` after text response
- Interactive processing with tool calls: Replaced `COMPLETED` with `END_AI_TURN`

#### Updated Python Client Examples:
- Simple client: Added handling for `END_AI_TURN` that returns `True` to indicate completion
- Advanced client: Added handling for `END_AI_TURN` and `API_CALL` messages with appropriate logging

### 3. Test Script (`test_end_ai_turn.py`)

Created a comprehensive test script that:
- Starts klawed in sqlite-queue mode
- Sends a simple test message
- Monitors all incoming messages
- Verifies that `END_AI_TURN` is received
- Reports success/failure

## Message Format

```json
{
  "messageType": "END_AI_TURN"
}
```

The message has no additional fields - just the message type.

## When END_AI_TURN is Sent

The event is sent by klawed when:
1. All tool calls have been executed
2. All `TOOL_RESULT` messages have been sent
3. The final `TEXT` response has been sent (if any)
4. The interactive processing loop has completed
5. Klawed is ready to receive the next user message

## Client Usage

### Simple Detection
```python
if msg_type == "END_AI_TURN":
    print("AI is ready for next instruction")
    return True  # Completion detected
```

### With UI Updates
```python
elif msg_type == "END_AI_TURN":
    # Hide loading spinner
    ui.hide_loading()
    # Enable input controls
    ui.enable_input()
    # Notify user
    ui.show_notification("Ready for input")
    return True
```

### With State Management
```python
elif msg_type == "END_AI_TURN":
    self.state = "READY"
    self.in_progress = False
    self.enable_submit_button()
    return True
```

## Benefits

1. **Simpler Client Logic**: Clients no longer need to track pending tool calls to detect completion
2. **Explicit State**: Clear signal that klawed is waiting for input
3. **Better UX**: Enables accurate loading indicators and input state management
4. **Event-Driven**: Fits well with event-driven client architectures
5. **Backwards Compatible**: Clients can still use tool tracking if needed

## Testing

Run the test script:
```bash
export OPENAI_API_KEY="your-key"
python3 test_end_ai_turn.py
```

Expected output:
- `[RECV] API_CALL` - Klawed is calling the AI
- `[RECV] TEXT` - AI response text
- `[RECV] END_AI_TURN` - Turn completed
- `[TEST] ✓ END_AI_TURN event was received!`

## Related Files

- `src/sqlite_queue.c` - Implementation
- `src/sqlite_queue.h` - Header (no changes needed)
- `docs/sqlite-queue.md` - Documentation
- `test_end_ai_turn.py` - Test script

## Future Enhancements

Possible future additions to the END_AI_TURN message:
- `duration_ms`: Total processing time
- `tool_count`: Number of tools executed
- `token_usage`: Token usage for the turn
- `message_count`: Number of messages exchanged
