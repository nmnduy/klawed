- [x] src/completion.c:80-200 - Directory completion system with many small string allocations for file/directory names
- [x] src/completion.c:172-200 - Array expansion for completion options using reallocarray
- [x] src/openai_provider.c:85-160 - Streaming context with accumulated text buffer allocations
- [x] src/openai_provider.c:547-800 - API response parsing with tool call allocations and error messages
- [ ] src/openai_provider.c:772-802 - Tool call array allocation and string duplication
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

**Patterns established:**
1. Embed arena pointer in wrapper structure with magic number (completion system)
2. Add arena pointer directly to existing structure (streaming context)
3. Allocate all related memory from the arena
4. Free with single `arena_destroy` call
5. Maintain backward compatibility with existing API
6. For temporary buffers: consider algorithmic optimization (two-pass) instead of arena

**Solutions for challenges:**
- Streaming contexts: Use arena allocation with copy instead of `realloc`
- API response parsing: For temporary buffers, optimize algorithm to avoid realloc
- Tool call allocations: Still pending - requires modifying `ApiResponse` structure or creating wrapper

**Remaining challenges:**
- API response tool calls require modifying `ApiResponse` structure used in multiple places
- Need to handle potential arena size limits for variable-length data