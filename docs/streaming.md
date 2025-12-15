# Streaming Response Support

## Overview

Claude Code now supports real-time streaming of responses using Server-Sent Events (SSE). This allows the assistant's responses to be displayed incrementally as they are generated, providing immediate feedback to the user.

## Architecture

The streaming implementation consists of several layers:

### 1. HTTP Client Layer (`src/http_client.c`)

**SSE Parser:**
- Parses Server-Sent Events (SSE) line-by-line
- Handles `event:` and `data:` fields
- Accumulates multi-line data blocks
- Dispatches complete events to callbacks

**Key Components:**
- `SSEParserState`: Parser state for tracking event accumulation
- `streaming_write_callback()`: curl callback that feeds data to parser
- `sse_parser_process_line()`: Processes individual SSE lines
- `sse_parser_dispatch_event()`: Calls user callback with complete events

**API:**
```c
// Execute streaming HTTP request
HttpResponse* http_client_execute_stream(
    const HttpRequest *req,
    HttpStreamCallback stream_cb,    // Called for each SSE event
    void *stream_data,
    HttpProgressCallback progress_cb,
    void *progress_data
);
```

### 2. Event Types

Streaming events support both Anthropic and OpenAI streaming formats:

```c
typedef enum {
    // Anthropic Messages API events
    SSE_EVENT_MESSAGE_START,        // message_start event
    SSE_EVENT_CONTENT_BLOCK_START,  // content_block_start event
    SSE_EVENT_CONTENT_BLOCK_DELTA,  // content_block_delta event (text streaming)
    SSE_EVENT_CONTENT_BLOCK_STOP,   // content_block_stop event
    SSE_EVENT_MESSAGE_DELTA,        // message_delta event (stop_reason, etc.)
    SSE_EVENT_MESSAGE_STOP,         // message_stop event
    SSE_EVENT_ERROR,                // error event
    SSE_EVENT_PING,                 // ping event (keepalive)
    
    // OpenAI Chat Completions API events
    SSE_EVENT_OPENAI_CHUNK,         // OpenAI chunk (default "data:" event)
    SSE_EVENT_OPENAI_DONE           // OpenAI [DONE] marker
} StreamEventType;
```

### 3. Provider Layers

#### Anthropic Provider (`src/anthropic_provider.c`)

**StreamingContext:**
- Accumulates text deltas from `content_block_delta` events
- Tracks tool calls and their parameters
- Reconstructs complete message from streaming events
- Updates TUI in real-time

**Key Functions:**
- `streaming_event_handler()`: Processes each SSE event
- Dispatches text deltas to TUI via `tui_update_last_conversation_line()`
- Builds synthetic response from accumulated data for logging

#### OpenAI Provider (`src/openai_provider.c`)

**OpenAIStreamingContext:**
- Accumulates text deltas from OpenAI chunk events
- Tracks tool calls with incremental updates
- Reconstructs complete message from streaming chunks
- Updates TUI in real-time

**Key Functions:**
- `openai_streaming_event_handler()`: Processes OpenAI SSE chunks
- Handles `delta.content` for text streaming
- Accumulates `delta.tool_calls` for function calling
- Builds synthetic OpenAI-format response for compatibility

#### Bedrock Provider (`src/bedrock_provider.c`)

**BedrockStreamingContext:**
- Accumulates text deltas from content_block_delta events
- Tracks tool calls and their parameters
- Reconstructs complete message from streaming events
- Updates TUI in real-time

**Key Functions:**
- `bedrock_streaming_event_handler()`: Processes Bedrock SSE events (same format as Anthropic)
- Dispatches text deltas to TUI via `tui_update_last_conversation_line()`
- Builds synthetic Anthropic-format response, then converts to OpenAI format for logging
- Uses AWS SigV4 signing for the streaming endpoint

**Implementation Notes:**
- Bedrock streaming uses the `invoke-with-response-stream` endpoint
- Event handling is identical to Anthropic provider (same Messages API format)
- AWS SigV4 signature is computed for the streaming endpoint URL
- Supports all Bedrock credential sources (env vars, SSO, config files)

