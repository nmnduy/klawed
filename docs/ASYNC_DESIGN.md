# Async TUI Architecture

This document captures the threading and ownership model that powers the async
terminal UI introduced in Phase 5.

## Thread Model

- **Main thread**
  - Runs the ncurses event loop (`tui_event_loop`).
  - Polls keyboard input and processes resize signals.
  - Batches messages from the TUI queue via `process_tui_messages`.
  - Updates the display exclusively on this thread; no ncurses calls occur
    outside of it.

- **Worker thread**
  - Started through `ai_worker_start`.
  - Blocks on `AIInstructionQueue` for pending instructions.
  - Performs API calls, tool execution, and posts UI updates with
    `post_tui_message`.

The separation ensures the UI remains responsive while long-running operations
execute off the main thread.

## Queues and Ownership

### AI Instruction Queue

- Backed by a bounded circular buffer (`AIInstructionQueue`).
- `enqueue_instruction` copies the submitted text; the queue owns that memory
  until `dequeue_instruction` succeeds.
- After dequeue, ownership of `AIInstruction.text` transfers to the worker.
- The `conversation_state` pointer is a borrowed reference protected by
  `conversation_state_lock`.
- The queue blocks writers when full, providing natural backpressure.

### TUI Message Queue

- Workers push `TUIMessage` instances via `post_tui_message`.
- Text payloads are copied on enqueue.
- The main thread polls with `poll_tui_message`, takes ownership of `msg.text`,
  and must free it after dispatch.
- When the queue reaches capacity the oldest message is evicted (FIFO) with a
  debug log, preventing unbounded growth.
- `tui_drain_message_queue` can be called during shutdown to flush remaining
  messages before resources are released.

## Message Dispatch

- `process_tui_messages` limits processing to `TUI_MAX_MESSAGES_PER_FRAME`
  messages per loop iteration (currently 10) to protect frame time.
- Messages map to handlers:
  - `TUI_MSG_ADD_LINE` → append conversation entry using color inferred from the
    prefix (e.g. `[User]`, `[Assistant]`, `[Tool]`).
  - `TUI_MSG_STATUS` → update status bar text.
  - `TUI_MSG_CLEAR` → reset the conversation history.
  - `TUI_MSG_ERROR` → surface errors in the transcript.
- Unknown message types are currently ignored; `TUI_MSG_TODO_UPDATE` is a
  placeholder for a later todo panel integration.

## Shutdown Flow

1. Main thread requests the worker to stop (`ai_worker_stop`), which sets
   `running = 0`, signals the instruction queue, and joins the thread.
2. `tui_drain_message_queue` runs to display any final worker messages.
3. Queues are shutdown/freed (`ai_queue_free`, `tui_msg_queue_shutdown`,
   `tui_msg_queue_free`).
4. `tui_cleanup` tears down ncurses windows.

This order guarantees that outstanding UI updates are rendered before ncurses
exits.

## Backpressure & Status Updates

- `ai_queue_depth` is sampled after enqueue to show pending instruction counts.
- When the depth drops to zero the status line reports that processing is
  underway, signalling that the queue drained and work is in-flight.
- If enqueue fails (e.g. shutdown initiated) a direct error message is shown.

## Memory Ownership Summary

- **Instructions**: `enqueue_instruction` duplicates input text; ownership moves
  to the worker after `dequeue_instruction`. Worker frees text after handling.
- **TUI Messages**: `post_tui_message` duplicates message text; ownership moves
  to the main thread when `poll_tui_message` succeeds, and `process_tui_messages`
  frees it after dispatch.
- **ConversationState**: Shared structure guarded by
  `conversation_state_lock`. Read/write call-sites must hold the mutex.
- **Spinner / UI helpers**: Live only on the thread that created them and must
  not cross thread boundaries.

