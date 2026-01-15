## Refactoring Progress

### ✅ Completed Steps

#### Step 1: Utility Functions (COMPLETED - Commit 5066c6d)
**Status**: All utility functions successfully extracted to `src/util/`
- ✅ `file_utils.c/.h` - File I/O operations (read_file, write_file, resolve_path, mkdir_p, save_binary_file)
- ✅ `string_utils.c/.h` - String manipulation (strip_ansi_escapes)
- ✅ `timestamp_utils.c/.h` - Time/date formatting (get_current_timestamp, generate_timestamped_filename, get_current_date, generate_session_id)
- ✅ `format_utils.c/.h` - Value formatting (format_file_size)
- ✅ `env_utils.c/.h` - Environment/system info (get_env_int_retry, get_platform, get_os_version, exec_shell_command)
- ✅ `output_utils.c/.h` - Tool output handling (tool_emit_line, emit_diff_line)
- ✅ `diff_utils.c/.h` - Diff display (show_diff)

**Lines extracted**: ~1,087 lines  
**Build status**: ✅ Successful  
**Documentation**: REFACTORING_STEP1_SUMMARY.md

#### Step 2: Tool Implementations (COMPLETED - Commit 7ba03c5)
**Status**: All tool implementations successfully extracted to `src/tools/`
- ✅ `tool_filesystem.c/.h` - Read, Write, Edit, MultiEdit, Glob
- ✅ `tool_bash.c/.h` - Bash command execution
- ✅ `tool_search.c/.h` - Grep tool
- ✅ `tool_subagent.c/.h` - Subagent, CheckSubagentProgress, InterruptSubagent
- ✅ `tool_image.c/.h` - UploadImage
- ✅ `tool_sleep.c/.h` - Sleep
- ✅ `tool_todo.c/.h` - TodoWrite

**Lines extracted**: ~2,720 lines  
**Build status**: ✅ Successful  
**Documentation**: REFACTORING_STEP2_SUMMARY.md

**Total progress**: ~3,807 lines extracted from src/klawed.c (originally 10,282 lines)

### 🔄 Remaining Steps

The following refactoring steps remain to complete the full modularization:

---

## Proposed Refactoring Structure for `src/klawed.c`

### 1. **API Communication Layer** (`src/api/`) - ❌ TODO
- `api_client.c` - Core API request/response handling, retry logic
- `api_response.c` - ApiResponse structure management and parsing
- `api_builder.c` - Request JSON building from conversation state
- `provider_registry.c` - Provider initialization and management

### 2. **Tool System** (`src/tools/`)

**✅ Tool Implementations (COMPLETED):**
- ✅ `tool_filesystem.c/.h` - Read, Write, Edit, MultiEdit, Glob
- ✅ `tool_bash.c/.h` - Bash command execution
- ✅ `tool_search.c/.h` - Grep tool
- ✅ `tool_subagent.c/.h` - Subagent, CheckSubagentProgress, InterruptSubagent
- ✅ `tool_image.c/.h` - UploadImage
- ✅ `tool_sleep.c/.h` - Sleep
- ✅ `tool_todo.c/.h` - TodoWrite

**❌ Tool System Core (TODO):**
- `tool_registry.c` - Tool registration, lookup, and execution dispatcher
- `tool_definitions.c` - Tool schema definitions for API
- `tool_validation.c` - Tool allowlist validation (anti-hallucination)
- `tool_executor.c` - Parallel tool execution with threading
- `tool_output.c` - Tool output formatting (human/machine readable)

### 3. **Conversation Management** (`src/conversation/`) - ❌ TODO
- `conversation_state.c` - State initialization, locking, lifecycle
- `message_builder.c` - Message creation (user, assistant, system)
- `message_parser.c` - OpenAI message format parsing
- `content_types.c` - InternalContent management (text, images, tool calls/results)

### 4. **Interactive Mode** (`src/interactive/`) - ❌ TODO
- `interactive_loop.c` - Main interactive mode entry point
- `input_handler.c` - Input submission, vim commands, interrupt handling
- `response_processor.c` - Process API responses recursively
- `command_dispatch.c` - Route vim-style commands (`:q`, `:clear`, `:!cmd`)

### 5. **Single Command Mode** (`src/oneshot/`) - ❌ TODO
- `oneshot_mode.c` - Single command execution
- `oneshot_processor.c` - Recursive response processing for tool calls
- `oneshot_output.c` - Human/machine-readable output formatting