### 4. TUI Layer (`src/tui.c`)

**Streaming Display:**
- `tui_update_last_conversation_line()`: Appends text to last conversation entry
- Updates entry data structure for history
- Redraws pad with new text
- Auto-scrolls to bottom in INSERT mode

## Usage

### Enable Streaming

Set the environment variable:

```bash
export CLAUDE_C_ENABLE_STREAMING=1
./build/claude-c "your prompt"
```

Or in your shell configuration:

```bash
# ~/.bashrc or ~/.zshrc
export CLAUDE_C_ENABLE_STREAMING=1
```

### Behavior

**With streaming enabled:**
- Text appears character-by-character as the model generates it
- Provides immediate feedback
- Lower perceived latency
- Same final result as non-streaming mode

**Without streaming (default):**
- Response appears all at once when complete
- Simpler implementation
- Less network traffic (single response)

## Implementation Details

### SSE Formats

#### Anthropic Format

Server-Sent Events follow this format:

```
event: message_start
data: {"type":"message_start","message":{"id":"msg_123",...}}

event: content_block_start
data: {"type":"content_block_start","index":0,"content_block":{"type":"text"}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello"}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":" world"}}

event: content_block_stop
data: {"type":"content_block_stop","index":0}

event: message_delta
data: {"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"output_tokens":10}}

event: message_stop
data: {"type":"message_stop"}
```

#### OpenAI Format

OpenAI uses implicit events (no `event:` field):

```
data: {"id":"chatcmpl-123","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","content":""},"finish_reason":null}]}

data: {"id":"chatcmpl-123","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"content":"Hello"},"finish_reason":null}]}

data: {"id":"chatcmpl-123","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"content":" world"},"finish_reason":null}]}

data: {"id":"chatcmpl-123","object":"chat.completion.chunk","choices":[{"index":0,"delta":{},"finish_reason":"stop"}]}

data: [DONE]
```

**Key Differences:**
- No explicit `event:` field - parser detects OpenAI format by content
- Each chunk is a complete JSON object with `choices` array
- `delta.content` contains incremental text
- `delta.tool_calls` array for function calling (indexed, incremental)
- `[DONE]` marker signals end of stream

#### Bedrock Format

AWS Bedrock uses the same Anthropic Messages API streaming format for Claude models:

```
event: message_start
data: {"type":"message_start","message":{"id":"msg_123",...}}

event: content_block_start
data: {"type":"content_block_start","index":0,"content_block":{"type":"text"}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello"}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":" world"}}

event: content_block_stop
data: {"type":"content_block_stop","index":0}

event: message_delta
data: {"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"output_tokens":10}}

event: message_stop
data: {"type":"message_stop"}
```

**Key Details:**
- Bedrock uses the `invoke-with-response-stream` endpoint instead of `invoke`
- Event format is identical to Anthropic's Messages API
- Supports text deltas and tool use streaming
- AWS SigV4 signing is applied to the streaming endpoint

### Text Accumulation

#### Anthropic

The streaming handler accumulates text in `StreamingContext`:

1. `message_start`: Initialize empty assistant line in TUI
2. `content_block_delta` with `text_delta`: 
   - Append to `accumulated_text` buffer
   - Call `tui_update_last_conversation_line()` to display
3. `message_stop`: Build final synthetic response for logging

#### OpenAI

The streaming handler accumulates text in `OpenAIStreamingContext`:

1. First `delta.content`: Initialize empty assistant line in TUI
2. Subsequent `delta.content`: 
   - Append to `accumulated_text` buffer
   - Call `tui_update_last_conversation_line()` to display
3. `finish_reason` present: Build final synthetic response for logging
4. `delta.tool_calls`: Incrementally accumulate tool call data by index

### Response Reconstruction

#### Anthropic

After streaming completes, the provider builds a synthetic response JSON:

