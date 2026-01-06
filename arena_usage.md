- [x] src/completion.c:80-200 - Directory completion system with many small string allocations for file/directory names
- [x] src/completion.c:172-200 - Array expansion for completion options using reallocarray
- [x] src/openai_provider.c:85-160 - Streaming context with accumulated text buffer allocations
- [x] src/openai_provider.c:547-800 - API response parsing with tool call allocations and error messages
- [x] src/openai_provider.c:772-802 - Tool call array allocation and string duplication
- [ ] src/anthropic_provider.c:371-389 - Streaming context with multiple text buffers
- [ ] src/anthropic_provider.c:819-890 - Anthropic API response parsing with tool allocations
- [ ] src/anthropic_provider.c:868-887 - Tool call array and string allocations

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

**Patterns established:**
1. Embed arena pointer in wrapper structure with magic number (completion system)
2. Add arena pointer directly to existing structure (streaming context, ApiResponse)
3. Allocate all related memory from the arena
4. Free with single `arena_destroy` call
5. Maintain backward compatibility with existing API
6. For temporary buffers: consider algorithmic optimization (two-pass) instead of arena
7. Create helper functions for common operations (e.g., `arena_strdup()`)

**Solutions for challenges:**
- Streaming contexts: Use arena allocation with copy instead of `realloc`
- API response parsing: For temporary buffers, optimize algorithm to avoid realloc
- Tool call allocations: Modified `ApiResponse` structure to include arena pointer
  - Added `Arena *arena` field to `ApiResponse`
  - Updated `api_response_free()` to handle both allocation methods
  - Created arena for each ApiResponse in OpenAI provider
  - All strings and arrays allocated from arena (cJSON objects still use heap)

**Remaining challenges:**
- Need to update other providers (anthropic, bedrock, openai_responses) to use arena allocation for consistency
- cJSON objects still use heap allocation (cJSON_Parse, cJSON_CreateObject)
- Need to handle potential arena size limits for variable-length data
- Other providers still use old allocation method (arena = NULL)

**Next steps/TODO:**
1. Update anthropic provider to use arena allocation for ApiResponse
2. Update bedrock provider to use arena allocation for ApiResponse  
3. Update openai_responses.c to use arena allocation for ApiResponse
4. Consider adding arena to anthropic StreamingContext structure
5. Investigate if cJSON objects can be allocated from arena (may require cJSON modification)