- [x] src/completion.c:80-200 - Directory completion system with many small string allocations for file/directory names
- [x] src/completion.c:172-200 - Array expansion for completion options using reallocarray
- [ ] src/openai_provider.c:85-160 - Streaming context with accumulated text buffer allocations
- [ ] src/openai_provider.c:547-800 - API response parsing with tool call allocations and error messages
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

**Pattern established:**
1. Embed arena pointer in wrapper structure with magic number
2. Allocate all related memory from the arena
3. Free with single `arena_destroy` call
4. Maintain backward compatibility with existing API

**Challenges for remaining items:**
- Streaming contexts use `realloc` for growing buffers - arena doesn't support realloc
- API response tool calls require modifying `ApiResponse` structure used in multiple places
- Need to handle potential arena size limits for variable-length data