```json
{
  "id": "streaming",
  "type": "message",
  "role": "assistant",
  "content": [
    {
      "type": "text",
      "text": "<accumulated text>"
    }
  ],
  "stop_reason": "end_turn"
}
```

This ensures compatibility with existing logging and persistence code.

#### OpenAI

After streaming completes, the provider builds a synthetic response in OpenAI format:

```json
{
  "id": "chatcmpl-123",
  "object": "chat.completion",
  "model": "gpt-4",
  "created": 1234567890,
  "choices": [
    {
      "index": 0,
      "message": {
        "role": "assistant",
        "content": "<accumulated text>",
        "tool_calls": [...]
      },
      "finish_reason": "stop"
    }
  ],
  "usage": {
    "prompt_tokens": 0,
    "completion_tokens": 0,
    "total_tokens": 0
  }
}
```

**Note**: Usage stats are placeholder zeros since OpenAI streaming doesn't always include final token counts in all streaming modes.

## Error Handling

- **Network errors**: Streaming aborts, error shown in TUI
- **Parse errors**: Invalid SSE lines are logged but don't crash
- **Interrupt (Ctrl+C)**: Streaming callback checks `interrupt_requested` flag
- **Timeout**: Standard curl timeout applies to entire stream

## Performance Considerations

- **Buffer Growth**: Text buffers use exponential growth (2x) to minimize reallocations
- **TUI Updates**: Each delta triggers a pad refresh - efficient for typical response sizes
- **Memory**: Accumulated text is kept for history, no different than non-streaming

## Future Enhancements

Potential improvements:

1. **Tool Streaming**: Support streaming tool calls (currently text-only)
2. **Rate Limiting**: Throttle TUI updates if deltas arrive too fast
3. **Chunked Display**: Buffer small deltas before displaying (smoother animation)
4. **Progress Indication**: Show token count or progress during streaming
5. **Partial Parsing**: Start processing code/tool calls before complete

## Debugging

Enable debug logging to see streaming events:

```bash
export CLAUDE_LOG_LEVEL=DEBUG
./build/claude-c "test prompt"
```

Look for log messages like:
- `Stream: message_start`
- `Stream: content_block_delta`
- `Stream delta: <text>`

## Testing

To test streaming without changing code:

```bash
# Enable streaming
export CLAUDE_C_ENABLE_STREAMING=1

# Test with a prompt that generates longer response
./build/claude-c "Write a detailed explanation of how TCP works"

# You should see text appearing incrementally
```

## Compatibility

- **Anthropic API**: ✅ Fully compatible with Messages API streaming
- **OpenAI API**: ✅ Fully compatible with Chat Completions API streaming
- **Bedrock**: ✅ Fully compatible with Bedrock streaming (invoke-with-response-stream)
- **Caching**: Works with prompt caching enabled
- **TUI**: Required - streaming won't work in non-TUI mode

## Technical Notes

### Why SSE instead of WebSocket?

- Simpler protocol (HTTP-based)
- Unidirectional (we only receive)
- Better compatibility with proxies/firewalls
- Standard format used by major AI APIs

### Thread Safety

Streaming callback runs in curl's context (same thread as request). TUI updates are immediate and thread-safe since they happen in the same thread.

### Interrupt Handling

The streaming callback checks `state->interrupt_requested` on each event. When user presses Ctrl+C:
1. Main thread sets flag
2. Streaming callback sees flag
3. Returns non-zero to abort
4. curl cleanly terminates

## Code References

- **HTTP Client**: `src/http_client.c` - SSE parser and streaming execution
- **Anthropic Provider**: `src/anthropic_provider.c` - Anthropic event handling and text accumulation
- **OpenAI Provider**: `src/openai_provider.c` - OpenAI chunk handling and response reconstruction  
- **TUI**: `src/tui.c` - `tui_update_last_conversation_line()`
- **Types**: `src/http_client.h` - `StreamEvent`, `StreamEventType`
- **State**: `src/claude_internal.h` - `ConversationState.tui` pointer
