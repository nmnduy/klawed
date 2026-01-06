- [x] src/completion.c:80-200 - Directory completion system with many small string allocations for file/directory names
- [x] src/completion.c:172-200 - Array expansion for completion options using reallocarray
- [x] src/openai_provider.c:85-160 - Streaming context with accumulated text buffer allocations
- [x] src/openai_provider.c:547-800 - API response parsing with tool call allocations and error messages
- [x] src/openai_provider.c:772-802 - Tool call array allocation and string duplication
- [x] src/anthropic_provider.c:371-389 - Streaming context with multiple text buffers
- [x] src/anthropic_provider.c:819-890 - Anthropic API response parsing with tool allocations
- [x] src/anthropic_provider.c:868-887 - Tool call array and string allocations

## Progress Summary

**Completed:**
- ✅ Completion system (completion.c) now uses arena allocation
  - Created `ArenaCompletionResult` wrapper with magic number for identification
  - All strings and arrays allocated from arena (16KB default size)
  - Single `arena_destroy` call frees all completion memory
  - Backward compatible: `ncurses_completion_free` handles both arena and regular allocations

- ✅ OpenAI streaming context (openai_provider.c:85-160) now uses arena allocation
  - Added `Arena *arena` field to `OpenAIStreamingContext` structure
  - Created arena (64KB) during context initialization
  - All strings (accumulated_text, finish_reason, model, message_id) allocated from arena
  - Buffer growth uses arena allocation with copy instead of `realloc`
  - Single `arena_destroy` call in `openai_streaming_context_free` frees all memory

- ✅ API response parsing text buffer (openai_provider.c:547-800) optimized
  - Eliminated `realloc` for text accumulation in Responses API format parsing
  - Used two-pass approach: calculate total length first, then allocate once
  - More efficient than arena allocation for this temporary buffer

- ✅ OpenAI provider tool call allocations (openai_provider.c:772-802) now use arena allocation
  - Added `Arena *arena` field to `ApiResponse` structure in `klawed_internal.h`
  - Updated `api_response_free()` to handle both arena and heap allocation
  - Created arena (16KB) for each ApiResponse in OpenAI provider
  - All ApiResponse structures, tool arrays, and strings allocated from arena
  - Added `arena_strdup()` helper function for safe string duplication
  - Single `arena_destroy()` call frees all ApiResponse memory

- ✅ Anthropic provider streaming context (anthropic_provider.c:371-389) now uses arena allocation
  - Added `Arena *arena` field to `StreamingContext` structure
  - Created arena (64KB) during context initialization
  - All strings (accumulated_text, content_block_type, tool_use_id, tool_use_name, tool_input_json, stop_reason) allocated from arena
  - Buffer growth uses arena allocation with copy instead of `realloc`
  - Single `arena_destroy` call in `streaming_context_free` frees all memory

- ✅ Anthropic provider API response parsing (anthropic_provider.c:819-890) now uses arena allocation
  - Created arena (16KB) for each ApiResponse in anthropic provider
  - All ApiResponse structures, tool arrays, and strings allocated from arena
  - Reused `arena_strdup()` helper function for safe string duplication
  - Single `arena_destroy()` call frees all ApiResponse memory

- ✅ Bedrock provider API response parsing now uses arena allocation
  - Created arena (16KB) for each ApiResponse in bedrock provider
  - All ApiResponse structures, tool arrays, and strings allocated from arena
  - Added `arena_strdup()` helper function for safe string duplication
  - Single `arena_destroy()` call frees all ApiResponse memory

- ✅ Bedrock provider streaming context now uses arena allocation
  - Added `Arena *arena` field to `BedrockStreamingContext` structure
  - Created arena (64KB) during context initialization
  - All strings (accumulated_text, content_block_type, tool_use_id, tool_use_name, tool_input_json, stop_reason) allocated from arena
  - Buffer growth uses arena allocation with copy instead of `realloc`
  - Single `arena_destroy` call in `bedrock_streaming_context_free` frees all memory

- ✅ OpenAI Responses API format conversion (openai_responses.c) now uses arena allocation
  - Created arena (16KB) for each ApiResponse in parse_responses_http_response()
  - All ApiResponse structures, tool arrays, and strings allocated from arena
  - Added `arena_strdup()` helper function for safe string duplication
  - Text accumulation buffer uses arena allocation with copy instead of `realloc`
  - Single `arena_destroy()` call frees all ApiResponse memory

**Patterns established:**
1. Embed arena pointer in wrapper structure with magic number (completion system)
2. Add arena pointer directly to existing structure (streaming context, ApiResponse)
3. Allocate all related memory from the arena
4. Free with single `arena_destroy` call
5. Maintain backward compatibility with existing API
6. For temporary buffers: consider algorithmic optimization (two-pass) instead of arena
7. Create helper functions for common operations (e.g., `arena_strdup()`)
8. Handle buffer growth with arena allocation + copy instead of `realloc`

**Solutions for challenges:**
- Streaming contexts: Use arena allocation with copy instead of `realloc`
- API response parsing: For temporary buffers, optimize algorithm to avoid realloc
- Tool call allocations: Modified `ApiResponse` structure to include arena pointer
  - Added `Arena *arena` field to `ApiResponse`
  - Updated `api_response_free()` to handle both allocation methods
  - Created arena for each ApiResponse in OpenAI and anthropic providers
  - All strings and arrays allocated from arena (cJSON objects still use heap)
- Buffer growth in streaming: Use arena allocation with copy for both accumulated_text and tool_input_json buffers

**Remaining challenges:**
- cJSON objects still use heap allocation (cJSON_Parse, cJSON_CreateObject)
  - cJSON is an external library that would require modification to support arena allocation
  - Acceptable limitation: cJSON objects are relatively small and infrequent compared to string/array allocations
- Need to handle potential arena size limits for variable-length data
  - Current solution: Use algorithmic optimization (two-pass) for temporary buffers where possible
  - For streaming contexts: Use arena allocation with copy for buffer growth

**Implementation Status: COMPLETE** ✅

All major allocation hotspots have been converted to arena allocation. The implementation provides:
- Significant reduction in memory fragmentation
- Simplified memory management (single free call)
- Faster allocation for related objects
- Maintained backward compatibility
- Graceful fallback to heap allocation if needed

**Remaining considerations:**
- cJSON objects use heap allocation (external library limitation)
- Monitor arena size usage in production
- Consider dynamic arena sizing if overflow scenarios are observed