### 6. **System Prompt & Context** (`src/context/`) - ❌ TODO
- `system_prompt.c` - Build system prompt with environment info
- `environment.c` - Platform detection, OS info, git status
- `memory_injection.c` - Memory context injection (HAVE_MEMVID)
- `klawed_md.c` - Read and parse KLAWED.md file

### 7. **Utility Functions** (`src/util/`) - ✅ COMPLETED
- ✅ `file_utils.c/.h` - read_file, write_file, resolve_path, mkdir_p, save_binary_file
- ✅ `string_utils.c/.h` - String manipulation (strip_ansi_escapes)
- ✅ `diff_utils.c/.h` - Diff generation and display
- ✅ `timestamp_utils.c/.h` - Timestamp generation (get_current_timestamp, generate_timestamped_filename, get_current_date, generate_session_id)
- ✅ `format_utils.c/.h` - Value formatting (format_file_size)
- ✅ `env_utils.c/.h` - Environment/system info (get_env_int_retry, get_platform, get_os_version, exec_shell_command)
- ✅ `output_utils.c/.h` - Tool output handling (tool_emit_line, emit_diff_line)

### 8. **UI Components** (`src/ui/`) - ❌ TODO
- `ui_output.c` - UI abstraction (ui_append_line, ui_set_status, ui_show_error)
- `print_helpers.c` - print_assistant, print_tool, print_error
- `tool_output_display.c` - Tool-specific output formatting (grep, bash, etc.)

### 9. **Session Management** (`src/session/`) - ❌ TODO
- `session_id.c` - Session ID generation
- `session_persistence.c` - Session loading/saving (resume feature)
- `token_usage.c` - Token usage tracking and reporting

### 10. **Main Entry Point** (`src/`) - ❌ TODO
- `klawed_main.c` - Minimal main() with argument parsing, mode dispatch, cleanup
- `startup.c` - Initialization (logging, curl, colorscheme, persistence, MCP, memvid)
- `cleanup.c` - Cleanup routines

### 11. **Daemon Modes** (already exist but reference from main)
- `zmq_daemon.c` (already in codebase)
- `uds_daemon.c` (already in codebase)
- `sqlite_queue_daemon.c` (already in codebase)

---

## Benefits of This Structure:

1. **Separation of Concerns**: Each module has a clear, single responsibility
2. **Testability**: Individual modules can be unit tested in isolation
3. **Maintainability**: Changes to tool implementations don't affect API layer, etc.
4. **Reduced Cognitive Load**: Files are 200-500 lines instead of 10,000+
5. **Parallel Development**: Multiple developers can work on different modules
6. **Reusability**: Components like `file_utils.c` can be reused across tools

## Migration Strategy:

1. ✅ **COMPLETED**: **util** functions (file_utils, string_utils, etc.) - no dependencies
2. ✅ **COMPLETED**: **tool implementations** - extracted all 7 tool modules
3. ❌ **TODO**: **conversation management** (well-defined boundaries)
4. ❌ **TODO**: **tool system core** (registry, definitions, validation, executor, output)
5. ❌ **TODO**: **system prompt & context** (environment, memory, klawed_md)
6. ❌ **TODO**: **API layer** (depends on conversation)
7. ❌ **TODO**: **UI components** (print helpers, output display)
8. ❌ **TODO**: **session management** (session ID, persistence, token tracking)
9. ❌ **TODO**: **interactive/oneshot modes** (depends on API + tools)
10. ❌ **TODO**: slim down **main** to just initialization + dispatch
11. ❌ **TODO**: **final cleanup** - remove `#if 0` wrapped code, run comprehensive tests

This keeps the codebase functional during refactoring - each step is a working state.

---

## Summary of Completed Work

**Total extracted**: ~3,807 lines from src/klawed.c (37% of original 10,282 lines)
- Utility functions: ~1,087 lines (7 modules)
- Tool implementations: ~2,720 lines (7 modules)

**Commits**:
- `5066c6d` - Utility functions extraction
- `7ba03c5` - Tool implementations extraction

**Build status**: ✅ All changes compile successfully with no errors or warnings

**Next recommended steps**:
1. Thorough testing of extracted utilities and tools
2. Continue with conversation management extraction (Step 3)
3. Extract tool system core (registry, definitions, etc.) - Step 4
4. Progressively extract remaining modules following the migration strategy
