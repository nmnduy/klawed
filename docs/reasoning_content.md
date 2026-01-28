## Summary: `reasoning_content` Support for Moonshot/Kimi and OpenAI-compatible APIs

### The Issue

The error you're seeing:
```
thinking is enabled but reasoning_content is missing in assistant tool call message at index 2 (HTTP 400)
```

This is an HTTP 400 error from the Moonshot/Kimi API, not from klawed. The API is rejecting your request because:

1. **Moonshot/Kimi thinking models** (kimi-k2-thinking, kimi-k2.5) return a `reasoning_content` field alongside `content` in assistant messages
2. When you send the conversation history back to the API for tool calls, **Moonshot requires that `reasoning_content` be included** in the assistant messages
3. klawed currently doesn't capture or preserve `reasoning_content` from API responses

### Provider Differences

| Provider | Model | `reasoning_content` Handling |
|----------|-------|------------------------------|
| **Moonshot/Kimi** | kimi-k2-thinking, kimi-k2.5 | **MUST include** in subsequent requests |
| **DeepSeek** | deepseek-reasoner | **MUST NOT include** (causes 400 error) |
| **OpenAI** | o1, o1-mini, o1-preview, o3-mini | Uses different mechanism (no reasoning_content) |

### What Needs to be Implemented

To support Moonshot/Kimi thinking models, klawed needs:

1. **Parse `reasoning_content`** from API responses at the same level as `content`
2. **Store it in the InternalMessage** structure (add a `reasoning_content` field to track it)
3. **Include `reasoning_content`** when building subsequent API requests

### The Complication

The tricky part is that different providers handle this differently:
- **Moonshot/Kimi**: Must include reasoning_content in subsequent requests
- **DeepSeek**: Must NOT include reasoning_content in subsequent requests

This means we'd need provider-specific logic or a configuration option.

### Recommended Approach

Add a configuration option in the provider config:
```json
{
  "reasoning_content_mode": "preserve"  // or "discard"
}
```

Where:
- `"preserve"` (default for Moonshot): Include reasoning_content in subsequent API calls
- `"discard"` (default for DeepSeek): Strip reasoning_content from subsequent API calls

### Quick Workaround

If you need this working immediately for Moonshot, the simplest workaround would be to:
1. Don't use tool calling with thinking models, OR
2. Use kimi-k2.5 with thinking disabled: add `"thinking": {"type": "disabled"}` to requests

Would you like me to implement the full `reasoning_content` support for klawed? This would involve:

1. Adding a `reasoning_content` field to `InternalContent` or `InternalMessage`
2. Modifying `message_parser.c` to extract `reasoning_content` from responses
3. Modifying `api_builder.c` to include `reasoning_content` in requests when present
4. Adding provider configuration for reasoning_content handling mode
5. Updating the streaming parser in `openai_provider.c` to capture reasoning_content from streamed responses
