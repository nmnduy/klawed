## Summary: `reasoning_content` Support for Moonshot/Kimi and OpenAI-compatible APIs

### Current Implementation Status: ✅ IMPLEMENTED

klawed now supports `reasoning_content` for Moonshot/Kimi thinking models. The implementation:

1. **Parses `reasoning_content`** from API responses (both streaming and non-streaming)
2. **Stores it in `InternalContent`** structure alongside text content
3. **Includes `reasoning_content`** in subsequent API requests when using Moonshot provider
4. **Handles empty content correctly**: When the model returns `reasoning_content` with empty `content`, we send `content: ""` (empty string) instead of `content: null` to match Moonshot API expectations

### Provider Differences

| Provider | Model | `reasoning_content` Handling |
|----------|-------|------------------------------|
| **Moonshot/Kimi** | kimi-k2-thinking, kimi-k2.5 | **MUST include** in subsequent requests |
| **Kimi Coding Plan** | kimi-for-coding | **MUST include** in subsequent requests |
| **DeepSeek** | deepseek-reasoner | **MUST NOT include** (causes 400 error) |
| **OpenAI** | o1, o1-mini, o1-preview, o3-mini | Uses different mechanism (no reasoning_content) |

### Configuration

The provider type automatically determines the behavior:
- `"provider_type": "moonshot"` → preserves `reasoning_content`
- `"provider_type": "kimi_coding_plan"` → preserves `reasoning_content`
- `"provider_type": "deepseek"` → discards `reasoning_content`
- `"provider_type": "openai"` → discards `reasoning_content` (default)

### Example Moonshot Configuration

```json
{
  "providers": {
    "kimi-k2.5": {
      "provider_type": "moonshot",
      "provider_name": "Moonshot AI",
      "model": "kimi-k2.5",
      "api_base": "https://api.moonshot.ai",
      "api_key_env": "MOONSHOT_AI_API_KEY"
    }
  }
}
```

### Example Kimi Coding Plan Configuration

```json
{
  "providers": {
    "kimi-for-coding": {
      "provider_type": "kimi_coding_plan",
      "provider_name": "Kimi Coding Plan",
      "model": "kimi-for-coding"
    }
  }
}
```

**Note:** Kimi Coding Plan uses OAuth device flow authentication. No API key is required - you'll be prompted to authorize via browser on first use. The OAuth token will be stored in `~/.kimi/credentials/kimi-code.json`.

### Technical Details

#### Important: Empty Content Handling

Moonshot/Kimi returns `content: ""` (empty string) when making tool calls with `reasoning_content`. When echoing these messages back, we must:
- Send `content: ""` (empty string), NOT `content: null`
- Include `reasoning_content` alongside the empty content

This is handled in `src/openai_messages.c`:
```c
// Moonshot/Kimi: use empty string when reasoning_content is present
if (include_reasoning_content && reasoning_content_str) {
    cJSON_AddStringToObject(asst_msg, "content", "");
}
```

#### Where reasoning_content is Stored

The `reasoning_content` is stored in the `InternalContent` structure:
- For text content blocks with `reasoning_content`
- For tool-call-only messages (stored on the first tool call)

See `src/klawed_internal.h`:
```c
typedef struct InternalContent {
    // ... other fields ...
    char *reasoning_content; // Reasoning content from thinking models (may be NULL)
} InternalContent;
```

#### Streaming Support

The streaming parser in `src/openai_provider.c` accumulates `reasoning_content` from SSE delta events and attaches it to the final response message.

### Known Error Messages

If you see these errors, check:

1. **"thinking is enabled but reasoning_content is missing in assistant tool call message"** (HTTP 400)
   - Solution: Ensure `provider_type` is set to `"moonshot"` or `"kimi_coding_plan"` in your configuration

2. **"Invalid response format: no choices or output"** (HTTP 200 with empty response)
   - This can happen if the API is overloaded or has issues
   - May also occur if `content: null` is sent instead of `content: ""`
   - Solution: Update to latest klawed version with the empty content fix

### References

- `src/moonshot_provider.c` - Moonshot provider implementation
- `src/openai_messages.c` - Request building with reasoning_content preservation
- `src/conversation/message_parser.c` - Response parsing with reasoning_content extraction
- `src/openai_provider.c` - Streaming support for reasoning_content
