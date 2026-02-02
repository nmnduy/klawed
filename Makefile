# Makefile for Claude Code - Pure C Edition
#
# Voice Input Control:
#   make VOICE=1  - Enable voice input (requires ffmpeg + whisper.cpp submodule)
#   make VOICE=0  - Disable voice input (default)
#   make          - Disabled by default
#
# Memvid Integration (video-based memory storage):
#   make MEMVID=1 - Enable memvid support (requires memvid-ffi library)
#   make MEMVID=0 - Disable memvid support (default)
#   make          - Auto-detect if libmemvid_ffi is available
#   Memvid FFI is vendored in: vendors/memvid-ffi/memvid-ffi/

CC ?= gcc
CLANG = clang
# Detect OS for ncurses library linking
UNAME_S := $(shell uname -s)

CFLAGS = -Werror -Wall -Wextra -Wpedantic -Wformat=2 -Wconversion -Wshadow -Wcast-qual -Wcast-align -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wuninitialized -Warray-bounds -Wvla -Wwrite-strings -Wnull-dereference -Wimplicit-fallthrough -Wsign-conversion -Wsign-compare -Wfloat-equal -Wpointer-arith -Wbad-function-cast -Wstrict-overflow -Waggregate-return -Wredundant-decls -Wnested-externs -Winline -Wswitch-enum -Wswitch-default -Wenum-conversion -Wdisabled-optimization -O2 -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE=1 -Wno-aggregate-return -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE $(SANITIZERS)
DEBUG_CFLAGS = -Werror -Wall -Wextra -Wpedantic -Wformat=2 -Wconversion -Wshadow -Wcast-qual -Wcast-align -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wuninitialized -Warray-bounds -Wvla -Wwrite-strings -Wnull-dereference -Wimplicit-fallthrough -Wsign-conversion -Wsign-compare -Wfloat-equal -Wpointer-arith -Wbad-function-cast -Wstrict-overflow -Waggregate-return -Wredundant-decls -Wnested-externs -Winline -Wswitch-enum -Wswitch-default -Wenum-conversion -Wdisabled-optimization -g -O0 -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE=1 -fsanitize=address -fno-omit-frame-pointer -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE

# macOS needs _DARWIN_C_SOURCE for strlcpy/strlcat in string.h
ifeq ($(UNAME_S),Darwin)
    CFLAGS += -D_DARWIN_C_SOURCE
    DEBUG_CFLAGS += -D_DARWIN_C_SOURCE
endif

# Disable implicit rules to avoid conflicts with our pattern rules
# .SUFFIXES:

# Default ncurses library (Linux)
NCURSES_LIB = -lncursesw

ifeq ($(UNAME_S),Darwin)
    # macOS typically uses just ncurses (not ncursesw)
    NCURSES_LIB = -lncurses
endif

LDFLAGS = -lcurl -lpthread -lsqlite3 -lssl -lcrypto -lbsd -lm $(NCURSES_LIB) $(SANITIZERS) -Wl,-pie
ifeq ($(UNAME_S),Linux)
    LDFLAGS += -Wl,-z,relro -Wl,-z,now
endif
DEBUG_LDFLAGS = -lcurl -lpthread -lsqlite3 -lssl -lcrypto -lbsd -lm $(NCURSES_LIB) -fsanitize=address -Wl,-pie
ifeq ($(UNAME_S),Linux)
    DEBUG_LDFLAGS += -Wl,-z,relro -Wl,-z,now
endif

# Installation prefix (can be overridden via command line)
INSTALL_PREFIX ?= $(HOME)/.local
WEB_BROWSE_AGENT_DIR := tools/web_browse_agent
WEB_BROWSE_AGENT_BIN := $(WEB_BROWSE_AGENT_DIR)/web_browse_agent
# Get web_browse_agent version from its own version.go file (not klawed's git tags)
# The version.go file contains: const Version = "X.Y.Z"
WEB_BROWSE_AGENT_VERSION := $(shell \
	if [ -f "$(WEB_BROWSE_AGENT_DIR)/pkg/version/version.go" ]; then \
		sed -n 's/.*Version *= *"\([0-9.]*\)".*/\1/p' "$(WEB_BROWSE_AGENT_DIR)/pkg/version/version.go" | head -1; \
	elif [ -d "$(WEB_BROWSE_AGENT_DIR)/.git" ]; then \
		cd "$(WEB_BROWSE_AGENT_DIR)" && git describe --always --tags 2>/dev/null || git rev-parse --short HEAD 2>/dev/null || echo "unknown"; \
	else \
		echo "unknown"; \
	fi)

# Version management
VERSION_FILE := VERSION
VERSION := $(shell cat $(VERSION_FILE) 2>/dev/null || echo "unknown")
BUILD_DATE := $(shell date +%Y-%m-%d)
VERSION_H := src/version.h

ifeq ($(UNAME_S),Darwin)
    # macOS - check for Homebrew installation
    HOMEBREW_PREFIX := $(shell brew --prefix 2>/dev/null)
    ifeq ($(HOMEBREW_PREFIX),)
        # Fallback to common Homebrew paths if brew command not found
        ifeq ($(shell uname -m),arm64)
            # Apple Silicon (M1/M2/M3)
            HOMEBREW_PREFIX := /opt/homebrew
        else
            # Intel Mac
            HOMEBREW_PREFIX := /usr/local
        endif
    endif
    CFLAGS += -I$(HOMEBREW_PREFIX)/include
    DEBUG_CFLAGS += -I$(HOMEBREW_PREFIX)/include
    LDFLAGS += -L$(HOMEBREW_PREFIX)/lib -lcjson
    DEBUG_LDFLAGS += -L$(HOMEBREW_PREFIX)/lib -lcjson
else ifeq ($(UNAME_S),Linux)
    # Linux
    LDFLAGS += -lcjson
    DEBUG_LDFLAGS += -lcjson
endif

# Optional voice input with whisper.cpp (VOICE=auto|1|0)
# Version pinning: whisper.cpp is pinned to a stable release to ensure
# reproducible builds and avoid breaking changes. Update WHISPER_VERSION
# when upgrading, test thoroughly, and document any API changes needed.
VOICE ?= auto
WHISPER_VERSION = v1.8.2
# WHISPER_VERSION notes: v1.8.2 released 2024-10-15, tested and stable
WHISPER_DIR = external/whisper.cpp
WHISPER_BUILD_DIR = $(WHISPER_DIR)/build
WHISPER_LIB = $(WHISPER_BUILD_DIR)/src/libwhisper.a
WHISPER_GGML_LIB = $(WHISPER_BUILD_DIR)/ggml/src/libggml.a
WHISPER_GGML_BASE_LIB = $(WHISPER_BUILD_DIR)/ggml/src/libggml-base.a
WHISPER_GGML_CPU_LIB = $(WHISPER_BUILD_DIR)/ggml/src/libggml-cpu.a
WHISPER_GGML_BLAS_LIB = $(WHISPER_BUILD_DIR)/ggml/src/ggml-blas/libggml-blas.a
WHISPER_METAL_LIB = $(WHISPER_BUILD_DIR)/ggml/src/ggml-metal/libggml-metal.a
DEFAULT_MODEL = whisper_models/ggml-small.en.bin

# Voice input configuration (VOICE=auto|1|0)
# Default: disabled (use VOICE=1 to enable)
VOICE ?= auto

ifeq ($(VOICE),1)
    # Explicitly enable voice with whisper.cpp
    CFLAGS += -DHAVE_WHISPER=1 -I$(WHISPER_DIR)/include -I$(WHISPER_DIR)/ggml/include
    VOICE_INPUT_SRC = src/voice_input.c
else
    # Default/disabled: use stub implementation
    CFLAGS += -DDISABLE_VOICE=1
    VOICE_INPUT_SRC = src/voice_stub.c
endif

# Optional Memvid support for video-based memory storage (MEMVID=auto|1|0)
# Memvid FFI library path (uses vendored submodule)
# Static linking is used to avoid needing .so files at runtime in containers
MEMVID_FFI_DIR = $(CURDIR)/vendors/memvid-ffi
MEMVID_FFI_LIB = $(MEMVID_FFI_DIR)/target/release/libmemvid_ffi.a
MEMVID ?= auto

ifeq ($(MEMVID),1)
    # Explicitly enable memvid with static linking
    CFLAGS += -DHAVE_MEMVID=1
    DEBUG_CFLAGS += -DHAVE_MEMVID=1
    MEMVID_SRC = src/memvid.c
    # Static linking: directly link the .a file instead of -l flag
    MEMVID_LIBS = $(MEMVID_FFI_LIB)
    # Add Rust stdlib dependencies based on OS
    ifeq ($(UNAME_S),Darwin)
        MEMVID_LIBS += -framework Security -framework CoreFoundation
    else ifeq ($(UNAME_S),Linux)
        MEMVID_LIBS += -lpthread -ldl -lm
    endif
    LDFLAGS += $(MEMVID_LIBS)
    DEBUG_LDFLAGS += $(MEMVID_LIBS)
else ifeq ($(MEMVID),0)
    # Explicitly disable memvid
    CFLAGS += -DDISABLE_MEMVID=1
    DEBUG_CFLAGS += -DDISABLE_MEMVID=1
    MEMVID_SRC = src/memvid.c
    MEMVID_LIBS =
else
    # Default: auto-detect by checking if libmemvid_ffi exists
    ifeq ($(shell test -f $(MEMVID_FFI_LIB) && echo yes),yes)
        CFLAGS += -DHAVE_MEMVID=1
        DEBUG_CFLAGS += -DHAVE_MEMVID=1
        MEMVID_SRC = src/memvid.c
        # Static linking: directly link the .a file instead of -l flag
        MEMVID_LIBS = $(MEMVID_FFI_LIB)
        ifeq ($(UNAME_S),Darwin)
            MEMVID_LIBS += -framework Security -framework CoreFoundation
        else ifeq ($(UNAME_S),Linux)
            MEMVID_LIBS += -lpthread -ldl -lm
        endif
        LDFLAGS += $(MEMVID_LIBS)
        DEBUG_LDFLAGS += $(MEMVID_LIBS)
    else
        # Memvid not available, disable it
        CFLAGS += -DDISABLE_MEMVID=1
        DEBUG_CFLAGS += -DDISABLE_MEMVID=1
        MEMVID_SRC = src/memvid.c
        MEMVID_LIBS =
    endif
endif

BUILD_DIR = build

TARGET = $(BUILD_DIR)/klawed

# Special rule for sqlite_queue_test.o (needs -DTEST_BUILD)
# This rule is only used by test targets via BUILD_SQLITE_QUEUE_TEST_OBJ macro
# $(BUILD_DIR)/sqlite_queue_test.o: src/sqlite_queue.c
#	@mkdir -p $(BUILD_DIR)
#	@echo "Compiling SQLite queue for tests (TEST_BUILD)..."
#	$(CC) $(CFLAGS) -DTEST_BUILD -c -o $@ $<
TEST_EDIT_TARGET = $(BUILD_DIR)/test_edit
TEST_EDIT_REGEX_TARGET = $(BUILD_DIR)/test_edit_regex_enhancements
TEST_READ_TARGET = $(BUILD_DIR)/test_read
TEST_TODO_TARGET = $(BUILD_DIR)/test_todo
TEST_TODO_WRITE_TARGET = $(BUILD_DIR)/test_todo_write
TEST_COMPACTION_TARGET = $(BUILD_DIR)/test_compaction
TEST_TIMING_TARGET = $(BUILD_DIR)/test_tool_timing
TEST_PASTE_TARGET = $(BUILD_DIR)/test_paste
TEST_RETRY_JITTER_TARGET = $(BUILD_DIR)/test_retry_jitter
TEST_OPENAI_FORMAT_TARGET = $(BUILD_DIR)/test_openai_format
TEST_OPENAI_RESPONSES_TARGET = $(BUILD_DIR)/test_openai_responses
TEST_OPENAI_RESPONSE_PARSING_TARGET = $(BUILD_DIR)/test_openai_response_parsing
TEST_CONVERSATION_FREE_TARGET = $(BUILD_DIR)/test_conversation_free
TEST_MEMORY_NULL_FIX_TARGET = $(BUILD_DIR)/test_memory_null_fix
TEST_DUMP_UTILS_TARGET = $(BUILD_DIR)/test_dump_utils
TEST_WRITE_DIFF_INTEGRATION_TARGET = $(BUILD_DIR)/test_write_diff_integration
TEST_ROTATION_TARGET = $(BUILD_DIR)/test_rotation
TEST_FUNCTION_CONTEXT_TARGET = $(BUILD_DIR)/test_function_context
TEST_THREAD_CANCEL_TARGET = $(BUILD_DIR)/test_thread_cancel
TEST_AWS_CRED_ROTATION_TARGET = $(BUILD_DIR)/test_aws_credential_rotation
TEST_MESSAGE_QUEUE_TARGET = $(BUILD_DIR)/test_message_queue
TEST_EVENT_LOOP_TARGET = $(BUILD_DIR)/test_event_loop
TEST_TEXT_WRAP_TARGET = $(BUILD_DIR)/test_text_wrap
TEST_JSON_PARSING_TARGET = $(BUILD_DIR)/test_json_parsing
TEST_MCP_TARGET = $(BUILD_DIR)/test_mcp
TEST_WM_TARGET = $(BUILD_DIR)/test_window_manager
TEST_TOOL_RESULTS_REGRESSION_TARGET = $(BUILD_DIR)/test_tool_results_regression
TEST_DUPLICATE_TOOL_DETECTION_TARGET = $(BUILD_DIR)/test_duplicate_tool_detection
TEST_ARRAY_RESIZE_TARGET = $(BUILD_DIR)/test_array_resize
TEST_ARENA_TARGET = $(BUILD_DIR)/test_arena
TEST_CONFIG_TARGET = $(BUILD_DIR)/test_config
TEST_CONFIG_MERGE_TARGET = $(BUILD_DIR)/test_config_merge
TEST_MEMVID_TARGET = $(BUILD_DIR)/test_memvid
TEST_TOKEN_USAGE_TARGET = $(BUILD_DIR)/test_token_usage
TEST_HTTP_CLIENT_TARGET = $(BUILD_DIR)/test_http_client
TEST_SQLITE_QUEUE_TARGET = $(BUILD_DIR)/test_sqlite_queue
TEST_SQLITE_QUEUE_SEEDING_TARGET = $(BUILD_DIR)/test_sqlite_queue_seeding
TEST_PROVIDER_INIT_FROM_CONFIG_TARGET = $(BUILD_DIR)/test_provider_init_from_config
TEST_DATA_DIR_TARGET = $(BUILD_DIR)/test_data_dir
QUERY_TOOL = $(BUILD_DIR)/query_logs
MEMVID_REPL = $(BUILD_DIR)/memvid_repl
MEMVID_REPL_SRC = tools/memvid_repl.c
SRC = src/klawed.c
ARRAY_RESIZE_SRC = src/array_resize.c
ARRAY_RESIZE_OBJ = $(BUILD_DIR)/array_resize.o
LOGGER_SRC = src/logger.c
LOGGER_OBJ = $(BUILD_DIR)/logger.o
CONFIG_SRC = src/config.c
CONFIG_OBJ = $(BUILD_DIR)/config.o
PERSISTENCE_SRC = src/persistence.c
PERSISTENCE_OBJ = $(BUILD_DIR)/persistence.o
TOKEN_USAGE_DB_SRC = src/token_usage_db.c
TOKEN_USAGE_DB_OBJ = $(BUILD_DIR)/token_usage_db.o
TOKEN_USAGE_DB_MIGRATIONS_SRC = src/token_usage_db_migrations.c
TOKEN_USAGE_DB_MIGRATIONS_OBJ = $(BUILD_DIR)/token_usage_db_migrations.o
MIGRATIONS_SRC = src/migrations.c
MIGRATIONS_OBJ = $(BUILD_DIR)/migrations.o
COMMANDS_SRC = src/commands.c
COMMANDS_OBJ = $(BUILD_DIR)/commands.o
PROVIDER_COMMAND_SRC = src/provider_command.c
PROVIDER_COMMAND_OBJ = $(BUILD_DIR)/provider_command.o
CONFIG_COMMAND_SRC = src/config_command.c
CONFIG_COMMAND_OBJ = $(BUILD_DIR)/config_command.o
THEME_EXPLORER_SRC = src/theme_explorer.c
THEME_EXPLORER_OBJ = $(BUILD_DIR)/theme_explorer.o
HELP_MODAL_SRC = src/help_modal.c
HELP_MODAL_OBJ = $(BUILD_DIR)/help_modal.o
COMPLETION_SRC = src/completion.c
COMPLETION_OBJ = $(BUILD_DIR)/completion.o
TUI_SRC = src/tui.c
TUI_OBJ = $(BUILD_DIR)/tui.o
TUI_CORE_SRC = src/tui_core.c
TUI_CORE_OBJ = $(BUILD_DIR)/tui_core.o
TUI_CONVERSATION_SRC = src/tui_conversation.c
TUI_CONVERSATION_OBJ = $(BUILD_DIR)/tui_conversation.o
TUI_WINDOW_SRC = src/tui_window.c
TUI_WINDOW_OBJ = $(BUILD_DIR)/tui_window.o
TUI_RENDER_SRC = src/tui_render.c
TUI_RENDER_OBJ = $(BUILD_DIR)/tui_render.o
TUI_MODES_SRC = src/tui_modes.c
TUI_MODES_OBJ = $(BUILD_DIR)/tui_modes.o
TUI_INPUT_SRC = src/tui_input.c
TUI_INPUT_OBJ = $(BUILD_DIR)/tui_input.o
TUI_HISTORY_SRC = src/tui_history.c
TUI_HISTORY_OBJ = $(BUILD_DIR)/tui_history.o
TUI_COMPLETION_SRC = src/tui_completion.c
TUI_COMPLETION_OBJ = $(BUILD_DIR)/tui_completion.o
TUI_PASTE_SRC = src/tui_paste.c
TUI_PASTE_OBJ = $(BUILD_DIR)/tui_paste.o
TUI_SEARCH_SRC = src/tui_search.c
TUI_SEARCH_OBJ = $(BUILD_DIR)/tui_search.o
FILE_SEARCH_SRC = src/file_search.c
HISTORY_SEARCH_SRC = src/history_search.c
HISTORY_SEARCH_OBJ = $(BUILD_DIR)/history_search.o
FILE_SEARCH_OBJ = $(BUILD_DIR)/file_search.o
HISTORY_FILE_SRC = src/history_file.c
HISTORY_FILE_OBJ = $(BUILD_DIR)/history_file.o
TODO_SRC = src/todo.c
TODO_OBJ = $(BUILD_DIR)/todo.o
AWS_BEDROCK_SRC = src/aws_bedrock.c
AWS_BEDROCK_OBJ = $(BUILD_DIR)/aws_bedrock.o
BEDROCK_CONVERSE_SRC = src/bedrock_converse.c
BEDROCK_CONVERSE_OBJ = $(BUILD_DIR)/bedrock_converse.o
PROVIDER_SRC = src/provider.c
PROVIDER_OBJ = $(BUILD_DIR)/provider.o
PROVIDER_CONFIG_LOADER_SRC = src/provider_config_loader.c
PROVIDER_CONFIG_LOADER_OBJ = $(BUILD_DIR)/provider_config_loader.o
OPENAI_PROVIDER_SRC = src/openai_provider.c
OPENAI_PROVIDER_OBJ = $(BUILD_DIR)/openai_provider.o
OPENAI_MESSAGES_SRC = src/openai_messages.c
OPENAI_MESSAGES_OBJ = $(BUILD_DIR)/openai_messages.o
OPENAI_RESPONSES_SRC = src/openai_responses.c
OPENAI_RESPONSES_OBJ = $(BUILD_DIR)/openai_responses.o
BEDROCK_PROVIDER_SRC = src/bedrock_provider.c
BEDROCK_PROVIDER_OBJ = $(BUILD_DIR)/bedrock_provider.o
ANTHROPIC_PROVIDER_SRC = src/anthropic_provider.c
ANTHROPIC_PROVIDER_OBJ = $(BUILD_DIR)/anthropic_provider.o
DEEPSEEK_PROVIDER_SRC = src/deepseek_provider.c
DEEPSEEK_PROVIDER_OBJ = $(BUILD_DIR)/deepseek_provider.o
MOONSHOT_PROVIDER_SRC = src/moonshot_provider.c
MOONSHOT_PROVIDER_OBJ = $(BUILD_DIR)/moonshot_provider.o
BUILTIN_THEMES_SRC = src/builtin_themes.c
BUILTIN_THEMES_OBJ = $(BUILD_DIR)/builtin_themes.o
MESSAGE_QUEUE_SRC = src/message_queue.c
MESSAGE_QUEUE_OBJ = $(BUILD_DIR)/message_queue.o
AI_WORKER_SRC = src/ai_worker.c
AI_WORKER_OBJ = $(BUILD_DIR)/ai_worker.o
VOICE_INPUT_OBJ = $(BUILD_DIR)/voice_input.o
SQLITE_QUEUE_SRC = src/sqlite_queue.c
SQLITE_QUEUE_OBJ = $(BUILD_DIR)/sqlite_queue.o
MEMVID_OBJ = $(BUILD_DIR)/memvid.o
MEMORY_DB_SRC = src/memory_db.c
MEMORY_DB_OBJ = $(BUILD_DIR)/memory_db.o
COMPACTION_SRC = src/compaction.c
COMPACTION_OBJ = $(BUILD_DIR)/compaction.o
BACKGROUND_INIT_SRC = src/background_init.c
BACKGROUND_INIT_OBJ = $(BUILD_DIR)/background_init.o

MCP_SRC = src/mcp.c
MCP_OBJ = $(BUILD_DIR)/mcp.o
MCP_TEST_OBJ = $(BUILD_DIR)/mcp_test.o
WINDOW_MANAGER_SRC = src/window_manager.c
WINDOW_MANAGER_OBJ = $(BUILD_DIR)/window_manager.o
TOOL_UTILS_SRC = src/tool_utils.c
TOOL_UTILS_OBJ = $(BUILD_DIR)/tool_utils.o
PROCESS_UTILS_SRC = src/process_utils.c
PROCESS_UTILS_OBJ = $(BUILD_DIR)/process_utils.o
DUMP_UTILS_SRC = src/dump_utils.c
DUMP_UTILS_OBJ = $(BUILD_DIR)/dump_utils.o
SUBAGENT_MANAGER_SRC = src/subagent_manager.c
SUBAGENT_MANAGER_OBJ = $(BUILD_DIR)/subagent_manager.o
EXPLORE_TOOLS_SRC = src/explore_tools.c
EXPLORE_TOOLS_OBJ = $(BUILD_DIR)/explore_tools.o
EXPLORE_TOOLS_ALL_OBJ = $(BUILD_DIR)/explore_tools_all.o
BASE64_SRC = src/base64.c
HTTP_CLIENT_SRC = src/http_client.c
HTTP_CLIENT_OBJ = $(BUILD_DIR)/http_client.o
BASE64_OBJ = $(BUILD_DIR)/base64.o
SESSION_SRC = src/session.c
SESSION_OBJ = $(BUILD_DIR)/session.o
# Socket support removed - will be reimplemented with ZMQ
RETRY_LOGIC_SRC = src/retry_logic.c
RETRY_LOGIC_OBJ = $(BUILD_DIR)/retry_logic.o
# Data directory utilities
DATA_DIR_SRC = src/data_dir.c
DATA_DIR_OBJ = $(BUILD_DIR)/data_dir.o
# Utility modules
FILE_UTILS_SRC = src/util/file_utils.c
FILE_UTILS_OBJ = $(BUILD_DIR)/file_utils.o
STRING_UTILS_SRC = src/util/string_utils.c
STRING_UTILS_OBJ = $(BUILD_DIR)/string_utils.o
TIMESTAMP_UTILS_SRC = src/util/timestamp_utils.c
TIMESTAMP_UTILS_OBJ = $(BUILD_DIR)/timestamp_utils.o
FORMAT_UTILS_SRC = src/util/format_utils.c
FORMAT_UTILS_OBJ = $(BUILD_DIR)/format_utils.o
ENV_UTILS_SRC = src/util/env_utils.c
ENV_UTILS_OBJ = $(BUILD_DIR)/env_utils.o
OUTPUT_UTILS_SRC = src/util/output_utils.c
OUTPUT_UTILS_OBJ = $(BUILD_DIR)/output_utils.o
DIFF_UTILS_SRC = src/util/diff_utils.c
DIFF_UTILS_OBJ = $(BUILD_DIR)/diff_utils.o
# Tool modules
TOOL_SLEEP_SRC = src/tools/tool_sleep.c
TOOL_SLEEP_OBJ = $(BUILD_DIR)/tool_sleep.o
TOOL_TODO_SRC = src/tools/tool_todo.c
TOOL_TODO_OBJ = $(BUILD_DIR)/tool_todo.o
TOOL_IMAGE_SRC = src/tools/tool_image.c
TOOL_IMAGE_OBJ = $(BUILD_DIR)/tool_image.o
TOOL_SEARCH_SRC = src/tools/tool_search.c
TOOL_SEARCH_OBJ = $(BUILD_DIR)/tool_search.o
TOOL_FILESYSTEM_SRC = src/tools/tool_filesystem.c
TOOL_FILESYSTEM_OBJ = $(BUILD_DIR)/tool_filesystem.o
TOOL_BASH_SRC = src/tools/tool_bash.c
TOOL_BASH_OBJ = $(BUILD_DIR)/tool_bash.o
TOOL_SUBAGENT_SRC = src/tools/tool_subagent.c
TOOL_SUBAGENT_OBJ = $(BUILD_DIR)/tool_subagent.o
TOOL_REGISTRY_SRC = src/tools/tool_registry.c
TOOL_REGISTRY_OBJ = $(BUILD_DIR)/tool_registry.o
TOOL_EXECUTOR_SRC = src/tools/tool_executor.c
TOOL_EXECUTOR_OBJ = $(BUILD_DIR)/tool_executor.o
TOOL_DEFINITIONS_SRC = src/tools/tool_definitions.c
TOOL_DEFINITIONS_OBJ = $(BUILD_DIR)/tool_definitions.o

# Conversation modules
CONTENT_TYPES_SRC = src/conversation/content_types.c
CONTENT_TYPES_OBJ = $(BUILD_DIR)/content_types.o
CONVERSATION_STATE_SRC = src/conversation/conversation_state.c
CONVERSATION_STATE_OBJ = $(BUILD_DIR)/conversation_state.o
MESSAGE_BUILDER_SRC = src/conversation/message_builder.c
MESSAGE_BUILDER_OBJ = $(BUILD_DIR)/message_builder.o
MESSAGE_PARSER_SRC = src/conversation/message_parser.c
MESSAGE_PARSER_OBJ = $(BUILD_DIR)/message_parser.o

# Context modules
CONTEXT_SYSTEM_PROMPT_SRC = src/context/system_prompt.c
CONTEXT_SYSTEM_PROMPT_OBJ = $(BUILD_DIR)/system_prompt.o
CONTEXT_ENVIRONMENT_SRC = src/context/environment.c
CONTEXT_ENVIRONMENT_OBJ = $(BUILD_DIR)/environment.o
CONTEXT_KLAWED_MD_SRC = src/context/klawed_md.c
CONTEXT_KLAWED_MD_OBJ = $(BUILD_DIR)/klawed_md.o
CONTEXT_MEMORY_INJECTION_SRC = src/context/memory_injection.c
CONTEXT_MEMORY_INJECTION_OBJ = $(BUILD_DIR)/memory_injection.o

# UI modules
UI_OUTPUT_SRC = src/ui/ui_output.c
UI_OUTPUT_OBJ = $(BUILD_DIR)/ui_output.o
PRINT_HELPERS_SRC = src/ui/print_helpers.c
PRINT_HELPERS_OBJ = $(BUILD_DIR)/print_helpers.o
TOOL_OUTPUT_DISPLAY_SRC = src/ui/tool_output_display.c
TOOL_OUTPUT_DISPLAY_OBJ = $(BUILD_DIR)/tool_output_display.o

# Session modules
SESSION_TOKEN_USAGE_SRC = src/session/token_usage.c
SESSION_TOKEN_USAGE_OBJ = $(BUILD_DIR)/session_token_usage.o
SESSION_PERSISTENCE_SRC = src/session/session_persistence.c
SESSION_PERSISTENCE_OBJ = $(BUILD_DIR)/session_persistence.o

# API layer modules
API_RESPONSE_SRC = src/api/api_response.c
API_RESPONSE_OBJ = $(BUILD_DIR)/api_response.o
API_BUILDER_SRC = src/api/api_builder.c
API_BUILDER_OBJ = $(BUILD_DIR)/api_builder.o
API_CLIENT_SRC = src/api/api_client.c
API_CLIENT_OBJ = $(BUILD_DIR)/api_client.o

# Interactive mode modules
INTERACTIVE_LOOP_SRC = src/interactive/interactive_loop.c
INTERACTIVE_LOOP_OBJ = $(BUILD_DIR)/interactive_loop.o
INPUT_HANDLER_SRC = src/interactive/input_handler.c
INPUT_HANDLER_OBJ = $(BUILD_DIR)/input_handler.o
RESPONSE_PROCESSOR_SRC = src/interactive/response_processor.c
RESPONSE_PROCESSOR_OBJ = $(BUILD_DIR)/response_processor.o
COMMAND_DISPATCH_SRC = src/interactive/command_dispatch.c
COMMAND_DISPATCH_OBJ = $(BUILD_DIR)/command_dispatch.o

# Oneshot mode modules
ONESHOT_MODE_SRC = src/oneshot/oneshot_mode.c
ONESHOT_MODE_OBJ = $(BUILD_DIR)/oneshot_mode.o
ONESHOT_PROCESSOR_SRC = src/oneshot/oneshot_processor.c
ONESHOT_PROCESSOR_OBJ = $(BUILD_DIR)/oneshot_processor.o
ONESHOT_OUTPUT_SRC = src/oneshot/oneshot_output.c
ONESHOT_OUTPUT_OBJ = $(BUILD_DIR)/oneshot_output.o

TEST_EDIT_SRC = tests/test_edit.c
TEST_EDIT_REGEX_SRC = tests/test_edit_regex_enhancements.c
TEST_READ_SRC = tests/test_read.c
TEST_TODO_SRC = tests/test_todo.c
TEST_TODO_WRITE_SRC = tests/test_todo_write.c
TEST_COMPACTION_SRC = tests/test_compaction.c
TEST_PASTE_SRC = tests/test_paste.c
TEST_RETRY_JITTER_SRC = tests/test_retry_jitter.c
TEST_OPENAI_FORMAT_SRC = tests/test_openai_format.c
TEST_OPENAI_RESPONSES_SRC = tests/test_openai_responses.c
TEST_OPENAI_RESPONSE_PARSING_SRC = tests/test_openai_response_parsing.c
TEST_CONVERSATION_FREE_SRC = tests/test_conversation_free.c
TEST_MEMORY_NULL_FIX_SRC = tests/test_memory_null_fix.c
TEST_WRITE_DIFF_INTEGRATION_SRC = tests/test_write_diff_integration.c
TEST_ROTATION_SRC = tests/test_rotation.c
TEST_FUNCTION_CONTEXT_SRC = tests/test_function_context.c
TEST_THREAD_CANCEL_SRC = tests/test_thread_cancel.c
TEST_AWS_CRED_ROTATION_SRC = tests/test_aws_credential_rotation.c
TEST_MESSAGE_QUEUE_SRC = tests/test_message_queue.c
TEST_EVENT_LOOP_SRC = tests/test_event_loop.c
TEST_JSON_PARSING_SRC = tests/test_json_parsing.c
TEST_STUBS_SRC = tests/test_stubs.c
TEST_STUBS_OBJ = $(BUILD_DIR)/test_stubs.o
TEST_MCP_SRC = tests/test_mcp.c
TEST_MCP_IMAGE_SRC = tests/test_mcp_image.c
TEST_MCP_IMAGE_TARGET = $(BUILD_DIR)/test_mcp_image
TEST_WM_SRC = tests/test_window_manager.c
TEST_TOOL_RESULTS_REGRESSION_SRC = tests/test_tool_results_regression.c
TEST_BASE64_SRC = tests/test_base64.c
TEST_BASE64_TARGET = $(BUILD_DIR)/test_base64
TEST_CANCEL_FLOW_TARGET = $(BUILD_DIR)/test_cancel_flow
TEST_BASH_SUMMARY_TARGET = $(BUILD_DIR)/test_bash_summary
TEST_BASH_SUMMARY_SRC = tests/test_bash_summary.c
TEST_BASH_TIMEOUT_TARGET = $(BUILD_DIR)/test_bash_timeout
TEST_BASH_STDERR_TARGET = $(BUILD_DIR)/test_bash_stderr
TEST_BASH_TRUNCATION_TARGET = $(BUILD_DIR)/test_bash_truncation
TEST_HISTORY_FILE_TARGET = $(BUILD_DIR)/test_history_file
TEST_TUI_INPUT_BUFFER_TARGET = $(BUILD_DIR)/test_tui_input_buffer
TEST_TUI_AUTO_SCROLL_TARGET = $(BUILD_DIR)/test_tui_auto_scroll
TEST_TOOL_DETAILS_TARGET = $(BUILD_DIR)/test_tool_details_simple
TEST_BASH_TIMEOUT_SRC = tests/test_bash_timeout.c
TEST_BASH_STDERR_SRC = tests/test_bash_stderr.c
TEST_BASH_TRUNCATION_SRC = tests/test_bash_truncation.c
TEST_HISTORY_FILE_SRC = tests/test_history_file.c
TEST_TUI_INPUT_BUFFER_SRC = tests/test_tui_input_buffer.c
TEST_TUI_AUTO_SCROLL_SRC = tests/test_tui_auto_scroll.c
TEST_TOOL_DETAILS_SRC = tests/test_tool_details_simple.c
TEST_DUPLICATE_TOOL_DETECTION_SRC = tests/test_duplicate_tool_detection.c
TEST_ARRAY_RESIZE_SRC = tests/test_array_resize.c
TEST_ARENA_SRC = tests/test_arena.c
TEST_MEMVID_SRC = tests/test_memvid.c
TEST_TOKEN_USAGE_SRC = tests/test_token_usage.c
TEST_TOKEN_USAGE_COMPREHENSIVE_SRC = tests/test_token_usage_comprehensive.c
TEST_TOKEN_USAGE_SESSION_TOTALS_SRC = tests/test_token_usage_session_totals.c
TEST_HTTP_CLIENT_SRC = tests/test_http_client.c
TEST_SQLITE_QUEUE_SRC = tests/test_sqlite_queue.c
TEST_SQLITE_QUEUE_SEEDING_SRC = tests/test_sqlite_queue_seeding.c
TEST_DUMP_UTILS_SRC = tests/test_dump_utils.c
TEST_FILE_SEARCH_SRC = tests/test_file_search.c
TEST_FILE_SEARCH_TARGET = $(BUILD_DIR)/test_file_search

.PHONY: all clean check-deps install install-web-browse-agent test test-edit test-read test-todo test-todo-write test-compaction test-paste test-retry-jitter test-openai-format test-openai-responses test-openai-response-parsing test-memory-null-fix test-write-diff-integration test-rotation test-function-context test-thread-cancel test-aws-cred-rotation test-message-queue test-event-loop test-wrap test-mcp test-mcp-image test-bash-summary test-bash-timeout test-bash-stderr test-bash-truncation test-tool-results-regression test-tool-details test-duplicate-tool-detection test-array-resize test-token-usage test-token-usage-comprehensive test-http-client test-sqlite-queue test-file-search test-provider-init-from-config query-tool memvid-repl debug analyze sanitize-ub sanitize-all sanitize-leak valgrind memscan comprehensive-scan clang-tidy cppcheck flawfinder version show-version update-version bump-version bump-patch bump-minor-version build clang ci-test ci-gcc ci-clang ci-gcc-sanitize ci-clang-sanitize ci-all fmt-whitespace memvid-ffi memvid-ffi-clean check-memvid test-memvid docker-sandbox docker-push docker-rotate

all: check-deps $(TARGET)
TEST_TOKEN_USAGE_COMPREHENSIVE_SRC = tests/test_token_usage_comprehensive.c
TEST_TOKEN_USAGE_SESSION_TOTALS_TARGET = $(BUILD_DIR)/test_token_usage_session_totals
TEST_TOKEN_USAGE_SESSION_TOTALS_SRC = tests/test_token_usage_session_totals.c

build: check-deps $(TARGET)

clang: check-deps $(BUILD_DIR)/klawed-clang

debug: check-deps $(BUILD_DIR)/klawed-debug

query-tool: check-deps $(QUERY_TOOL)

memvid-repl: check-deps $(MEMVID_REPL)

test: $(TARGET) test-edit test-read test-todo test-paste test-json-parsing test-timing test-openai-format test-openai-responses test-openai-response-parsing test-memory-null-fix test-dump-utils test-write-diff-integration test-rotation test-function-context test-thread-cancel test-aws-cred-rotation test-message-queue test-wrap test-mcp test-mcp-image test-wm test-bash-summary test-bash-timeout test-bash-stderr test-bash-truncation test-cancel-flow test-tool-results-regression test-base64 test-history-file test-tui-input-buffer test-tui-auto-scroll test-tool-details test-array-resize test-arena test-config test-config-merge test-token-usage test-token-usage-comprehensive test-token-usage-session-totals test-http-client test-sqlite-queue-seeding test-file-search test-provider-init-from-config test-provider-init

test-edit: check-deps $(TARGET) $(TEST_EDIT_TARGET)
	@echo ""
	@echo "Running Edit tool tests..."
	@echo ""
	@./$(TEST_EDIT_TARGET)

test-edit-regex: check-deps $(TEST_EDIT_REGEX_TARGET)
	@echo ""
	@echo "Running Edit tool regex enhancement tests..."
	@echo ""
	@./$(TEST_EDIT_REGEX_TARGET)

test-read: check-deps $(TEST_READ_TARGET)
	@echo ""
	@echo "Running Read tool tests..."
	@echo ""
	@./$(TEST_READ_TARGET)

test-todo: check-deps $(TEST_TODO_TARGET)
	@echo ""
	@echo "Running TODO list tests..."
	@echo ""
	@./$(TEST_TODO_TARGET)

test-todo-write: check-deps $(TEST_TODO_WRITE_TARGET)
	@echo ""
	@echo "Running TodoWrite tool tests..."
	@echo ""
	@./$(TEST_TODO_WRITE_TARGET)

test-compaction: check-deps $(TEST_COMPACTION_TARGET)
	@echo ""
	@echo "Running compaction tests..."
	@echo ""
	@./$(TEST_COMPACTION_TARGET)

test-tool-results-regression: check-deps $(TEST_TOOL_RESULTS_REGRESSION_TARGET)
	@echo ""
	@echo "Running tool results regression tests..."
	@echo ""
	@./$(TEST_TOOL_RESULTS_REGRESSION_TARGET)

test-paste: check-deps $(TEST_PASTE_TARGET)
	@echo ""
	@echo "Running Paste Handler tests..."
	@echo ""
	@./$(TEST_PASTE_TARGET)

test-json-parsing: check-deps $(TEST_JSON_PARSING_TARGET)
	@echo ""
	@echo "Running JSON parsing tests..."
	@echo ""
	@./$(TEST_JSON_PARSING_TARGET)

test-retry-jitter: check-deps $(TEST_RETRY_JITTER_TARGET)
	@echo ""
	@echo "Running Retry Jitter tests..."
	@echo ""
	@./$(TEST_RETRY_JITTER_TARGET)

test-timing: check-deps $(TEST_TIMING_TARGET)
	@echo ""
	@echo "Running tool timing tests..."
	@echo ""
	@./$(TEST_TIMING_TARGET)

test-openai-format: check-deps $(TEST_OPENAI_FORMAT_TARGET)
	@echo ""
	@echo "Running OpenAI message format validation tests..."
	@echo ""
	@./$(TEST_OPENAI_FORMAT_TARGET)

test-openai-responses: check-deps $(TEST_OPENAI_RESPONSES_TARGET)
	@echo ""
	@echo "Running OpenAI Responses API parsing tests..."
	@echo ""
	@./$(TEST_OPENAI_RESPONSES_TARGET)

test-openai-response-parsing: check-deps $(TEST_OPENAI_RESPONSE_PARSING_TARGET)
	@echo ""
	@echo "Running OpenAI response parsing regression tests..."
	@echo ""
	@./$(TEST_OPENAI_RESPONSE_PARSING_TARGET)

test-conversation-free: check-deps $(TEST_CONVERSATION_FREE_TARGET)
	@echo ""
	@echo "Running conversation memory management tests..."
	@echo ""
	@./$(TEST_CONVERSATION_FREE_TARGET)

test-memory-null-fix: check-deps $(TEST_MEMORY_NULL_FIX_TARGET)
	@echo ""
	@echo "Running memory NULL fix regression tests..."
	@echo ""
	@./$(TEST_MEMORY_NULL_FIX_TARGET)

test-dump-utils: check-deps $(TEST_DUMP_UTILS_TARGET)
	@echo ""
	@echo "Running dump utils tests..."
	@echo ""
	@./$(TEST_DUMP_UTILS_TARGET)

test-write-diff-integration: check-deps $(TEST_WRITE_DIFF_INTEGRATION_TARGET)
	@echo ""
	@echo "Running Write tool diff integration tests..."
	@echo ""
	@./$(TEST_WRITE_DIFF_INTEGRATION_TARGET)

test-rotation: check-deps $(TEST_ROTATION_TARGET)
	@echo ""
	@echo "Running database rotation tests..."
	@echo ""
	@./$(TEST_ROTATION_TARGET)





test-thread-cancel: check-deps $(TEST_THREAD_CANCEL_TARGET)
	@echo ""
	@echo "Running Thread Cancellation tests..."
	@echo ""
	@./$(TEST_THREAD_CANCEL_TARGET)

test-aws-cred-rotation: check-deps $(TEST_AWS_CRED_ROTATION_TARGET)
	@echo ""
	@echo "Running AWS Credential Rotation tests..."
	@echo ""
	@./$(TEST_AWS_CRED_ROTATION_TARGET)

test-message-queue: check-deps $(TEST_MESSAGE_QUEUE_TARGET)
	@echo ""
	@echo "Running Message Queue tests..."
	@echo ""
	@./$(TEST_MESSAGE_QUEUE_TARGET)

test-event-loop: check-deps $(TEST_EVENT_LOOP_TARGET)
	@echo ""
	@echo "Running Event Loop test (interactive)..."
	@echo "Type some text and press Enter. Type 'quit' to exit."
	@echo ""
	@./$(TEST_EVENT_LOOP_TARGET)

test-wrap: check-deps $(TEST_TEXT_WRAP_TARGET)
	@echo ""
	@echo "Running Text Wrapping tests..."
	@echo ""
	@./$(TEST_TEXT_WRAP_TARGET)

test-mcp: check-deps $(TEST_MCP_TARGET)
	@echo ""
	@echo "Running MCP integration tests..."
	@echo ""
	@./$(TEST_MCP_TARGET)

test-mcp-image: check-deps $(TEST_MCP_IMAGE_TARGET)
	@echo ""
	@echo "Running MCP image content handling tests..."
	@echo ""
	@./$(TEST_MCP_IMAGE_TARGET)

test-wm: check-deps $(TEST_WM_TARGET)
	@echo ""
	@echo "Running Window Manager tests..."
	@echo ""
	@./$(TEST_WM_TARGET)

test-bash-timeout: check-deps $(TEST_BASH_TIMEOUT_TARGET)
	@echo ""
	@echo "Running Bash Timeout tests..."
	@echo ""
	@./$(TEST_BASH_TIMEOUT_TARGET)

test-bash-stderr: check-deps $(TEST_BASH_STDERR_TARGET)
	@echo ""
	@echo "Running Bash Stderr Output Fix tests..."
	@echo ""
	@./$(TEST_BASH_STDERR_TARGET)

test-bash-truncation: check-deps $(TEST_BASH_TRUNCATION_TARGET)
	@echo ""
	@echo "Running Bash Output Truncation tests..."
	@echo ""
	@./$(TEST_BASH_TRUNCATION_TARGET)

test-base64: check-deps $(TEST_BASE64_TARGET)
	@echo ""
	@echo "Running Base64 encoding/decoding tests..."
	@echo ""
	@./$(TEST_BASE64_TARGET)

test-history-file: check-deps $(TEST_HISTORY_FILE_TARGET)
	@echo ""
	@echo "Running History File tests..."
	@echo ""
	@./$(TEST_HISTORY_FILE_TARGET)

test-tui-input-buffer: check-deps $(TEST_TUI_INPUT_BUFFER_TARGET)
	@echo ""
	@echo "Running TUI Input Buffer tests..."
	@echo ""
	@./$(TEST_TUI_INPUT_BUFFER_TARGET)

test-tui-auto-scroll: check-deps $(TEST_TUI_AUTO_SCROLL_TARGET)
	@echo ""
	@echo "Running TUI Auto-Scroll tests..."
	@echo ""
	@./$(TEST_TUI_AUTO_SCROLL_TARGET)

test-tool-details: check-deps $(TEST_TOOL_DETAILS_TARGET)
	@echo ""
	@echo "Running Tool Details Display tests..."
	@echo ""
	@./$(TEST_TOOL_DETAILS_TARGET)

test-duplicate-tool-detection: check-deps $(TEST_DUPLICATE_TOOL_DETECTION_TARGET)
	@echo ""
	@echo "Running Duplicate Tool Detection tests..."
	@echo ""
	@./$(TEST_DUPLICATE_TOOL_DETECTION_TARGET)

test-array-resize: check-deps $(TEST_ARRAY_RESIZE_TARGET)
	@echo ""
	@echo "Running Array Resize tests..."
	@echo ""
	@./$(TEST_ARRAY_RESIZE_TARGET)

test-arena: check-deps $(TEST_ARENA_TARGET)
	@echo ""
	@echo "Running Arena Allocator tests..."
	@echo ""
	@./$(TEST_ARENA_TARGET)

test-config: check-deps $(TEST_CONFIG_TARGET)
	@echo ""
	@echo "Running Config tests..."
	@echo ""
	@./$(TEST_CONFIG_TARGET)
	@rm -f .klawed/config.json

test-config-merge: check-deps $(TEST_CONFIG_MERGE_TARGET)
	@echo ""
	@echo "Running Config merge tests..."
	@echo ""
	@./$(TEST_CONFIG_MERGE_TARGET)
	@rm -f .klawed/config.json

test-provider-init-from-config: check-deps $(TEST_PROVIDER_INIT_FROM_CONFIG_TARGET)
	@echo ""
	@echo "Running provider_init_from_config tests..."
	@echo ""
	@./$(TEST_PROVIDER_INIT_FROM_CONFIG_TARGET)

test-data-dir: check-deps $(TEST_DATA_DIR_TARGET)
	@echo ""
	@echo "Running Data Directory tests..."
	@echo ""
	@./$(TEST_DATA_DIR_TARGET)

test-memvid: check-deps $(TEST_MEMVID_TARGET)
	@echo ""
	@echo "Running Memvid Memory Tool tests..."
	@echo ""
	@./$(TEST_MEMVID_TARGET)

test-token-usage: check-deps $(TEST_TOKEN_USAGE_TARGET)
	@echo ""
	@echo "Running Token Usage tests..."
	@echo ""
	@./$(TEST_TOKEN_USAGE_TARGET)

test-token-usage-session-totals: check-deps $(TEST_TOKEN_USAGE_SESSION_TOTALS_TARGET)
	@echo ""
	@echo "Running Token Usage Session Totals tests..."
	@echo ""
	@./$(TEST_TOKEN_USAGE_SESSION_TOTALS_TARGET)

test-http-client: check-deps $(TEST_HTTP_CLIENT_TARGET)
	@echo ""
	@echo "Running HTTP Client tests..."
	@echo ""
	@./$(TEST_HTTP_CLIENT_TARGET)



test-sqlite-queue: check-deps $(TEST_SQLITE_QUEUE_TARGET)
	@echo ""
	@echo "Running SQLite Queue tests..."
	@echo ""
	@./$(TEST_SQLITE_QUEUE_TARGET)

test-sqlite-queue-seeding: check-deps $(TARGET) $(TEST_SQLITE_QUEUE_SEEDING_TARGET)
	@echo ""
	@echo "Running SQLite Queue Seeding tests..."
	@echo ""
	@./$(TEST_SQLITE_QUEUE_SEEDING_TARGET)

test-file-search: check-deps $(TEST_FILE_SEARCH_TARGET)
	@echo ""
	@echo "Running File Search fuzzy matching tests..."
	@echo ""
	@./$(TEST_FILE_SEARCH_TARGET)

# Socket test removed

$(TARGET): $(SRC) $(LOGGER_OBJ) $(CONFIG_OBJ) $(PERSISTENCE_OBJ) $(TOKEN_USAGE_DB_OBJ) $(TOKEN_USAGE_DB_MIGRATIONS_OBJ) $(MIGRATIONS_OBJ) $(COMMANDS_OBJ) $(THEME_EXPLORER_OBJ) $(HELP_MODAL_OBJ) $(COMPLETION_OBJ) $(TUI_OBJ) $(TUI_CORE_OBJ) $(TUI_CONVERSATION_OBJ) $(TUI_WINDOW_OBJ) $(TUI_RENDER_OBJ) $(TUI_MODES_OBJ) $(TUI_INPUT_OBJ) $(TUI_HISTORY_OBJ) $(TUI_COMPLETION_OBJ) $(TUI_PASTE_OBJ) $(TUI_SEARCH_OBJ) $(FILE_SEARCH_OBJ) $(HISTORY_SEARCH_OBJ) $(WINDOW_MANAGER_OBJ) $(TODO_OBJ) $(AWS_BEDROCK_OBJ) $(BEDROCK_CONVERSE_OBJ) $(PROVIDER_OBJ) $(PROVIDER_CONFIG_LOADER_OBJ) $(OPENAI_PROVIDER_OBJ) $(OPENAI_MESSAGES_OBJ) $(OPENAI_RESPONSES_OBJ) $(BEDROCK_PROVIDER_OBJ) $(ANTHROPIC_PROVIDER_OBJ) $(DEEPSEEK_PROVIDER_OBJ) $(MOONSHOT_PROVIDER_OBJ) $(BUILTIN_THEMES_OBJ) $(MESSAGE_QUEUE_OBJ) $(AI_WORKER_OBJ) $(VOICE_INPUT_OBJ) $(SQLITE_QUEUE_OBJ) $(MCP_OBJ) $(TOOL_UTILS_OBJ) $(PROCESS_UTILS_OBJ) $(DUMP_UTILS_OBJ) $(SUBAGENT_MANAGER_OBJ) $(EXPLORE_TOOLS_OBJ) $(BASE64_OBJ) $(HISTORY_FILE_OBJ) $(ARRAY_RESIZE_OBJ) $(HTTP_CLIENT_OBJ) $(SESSION_OBJ) $(RETRY_LOGIC_OBJ) $(MEMVID_OBJ) $(MEMORY_DB_OBJ) $(COMPACTION_OBJ) $(BACKGROUND_INIT_OBJ) $(DATA_DIR_OBJ) $(FILE_UTILS_OBJ) $(STRING_UTILS_OBJ) $(TIMESTAMP_UTILS_OBJ) $(FORMAT_UTILS_OBJ) $(ENV_UTILS_OBJ) $(OUTPUT_UTILS_OBJ) $(DIFF_UTILS_OBJ) $(TOOL_SLEEP_OBJ) $(TOOL_TODO_OBJ) $(TOOL_IMAGE_OBJ) $(TOOL_SEARCH_OBJ) $(TOOL_FILESYSTEM_OBJ) $(TOOL_BASH_OBJ) $(TOOL_SUBAGENT_OBJ) $(TOOL_REGISTRY_OBJ) $(TOOL_EXECUTOR_OBJ) $(TOOL_DEFINITIONS_OBJ) $(CONTENT_TYPES_OBJ) $(CONVERSATION_STATE_OBJ) $(MESSAGE_BUILDER_OBJ) $(MESSAGE_PARSER_OBJ) $(CONTEXT_SYSTEM_PROMPT_OBJ) $(CONTEXT_ENVIRONMENT_OBJ) $(CONTEXT_KLAWED_MD_OBJ) $(CONTEXT_MEMORY_INJECTION_OBJ) $(UI_OUTPUT_OBJ) $(PRINT_HELPERS_OBJ) $(TOOL_OUTPUT_DISPLAY_OBJ) $(SESSION_TOKEN_USAGE_OBJ) $(SESSION_PERSISTENCE_OBJ) $(API_RESPONSE_OBJ) $(API_BUILDER_OBJ) $(API_CLIENT_OBJ) $(INTERACTIVE_LOOP_OBJ) $(INPUT_HANDLER_OBJ) $(RESPONSE_PROCESSOR_OBJ) $(COMMAND_DISPATCH_OBJ) $(ONESHOT_MODE_OBJ) $(ONESHOT_PROCESSOR_OBJ) $(ONESHOT_OUTPUT_OBJ) $(PROVIDER_COMMAND_OBJ) $(CONFIG_COMMAND_OBJ) $(VERSION_H)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LOGGER_OBJ) $(CONFIG_OBJ) $(PERSISTENCE_OBJ) $(TOKEN_USAGE_DB_OBJ) $(TOKEN_USAGE_DB_MIGRATIONS_OBJ) $(MIGRATIONS_OBJ) $(COMMANDS_OBJ) $(THEME_EXPLORER_OBJ) $(HELP_MODAL_OBJ) $(COMPLETION_OBJ) $(TUI_OBJ) $(TUI_CORE_OBJ) $(TUI_CONVERSATION_OBJ) $(TUI_WINDOW_OBJ) $(TUI_RENDER_OBJ) $(TUI_MODES_OBJ) $(TUI_INPUT_OBJ) $(TUI_HISTORY_OBJ) $(TUI_COMPLETION_OBJ) $(TUI_PASTE_OBJ) $(TUI_SEARCH_OBJ) $(FILE_SEARCH_OBJ) $(HISTORY_SEARCH_OBJ) $(WINDOW_MANAGER_OBJ) $(TODO_OBJ) $(AWS_BEDROCK_OBJ) $(BEDROCK_CONVERSE_OBJ) $(PROVIDER_OBJ) $(PROVIDER_CONFIG_LOADER_OBJ) $(OPENAI_PROVIDER_OBJ) $(OPENAI_MESSAGES_OBJ) $(OPENAI_RESPONSES_OBJ) $(BEDROCK_PROVIDER_OBJ) $(ANTHROPIC_PROVIDER_OBJ) $(DEEPSEEK_PROVIDER_OBJ) $(MOONSHOT_PROVIDER_OBJ) $(BUILTIN_THEMES_OBJ) $(MESSAGE_QUEUE_OBJ) $(AI_WORKER_OBJ) $(VOICE_INPUT_OBJ) $(SQLITE_QUEUE_OBJ) $(MCP_OBJ) $(TOOL_UTILS_OBJ) $(PROCESS_UTILS_OBJ) $(DUMP_UTILS_OBJ) $(SUBAGENT_MANAGER_OBJ) $(EXPLORE_TOOLS_OBJ) $(BASE64_OBJ) $(HISTORY_FILE_OBJ) $(ARRAY_RESIZE_OBJ) $(HTTP_CLIENT_OBJ) $(SESSION_OBJ) $(RETRY_LOGIC_OBJ) $(MEMVID_OBJ) $(MEMORY_DB_OBJ) $(COMPACTION_OBJ) $(BACKGROUND_INIT_OBJ) $(DATA_DIR_OBJ) $(FILE_UTILS_OBJ) $(STRING_UTILS_OBJ) $(TIMESTAMP_UTILS_OBJ) $(FORMAT_UTILS_OBJ) $(ENV_UTILS_OBJ) $(OUTPUT_UTILS_OBJ) $(DIFF_UTILS_OBJ) $(TOOL_SLEEP_OBJ) $(TOOL_TODO_OBJ) $(TOOL_IMAGE_OBJ) $(TOOL_SEARCH_OBJ) $(TOOL_FILESYSTEM_OBJ) $(TOOL_BASH_OBJ) $(TOOL_SUBAGENT_OBJ) $(TOOL_REGISTRY_OBJ) $(TOOL_EXECUTOR_OBJ) $(TOOL_DEFINITIONS_OBJ) $(CONTENT_TYPES_OBJ) $(CONVERSATION_STATE_OBJ) $(MESSAGE_BUILDER_OBJ) $(MESSAGE_PARSER_OBJ) $(CONTEXT_SYSTEM_PROMPT_OBJ) $(CONTEXT_ENVIRONMENT_OBJ) $(CONTEXT_KLAWED_MD_OBJ) $(CONTEXT_MEMORY_INJECTION_OBJ) $(UI_OUTPUT_OBJ) $(PRINT_HELPERS_OBJ) $(TOOL_OUTPUT_DISPLAY_OBJ) $(SESSION_TOKEN_USAGE_OBJ) $(SESSION_PERSISTENCE_OBJ) $(API_RESPONSE_OBJ) $(API_BUILDER_OBJ) $(API_CLIENT_OBJ) $(INTERACTIVE_LOOP_OBJ) $(INPUT_HANDLER_OBJ) $(RESPONSE_PROCESSOR_OBJ) $(COMMAND_DISPATCH_OBJ) $(ONESHOT_MODE_OBJ) $(ONESHOT_PROCESSOR_OBJ) $(ONESHOT_OUTPUT_OBJ) $(PROVIDER_COMMAND_OBJ) $(CONFIG_COMMAND_OBJ) $(LDFLAGS)
	@echo ""
	@echo "✓ Build successful!"
	@echo "Version: $(VERSION)"
	@echo "Run: ./$(TARGET) \"your prompt here\""
	@echo ""

# Build HTTP client object
$(BUILD_DIR)/http_client.o: src/http_client.c src/http_client.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/http_client.o src/http_client.c

# Build OpenAI responses object
$(BUILD_DIR)/openai_responses.o: $(OPENAI_RESPONSES_SRC) src/openai_responses.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/openai_responses.o $(OPENAI_RESPONSES_SRC)

# Build dump utils object
$(BUILD_DIR)/dump_utils.o: $(DUMP_UTILS_SRC) src/dump_utils.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/dump_utils.o $(DUMP_UTILS_SRC)

# Build session object
$(BUILD_DIR)/session.o: src/session.c src/session.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/session.o src/session.c

$(BUILD_DIR)/retry_logic.o: src/retry_logic.c src/retry_logic.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/retry_logic.o src/retry_logic.c

# Build provider command object
$(BUILD_DIR)/provider_command.o: $(PROVIDER_COMMAND_SRC) src/provider_command.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/provider_command.o $(PROVIDER_COMMAND_SRC)

# Build config command object
$(BUILD_DIR)/config_command.o: $(CONFIG_COMMAND_SRC) src/config_command.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/config_command.o $(CONFIG_COMMAND_SRC)

# Build SQLite queue object
$(BUILD_DIR)/sqlite_queue.o: $(SQLITE_QUEUE_SRC) src/sqlite_queue.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/sqlite_queue.o $(SQLITE_QUEUE_SRC)

# Build memvid object (video-based memory storage)
# MEMVID_SRC can be src/memvid.c (real) or src/memvid_stub.c (stub)
$(BUILD_DIR)/memvid.o: $(MEMVID_SRC)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/memvid.o $(MEMVID_SRC)

# Build memory_db object (SQLite-based memory storage with FTS5)
$(BUILD_DIR)/memory_db.o: $(MEMORY_DB_SRC) src/memory_db.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/memory_db.o $(MEMORY_DB_SRC)

# Build compaction object (context compaction with memvid)
$(BUILD_DIR)/compaction.o: $(COMPACTION_SRC) src/compaction.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/compaction.o $(COMPACTION_SRC)

# Build background_init object (async resource loading)
$(BUILD_DIR)/background_init.o: $(BACKGROUND_INIT_SRC) src/background_init.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/background_init.o $(BACKGROUND_INIT_SRC)

# Build data_dir object
$(BUILD_DIR)/data_dir.o: $(DATA_DIR_SRC) src/data_dir.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/data_dir.o $(DATA_DIR_SRC)

# Build utility objects
$(BUILD_DIR)/file_utils.o: $(FILE_UTILS_SRC) src/util/file_utils.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/file_utils.o $(FILE_UTILS_SRC)

$(BUILD_DIR)/string_utils.o: $(STRING_UTILS_SRC) src/util/string_utils.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/string_utils.o $(STRING_UTILS_SRC)

$(BUILD_DIR)/timestamp_utils.o: $(TIMESTAMP_UTILS_SRC) src/util/timestamp_utils.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/timestamp_utils.o $(TIMESTAMP_UTILS_SRC)

$(BUILD_DIR)/format_utils.o: $(FORMAT_UTILS_SRC) src/util/format_utils.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/format_utils.o $(FORMAT_UTILS_SRC)

$(BUILD_DIR)/env_utils.o: $(ENV_UTILS_SRC) src/util/env_utils.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/env_utils.o $(ENV_UTILS_SRC)

$(BUILD_DIR)/output_utils.o: $(OUTPUT_UTILS_SRC) src/util/output_utils.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/output_utils.o $(OUTPUT_UTILS_SRC)

$(BUILD_DIR)/diff_utils.o: $(DIFF_UTILS_SRC) src/util/diff_utils.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/diff_utils.o $(DIFF_UTILS_SRC)

# Build tool objects
$(BUILD_DIR)/tool_sleep.o: $(TOOL_SLEEP_SRC) src/tools/tool_sleep.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/tool_sleep.o $(TOOL_SLEEP_SRC)

$(BUILD_DIR)/tool_todo.o: $(TOOL_TODO_SRC) src/tools/tool_todo.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/tool_todo.o $(TOOL_TODO_SRC)

$(BUILD_DIR)/tool_image.o: $(TOOL_IMAGE_SRC) src/tools/tool_image.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/tool_image.o $(TOOL_IMAGE_SRC)

$(BUILD_DIR)/tool_search.o: $(TOOL_SEARCH_SRC) src/tools/tool_search.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/tool_search.o $(TOOL_SEARCH_SRC)

$(BUILD_DIR)/tool_filesystem.o: $(TOOL_FILESYSTEM_SRC) src/tools/tool_filesystem.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/tool_filesystem.o $(TOOL_FILESYSTEM_SRC)

$(BUILD_DIR)/tool_bash.o: $(TOOL_BASH_SRC) src/tools/tool_bash.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/tool_bash.o $(TOOL_BASH_SRC)

$(BUILD_DIR)/tool_subagent.o: $(TOOL_SUBAGENT_SRC) src/tools/tool_subagent.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/tool_subagent.o $(TOOL_SUBAGENT_SRC)

$(BUILD_DIR)/tool_registry.o: $(TOOL_REGISTRY_SRC) src/tools/tool_registry.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/tool_registry.o $(TOOL_REGISTRY_SRC)

$(BUILD_DIR)/tool_executor.o: $(TOOL_EXECUTOR_SRC) src/tools/tool_executor.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/tool_executor.o $(TOOL_EXECUTOR_SRC)

$(BUILD_DIR)/tool_definitions.o: $(TOOL_DEFINITIONS_SRC) src/tools/tool_definitions.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/tool_definitions.o $(TOOL_DEFINITIONS_SRC)

# Build conversation objects
$(BUILD_DIR)/content_types.o: $(CONTENT_TYPES_SRC) src/conversation/content_types.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/content_types.o $(CONTENT_TYPES_SRC)

$(BUILD_DIR)/conversation_state.o: $(CONVERSATION_STATE_SRC) src/conversation/conversation_state.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/conversation_state.o $(CONVERSATION_STATE_SRC)

$(BUILD_DIR)/message_builder.o: $(MESSAGE_BUILDER_SRC) src/conversation/message_builder.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/message_builder.o $(MESSAGE_BUILDER_SRC)

$(BUILD_DIR)/message_parser.o: $(MESSAGE_PARSER_SRC) src/conversation/message_parser.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/message_parser.o $(MESSAGE_PARSER_SRC)

# Build context objects
$(BUILD_DIR)/system_prompt.o: $(CONTEXT_SYSTEM_PROMPT_SRC) src/context/system_prompt.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/system_prompt.o $(CONTEXT_SYSTEM_PROMPT_SRC)

$(BUILD_DIR)/environment.o: $(CONTEXT_ENVIRONMENT_SRC) src/context/environment.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/environment.o $(CONTEXT_ENVIRONMENT_SRC)

$(BUILD_DIR)/klawed_md.o: $(CONTEXT_KLAWED_MD_SRC) src/context/klawed_md.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/klawed_md.o $(CONTEXT_KLAWED_MD_SRC)

$(BUILD_DIR)/memory_injection.o: $(CONTEXT_MEMORY_INJECTION_SRC) src/context/memory_injection.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/memory_injection.o $(CONTEXT_MEMORY_INJECTION_SRC)

# Build UI module objects
$(BUILD_DIR)/ui_output.o: $(UI_OUTPUT_SRC) src/ui/ui_output.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/ui_output.o $(UI_OUTPUT_SRC)

$(BUILD_DIR)/print_helpers.o: $(PRINT_HELPERS_SRC) src/ui/print_helpers.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/print_helpers.o $(PRINT_HELPERS_SRC)

$(BUILD_DIR)/tool_output_display.o: $(TOOL_OUTPUT_DISPLAY_SRC) src/ui/tool_output_display.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/tool_output_display.o $(TOOL_OUTPUT_DISPLAY_SRC)

# Build session module objects
$(BUILD_DIR)/session_token_usage.o: $(SESSION_TOKEN_USAGE_SRC) src/session/token_usage.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/session_token_usage.o $(SESSION_TOKEN_USAGE_SRC)

$(BUILD_DIR)/session_persistence.o: $(SESSION_PERSISTENCE_SRC) src/session/session_persistence.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/session_persistence.o $(SESSION_PERSISTENCE_SRC)

# Build API layer objects
$(BUILD_DIR)/api_response.o: $(API_RESPONSE_SRC) src/api/api_response.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/api_response.o $(API_RESPONSE_SRC)

$(BUILD_DIR)/api_builder.o: $(API_BUILDER_SRC) src/api/api_builder.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/api_builder.o $(API_BUILDER_SRC)

$(BUILD_DIR)/api_client.o: $(API_CLIENT_SRC) src/api/api_client.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/api_client.o $(API_CLIENT_SRC)

# Build interactive mode objects
$(BUILD_DIR)/interactive_loop.o: $(INTERACTIVE_LOOP_SRC) src/interactive/interactive_loop.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/interactive_loop.o $(INTERACTIVE_LOOP_SRC)

$(BUILD_DIR)/input_handler.o: $(INPUT_HANDLER_SRC) src/interactive/input_handler.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/input_handler.o $(INPUT_HANDLER_SRC)

$(BUILD_DIR)/response_processor.o: $(RESPONSE_PROCESSOR_SRC) src/interactive/response_processor.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/response_processor.o $(RESPONSE_PROCESSOR_SRC)

$(BUILD_DIR)/command_dispatch.o: $(COMMAND_DISPATCH_SRC) src/interactive/command_dispatch.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/command_dispatch.o $(COMMAND_DISPATCH_SRC)

# Build oneshot mode objects
$(BUILD_DIR)/oneshot_mode.o: $(ONESHOT_MODE_SRC) src/oneshot/oneshot_mode.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/oneshot_mode.o $(ONESHOT_MODE_SRC)

$(BUILD_DIR)/oneshot_processor.o: $(ONESHOT_PROCESSOR_SRC) src/oneshot/oneshot_processor.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/oneshot_processor.o $(ONESHOT_PROCESSOR_SRC)

$(BUILD_DIR)/oneshot_output.o: $(ONESHOT_OUTPUT_SRC) src/oneshot/oneshot_output.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/oneshot_output.o $(ONESHOT_OUTPUT_SRC)

$(VERSION_H): $(VERSION_FILE)
	@echo "Generating version.h..."
	@mkdir -p src
	@# Parse version string (e.g., "1.2.3-beta.1" -> 1, 2, 3)
	@VERSION_MAJOR=$$(echo "$(VERSION)" | sed 's/^\([0-9]*\)\..*/\1/' | head -1); \
	VERSION_MINOR=$$(echo "$(VERSION)" | sed 's/^[0-9]*\.\([0-9]*\)\..*/\1/' | head -1); \
	VERSION_PATCH=$$(echo "$(VERSION)" | sed 's/^[0-9]*\.[0-9]*\.\([0-9]*\).*/\1/' | head -1); \
	VERSION_NUMBER=$$(printf "%d" $$(($$VERSION_MAJOR * 65536 + $$VERSION_MINOR * 256 + $$VERSION_PATCH))); \
	printf "0x%06x" $$VERSION_NUMBER > /tmp/version_num.tmp; \
	VERSION_HEX=$$(cat /tmp/version_num.tmp); \
	rm -f /tmp/version_num.tmp; \
	{ \
	echo "/*"; \
	echo " * version.h - Central version management for Klawed"; \
	echo " *"; \
	echo " * This file provides a single source of truth for version information."; \
	echo " * It's automatically generated from the VERSION file during build."; \
	echo " */"; \
	echo ""; \
	echo "#ifndef VERSION_H"; \
	echo "#define VERSION_H"; \
	echo ""; \
	echo "// Version string (e.g., \"0.0.2\", \"1.0.0\", \"1.2.3-beta.1\")"; \
	echo "#define KLAWED_VERSION \"$(VERSION)\""; \
	echo ""; \
	echo "// Version components for programmatic use"; \
	echo "#define KLAWED_VERSION_MAJOR $$VERSION_MAJOR"; \
	echo "#define KLAWED_VERSION_MINOR $$VERSION_MINOR"; \
	echo "#define KLAWED_VERSION_PATCH $$VERSION_PATCH"; \
	echo ""; \
	echo "// Version as numeric value for comparisons (e.g., 0x000002)"; \
	echo "#define KLAWED_VERSION_NUMBER $$VERSION_HEX"; \
	echo ""; \
	echo "// Build timestamp (automatically generated)"; \
	echo "#define KLAWED_BUILD_TIMESTAMP \"$(BUILD_DATE)\""; \
	echo ""; \
	echo "// Full version string with build info"; \
	echo "#define KLAWED_VERSION_FULL \"$(VERSION) (built $(BUILD_DATE))\""; \
	echo ""; \
	echo "#endif // VERSION_H"; \
	} > $(VERSION_H)
	@echo "✓ Version: $(VERSION)"

# Debug build with AddressSanitizer for finding memory bugs
$(BUILD_DIR)/klawed-debug: $(SRC) $(LOGGER_SRC) $(PERSISTENCE_SRC) $(MIGRATIONS_SRC) $(COMMANDS_SRC) $(COMPLETION_SRC) $(TUI_SRC) $(WINDOW_MANAGER_SRC) $(TODO_SRC) $(AWS_BEDROCK_SRC) $(PROVIDER_SRC) $(OPENAI_PROVIDER_SRC) $(OPENAI_MESSAGES_SRC) $(OPENAI_RESPONSES_SRC) $(BEDROCK_PROVIDER_SRC) $(ANTHROPIC_PROVIDER_SRC) $(BUILTIN_THEMES_SRC) $(MESSAGE_QUEUE_SRC) $(AI_WORKER_SRC) $(VOICE_INPUT_SRC) $(ZMQ_SOCKET_SRC) $(ZMQ_CLIENT_SRC) $(ZMQ_MESSAGE_QUEUE_SRC) $(ZMQ_DAEMON_SRC) $(SQLITE_QUEUE_SRC) $(MCP_SRC) $(TOOL_UTILS_SRC) $(PROCESS_UTILS_SRC) $(SUBAGENT_MANAGER_SRC) $(BASE64_SRC) $(HISTORY_FILE_SRC) $(ARRAY_RESIZE_SRC) $(HTTP_CLIENT_SRC) $(SESSION_SRC) $(RETRY_LOGIC_SRC)
	@mkdir -p $(BUILD_DIR)
	@echo "Building with AddressSanitizer (debug mode)..."
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/logger_debug.o $(LOGGER_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/migrations_debug.o $(MIGRATIONS_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/persistence_debug.o $(PERSISTENCE_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/commands_debug.o $(COMMANDS_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/completion_debug.o $(COMPLETION_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/tui_debug.o $(TUI_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/window_manager_debug.o $(WINDOW_MANAGER_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/todo_debug.o $(TODO_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/aws_bedrock_debug.o $(AWS_BEDROCK_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/provider_debug.o $(PROVIDER_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/openai_provider_debug.o $(OPENAI_PROVIDER_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/openai_messages_debug.o $(OPENAI_MESSAGES_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/openai_responses_debug.o $(OPENAI_RESPONSES_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/bedrock_provider_debug.o $(BEDROCK_PROVIDER_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/anthropic_provider_debug.o $(ANTHROPIC_PROVIDER_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/deepseek_provider_debug.o $(DEEPSEEK_PROVIDER_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/moonshot_provider_debug.o $(MOONSHOT_PROVIDER_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/builtin_themes_debug.o $(BUILTIN_THEMES_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/message_queue_debug.o $(MESSAGE_QUEUE_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/ai_worker_debug.o $(AI_WORKER_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/voice_input_debug.o $(VOICE_INPUT_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/mcp_debug.o $(MCP_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/subagent_manager_debug.o $(SUBAGENT_MANAGER_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/base64_debug.o $(BASE64_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/history_file_debug.o $(HISTORY_FILE_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/array_resize_debug.o $(ARRAY_RESIZE_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/http_client_debug.o $(HTTP_CLIENT_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/session_debug.o $(SESSION_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/retry_logic_debug.o $(RETRY_LOGIC_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/sqlite_queue_debug.o $(SQLITE_QUEUE_SRC)
	$(CC) $(DEBUG_CFLAGS) -c -o $(BUILD_DIR)/process_utils_debug.o $(PROCESS_UTILS_SRC)
	$(CC) $(DEBUG_CFLAGS) -o $(BUILD_DIR)/klawed-debug $(SRC) $(BUILD_DIR)/logger_debug.o $(BUILD_DIR)/persistence_debug.o $(BUILD_DIR)/migrations_debug.o $(BUILD_DIR)/commands_debug.o $(BUILD_DIR)/completion_debug.o $(BUILD_DIR)/tui_debug.o $(BUILD_DIR)/window_manager_debug.o $(BUILD_DIR)/todo_debug.o $(BUILD_DIR)/aws_bedrock_debug.o $(BUILD_DIR)/provider_debug.o $(BUILD_DIR)/openai_provider_debug.o $(BUILD_DIR)/openai_messages_debug.o $(BUILD_DIR)/openai_responses_debug.o $(BUILD_DIR)/bedrock_provider_debug.o $(BUILD_DIR)/anthropic_provider_debug.o $(BUILD_DIR)/deepseek_provider_debug.o $(BUILD_DIR)/moonshot_provider_debug.o $(BUILD_DIR)/builtin_themes_debug.o $(BUILD_DIR)/message_queue_debug.o $(BUILD_DIR)/ai_worker_debug.o $(BUILD_DIR)/voice_input_debug.o $(BUILD_DIR)/mcp_debug.o $(BUILD_DIR)/subagent_manager_debug.o $(BUILD_DIR)/base64_debug.o $(BUILD_DIR)/history_file_debug.o $(BUILD_DIR)/array_resize_debug.o $(BUILD_DIR)/http_client_debug.o $(BUILD_DIR)/session_debug.o $(BUILD_DIR)/retry_logic_debug.o $(BUILD_DIR)/sqlite_queue_debug.o $(BUILD_DIR)/process_utils_debug.o $(TOOL_UTILS_SRC) $(DEBUG_LDFLAGS)
	@echo ""
	@echo "✓ Debug build successful with AddressSanitizer!"
	@echo "Run: ./$(BUILD_DIR)/klawed-debug \"your prompt here\""
	@echo ""
	@echo "AddressSanitizer will detect:"
	@echo "  - Use-after-free"
	@echo "  - Double-free"
	@echo "  - Heap/stack buffer overflows"
	@echo "  - Memory leaks"
	@echo ""

# Build with clang compiler
$(BUILD_DIR)/klawed-clang: $(SRC) $(LOGGER_OBJ) $(PERSISTENCE_OBJ) $(TOKEN_USAGE_DB_OBJ) $(TOKEN_USAGE_DB_MIGRATIONS_OBJ) $(MIGRATIONS_OBJ) $(COMMANDS_OBJ) $(COMPLETION_OBJ) $(TUI_OBJ) $(WINDOW_MANAGER_OBJ) $(TODO_OBJ) $(AWS_BEDROCK_OBJ) $(BEDROCK_CONVERSE_OBJ) $(PROVIDER_OBJ) $(OPENAI_PROVIDER_OBJ) $(OPENAI_MESSAGES_OBJ) $(OPENAI_RESPONSES_OBJ) $(BEDROCK_PROVIDER_OBJ) $(ANTHROPIC_PROVIDER_OBJ) $(DEEPSEEK_PROVIDER_OBJ) $(MOONSHOT_PROVIDER_OBJ) $(BUILTIN_THEMES_OBJ) $(AI_WORKER_OBJ) $(MESSAGE_QUEUE_OBJ) $(VOICE_INPUT_OBJ) $(SQLITE_QUEUE_OBJ) $(MCP_OBJ) $(TOOL_UTILS_SRC) $(PROCESS_UTILS_OBJ) $(HTTP_CLIENT_OBJ) $(RETRY_LOGIC_OBJ) $(VERSION_H)
	@mkdir -p $(BUILD_DIR)
	@echo "Building with clang compiler..."
	$(CLANG) $(CFLAGS) -o $(BUILD_DIR)/klawed-clang $(SRC) $(LOGGER_OBJ) $(PERSISTENCE_OBJ) $(TOKEN_USAGE_DB_OBJ) $(TOKEN_USAGE_DB_MIGRATIONS_OBJ) $(MIGRATIONS_OBJ) $(COMMANDS_OBJ) $(COMPLETION_OBJ) $(TUI_OBJ) $(WINDOW_MANAGER_OBJ) $(TODO_OBJ) $(AWS_BEDROCK_OBJ) $(BEDROCK_CONVERSE_OBJ) $(PROVIDER_OBJ) $(OPENAI_PROVIDER_OBJ) $(OPENAI_MESSAGES_OBJ) $(OPENAI_RESPONSES_OBJ) $(BEDROCK_PROVIDER_OBJ) $(ANTHROPIC_PROVIDER_OBJ) $(DEEPSEEK_PROVIDER_OBJ) $(MOONSHOT_PROVIDER_OBJ) $(BUILTIN_THEMES_OBJ) $(MESSAGE_QUEUE_OBJ) $(AI_WORKER_OBJ) $(VOICE_INPUT_OBJ) $(SQLITE_QUEUE_OBJ) $(MCP_OBJ) $(TOOL_UTILS_SRC) $(PROCESS_UTILS_OBJ) $(HTTP_CLIENT_OBJ) $(RETRY_LOGIC_OBJ) $(LDFLAGS)
	@echo ""
	@echo "✓ Clang build successful!"
	@echo "Version: $(VERSION)"
	@echo "Run: ./$(BUILD_DIR)/klawed-clang \"your prompt here\""
	@echo ""

# Static analysis with compiler's built-in analyzer
analyze: check-deps
	@mkdir -p $(BUILD_DIR)
	@echo "Running static analysis..."
	@echo ""
	@if command -v clang >/dev/null 2>&1; then \
		echo "Using clang --analyze..."; \
		clang --analyze -Xanalyzer -analyzer-output=text $(CFLAGS) $(SRC) $(LOGGER_SRC) $(PERSISTENCE_SRC) $(MIGRATIONS_SRC) 2>&1 | tee $(BUILD_DIR)/analyze.log; \
	else \
		echo "Using gcc -fanalyzer..."; \
		gcc -fanalyzer $(CFLAGS) -c $(SRC) $(LOGGER_SRC) $(PERSISTENCE_SRC) $(MIGRATIONS_SRC) 2>&1 | tee $(BUILD_DIR)/analyze.log; \
	fi
	@echo ""
	@echo "✓ Static analysis complete. Results saved to $(BUILD_DIR)/analyze.log"
	@echo ""

# Build with Undefined Behavior Sanitizer
# Note: Using -O1 instead of the regular -O2, and removing _FORTIFY_SOURCE to avoid conflicts
sanitize-ub: check-deps $(LOGGER_OBJ) $(CONFIG_OBJ) $(PERSISTENCE_OBJ) $(TOKEN_USAGE_DB_OBJ) $(TOKEN_USAGE_DB_MIGRATIONS_OBJ) $(MIGRATIONS_OBJ) $(COMMANDS_OBJ) $(THEME_EXPLORER_OBJ) $(HELP_MODAL_OBJ) $(COMPLETION_OBJ) $(TUI_OBJ) $(TUI_CORE_OBJ) $(TUI_CONVERSATION_OBJ) $(TUI_WINDOW_OBJ) $(TUI_RENDER_OBJ) $(TUI_MODES_OBJ) $(TUI_INPUT_OBJ) $(TUI_HISTORY_OBJ) $(TUI_COMPLETION_OBJ) $(TUI_PASTE_OBJ) $(TUI_SEARCH_OBJ) $(FILE_SEARCH_OBJ) $(HISTORY_SEARCH_OBJ) $(WINDOW_MANAGER_OBJ) $(TODO_OBJ) $(AWS_BEDROCK_OBJ) $(BEDROCK_CONVERSE_OBJ) $(PROVIDER_OBJ) $(OPENAI_PROVIDER_OBJ) $(OPENAI_MESSAGES_OBJ) $(OPENAI_RESPONSES_OBJ) $(BEDROCK_PROVIDER_OBJ) $(ANTHROPIC_PROVIDER_OBJ) $(DEEPSEEK_PROVIDER_OBJ) $(MOONSHOT_PROVIDER_OBJ) $(BUILTIN_THEMES_OBJ) $(MESSAGE_QUEUE_OBJ) $(AI_WORKER_OBJ) $(VOICE_INPUT_OBJ) $(SQLITE_QUEUE_OBJ) $(MCP_OBJ) $(TOOL_UTILS_OBJ) $(PROCESS_UTILS_OBJ) $(DUMP_UTILS_OBJ) $(SUBAGENT_MANAGER_OBJ) $(EXPLORE_TOOLS_OBJ) $(BASE64_OBJ) $(HISTORY_FILE_OBJ) $(ARRAY_RESIZE_OBJ) $(HTTP_CLIENT_OBJ) $(SESSION_OBJ) $(RETRY_LOGIC_OBJ) $(MEMVID_OBJ) $(COMPACTION_OBJ) $(BACKGROUND_INIT_OBJ) $(DATA_DIR_OBJ) $(FILE_UTILS_OBJ) $(STRING_UTILS_OBJ) $(TIMESTAMP_UTILS_OBJ) $(FORMAT_UTILS_OBJ) $(ENV_UTILS_OBJ) $(OUTPUT_UTILS_OBJ) $(DIFF_UTILS_OBJ) $(TOOL_SLEEP_OBJ) $(TOOL_TODO_OBJ) $(TOOL_IMAGE_OBJ) $(TOOL_SEARCH_OBJ) $(TOOL_FILESYSTEM_OBJ) $(TOOL_BASH_OBJ) $(TOOL_SUBAGENT_OBJ) $(TOOL_REGISTRY_OBJ) $(TOOL_EXECUTOR_OBJ) $(TOOL_DEFINITIONS_OBJ) $(CONTENT_TYPES_OBJ) $(CONVERSATION_STATE_OBJ) $(MESSAGE_BUILDER_OBJ) $(MESSAGE_PARSER_OBJ) $(CONTEXT_SYSTEM_PROMPT_OBJ) $(CONTEXT_ENVIRONMENT_OBJ) $(CONTEXT_KLAWED_MD_OBJ) $(CONTEXT_MEMORY_INJECTION_OBJ) $(UI_OUTPUT_OBJ) $(PRINT_HELPERS_OBJ) $(TOOL_OUTPUT_DISPLAY_OBJ) $(SESSION_TOKEN_USAGE_OBJ) $(SESSION_PERSISTENCE_OBJ) $(API_RESPONSE_OBJ) $(API_BUILDER_OBJ) $(API_CLIENT_OBJ) $(INTERACTIVE_LOOP_OBJ) $(INPUT_HANDLER_OBJ) $(RESPONSE_PROCESSOR_OBJ) $(COMMAND_DISPATCH_OBJ) $(ONESHOT_MODE_OBJ) $(ONESHOT_PROCESSOR_OBJ) $(ONESHOT_OUTPUT_OBJ) $(PROVIDER_COMMAND_OBJ) $(CONFIG_COMMAND_OBJ)
	@mkdir -p $(BUILD_DIR)
	@echo "Building with Undefined Behavior Sanitizer..."
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=undefined -fno-omit-frame-pointer -o $(BUILD_DIR)/klawed-ubsan $(SRC) $(LOGGER_OBJ) $(CONFIG_OBJ) $(PERSISTENCE_OBJ) $(TOKEN_USAGE_DB_OBJ) $(TOKEN_USAGE_DB_MIGRATIONS_OBJ) $(MIGRATIONS_OBJ) $(COMMANDS_OBJ) $(THEME_EXPLORER_OBJ) $(HELP_MODAL_OBJ) $(COMPLETION_OBJ) $(TUI_OBJ) $(TUI_CORE_OBJ) $(TUI_CONVERSATION_OBJ) $(TUI_WINDOW_OBJ) $(TUI_RENDER_OBJ) $(TUI_MODES_OBJ) $(TUI_INPUT_OBJ) $(TUI_HISTORY_OBJ) $(TUI_COMPLETION_OBJ) $(TUI_PASTE_OBJ) $(TUI_SEARCH_OBJ) $(FILE_SEARCH_OBJ) $(HISTORY_SEARCH_OBJ) $(WINDOW_MANAGER_OBJ) $(TODO_OBJ) $(AWS_BEDROCK_OBJ) $(BEDROCK_CONVERSE_OBJ) $(PROVIDER_OBJ) $(OPENAI_PROVIDER_OBJ) $(OPENAI_MESSAGES_OBJ) $(OPENAI_RESPONSES_OBJ) $(BEDROCK_PROVIDER_OBJ) $(ANTHROPIC_PROVIDER_OBJ) $(DEEPSEEK_PROVIDER_OBJ) $(MOONSHOT_PROVIDER_OBJ) $(BUILTIN_THEMES_OBJ) $(MESSAGE_QUEUE_OBJ) $(AI_WORKER_OBJ) $(VOICE_INPUT_OBJ) $(SQLITE_QUEUE_OBJ) $(MCP_OBJ) $(TOOL_UTILS_OBJ) $(PROCESS_UTILS_OBJ) $(DUMP_UTILS_OBJ) $(SUBAGENT_MANAGER_OBJ) $(EXPLORE_TOOLS_OBJ) $(BASE64_OBJ) $(HISTORY_FILE_OBJ) $(ARRAY_RESIZE_OBJ) $(HTTP_CLIENT_OBJ) $(SESSION_OBJ) $(RETRY_LOGIC_OBJ) $(MEMVID_OBJ) $(COMPACTION_OBJ) $(BACKGROUND_INIT_OBJ) $(DATA_DIR_OBJ) $(FILE_UTILS_OBJ) $(STRING_UTILS_OBJ) $(TIMESTAMP_UTILS_OBJ) $(FORMAT_UTILS_OBJ) $(ENV_UTILS_OBJ) $(OUTPUT_UTILS_OBJ) $(DIFF_UTILS_OBJ) $(TOOL_SLEEP_OBJ) $(TOOL_TODO_OBJ) $(TOOL_IMAGE_OBJ) $(TOOL_SEARCH_OBJ) $(TOOL_FILESYSTEM_OBJ) $(TOOL_BASH_OBJ) $(TOOL_SUBAGENT_OBJ) $(TOOL_REGISTRY_OBJ) $(TOOL_EXECUTOR_OBJ) $(TOOL_DEFINITIONS_OBJ) $(CONTENT_TYPES_OBJ) $(CONVERSATION_STATE_OBJ) $(MESSAGE_BUILDER_OBJ) $(MESSAGE_PARSER_OBJ) $(CONTEXT_SYSTEM_PROMPT_OBJ) $(CONTEXT_ENVIRONMENT_OBJ) $(CONTEXT_KLAWED_MD_OBJ) $(CONTEXT_MEMORY_INJECTION_OBJ) $(UI_OUTPUT_OBJ) $(PRINT_HELPERS_OBJ) $(TOOL_OUTPUT_DISPLAY_OBJ) $(SESSION_TOKEN_USAGE_OBJ) $(SESSION_PERSISTENCE_OBJ) $(API_RESPONSE_OBJ) $(API_BUILDER_OBJ) $(API_CLIENT_OBJ) $(INTERACTIVE_LOOP_OBJ) $(INPUT_HANDLER_OBJ) $(RESPONSE_PROCESSOR_OBJ) $(COMMAND_DISPATCH_OBJ) $(ONESHOT_MODE_OBJ) $(ONESHOT_PROCESSOR_OBJ) $(ONESHOT_OUTPUT_OBJ) $(PROVIDER_COMMAND_OBJ) $(CONFIG_COMMAND_OBJ) $(LDFLAGS) -fsanitize=undefined
	@echo ""
	@echo "✓ Build successful with UBSan!"
	@echo "Run: ./$(BUILD_DIR)/klawed-ubsan \"your prompt here\""
	@echo ""

# Build with combined Address + Undefined Behavior Sanitizers (recommended)
# Note: Using -O1 instead of the regular -O2, and removing _FORTIFY_SOURCE to avoid conflicts
sanitize-all: check-deps
	@mkdir -p $(BUILD_DIR)
	@echo "Building with Address + Undefined Behavior Sanitizers (recommended for testing)..."
	@# Detect compiler to conditionally add GCC-specific flags
	@COMPILER_TYPE=$$($(CC) --version 2>&1 | grep -q "clang" && echo "clang" || echo "gcc"); \
	if [ "$$COMPILER_TYPE" = "gcc" ]; then \
		EXTRA_FLAGS="-Wno-format-truncation"; \
	else \
		EXTRA_FLAGS=""; \
	fi; \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/logger_all.o $(LOGGER_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/migrations_all.o $(MIGRATIONS_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/token_usage_db_all.o $(TOKEN_USAGE_DB_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/token_usage_db_migrations_all.o $(TOKEN_USAGE_DB_MIGRATIONS_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/persistence_all.o $(PERSISTENCE_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/commands_all.o $(COMMANDS_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/theme_explorer_all.o $(THEME_EXPLORER_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/help_modal_all.o $(HELP_MODAL_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/completion_all.o $(COMPLETION_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tui_all.o $(TUI_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tui_core_all.o $(TUI_CORE_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tui_conversation_all.o $(TUI_CONVERSATION_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tui_window_all.o $(TUI_WINDOW_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tui_render_all.o $(TUI_RENDER_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tui_modes_all.o $(TUI_MODES_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tui_input_all.o $(TUI_INPUT_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tui_history_all.o $(TUI_HISTORY_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tui_completion_all.o $(TUI_COMPLETION_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tui_paste_all.o $(TUI_PASTE_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tui_search_all.o $(TUI_SEARCH_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/file_search_all.o $(FILE_SEARCH_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/history_search_all.o $(HISTORY_SEARCH_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/window_manager_all.o $(WINDOW_MANAGER_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/todo_all.o $(TODO_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/aws_bedrock_all.o $(AWS_BEDROCK_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/bedrock_converse_all.o $(BEDROCK_CONVERSE_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/provider_all.o $(PROVIDER_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/provider_config_loader_all.o $(PROVIDER_CONFIG_LOADER_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/openai_provider_all.o $(OPENAI_PROVIDER_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/openai_messages_all.o $(OPENAI_MESSAGES_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/openai_responses_all.o $(OPENAI_RESPONSES_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/bedrock_provider_all.o $(BEDROCK_PROVIDER_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/anthropic_provider_all.o $(ANTHROPIC_PROVIDER_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/deepseek_provider_all.o $(DEEPSEEK_PROVIDER_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/moonshot_provider_all.o $(MOONSHOT_PROVIDER_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/builtin_themes_all.o $(BUILTIN_THEMES_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/message_queue_all.o $(MESSAGE_QUEUE_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/ai_worker_all.o $(AI_WORKER_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/voice_input_all.o $(VOICE_INPUT_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/sqlite_queue_all.o $(SQLITE_QUEUE_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/mcp_all.o $(MCP_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tool_utils_all.o $(TOOL_UTILS_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/process_utils_all.o $(PROCESS_UTILS_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/dump_utils_all.o $(DUMP_UTILS_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/subagent_manager_all.o $(SUBAGENT_MANAGER_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/explore_tools_all.o $(EXPLORE_TOOLS_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/base64_all.o $(BASE64_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/history_file_all.o $(HISTORY_FILE_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/array_resize_all.o $(ARRAY_RESIZE_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/http_client_all.o $(HTTP_CLIENT_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/session_all.o $(SESSION_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/retry_logic_all.o $(RETRY_LOGIC_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/memvid_all.o $(MEMVID_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/compaction_all.o $(COMPACTION_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/data_dir_all.o $(DATA_DIR_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/file_utils_all.o $(FILE_UTILS_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/string_utils_all.o $(STRING_UTILS_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/timestamp_utils_all.o $(TIMESTAMP_UTILS_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/format_utils_all.o $(FORMAT_UTILS_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/env_utils_all.o $(ENV_UTILS_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/output_utils_all.o $(OUTPUT_UTILS_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/diff_utils_all.o $(DIFF_UTILS_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tool_sleep_all.o $(TOOL_SLEEP_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tool_todo_all.o $(TOOL_TODO_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tool_image_all.o $(TOOL_IMAGE_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tool_search_all.o $(TOOL_SEARCH_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tool_filesystem_all.o $(TOOL_FILESYSTEM_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tool_bash_all.o $(TOOL_BASH_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tool_subagent_all.o $(TOOL_SUBAGENT_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tool_registry_all.o $(TOOL_REGISTRY_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tool_executor_all.o $(TOOL_EXECUTOR_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tool_definitions_all.o $(TOOL_DEFINITIONS_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/content_types_all.o $(CONTENT_TYPES_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/conversation_state_all.o $(CONVERSATION_STATE_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/message_builder_all.o $(MESSAGE_BUILDER_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/message_parser_all.o $(MESSAGE_PARSER_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/system_prompt_all.o $(CONTEXT_SYSTEM_PROMPT_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/environment_all.o $(CONTEXT_ENVIRONMENT_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/klawed_md_all.o $(CONTEXT_KLAWED_MD_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/memory_injection_all.o $(CONTEXT_MEMORY_INJECTION_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/ui_output_all.o $(UI_OUTPUT_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/print_helpers_all.o $(PRINT_HELPERS_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/tool_output_display_all.o $(TOOL_OUTPUT_DISPLAY_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/session_token_usage_all.o $(SESSION_TOKEN_USAGE_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/session_persistence_all.o $(SESSION_PERSISTENCE_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/api_response_all.o $(API_RESPONSE_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/api_builder_all.o $(API_BUILDER_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/api_client_all.o $(API_CLIENT_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/interactive_loop_all.o $(INTERACTIVE_LOOP_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/input_handler_all.o $(INPUT_HANDLER_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/response_processor_all.o $(RESPONSE_PROCESSOR_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/command_dispatch_all.o $(COMMAND_DISPATCH_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/oneshot_mode_all.o $(ONESHOT_MODE_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/oneshot_processor_all.o $(ONESHOT_PROCESSOR_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/oneshot_output_all.o $(ONESHOT_OUTPUT_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/config_all.o $(CONFIG_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/config_command_all.o $(CONFIG_COMMAND_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/background_init_all.o $(BACKGROUND_INIT_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -c -o $(BUILD_DIR)/provider_command_all.o $(PROVIDER_COMMAND_SRC); \
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $$EXTRA_FLAGS -o $(BUILD_DIR)/klawed-allsan $(SRC) \
		$(BUILD_DIR)/logger_all.o $(BUILD_DIR)/persistence_all.o $(BUILD_DIR)/migrations_all.o \
		$(BUILD_DIR)/token_usage_db_all.o $(BUILD_DIR)/token_usage_db_migrations_all.o \
		$(BUILD_DIR)/commands_all.o $(BUILD_DIR)/theme_explorer_all.o $(BUILD_DIR)/help_modal_all.o \
		$(BUILD_DIR)/completion_all.o \
		$(BUILD_DIR)/tui_all.o $(BUILD_DIR)/tui_core_all.o $(BUILD_DIR)/tui_conversation_all.o \
		$(BUILD_DIR)/tui_window_all.o $(BUILD_DIR)/tui_render_all.o $(BUILD_DIR)/tui_modes_all.o \
		$(BUILD_DIR)/tui_input_all.o $(BUILD_DIR)/tui_history_all.o $(BUILD_DIR)/tui_completion_all.o \
		$(BUILD_DIR)/tui_paste_all.o $(BUILD_DIR)/tui_search_all.o \
		$(BUILD_DIR)/file_search_all.o $(BUILD_DIR)/history_search_all.o \
		$(BUILD_DIR)/window_manager_all.o \
		$(BUILD_DIR)/todo_all.o \
		$(BUILD_DIR)/aws_bedrock_all.o $(BUILD_DIR)/bedrock_converse_all.o \
		$(BUILD_DIR)/provider_all.o $(BUILD_DIR)/provider_config_loader_all.o \
		$(BUILD_DIR)/openai_provider_all.o \
		$(BUILD_DIR)/openai_messages_all.o $(BUILD_DIR)/openai_responses_all.o \
		$(BUILD_DIR)/bedrock_provider_all.o $(BUILD_DIR)/anthropic_provider_all.o \
		$(BUILD_DIR)/deepseek_provider_all.o $(BUILD_DIR)/moonshot_provider_all.o \
		$(BUILD_DIR)/builtin_themes_all.o \
		$(BUILD_DIR)/message_queue_all.o $(BUILD_DIR)/ai_worker_all.o \
		$(BUILD_DIR)/voice_input_all.o \
		$(BUILD_DIR)/sqlite_queue_all.o \
		$(BUILD_DIR)/mcp_all.o \
		$(BUILD_DIR)/tool_utils_all.o $(BUILD_DIR)/process_utils_all.o \
		$(BUILD_DIR)/dump_utils_all.o \
		$(BUILD_DIR)/subagent_manager_all.o $(BUILD_DIR)/explore_tools_all.o \
		$(BUILD_DIR)/base64_all.o $(BUILD_DIR)/history_file_all.o \
		$(BUILD_DIR)/array_resize_all.o $(BUILD_DIR)/http_client_all.o \
		$(BUILD_DIR)/session_all.o \
		$(BUILD_DIR)/retry_logic_all.o \
		$(BUILD_DIR)/memvid_all.o $(BUILD_DIR)/compaction_all.o \
		$(BUILD_DIR)/data_dir_all.o \
		$(BUILD_DIR)/file_utils_all.o $(BUILD_DIR)/string_utils_all.o \
		$(BUILD_DIR)/timestamp_utils_all.o $(BUILD_DIR)/format_utils_all.o \
		$(BUILD_DIR)/env_utils_all.o $(BUILD_DIR)/output_utils_all.o $(BUILD_DIR)/diff_utils_all.o \
		$(BUILD_DIR)/tool_sleep_all.o $(BUILD_DIR)/tool_todo_all.o \
		$(BUILD_DIR)/tool_image_all.o $(BUILD_DIR)/tool_search_all.o \
		$(BUILD_DIR)/tool_filesystem_all.o $(BUILD_DIR)/tool_bash_all.o \
		$(BUILD_DIR)/tool_subagent_all.o $(BUILD_DIR)/tool_registry_all.o \
		$(BUILD_DIR)/tool_executor_all.o $(BUILD_DIR)/tool_definitions_all.o \
		$(BUILD_DIR)/content_types_all.o $(BUILD_DIR)/conversation_state_all.o \
		$(BUILD_DIR)/message_builder_all.o $(BUILD_DIR)/message_parser_all.o \
		$(BUILD_DIR)/system_prompt_all.o $(BUILD_DIR)/environment_all.o \
		$(BUILD_DIR)/klawed_md_all.o $(BUILD_DIR)/memory_injection_all.o \
		$(BUILD_DIR)/ui_output_all.o $(BUILD_DIR)/print_helpers_all.o $(BUILD_DIR)/tool_output_display_all.o \
		$(BUILD_DIR)/session_token_usage_all.o $(BUILD_DIR)/session_persistence_all.o \
		$(BUILD_DIR)/api_response_all.o $(BUILD_DIR)/api_builder_all.o $(BUILD_DIR)/api_client_all.o \
		$(BUILD_DIR)/interactive_loop_all.o $(BUILD_DIR)/input_handler_all.o \
		$(BUILD_DIR)/response_processor_all.o $(BUILD_DIR)/command_dispatch_all.o \
		$(BUILD_DIR)/oneshot_mode_all.o $(BUILD_DIR)/oneshot_processor_all.o $(BUILD_DIR)/oneshot_output_all.o \
		$(BUILD_DIR)/config_all.o $(BUILD_DIR)/config_command_all.o $(BUILD_DIR)/background_init_all.o $(BUILD_DIR)/provider_command_all.o \
		$(LDFLAGS) -fsanitize=address,undefined
	@echo ""
	@echo "✓ Build successful with combined sanitizers!"
	@echo "Run: ./$(BUILD_DIR)/klawed-allsan \"your prompt here\""
	@echo ""
	@echo "This build detects:"
	@echo "  - Use-after-free, double-free, buffer overflows (AddressSanitizer)"
	@echo "  - Undefined behavior, integer overflows, null dereferences (UBSan)"
	@echo ""

# Build with Leak Sanitizer only
# Note: Using -O1 instead of the regular -O2, and removing _FORTIFY_SOURCE to avoid conflicts
sanitize-leak: check-deps $(LOGGER_OBJ) $(CONFIG_OBJ) $(PERSISTENCE_OBJ) $(TOKEN_USAGE_DB_OBJ) $(TOKEN_USAGE_DB_MIGRATIONS_OBJ) $(MIGRATIONS_OBJ) $(COMMANDS_OBJ) $(THEME_EXPLORER_OBJ) $(HELP_MODAL_OBJ) $(COMPLETION_OBJ) $(TUI_OBJ) $(TUI_CORE_OBJ) $(TUI_CONVERSATION_OBJ) $(TUI_WINDOW_OBJ) $(TUI_RENDER_OBJ) $(TUI_MODES_OBJ) $(TUI_INPUT_OBJ) $(TUI_HISTORY_OBJ) $(TUI_COMPLETION_OBJ) $(TUI_PASTE_OBJ) $(TUI_SEARCH_OBJ) $(FILE_SEARCH_OBJ) $(HISTORY_SEARCH_OBJ) $(WINDOW_MANAGER_OBJ) $(TODO_OBJ) $(AWS_BEDROCK_OBJ) $(BEDROCK_CONVERSE_OBJ) $(PROVIDER_OBJ) $(PROVIDER_CONFIG_LOADER_OBJ) $(OPENAI_PROVIDER_OBJ) $(OPENAI_MESSAGES_OBJ) $(OPENAI_RESPONSES_OBJ) $(BEDROCK_PROVIDER_OBJ) $(ANTHROPIC_PROVIDER_OBJ) $(DEEPSEEK_PROVIDER_OBJ) $(MOONSHOT_PROVIDER_OBJ) $(BUILTIN_THEMES_OBJ) $(MESSAGE_QUEUE_OBJ) $(AI_WORKER_OBJ) $(VOICE_INPUT_OBJ) $(SQLITE_QUEUE_OBJ) $(MCP_OBJ) $(TOOL_UTILS_OBJ) $(PROCESS_UTILS_OBJ) $(DUMP_UTILS_OBJ) $(SUBAGENT_MANAGER_OBJ) $(EXPLORE_TOOLS_OBJ) $(BASE64_OBJ) $(HISTORY_FILE_OBJ) $(ARRAY_RESIZE_OBJ) $(HTTP_CLIENT_OBJ) $(SESSION_OBJ) $(RETRY_LOGIC_OBJ) $(MEMVID_OBJ) $(COMPACTION_OBJ) $(BACKGROUND_INIT_OBJ) $(DATA_DIR_OBJ) $(FILE_UTILS_OBJ) $(STRING_UTILS_OBJ) $(TIMESTAMP_UTILS_OBJ) $(FORMAT_UTILS_OBJ) $(ENV_UTILS_OBJ) $(OUTPUT_UTILS_OBJ) $(DIFF_UTILS_OBJ) $(TOOL_SLEEP_OBJ) $(TOOL_TODO_OBJ) $(TOOL_IMAGE_OBJ) $(TOOL_SEARCH_OBJ) $(TOOL_FILESYSTEM_OBJ) $(TOOL_BASH_OBJ) $(TOOL_SUBAGENT_OBJ) $(TOOL_REGISTRY_OBJ) $(TOOL_EXECUTOR_OBJ) $(TOOL_DEFINITIONS_OBJ) $(CONTENT_TYPES_OBJ) $(CONVERSATION_STATE_OBJ) $(MESSAGE_BUILDER_OBJ) $(MESSAGE_PARSER_OBJ) $(CONTEXT_SYSTEM_PROMPT_OBJ) $(CONTEXT_ENVIRONMENT_OBJ) $(CONTEXT_KLAWED_MD_OBJ) $(CONTEXT_MEMORY_INJECTION_OBJ) $(UI_OUTPUT_OBJ) $(PRINT_HELPERS_OBJ) $(TOOL_OUTPUT_DISPLAY_OBJ) $(SESSION_TOKEN_USAGE_OBJ) $(SESSION_PERSISTENCE_OBJ) $(API_RESPONSE_OBJ) $(API_BUILDER_OBJ) $(API_CLIENT_OBJ) $(INTERACTIVE_LOOP_OBJ) $(INPUT_HANDLER_OBJ) $(RESPONSE_PROCESSOR_OBJ) $(COMMAND_DISPATCH_OBJ) $(ONESHOT_MODE_OBJ) $(ONESHOT_PROCESSOR_OBJ) $(ONESHOT_OUTPUT_OBJ) $(PROVIDER_COMMAND_OBJ) $(CONFIG_COMMAND_OBJ)
	@mkdir -p $(BUILD_DIR)
	@echo "Building with Leak Sanitizer..."
	$(CC) $(filter-out -O2 -D_FORTIFY_SOURCE=2,$(CFLAGS)) -g -O1 -fsanitize=leak -fno-omit-frame-pointer -o $(BUILD_DIR)/klawed-lsan $(SRC) $(LOGGER_OBJ) $(CONFIG_OBJ) $(PERSISTENCE_OBJ) $(TOKEN_USAGE_DB_OBJ) $(TOKEN_USAGE_DB_MIGRATIONS_OBJ) $(MIGRATIONS_OBJ) $(COMMANDS_OBJ) $(THEME_EXPLORER_OBJ) $(HELP_MODAL_OBJ) $(COMPLETION_OBJ) $(TUI_OBJ) $(TUI_CORE_OBJ) $(TUI_CONVERSATION_OBJ) $(TUI_WINDOW_OBJ) $(TUI_RENDER_OBJ) $(TUI_MODES_OBJ) $(TUI_INPUT_OBJ) $(TUI_HISTORY_OBJ) $(TUI_COMPLETION_OBJ) $(TUI_PASTE_OBJ) $(TUI_SEARCH_OBJ) $(FILE_SEARCH_OBJ) $(HISTORY_SEARCH_OBJ) $(WINDOW_MANAGER_OBJ) $(TODO_OBJ) $(AWS_BEDROCK_OBJ) $(BEDROCK_CONVERSE_OBJ) $(PROVIDER_OBJ) $(PROVIDER_CONFIG_LOADER_OBJ) $(OPENAI_PROVIDER_OBJ) $(OPENAI_MESSAGES_OBJ) $(OPENAI_RESPONSES_OBJ) $(BEDROCK_PROVIDER_OBJ) $(ANTHROPIC_PROVIDER_OBJ) $(DEEPSEEK_PROVIDER_OBJ) $(MOONSHOT_PROVIDER_OBJ) $(BUILTIN_THEMES_OBJ) $(MESSAGE_QUEUE_OBJ) $(AI_WORKER_OBJ) $(VOICE_INPUT_OBJ) $(SQLITE_QUEUE_OBJ) $(MCP_OBJ) $(TOOL_UTILS_OBJ) $(PROCESS_UTILS_OBJ) $(DUMP_UTILS_OBJ) $(SUBAGENT_MANAGER_OBJ) $(EXPLORE_TOOLS_OBJ) $(BASE64_OBJ) $(HISTORY_FILE_OBJ) $(ARRAY_RESIZE_OBJ) $(HTTP_CLIENT_OBJ) $(SESSION_OBJ) $(RETRY_LOGIC_OBJ) $(MEMVID_OBJ) $(COMPACTION_OBJ) $(BACKGROUND_INIT_OBJ) $(DATA_DIR_OBJ) $(FILE_UTILS_OBJ) $(STRING_UTILS_OBJ) $(TIMESTAMP_UTILS_OBJ) $(FORMAT_UTILS_OBJ) $(ENV_UTILS_OBJ) $(OUTPUT_UTILS_OBJ) $(DIFF_UTILS_OBJ) $(TOOL_SLEEP_OBJ) $(TOOL_TODO_OBJ) $(TOOL_IMAGE_OBJ) $(TOOL_SEARCH_OBJ) $(TOOL_FILESYSTEM_OBJ) $(TOOL_BASH_OBJ) $(TOOL_SUBAGENT_OBJ) $(TOOL_REGISTRY_OBJ) $(TOOL_EXECUTOR_OBJ) $(TOOL_DEFINITIONS_OBJ) $(CONTENT_TYPES_OBJ) $(CONVERSATION_STATE_OBJ) $(MESSAGE_BUILDER_OBJ) $(MESSAGE_PARSER_OBJ) $(CONTEXT_SYSTEM_PROMPT_OBJ) $(CONTEXT_ENVIRONMENT_OBJ) $(CONTEXT_KLAWED_MD_OBJ) $(CONTEXT_MEMORY_INJECTION_OBJ) $(UI_OUTPUT_OBJ) $(PRINT_HELPERS_OBJ) $(TOOL_OUTPUT_DISPLAY_OBJ) $(SESSION_TOKEN_USAGE_OBJ) $(SESSION_PERSISTENCE_OBJ) $(API_RESPONSE_OBJ) $(API_BUILDER_OBJ) $(API_CLIENT_OBJ) $(INTERACTIVE_LOOP_OBJ) $(INPUT_HANDLER_OBJ) $(RESPONSE_PROCESSOR_OBJ) $(COMMAND_DISPATCH_OBJ) $(ONESHOT_MODE_OBJ) $(ONESHOT_PROCESSOR_OBJ) $(ONESHOT_OUTPUT_OBJ) $(PROVIDER_COMMAND_OBJ) $(CONFIG_COMMAND_OBJ) $(LDFLAGS) -fsanitize=leak
	@echo ""
	@echo "✓ Build successful with LeakSanitizer!"
	@echo "Run: ./$(BUILD_DIR)/klawed-lsan \"your prompt here\""
	@echo ""

# Run Valgrind memory checker on tests
valgrind: test-edit test-read
	@echo ""
	@echo "Running Valgrind on test suite..."
	@echo ""
	@if command -v valgrind >/dev/null 2>&1; then \
		echo "=== Testing Edit tool with Valgrind ==="; \
		valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_EDIT_TARGET); \
		echo ""; \
		echo "=== Testing Read tool with Valgrind ==="; \
		valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_READ_TARGET); \
		echo ""; \
		echo "✓ Valgrind checks complete - no memory leaks detected!"; \
		echo ""; \
	else \
		echo "⚠ Warning: valgrind not found. Skipping memory leak detection."; \
		echo "  Install with: brew install valgrind (macOS) or apt-get install valgrind (Linux)"; \
		echo "  Note: On macOS, valgrind may have limited support."; \
		echo ""; \
	fi

# Comprehensive memory bug scan - runs all analysis tools
memscan: analyze sanitize-all
	@echo ""
	@echo "=========================================="
	@echo "Comprehensive Memory Bug Scan Complete"
	@echo "=========================================="
	@echo ""
	@echo "Completed checks:"
	@echo "  ✓ Static analysis (see $(BUILD_DIR)/analyze.log)"
	@echo "  ✓ Built with combined sanitizers ($(BUILD_DIR)/klawed-allsan)"
	@echo ""
	@echo "Next steps:"
	@echo "  1. Review static analysis results: cat $(BUILD_DIR)/analyze.log"
	@echo "  2. Test with sanitizers: ./$(BUILD_DIR)/klawed-allsan \"test prompt\""
	@echo "  3. Run Valgrind: make valgrind"
	@echo ""
	@echo "For production testing, run all test suites with sanitizers:"
	@echo "  ./$(BUILD_DIR)/klawed-allsan --help"
	@echo ""

# Comprehensive bug finding - runs ALL available analysis tools
comprehensive-scan: analyze sanitize-all valgrind
	@echo ""
	@echo "=========================================="
	@echo "Comprehensive Bug Finding Scan Complete"
	@echo "=========================================="
	@echo ""
	@echo "Completed checks:"
	@echo "  ✓ Static analysis (see $(BUILD_DIR)/analyze.log)"
	@echo "  ✓ Built with combined sanitizers ($(BUILD_DIR)/klawed-allsan)"
	@if command -v valgrind >/dev/null 2>&1; then \
		echo "  ✓ Valgrind memory leak detection"; \
	else \
		echo "  ○ Valgrind memory leak detection (valgrind not installed)"; \
		echo "    Note: On Apple Silicon (M1/M2/M3), valgrind may not work properly."; \
		echo "    Consider using sanitizers instead: make sanitize-all"; \
	fi
	@echo ""
	@echo "Additional tools available (install manually):"
	@command -v clang-tidy >/dev/null 2>&1 && echo "  ✓ clang-tidy (static analysis)" || echo "  ○ clang-tidy (not installed)"
	@command -v cppcheck >/dev/null 2>&1 && echo "  ✓ cppcheck (static analysis)" || echo "  ○ cppcheck (not installed)"
	@command -v flawfinder >/dev/null 2>&1 && echo "  ✓ flawfinder (security analysis)" || echo "  ○ flawfinder (not installed)"
	@command -v scan-build >/dev/null 2>&1 && echo "  ✓ scan-build (clang static analyzer)" || echo "  ○ scan-build (not installed)"
	@echo ""
	@echo "Installation commands:"
	@echo "  macOS: brew install llvm cppcheck flawfinder"
	@echo "  Linux: sudo apt-get install clang-tidy cppcheck flawfinder valgrind"
	@echo ""
	@echo "Or run the setup script:"
	@echo "  ./setup-analysis-tools.sh"
	@echo ""
	@echo "Note: On macOS, after installing llvm, you may need to add to PATH:"
	@echo "  export PATH=\"/opt/homebrew/opt/llvm/bin:\$$PATH\""
	@echo "  or create symlinks:"
	@echo "  ln -s /opt/homebrew/opt/llvm/bin/clang-tidy /opt/homebrew/bin/"
	@echo "  ln -s /opt/homebrew/opt/llvm/bin/scan-build /opt/homebrew/bin/"
	@echo ""
	@echo "Next steps:"
	@echo "  1. Review static analysis: cat $(BUILD_DIR)/analyze.log"
	@echo "  2. Test with sanitizers: ./$(BUILD_DIR)/klawed-allsan \"test prompt\""
	@echo "  3. Run individual sanitizer builds: make sanitize-ub sanitize-leak"
	@echo ""

# Quick static analysis with clang-tidy (if available)
clang-tidy:
	@# Check for clang-tidy in PATH or common Homebrew locations
	@if command -v clang-tidy >/dev/null 2>&1; then \
		: ; \
	elif [ -f "/opt/homebrew/opt/llvm/bin/clang-tidy" ]; then \
		echo "Error: clang-tidy found at /opt/homebrew/opt/llvm/bin/clang-tidy but not in PATH."; \
		echo "Add to PATH or create symlink:"; \
		echo "  export PATH=\"/opt/homebrew/opt/llvm/bin:\$$PATH\""; \
		echo "  or"; \
		echo "  ln -s /opt/homebrew/opt/llvm/bin/clang-tidy /opt/homebrew/bin/"; \
		echo ""; \
		echo "Run: ./setup-analysis-tools.sh to set up automatically."; \
		exit 1; \
	elif [ -f "/usr/local/opt/llvm/bin/clang-tidy" ]; then \
		echo "Error: clang-tidy found at /usr/local/opt/llvm/bin/clang-tidy but not in PATH."; \
		echo "Add to PATH or create symlink:"; \
		echo "  export PATH=\"/usr/local/opt/llvm/bin:\$$PATH\""; \
		echo "  or"; \
		echo "  ln -s /usr/local/opt/llvm/bin/clang-tidy /usr/local/bin/"; \
		echo ""; \
		echo "Run: ./setup-analysis-tools.sh to set up automatically."; \
		exit 1; \
	else \
		echo "Error: clang-tidy not found."; \
		echo "Install with:"; \
		echo "  macOS: brew install llvm (clang-tidy is in llvm package)"; \
		echo "  Linux: sudo apt-get install clang-tidy"; \
		echo ""; \
		echo "Then run: ./setup-analysis-tools.sh to set up automatically."; \
		exit 1; \
	fi
	@echo "Running clang-tidy static analysis..."
	@clang-tidy $(SRC) $(LOGGER_SRC) $(PERSISTENCE_SRC) $(MIGRATIONS_SRC) -- $(CFLAGS) 2>&1 | tee $(BUILD_DIR)/clang-tidy.log || true
	@echo ""
	@echo "✓ clang-tidy analysis complete. Results saved to $(BUILD_DIR)/clang-tidy.log"
	@echo ""

# Security-focused static analysis with cppcheck (if available)
cppcheck:
	@command -v cppcheck >/dev/null 2>&1 || { echo "Error: cppcheck not found."; echo "Install with:"; echo "  macOS: brew install cppcheck"; echo "  Linux: sudo apt-get install cppcheck"; exit 1; }
	@echo "Running cppcheck security analysis..."
	@cppcheck --enable=all --inconclusive --suppress=missingIncludeSystem $(SRC) $(LOGGER_SRC) $(PERSISTENCE_SRC) $(MIGRATIONS_SRC) 2>&1 | tee $(BUILD_DIR)/cppcheck.log || true
	@echo ""
	@echo "✓ cppcheck analysis complete. Results saved to $(BUILD_DIR)/cppcheck.log"
	@echo ""

# Security vulnerability scanning with flawfinder (if available)
flawfinder:
	@command -v flawfinder >/dev/null 2>&1 || { echo "Error: flawfinder not found."; echo "Install with:"; echo "  macOS: brew install flawfinder"; echo "  Linux: sudo apt-get install flawfinder"; exit 1; }
	@echo "Running flawfinder security analysis..."
	@flawfinder $(SRC) $(LOGGER_SRC) $(PERSISTENCE_SRC) $(MIGRATIONS_SRC) 2>&1 | tee $(BUILD_DIR)/flawfinder.log || true
	@echo ""
	@echo "✓ flawfinder analysis complete. Results saved to $(BUILD_DIR)/flawfinder.log"
	@echo ""

$(LOGGER_OBJ): $(LOGGER_SRC) src/logger.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(LOGGER_OBJ) $(LOGGER_SRC)

$(CONFIG_OBJ): $(CONFIG_SRC) src/config.h src/tui.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(CONFIG_OBJ) $(CONFIG_SRC)

$(PERSISTENCE_OBJ): $(PERSISTENCE_SRC) src/persistence.h src/token_usage_db.h src/migrations.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(PERSISTENCE_OBJ) $(PERSISTENCE_SRC)

$(TOKEN_USAGE_DB_OBJ): $(TOKEN_USAGE_DB_SRC) src/token_usage_db.h src/token_usage_db_migrations.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(TOKEN_USAGE_DB_OBJ) $(TOKEN_USAGE_DB_SRC)

$(TOKEN_USAGE_DB_MIGRATIONS_OBJ): $(TOKEN_USAGE_DB_MIGRATIONS_SRC) src/token_usage_db_migrations.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(TOKEN_USAGE_DB_MIGRATIONS_OBJ) $(TOKEN_USAGE_DB_MIGRATIONS_SRC)

$(MIGRATIONS_OBJ): $(MIGRATIONS_SRC) src/migrations.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(MIGRATIONS_OBJ) $(MIGRATIONS_SRC)

$(COMMANDS_OBJ): $(COMMANDS_SRC) src/commands.h src/theme_explorer.h src/help_modal.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(COMMANDS_OBJ) $(COMMANDS_SRC)

$(THEME_EXPLORER_OBJ): $(THEME_EXPLORER_SRC) src/theme_explorer.h src/builtin_themes.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(THEME_EXPLORER_OBJ) $(THEME_EXPLORER_SRC)

$(HELP_MODAL_OBJ): $(HELP_MODAL_SRC) src/help_modal.h src/tui.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(HELP_MODAL_OBJ) $(HELP_MODAL_SRC)

$(COMPLETION_OBJ): $(COMPLETION_SRC) src/completion.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(COMPLETION_OBJ) $(COMPLETION_SRC)

$(TUI_OBJ): $(TUI_SRC) src/tui.h src/klawed_internal.h src/file_search.h $(FILE_SEARCH_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(TUI_OBJ) $(TUI_SRC)

$(TUI_CORE_OBJ): $(TUI_CORE_SRC) src/tui.h src/tui_core.h src/klawed_internal.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(TUI_CORE_OBJ) $(TUI_CORE_SRC)

$(TUI_CONVERSATION_OBJ): $(TUI_CONVERSATION_SRC) src/tui.h src/tui_conversation.h src/klawed_internal.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(TUI_CONVERSATION_OBJ) $(TUI_CONVERSATION_SRC)

$(TUI_WINDOW_OBJ): $(TUI_WINDOW_SRC) src/tui.h src/tui_window.h src/klawed_internal.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(TUI_WINDOW_OBJ) $(TUI_WINDOW_SRC)

$(TUI_RENDER_OBJ): $(TUI_RENDER_SRC) src/tui.h src/tui_render.h src/klawed_internal.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(TUI_RENDER_OBJ) $(TUI_RENDER_SRC)

$(TUI_MODES_OBJ): $(TUI_MODES_SRC) src/tui.h src/tui_modes.h src/klawed_internal.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(TUI_MODES_OBJ) $(TUI_MODES_SRC)

$(TUI_INPUT_OBJ): $(TUI_INPUT_SRC) src/tui.h src/tui_input.h src/klawed_internal.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(TUI_INPUT_OBJ) $(TUI_INPUT_SRC)

$(TUI_HISTORY_OBJ): $(TUI_HISTORY_SRC) src/tui.h src/tui_history.h src/klawed_internal.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(TUI_HISTORY_OBJ) $(TUI_HISTORY_SRC)

$(TUI_COMPLETION_OBJ): $(TUI_COMPLETION_SRC) src/tui.h src/tui_completion.h src/klawed_internal.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(TUI_COMPLETION_OBJ) $(TUI_COMPLETION_SRC)

$(TUI_PASTE_OBJ): $(TUI_PASTE_SRC) src/tui.h src/tui_paste.h src/klawed_internal.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(TUI_PASTE_OBJ) $(TUI_PASTE_SRC)

$(TUI_SEARCH_OBJ): $(TUI_SEARCH_SRC) src/tui.h src/tui_search.h src/klawed_internal.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(TUI_SEARCH_OBJ) $(TUI_SEARCH_SRC)

$(HISTORY_SEARCH_OBJ): $(HISTORY_SEARCH_SRC) src/history_search.h src/logger.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(HISTORY_SEARCH_OBJ) $(HISTORY_SEARCH_SRC)

$(FILE_SEARCH_OBJ): $(FILE_SEARCH_SRC) src/file_search.h src/logger.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(FILE_SEARCH_OBJ) $(FILE_SEARCH_SRC)

$(HISTORY_FILE_OBJ): $(HISTORY_FILE_SRC) src/history_file.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(HISTORY_FILE_OBJ) $(HISTORY_FILE_SRC)


$(WINDOW_MANAGER_OBJ): $(WINDOW_MANAGER_SRC) src/window_manager.h src/logger.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(WINDOW_MANAGER_OBJ) $(WINDOW_MANAGER_SRC)
$(AI_WORKER_OBJ): $(AI_WORKER_SRC) src/ai_worker.h src/message_queue.h src/klawed_internal.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(AI_WORKER_OBJ) $(AI_WORKER_SRC)

$(VOICE_INPUT_OBJ): $(VOICE_INPUT_SRC) src/voice_input.h src/logger.h $(VOICE_LIBS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(VOICE_INPUT_OBJ) $(VOICE_INPUT_SRC)

$(MCP_OBJ): $(MCP_SRC) src/mcp.h src/logger.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(MCP_OBJ) $(MCP_SRC)

$(MCP_TEST_OBJ): $(MCP_SRC) src/mcp.h src/logger.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(MCP_TEST_OBJ) $(MCP_SRC)

$(TODO_OBJ): $(TODO_SRC) src/todo.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(TODO_OBJ) $(TODO_SRC)

$(AWS_BEDROCK_OBJ): $(AWS_BEDROCK_SRC) src/aws_bedrock.h src/logger.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(AWS_BEDROCK_OBJ) $(AWS_BEDROCK_SRC)

$(BEDROCK_CONVERSE_OBJ): $(BEDROCK_CONVERSE_SRC) src/bedrock_converse.h src/aws_bedrock.h src/logger.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BEDROCK_CONVERSE_OBJ) $(BEDROCK_CONVERSE_SRC)

$(PROVIDER_OBJ): $(PROVIDER_SRC) src/provider.h src/openai_provider.h src/bedrock_provider.h src/anthropic_provider.h src/deepseek_provider.h src/moonshot_provider.h src/logger.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(PROVIDER_OBJ) $(PROVIDER_SRC)

$(PROVIDER_CONFIG_LOADER_OBJ): $(PROVIDER_CONFIG_LOADER_SRC) src/provider_config_loader.h src/config.h src/logger.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(PROVIDER_CONFIG_LOADER_OBJ) $(PROVIDER_CONFIG_LOADER_SRC)

$(OPENAI_PROVIDER_OBJ): $(OPENAI_PROVIDER_SRC) src/openai_provider.h src/provider.h src/logger.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(OPENAI_PROVIDER_OBJ) $(OPENAI_PROVIDER_SRC)

$(ANTHROPIC_PROVIDER_OBJ): $(ANTHROPIC_PROVIDER_SRC) src/anthropic_provider.h src/provider.h src/logger.h src/openai_messages.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(ANTHROPIC_PROVIDER_OBJ) $(ANTHROPIC_PROVIDER_SRC)

$(DEEPSEEK_PROVIDER_OBJ): $(DEEPSEEK_PROVIDER_SRC) src/deepseek_provider.h src/provider.h src/logger.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(DEEPSEEK_PROVIDER_OBJ) $(DEEPSEEK_PROVIDER_SRC)

$(MOONSHOT_PROVIDER_OBJ): $(MOONSHOT_PROVIDER_SRC) src/moonshot_provider.h src/provider.h src/logger.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(MOONSHOT_PROVIDER_OBJ) $(MOONSHOT_PROVIDER_SRC)

$(OPENAI_MESSAGES_OBJ): $(OPENAI_MESSAGES_SRC) src/openai_messages.h src/klawed_internal.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(OPENAI_MESSAGES_OBJ) $(OPENAI_MESSAGES_SRC)

$(BEDROCK_PROVIDER_OBJ): $(BEDROCK_PROVIDER_SRC) src/bedrock_provider.h src/bedrock_converse.h src/provider.h src/aws_bedrock.h src/logger.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BEDROCK_PROVIDER_OBJ) $(BEDROCK_PROVIDER_SRC)

$(BUILTIN_THEMES_OBJ): $(BUILTIN_THEMES_SRC) src/builtin_themes.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BUILTIN_THEMES_OBJ) $(BUILTIN_THEMES_SRC)

$(MESSAGE_QUEUE_OBJ): $(MESSAGE_QUEUE_SRC) src/message_queue.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(MESSAGE_QUEUE_OBJ) $(MESSAGE_QUEUE_SRC)

$(TOOL_UTILS_OBJ): $(TOOL_UTILS_SRC) src/tool_utils.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(TOOL_UTILS_OBJ) $(TOOL_UTILS_SRC)

$(PROCESS_UTILS_OBJ): $(PROCESS_UTILS_SRC) src/process_utils.h src/logger.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(PROCESS_UTILS_OBJ) $(PROCESS_UTILS_SRC)

$(SUBAGENT_MANAGER_OBJ): $(SUBAGENT_MANAGER_SRC) src/subagent_manager.h src/logger.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(SUBAGENT_MANAGER_OBJ) $(SUBAGENT_MANAGER_SRC)

$(EXPLORE_TOOLS_OBJ): $(EXPLORE_TOOLS_SRC) src/explore_tools.h src/logger.h src/http_client.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(EXPLORE_TOOLS_OBJ) $(EXPLORE_TOOLS_SRC)

$(BASE64_OBJ): $(BASE64_SRC) src/base64.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(BASE64_OBJ) $(BASE64_SRC)

$(ARRAY_RESIZE_OBJ): $(ARRAY_RESIZE_SRC) src/array_resize.h src/logger.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $(ARRAY_RESIZE_OBJ) $(ARRAY_RESIZE_SRC)

# Query tool - utility to inspect API call logs
$(QUERY_TOOL): $(QUERY_TOOL_SRC) $(PERSISTENCE_OBJ) $(TOKEN_USAGE_DB_OBJ) $(TOKEN_USAGE_DB_MIGRATIONS_OBJ) $(MIGRATIONS_OBJ)
	@mkdir -p $(BUILD_DIR)
	@echo "Building query tool..."
	@$(CC) $(CFLAGS) -o $(QUERY_TOOL) $(QUERY_TOOL_SRC) $(PERSISTENCE_OBJ) $(TOKEN_USAGE_DB_OBJ) $(TOKEN_USAGE_DB_MIGRATIONS_OBJ) $(MIGRATIONS_OBJ) -lsqlite3
	@echo ""
	@echo "✓ Query tool built successfully!"
	@echo "Run: ./$(QUERY_TOOL) --help"
	@echo ""

# Memvid REPL - interactive tool for testing memvid files
$(MEMVID_REPL): $(MEMVID_REPL_SRC) $(MEMVID_SRC) $(LOGGER_OBJ)
ifeq ($(MEMVID),0)
	@echo "Error: memvid-repl requires memvid support"
	@echo "Build with: make MEMVID=1 memvid-repl"
	@exit 1
else ifeq ($(shell test -f $(MEMVID_FFI_LIB) || echo no),no)
ifeq ($(MEMVID),1)
	@echo "Error: memvid-ffi library not found at $(MEMVID_FFI_LIB)"
	@echo "Build it first with: make memvid-ffi"
	@exit 1
else
	@echo "Error: memvid-repl requires memvid support"
	@echo "Build the FFI library first with: make memvid-ffi"
	@echo "Then build with: make MEMVID=1 memvid-repl"
	@exit 1
endif
else
	@mkdir -p $(BUILD_DIR)
	@echo "Building memvid REPL..."
	$(CC) $(CFLAGS) -DHAVE_MEMVID=1 -o $(MEMVID_REPL) $(MEMVID_REPL_SRC) $(MEMVID_SRC) $(LOGGER_OBJ) $(MEMVID_LIBS) -lbsd -lpthread
	@echo ""
	@echo "✓ Memvid REPL built successfully!"
	@echo "Run: ./$(MEMVID_REPL) [path/to/memory.mv2]"
	@echo ""
endif

# Test target for Window Manager - layout and pad capacity behavior
$(TEST_WM_TARGET): $(TEST_WM_SRC) $(WINDOW_MANAGER_OBJ) $(LOGGER_OBJ) $(DATA_DIR_OBJ)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling Window Manager tests..."
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_window_manager.o $(TEST_WM_SRC)
	@echo "Linking Window Manager test executable..."
	@$(CC) -o $(TEST_WM_TARGET) $(BUILD_DIR)/test_window_manager.o $(WINDOW_MANAGER_OBJ) $(LOGGER_OBJ) $(DATA_DIR_OBJ) $(LDFLAGS)
	@echo ""
	@echo "✓ Window Manager test build successful!"
	@echo ""

# Test target for Edit tool - compiles test suite with claude.c functions
# We rename claude's main to avoid conflict with test's main
# and export internal functions via TEST_BUILD flag
# Helper to ensure sqlite_queue_test.o is built (needs -DTEST_BUILD)
define BUILD_SQLITE_QUEUE_TEST_OBJ
	@if [ ! -f "$(SQLITE_QUEUE_TEST_OBJ)" ]; then \
		echo "Building $(SQLITE_QUEUE_TEST_OBJ)..."; \
		$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(SQLITE_QUEUE_TEST_OBJ) src/sqlite_queue.c; \
	fi
endef

# Helper to ensure api_client_test.o is built (needs -DTEST_BUILD to work with sqlite_queue_test.o)
define BUILD_API_CLIENT_TEST_OBJ
	@if [ ! -f "$(API_CLIENT_TEST_OBJ)" ]; then \
		echo "Building $(API_CLIENT_TEST_OBJ)..."; \
		$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(API_CLIENT_TEST_OBJ) src/api/api_client.c; \
	fi
endef

define BUILD_TOOL_SYSTEM_TEST_OBJS
	@if [ ! -f "$(TOOL_REGISTRY_TEST_OBJ)" ]; then \
		echo "Building $(TOOL_REGISTRY_TEST_OBJ)..."; \
		$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(TOOL_REGISTRY_TEST_OBJ) src/tools/tool_registry.c; \
	fi
	@if [ ! -f "$(TOOL_EXECUTOR_TEST_OBJ)" ]; then \
		echo "Building $(TOOL_EXECUTOR_TEST_OBJ)..."; \
		$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(TOOL_EXECUTOR_TEST_OBJ) src/tools/tool_executor.c; \
	fi
	@if [ ! -f "$(TOOL_DEFINITIONS_TEST_OBJ)" ]; then \
		echo "Building $(TOOL_DEFINITIONS_TEST_OBJ)..."; \
		$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(TOOL_DEFINITIONS_TEST_OBJ) src/tools/tool_definitions.c; \
	fi
endef

$(TEST_EDIT_TARGET): $(SRC) $(TEST_EDIT_SRC) $(TEST_COMMON_OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling claude.c for testing (renaming main)..."
	@$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(BUILD_DIR)/claude_test.o $(SRC)
	@echo "Compiling Edit tool test suite..."
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_edit.o $(TEST_EDIT_SRC)
	$(BUILD_SQLITE_QUEUE_TEST_OBJ)
	$(BUILD_API_CLIENT_TEST_OBJ)
	$(BUILD_TOOL_SYSTEM_TEST_OBJS)
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_EDIT_TARGET) $(BUILD_DIR)/claude_test.o $(BUILD_DIR)/test_edit.o $(SQLITE_QUEUE_TEST_OBJ) $(TOOL_REGISTRY_TEST_OBJ) $(TOOL_EXECUTOR_TEST_OBJ) $(TOOL_DEFINITIONS_TEST_OBJ) $(TEST_COMMON_OBJS) $(LDFLAGS)
	@echo ""
	@echo "✓ Edit tool test build successful!"
	@echo ""

# Test target for Edit tool regex enhancements
$(TEST_EDIT_REGEX_TARGET): $(SRC) $(TEST_EDIT_REGEX_SRC) $(TEST_COMMON_OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling claude.c for testing (renaming main)..."
	@$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(BUILD_DIR)/claude_test.o $(SRC)
	@echo "Compiling Edit tool regex enhancement test suite..."
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_edit_regex.o $(TEST_EDIT_REGEX_SRC)
	$(BUILD_SQLITE_QUEUE_TEST_OBJ)
	$(BUILD_API_CLIENT_TEST_OBJ)
	$(BUILD_TOOL_SYSTEM_TEST_OBJS)
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_EDIT_REGEX_TARGET) $(BUILD_DIR)/claude_test.o $(BUILD_DIR)/test_edit_regex.o $(SQLITE_QUEUE_TEST_OBJ) $(TOOL_REGISTRY_TEST_OBJ) $(TOOL_EXECUTOR_TEST_OBJ) $(TOOL_DEFINITIONS_TEST_OBJ) $(TEST_COMMON_OBJS) $(LDFLAGS)
	@echo ""
	@echo "✓ Edit tool regex enhancement test build successful!"
	@echo ""

# Test target for Read tool - compiles test suite with claude.c functions
$(TEST_READ_TARGET): $(SRC) $(TEST_READ_SRC) $(TEST_COMMON_OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling claude.c for read testing..."
	@$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(BUILD_DIR)/claude_read_test.o $(SRC)
	@echo "Compiling Read tool test suite..."
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_read.o $(TEST_READ_SRC)
	$(BUILD_SQLITE_QUEUE_TEST_OBJ)
	$(BUILD_API_CLIENT_TEST_OBJ)
	$(BUILD_TOOL_SYSTEM_TEST_OBJS)
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_READ_TARGET) $(BUILD_DIR)/claude_read_test.o $(BUILD_DIR)/test_read.o $(SQLITE_QUEUE_TEST_OBJ) $(TOOL_REGISTRY_TEST_OBJ) $(TOOL_EXECUTOR_TEST_OBJ) $(TOOL_DEFINITIONS_TEST_OBJ) $(TEST_COMMON_OBJS) $(LDFLAGS)
	@echo ""
	@echo "✓ Read tool test build successful!"
	@echo ""


# Test target for TODO list - tests task management functionality
$(TEST_TODO_TARGET): $(TODO_SRC) $(TEST_TODO_SRC) tests/test_todo_stubs.c
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling TODO list test suite..."
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/todo_test.o $(TODO_SRC)
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_todo.o $(TEST_TODO_SRC)
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_todo_stubs.o tests/test_todo_stubs.c
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_TODO_TARGET) $(BUILD_DIR)/todo_test.o $(BUILD_DIR)/test_todo.o $(BUILD_DIR)/test_todo_stubs.o $(LDFLAGS)
	@echo ""
	@echo "✓ TODO list test build successful!"
	@echo ""

# Test target for TodoWrite tool - tests integration with claude.c
$(TEST_TODO_WRITE_TARGET): $(SRC) $(TEST_TODO_WRITE_SRC) $(TEST_COMMON_OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling claude.c for TodoWrite testing (renaming main)..."
	@$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(BUILD_DIR)/claude_todowrite_test.o $(SRC)
	@echo "Compiling TodoWrite tool test suite..."
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_todo_write.o $(TEST_TODO_WRITE_SRC)
	$(BUILD_SQLITE_QUEUE_TEST_OBJ)
	$(BUILD_API_CLIENT_TEST_OBJ)
	$(BUILD_TOOL_SYSTEM_TEST_OBJS)
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_TODO_WRITE_TARGET) $(BUILD_DIR)/claude_todowrite_test.o $(BUILD_DIR)/test_todo_write.o $(SQLITE_QUEUE_TEST_OBJ) $(TOOL_REGISTRY_TEST_OBJ) $(TOOL_EXECUTOR_TEST_OBJ) $(TOOL_DEFINITIONS_TEST_OBJ) $(TEST_COMMON_OBJS) $(LDFLAGS)
	@echo ""
	@echo "✓ TodoWrite tool test build successful!"
	@echo ""

# Test target for compaction - tests context compaction functionality
$(TEST_COMPACTION_TARGET): $(COMPACTION_SRC) $(TEST_COMPACTION_SRC) tests/test_compaction_stubs.c
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling compaction test suite..."
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/compaction_test.o $(COMPACTION_SRC)
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_compaction.o $(TEST_COMPACTION_SRC)
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_compaction_stubs.o tests/test_compaction_stubs.c
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_COMPACTION_TARGET) $(BUILD_DIR)/compaction_test.o $(BUILD_DIR)/test_compaction.o $(BUILD_DIR)/test_compaction_stubs.o $(LDFLAGS)
	@echo ""
	@echo "✓ Compaction test build successful!"
	@echo ""

# Test target for Paste Handler - tests paste detection and sanitization
$(TEST_PASTE_TARGET): $(TEST_PASTE_SRC)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling Paste Handler test suite..."
	@$(CC) $(CFLAGS) -o $(TEST_PASTE_TARGET) $(TEST_PASTE_SRC)
	@echo ""
	@echo "✓ Paste Handler test build successful!"
	@echo ""

# Test target for Bash Timeout - tests bash command timeout functionality
$(TEST_BASH_TIMEOUT_TARGET): $(SRC) $(TEST_BASH_TIMEOUT_SRC) $(TEST_COMMON_OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling claude.c for bash timeout testing (renaming main)..."
	@$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(BUILD_DIR)/claude_bash_timeout_test.o $(SRC)
	@echo "Compiling Bash timeout test suite..."
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_bash_timeout.o $(TEST_BASH_TIMEOUT_SRC)
	$(BUILD_SQLITE_QUEUE_TEST_OBJ)
	$(BUILD_API_CLIENT_TEST_OBJ)
	$(BUILD_TOOL_SYSTEM_TEST_OBJS)
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_BASH_TIMEOUT_TARGET) $(BUILD_DIR)/claude_bash_timeout_test.o $(BUILD_DIR)/test_bash_timeout.o $(SQLITE_QUEUE_TEST_OBJ) $(TOOL_REGISTRY_TEST_OBJ) $(TOOL_EXECUTOR_TEST_OBJ) $(TOOL_DEFINITIONS_TEST_OBJ) $(TEST_COMMON_OBJS) $(LDFLAGS)
	@echo ""
	@echo "✓ Bash timeout test build successful!"
	@echo ""

# Test target for Bash Stderr Output Fix - tests stderr capture and redirection
$(TEST_BASH_STDERR_TARGET): $(SRC) $(TEST_BASH_STDERR_SRC) $(TEST_COMMON_OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling claude.c for bash stderr testing (renaming main)..."
	@$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(BUILD_DIR)/claude_bash_stderr_test.o $(SRC)
	@echo "Compiling Bash stderr test suite..."
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_bash_stderr.o $(TEST_BASH_STDERR_SRC)
	$(BUILD_SQLITE_QUEUE_TEST_OBJ)
	$(BUILD_API_CLIENT_TEST_OBJ)
	$(BUILD_TOOL_SYSTEM_TEST_OBJS)
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_BASH_STDERR_TARGET) $(BUILD_DIR)/claude_bash_stderr_test.o $(BUILD_DIR)/test_bash_stderr.o $(SQLITE_QUEUE_TEST_OBJ) $(TOOL_REGISTRY_TEST_OBJ) $(TOOL_EXECUTOR_TEST_OBJ) $(TOOL_DEFINITIONS_TEST_OBJ) $(TEST_COMMON_OBJS) $(LDFLAGS)
	@echo ""
	@echo "✓ Bash stderr test build successful!"
	@echo ""

# Test target for Bash Output Truncation - tests output size limiting and truncation
$(TEST_BASH_TRUNCATION_TARGET): $(SRC) $(TEST_BASH_TRUNCATION_SRC) $(TEST_COMMON_OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling claude.c for bash truncation testing (renaming main)..."
	@$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(BUILD_DIR)/claude_bash_truncation_test.o $(SRC)
	@echo "Compiling Bash truncation test suite..."
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_bash_truncation.o $(TEST_BASH_TRUNCATION_SRC)
	$(BUILD_SQLITE_QUEUE_TEST_OBJ)
	$(BUILD_API_CLIENT_TEST_OBJ)
	$(BUILD_TOOL_SYSTEM_TEST_OBJS)
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_BASH_TRUNCATION_TARGET) $(BUILD_DIR)/claude_bash_truncation_test.o $(BUILD_DIR)/test_bash_truncation.o $(SQLITE_QUEUE_TEST_OBJ) $(TOOL_REGISTRY_TEST_OBJ) $(TOOL_EXECUTOR_TEST_OBJ) $(TOOL_DEFINITIONS_TEST_OBJ) $(TEST_COMMON_OBJS) $(LDFLAGS)
	@echo ""
	@echo "✓ Bash truncation test build successful!"
	@echo ""

# Test target for JSON Parsing - tests JSON parsing error handling patterns
$(TEST_JSON_PARSING_TARGET): $(TEST_JSON_PARSING_SRC)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling JSON parsing test suite..."
	@$(CC) $(CFLAGS) -o $(TEST_JSON_PARSING_TARGET) $(TEST_JSON_PARSING_SRC) $(LDFLAGS)
	@echo ""
	@echo "✓ JSON parsing test build successful!"
	@echo ""

# Test target for Tool Details - tests MCP and built-in tool display
$(TEST_TOOL_DETAILS_TARGET): $(TEST_TOOL_DETAILS_SRC)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling Tool Details test suite..."
	@$(CC) $(CFLAGS) -o $(TEST_TOOL_DETAILS_TARGET) $(TEST_TOOL_DETAILS_SRC) $(LDFLAGS)
	@echo ""
	@echo "✓ Tool Details test build successful!"
	@echo ""

# Test target for Duplicate Tool Detection - tests detect_duplicate_tool_names()
$(TEST_DUPLICATE_TOOL_DETECTION_TARGET): $(TEST_DUPLICATE_TOOL_DETECTION_SRC) $(TOOL_DEFINITIONS_TEST_OBJ) $(TEST_STUBS_OBJ) $(LOGGER_OBJ) $(DATA_DIR_OBJ)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling Duplicate Tool Detection test suite..."
	@$(CC) $(CFLAGS) -o $(TEST_DUPLICATE_TOOL_DETECTION_TARGET) $(TEST_DUPLICATE_TOOL_DETECTION_SRC) $(TOOL_DEFINITIONS_TEST_OBJ) $(TEST_STUBS_OBJ) $(LOGGER_OBJ) $(DATA_DIR_OBJ) $(LDFLAGS)
	@echo ""
	@echo "✓ Duplicate Tool Detection test build successful!"
	@echo ""

# Test target for Array Resize - tests array/buffer resize utilities
$(TEST_ARRAY_RESIZE_TARGET): $(TEST_ARRAY_RESIZE_SRC) $(ARRAY_RESIZE_OBJ) $(LOGGER_OBJ) $(DATA_DIR_OBJ)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling Array Resize test suite..."
	@$(CC) $(CFLAGS) -o $(TEST_ARRAY_RESIZE_TARGET) $(TEST_ARRAY_RESIZE_SRC) $(ARRAY_RESIZE_OBJ) $(LOGGER_OBJ) $(DATA_DIR_OBJ) $(LDFLAGS)
	@echo ""
	@echo "✓ Array Resize test build successful!"
	@echo ""

$(TEST_ARENA_TARGET): $(TEST_ARENA_SRC)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling Arena Allocator test suite..."
	@$(CC) $(CFLAGS) -DARENA_IMPLEMENTATION -o $(TEST_ARENA_TARGET) $(TEST_ARENA_SRC) $(LDFLAGS)
	@echo ""
	@echo "✓ Arena Allocator test build successful!"
	@echo ""

# Test target for Config module - tests save/load and style conversion
$(TEST_CONFIG_TARGET): tests/test_config.c $(CONFIG_OBJ) $(LOGGER_OBJ) $(DATA_DIR_OBJ)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling Config test suite..."
	@$(CC) $(CFLAGS) -o $(TEST_CONFIG_TARGET) tests/test_config.c $(CONFIG_OBJ) $(LOGGER_OBJ) $(DATA_DIR_OBJ) $(LDFLAGS)
	@echo ""
	@echo "✓ Config test build successful!"
	@echo ""

# Test target for Config merge - tests that saving doesn't copy global config to local
$(TEST_CONFIG_MERGE_TARGET): tests/test_config_merge.c $(CONFIG_OBJ) $(LOGGER_OBJ) $(DATA_DIR_OBJ)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling Config merge test suite..."
	@$(CC) $(CFLAGS) -o $(TEST_CONFIG_MERGE_TARGET) tests/test_config_merge.c $(CONFIG_OBJ) $(LOGGER_OBJ) $(DATA_DIR_OBJ) $(LDFLAGS) -lcjson
	@echo ""
	@echo "✓ Config merge test build successful!"
	@echo ""

# Test target for Data Directory utilities
$(TEST_DATA_DIR_TARGET): tests/test_data_dir.c $(DATA_DIR_OBJ)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling Data Directory test suite..."
	@$(CC) $(CFLAGS) -o $(TEST_DATA_DIR_TARGET) tests/test_data_dir.c $(DATA_DIR_OBJ) $(LDFLAGS)
	@echo ""
	@echo "✓ Data Directory test build successful!"
	@echo ""

# Test target for provider_init_from_config - tests provider initialization from LLMProviderConfig
$(TEST_PROVIDER_INIT_FROM_CONFIG_TARGET): $(SRC) tests/test_provider_init_from_config.c $(TEST_COMMON_OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling klawed.c for provider_init_from_config testing (renaming main)..."
	@$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(BUILD_DIR)/claude_provider_test.o $(SRC)
	@echo "Compiling provider_init_from_config test suite..."
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_provider_init_from_config.o tests/test_provider_init_from_config.c
	$(BUILD_SQLITE_QUEUE_TEST_OBJ)
	$(BUILD_API_CLIENT_TEST_OBJ)
	$(BUILD_TOOL_SYSTEM_TEST_OBJS)
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_PROVIDER_INIT_FROM_CONFIG_TARGET) $(BUILD_DIR)/claude_provider_test.o $(BUILD_DIR)/test_provider_init_from_config.o $(SQLITE_QUEUE_TEST_OBJ) $(TOOL_REGISTRY_TEST_OBJ) $(TOOL_EXECUTOR_TEST_OBJ) $(TOOL_DEFINITIONS_TEST_OBJ) $(TEST_COMMON_OBJS) $(LDFLAGS)
	@echo ""
	@echo "✓ provider_init_from_config test build successful!"
	@echo ""

# Test target for provider_init - tests that named provider's model takes precedence
TEST_PROVIDER_INIT_TARGET = $(BUILD_DIR)/test_provider_init
$(TEST_PROVIDER_INIT_TARGET): $(SRC) tests/test_provider_init.c $(TEST_COMMON_OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling klawed.c for provider_init testing (renaming main)..."
	@$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(BUILD_DIR)/provider_init_test.o $(SRC)
	@echo "Compiling provider_init test suite..."
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_provider_init.o tests/test_provider_init.c
	$(BUILD_SQLITE_QUEUE_TEST_OBJ)
	$(BUILD_API_CLIENT_TEST_OBJ)
	$(BUILD_TOOL_SYSTEM_TEST_OBJS)
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_PROVIDER_INIT_TARGET) $(BUILD_DIR)/provider_init_test.o $(BUILD_DIR)/test_provider_init.o $(SQLITE_QUEUE_TEST_OBJ) $(TOOL_REGISTRY_TEST_OBJ) $(TOOL_EXECUTOR_TEST_OBJ) $(TOOL_DEFINITIONS_TEST_OBJ) $(TEST_COMMON_OBJS) $(LDFLAGS)
	@echo ""
	@echo "✓ provider_init test build successful!"
	@echo ""

test-provider-init: $(TEST_PROVIDER_INIT_TARGET)
	@echo ""
	@echo "Running provider_init tests..."
	@echo ""
	@./$(TEST_PROVIDER_INIT_TARGET)

# Test target for Memvid Memory Tools - tests parameter validation and availability
# Note: We compile memvid.c directly here with matching HAVE_MEMVID setting
$(TEST_MEMVID_TARGET): $(TEST_MEMVID_SRC) $(LOGGER_OBJ) $(DATA_DIR_OBJ)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling Memvid Memory Tool test suite..."
	@$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(BUILD_DIR)/memvid_test.o src/memvid.c
	@$(CC) $(CFLAGS) -DTEST_BUILD -o $(TEST_MEMVID_TARGET) $(TEST_MEMVID_SRC) $(BUILD_DIR)/memvid_test.o $(LOGGER_OBJ) $(DATA_DIR_OBJ) $(MEMVID_LIBS) $(LDFLAGS)
	@echo ""
	@echo "✓ Memvid Memory Tool test build successful!"
	@echo ""

# Test target for Token Usage - tests token usage tracking functionality
$(TEST_TOKEN_USAGE_TARGET): $(TEST_TOKEN_USAGE_SRC) $(LOGGER_OBJ) $(PERSISTENCE_OBJ) $(TOKEN_USAGE_DB_OBJ) $(TOKEN_USAGE_DB_MIGRATIONS_OBJ) $(MIGRATIONS_OBJ) $(DATA_DIR_OBJ)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling Token Usage test suite..."
	@$(CC) $(CFLAGS) -o $(TEST_TOKEN_USAGE_TARGET) $(TEST_TOKEN_USAGE_SRC) $(LOGGER_OBJ) $(PERSISTENCE_OBJ) $(TOKEN_USAGE_DB_OBJ) $(TOKEN_USAGE_DB_MIGRATIONS_OBJ) $(MIGRATIONS_OBJ) $(DATA_DIR_OBJ) $(LDFLAGS)
	@echo ""
	@echo "✓ Token Usage test build successful!"
	@echo ""

# Test target for Retry Jitter - tests exponential backoff with jitter
$(TEST_RETRY_JITTER_TARGET): $(TEST_RETRY_JITTER_SRC)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling Retry Jitter test suite..."
	@$(CC) $(CFLAGS) -o $(TEST_RETRY_JITTER_TARGET) $(TEST_RETRY_JITTER_SRC) -lm
	@echo ""
	@echo "✓ Retry Jitter test build successful!"
	@echo ""

# Test target for tool timing - ensures no 60-second delays
$(TEST_TIMING_TARGET): tests/test_tool_timing.c
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling tool timing test suite..."
	@$(CC) $(CFLAGS) -o $(TEST_TIMING_TARGET) tests/test_tool_timing.c -lpthread
	@echo ""
	@echo "✓ Tool timing test build successful!"
	@echo ""

# Test target for tool results regression - demonstrates bug in commit 414fbe8
$(TEST_TOOL_RESULTS_REGRESSION_TARGET): $(SRC) $(TEST_TOOL_RESULTS_REGRESSION_SRC) $(TEST_COMMON_OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling claude.c for tool results regression testing (renaming main)..."
	@$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(BUILD_DIR)/claude_tool_results_test.o $(SRC)
	@echo "Compiling tool results regression test suite..."
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_tool_results_regression.o $(TEST_TOOL_RESULTS_REGRESSION_SRC)
	$(BUILD_SQLITE_QUEUE_TEST_OBJ)
	$(BUILD_API_CLIENT_TEST_OBJ)
	$(BUILD_TOOL_SYSTEM_TEST_OBJS)
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_TOOL_RESULTS_REGRESSION_TARGET) $(BUILD_DIR)/claude_tool_results_test.o $(BUILD_DIR)/test_tool_results_regression.o $(SQLITE_QUEUE_TEST_OBJ) $(TOOL_REGISTRY_TEST_OBJ) $(TOOL_EXECUTOR_TEST_OBJ) $(TOOL_DEFINITIONS_TEST_OBJ) $(TEST_COMMON_OBJS) $(LDFLAGS)
	@echo ""
	@echo "✓ Tool results regression test build successful!"
	@echo ""

# Test target for Base64 encoding/decoding
$(TEST_BASE64_TARGET): $(BASE64_SRC) $(TEST_BASE64_SRC)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling Base64 implementation..."
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/base64_test.o $(BASE64_SRC)
	@echo "Compiling Base64 test suite..."
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_base64.o $(TEST_BASE64_SRC)
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_BASE64_TARGET) $(BUILD_DIR)/base64_test.o $(BUILD_DIR)/test_base64.o $(LDFLAGS)
	@echo ""
	@echo "✓ Base64 test build successful!"
	@echo ""

# Test target for OpenAI message format validation
$(TEST_OPENAI_FORMAT_TARGET): $(TEST_OPENAI_FORMAT_SRC)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling OpenAI format validation test suite..."
	@$(CC) $(CFLAGS) -o $(TEST_OPENAI_FORMAT_TARGET) $(TEST_OPENAI_FORMAT_SRC) $(LDFLAGS)
	@echo ""
	@echo "✓ OpenAI format test build successful!"
	@echo ""

# Test target for OpenAI Responses API parsing
$(TEST_OPENAI_RESPONSES_TARGET): $(TEST_OPENAI_RESPONSES_SRC)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling OpenAI Responses API parsing test suite..."
	@$(CC) $(CFLAGS) -o $(TEST_OPENAI_RESPONSES_TARGET) $(TEST_OPENAI_RESPONSES_SRC) $(LDFLAGS)
	@echo ""
	@echo "✓ OpenAI Responses API test build successful!"
	@echo ""

# Test target for OpenAI response parsing (regression tests for session loading)
$(TEST_OPENAI_RESPONSE_PARSING_TARGET): $(SRC) $(TEST_OPENAI_RESPONSE_PARSING_SRC) $(TEST_COMMON_OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling klawed.c for OpenAI response parsing testing (renaming main)..."
	@$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(BUILD_DIR)/claude_openai_response_test.o $(SRC)
	@echo "Compiling OpenAI response parsing regression test suite..."
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_openai_response_parsing.o $(TEST_OPENAI_RESPONSE_PARSING_SRC)
	$(BUILD_SQLITE_QUEUE_TEST_OBJ)
	$(BUILD_API_CLIENT_TEST_OBJ)
	$(BUILD_TOOL_SYSTEM_TEST_OBJS)
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_OPENAI_RESPONSE_PARSING_TARGET) $(BUILD_DIR)/claude_openai_response_test.o $(BUILD_DIR)/test_openai_response_parsing.o $(SQLITE_QUEUE_TEST_OBJ) $(TOOL_REGISTRY_TEST_OBJ) $(TOOL_EXECUTOR_TEST_OBJ) $(TOOL_DEFINITIONS_TEST_OBJ) $(TEST_COMMON_OBJS) $(LDFLAGS)
	@echo ""
	@echo "✓ OpenAI response parsing regression test build successful!"
	@echo ""

# Test target for conversation memory management (regression tests for double-free)
# Compile free_internal_message from openai_messages.c directly
$(TEST_CONVERSATION_FREE_TARGET): $(OPENAI_MESSAGES_SRC) $(LOGGER_SRC) $(TEST_CONVERSATION_FREE_SRC) $(DATA_DIR_OBJ)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling openai_messages.c for memory management tests..."
	@$(CC) $(CFLAGS) -I./src -c -o $(BUILD_DIR)/openai_messages_free.o $(OPENAI_MESSAGES_SRC)
	@echo "Compiling conversation memory management test suite..."
	@$(CC) $(CFLAGS) -I./src -c -o $(BUILD_DIR)/test_conversation_free.o $(TEST_CONVERSATION_FREE_SRC)
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_CONVERSATION_FREE_TARGET) $(BUILD_DIR)/openai_messages_free.o $(BUILD_DIR)/test_conversation_free.o $(LOGGER_OBJ) $(DATA_DIR_OBJ) $(LDFLAGS)
	@echo ""
	@echo "✓ Conversation memory management test build successful!"
	@echo ""

# Test target for memory NULL fix (standalone test)
$(TEST_MEMORY_NULL_FIX_TARGET): $(TEST_MEMORY_NULL_FIX_SRC)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling memory NULL fix regression test..."
	@$(CC) $(CFLAGS) -I./src -o $(TEST_MEMORY_NULL_FIX_TARGET) $(TEST_MEMORY_NULL_FIX_SRC) $(LDFLAGS) -lcjson
	@echo ""
	@echo "✓ Memory NULL fix regression test build successful!"
	@echo ""

# Test target for dump utils (conversation dump parsing)
$(TEST_DUMP_UTILS_TARGET): $(TEST_DUMP_UTILS_SRC) src/dump_utils.c src/dump_utils.h
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling dump utils test suite..."
	@$(CC) $(CFLAGS) -I./src -o $(TEST_DUMP_UTILS_TARGET) $(TEST_DUMP_UTILS_SRC) src/dump_utils.c $(LDFLAGS)
	@echo ""
	@echo "✓ Dump utils test build successful!"
	@echo ""

# Test target for cancel flow -> tool_result formatting
$(TEST_CANCEL_FLOW_TARGET): $(SRC) tests/test_cancel_flow.c $(TEST_COMMON_OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling claude.c for cancel flow testing..."
	@$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(BUILD_DIR)/klawed_cancel_flow_test.o $(SRC)
	@echo "Compiling cancel flow test suite..."
	@$(CC) $(CFLAGS) -I./src -c -o $(BUILD_DIR)/test_cancel_flow.o tests/test_cancel_flow.c
	$(BUILD_SQLITE_QUEUE_TEST_OBJ)
	$(BUILD_API_CLIENT_TEST_OBJ)
	$(BUILD_TOOL_SYSTEM_TEST_OBJS)
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_CANCEL_FLOW_TARGET) $(BUILD_DIR)/klawed_cancel_flow_test.o $(BUILD_DIR)/test_cancel_flow.o $(SQLITE_QUEUE_TEST_OBJ) $(TOOL_REGISTRY_TEST_OBJ) $(TOOL_EXECUTOR_TEST_OBJ) $(TOOL_DEFINITIONS_TEST_OBJ) $(TEST_COMMON_OBJS) $(LDFLAGS)
	@echo ""
	@echo "✓ Cancel flow test build successful!"
	@echo ""

test-cancel-flow: check-deps $(TEST_CANCEL_FLOW_TARGET)
	@echo ""
	@echo "Running cancel flow tests..."
	@echo ""
	@./$(TEST_CANCEL_FLOW_TARGET)

# Test target for Write tool diff integration
$(TEST_WRITE_DIFF_INTEGRATION_TARGET): $(SRC) $(TEST_WRITE_DIFF_INTEGRATION_SRC) $(TEST_COMMON_OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling claude.c for write diff testing..."
	@$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(BUILD_DIR)/claude_write_diff_test.o $(SRC)
	@echo "Compiling Write tool diff integration test suite..."
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_write_diff_integration.o $(TEST_WRITE_DIFF_INTEGRATION_SRC)
	$(BUILD_SQLITE_QUEUE_TEST_OBJ)
	$(BUILD_API_CLIENT_TEST_OBJ)
	$(BUILD_TOOL_SYSTEM_TEST_OBJS)
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_WRITE_DIFF_INTEGRATION_TARGET) $(BUILD_DIR)/claude_write_diff_test.o $(BUILD_DIR)/test_write_diff_integration.o $(SQLITE_QUEUE_TEST_OBJ) $(TOOL_REGISTRY_TEST_OBJ) $(TOOL_EXECUTOR_TEST_OBJ) $(TOOL_DEFINITIONS_TEST_OBJ) $(TEST_COMMON_OBJS) $(LDFLAGS)
	@echo ""
	@echo "✓ Write tool diff integration test build successful!"
	@echo ""

# Test target for database rotation
$(TEST_ROTATION_TARGET): $(TEST_ROTATION_SRC) $(LOGGER_OBJ) $(PERSISTENCE_OBJ) $(TOKEN_USAGE_DB_OBJ) $(TOKEN_USAGE_DB_MIGRATIONS_OBJ) $(MIGRATIONS_OBJ) $(DATA_DIR_OBJ)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling database rotation tests..."
	$(CC) $(CFLAGS) -o $(TEST_ROTATION_TARGET) $(TEST_ROTATION_SRC) $(LOGGER_OBJ) $(PERSISTENCE_OBJ) $(TOKEN_USAGE_DB_OBJ) $(TOKEN_USAGE_DB_MIGRATIONS_OBJ) $(MIGRATIONS_OBJ) $(DATA_DIR_OBJ) $(LDFLAGS)
	@echo ""
	@echo "✓ Rotation test build successful!"
	@echo ""



# Test target for function context @@ markers
$(TEST_FUNCTION_CONTEXT_TARGET): $(SRC) $(TEST_FUNCTION_CONTEXT_SRC) $(TEST_COMMON_OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling claude.c for function context testing..."
	@$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(BUILD_DIR)/claude_function_context_test.o $(SRC)
	@echo "Compiling Function Context test suite..."
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_function_context.o $(TEST_FUNCTION_CONTEXT_SRC)
	$(BUILD_SQLITE_QUEUE_TEST_OBJ)
	$(BUILD_API_CLIENT_TEST_OBJ)
	$(BUILD_TOOL_SYSTEM_TEST_OBJS)
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_FUNCTION_CONTEXT_TARGET) $(BUILD_DIR)/claude_function_context_test.o $(BUILD_DIR)/test_function_context.o $(SQLITE_QUEUE_TEST_OBJ) $(TOOL_REGISTRY_TEST_OBJ) $(TOOL_EXECUTOR_TEST_OBJ) $(TOOL_DEFINITIONS_TEST_OBJ) $(TEST_COMMON_OBJS) $(LDFLAGS)
	@echo ""
	@echo "✓ Function Context test build successful!"
	@echo ""

# Test target for thread cancellation
$(TEST_THREAD_CANCEL_TARGET): $(TEST_THREAD_CANCEL_SRC)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling Thread Cancellation test suite..."
	@$(CC) $(CFLAGS) -o $(TEST_THREAD_CANCEL_TARGET) $(TEST_THREAD_CANCEL_SRC) -lpthread
	@echo ""
	@echo "✓ Thread Cancellation test build successful!"
	@echo ""

# Test target for AWS credential rotation with polling
$(TEST_AWS_CRED_ROTATION_TARGET): $(TEST_AWS_CRED_ROTATION_SRC) $(AWS_BEDROCK_OBJ) $(LOGGER_OBJ) $(TOOL_UTILS_OBJ) $(DATA_DIR_OBJ)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling AWS Credential Rotation test suite..."
	@$(CC) $(CFLAGS) -o $(TEST_AWS_CRED_ROTATION_TARGET) $(TEST_AWS_CRED_ROTATION_SRC) $(AWS_BEDROCK_OBJ) $(LOGGER_OBJ) $(TOOL_UTILS_OBJ) $(DATA_DIR_OBJ) $(LDFLAGS)
	@echo ""
	@echo "✓ AWS Credential Rotation test build successful!"
	@echo ""

# Test target for message queues - tests thread-safe queues
$(TEST_MESSAGE_QUEUE_TARGET): $(TEST_MESSAGE_QUEUE_SRC) $(MESSAGE_QUEUE_OBJ) $(LOGGER_OBJ) $(DATA_DIR_OBJ)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling Message Queue test suite..."
	@$(CC) $(CFLAGS) -o $(TEST_MESSAGE_QUEUE_TARGET) $(TEST_MESSAGE_QUEUE_SRC) $(MESSAGE_QUEUE_OBJ) $(LOGGER_OBJ) $(DATA_DIR_OBJ) $(LDFLAGS)
	@echo ""
	@echo "✓ Message Queue test build successful!"
	@echo ""

$(TEST_EVENT_LOOP_TARGET): $(SRC) $(TEST_EVENT_LOOP_SRC) $(TEST_STUBS_SRC) $(TEST_COMMON_OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling claude.c for event loop testing (renaming main)..."
	@$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(BUILD_DIR)/claude_event_loop_test.o $(SRC)
	$(BUILD_SQLITE_QUEUE_TEST_OBJ)
	$(BUILD_API_CLIENT_TEST_OBJ)
	$(BUILD_TOOL_SYSTEM_TEST_OBJS)
	@echo "Compiling Event Loop test..."
	@$(CC) $(CFLAGS) -Wno-unused-function -o $(TEST_EVENT_LOOP_TARGET) $(BUILD_DIR)/claude_event_loop_test.o $(TEST_EVENT_LOOP_SRC) $(TEST_STUBS_SRC) $(SQLITE_QUEUE_TEST_OBJ) $(TOOL_REGISTRY_TEST_OBJ) $(TOOL_EXECUTOR_TEST_OBJ) $(TOOL_DEFINITIONS_TEST_OBJ) $(TEST_COMMON_OBJS) $(LDFLAGS)
	@echo ""
	@echo "✓ Event Loop test build successful!"
	@echo ""

$(TEST_TEXT_WRAP_TARGET): tests/test_text_wrap.c
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling Text Wrapping test..."
	@$(CC) $(CFLAGS) -o $(TEST_TEXT_WRAP_TARGET) tests/test_text_wrap.c -I./src
	@echo ""
	@echo "✓ Text Wrapping test build successful!"
	@echo ""

$(TEST_MCP_TARGET): $(TEST_MCP_SRC) $(MCP_TEST_OBJ) $(BASE64_OBJ) $(DATA_DIR_OBJ) $(LOGGER_OBJ)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling MCP integration tests..."
	@$(CC) $(CFLAGS) -DTEST_BUILD -o $(TEST_MCP_TARGET) $(TEST_MCP_SRC) $(MCP_TEST_OBJ) $(BASE64_OBJ) $(DATA_DIR_OBJ) $(LOGGER_OBJ) $(LDFLAGS)
	@echo ""
	@echo "✓ MCP test build successful!"
	@echo ""

$(TEST_MCP_IMAGE_TARGET): $(TEST_MCP_IMAGE_SRC) $(BASE64_OBJ)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling MCP image content handling tests..."
	@$(CC) $(CFLAGS) -o $(TEST_MCP_IMAGE_TARGET) $(TEST_MCP_IMAGE_SRC) $(BASE64_OBJ) $(LDFLAGS)
	@echo ""
	@echo "✓ MCP image test build successful!"
	@echo ""

install: $(TARGET)
	@echo "Installing klawed to $(INSTALL_PREFIX)/bin..."
	@mkdir -p $(INSTALL_PREFIX)/bin
	@rm -f $(INSTALL_PREFIX)/bin/klawed
	@cp $(TARGET) $(INSTALL_PREFIX)/bin/klawed
ifeq ($(UNAME_S),Darwin)
ifndef GITHUB_ACTIONS
	@echo "Signing binary for macOS..."
	@codesign --force --deep --sign - $(INSTALL_PREFIX)/bin/klawed 2>/dev/null || echo "Warning: Code signing failed (non-fatal)"
endif
endif
# Note: memvid is now statically linked, no shared library installation needed
	@echo "✓ Installation complete! Run 'klawed' from anywhere."
	@echo ""
	@echo "Note: Make sure $(INSTALL_PREFIX)/bin is in your PATH:"
	@echo "  export PATH=\"$(INSTALL_PREFIX)/bin:$$PATH\""

install-web-browse-agent:
	@echo "Building web_browse_agent (version: $(WEB_BROWSE_AGENT_VERSION))..."
	@command -v go >/dev/null 2>&1 || { echo "Error: go not found. Install Go to build web_browse_agent."; exit 1; }
	@cd $(WEB_BROWSE_AGENT_DIR) && GO111MODULE=on go build -ldflags "-s -w" -o web_browse_agent ./cmd/web_browse_agent
	@echo "Installing web_browse_agent to $(INSTALL_PREFIX)/bin..."
	@mkdir -p $(INSTALL_PREFIX)/bin
	@# Remove old binary first to avoid "Text file busy" error
	@rm -f $(INSTALL_PREFIX)/bin/web_browse_agent
	@cp $(WEB_BROWSE_AGENT_BIN) $(INSTALL_PREFIX)/bin/web_browse_agent
	@echo "$(WEB_BROWSE_AGENT_VERSION)" > $(INSTALL_PREFIX)/bin/web_browse_agent.version
	@echo "✓ web_browse_agent installed (version: $(WEB_BROWSE_AGENT_VERSION)). Ensure $(INSTALL_PREFIX)/bin is in your PATH."

clean:
	rm -rf $(BUILD_DIR)

# Format: Remove trailing whitespaces from source files
fmt-whitespace:
	@echo "Removing trailing whitespaces from source files..."
ifeq ($(UNAME_S),Darwin)
	@find src tests tools -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i '' 's/[[:space:]]*$$//' {} +
else
	@find src tests tools -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/[[:space:]]*$$//' {} +
endif
	@echo "✓ Trailing whitespaces removed"

check-deps:
	@echo "Checking dependencies..."
	@command -v $(CC) >/dev/null 2>&1 || { echo "Error: gcc not found. Please install gcc."; exit 1; }
	@command -v curl-config >/dev/null 2>&1 || { echo "Error: libcurl not found. Install with: brew install curl (macOS) or apt-get install libcurl4-openssl-dev (Linux)"; exit 1; }
	@command -v pkg-config >/dev/null 2>&1 || { echo "Warning: pkg-config not found. May have issues detecting OpenSSL."; }
	@pkg-config --exists openssl 2>/dev/null || { echo "Error: OpenSSL not found. Install with: brew install openssl (macOS) or apt-get install libssl-dev (Linux)"; exit 1; }
	@pkg-config --exists libcjson 2>/dev/null || { echo "Error: cJSON not found. Install with: brew install cjson (macOS) or apt-get install libcjson-dev (Linux)"; exit 1; }
	@pkg-config --exists libbsd 2>/dev/null || { echo "Error: libbsd not found. Install with: brew install libbsd (macOS) or apt-get install libbsd-dev (Linux)"; exit 1; }
	@# Check for memvid-ffi if explicitly enabled
	@if [ "$(MEMVID)" = "1" ]; then \
		echo "Checking for memvid-ffi (MEMVID=1 specified)..."; \
		if [ ! -f "$(MEMVID_FFI_LIB)" ]; then \
			echo "Error: memvid-ffi library not found at $(MEMVID_FFI_LIB)"; \
			echo "Build it with: make memvid-ffi"; \
			exit 1; \
		fi; \
		echo "✓ memvid-ffi found"; \
	fi
	@echo "✓ All dependencies found"
	@echo ""

help:
	@echo "Claude Code - Pure C Edition - Build System"
	@echo ""
	@echo "Usage:"
	@echo "  make           - Build the klawed executable"
	@echo "  make clang     - Build with clang compiler"
	@echo "  make debug     - Build with AddressSanitizer (memory bug detection)"
	@echo "  make test      - Build and run all unit tests"
	@echo "  make test-edit - Build and run Edit tool tests only"
	@echo "  make test-read - Build and run Read tool tests only"
	@echo "  make test-todo - Build and run TODO list tests only"
	@echo "  make test-paste - Build and run Paste Handler tests only"
	@echo "  make test-json-parsing - Build and run JSON parsing tests only"
	@echo "  make test-retry-jitter - Build and run Retry Jitter tests only"
	@echo "  make test-message-queue - Build and run Message Queue tests only"
	@echo "  make test-token-usage - Build and run Token Usage tests only"
	@echo "  make query-tool - Build the API call log query utility"
	@echo "  make memvid-repl - Build the memvid interactive REPL (requires MEMVID=1)"
	@echo "  make clean     - Remove built files"
	@echo "  make install   - Install to \$$HOME/.local/bin as klawed (default)"
	@echo "  make install INSTALL_PREFIX=/usr/local - Install to /usr/local/bin (requires sudo)"
	@echo "  make install INSTALL_PREFIX=/opt - Install to /opt/bin (requires sudo)"
	@echo "  make check-deps - Check if all dependencies are installed"
	@echo ""
	@echo "CI Testing (replicate GitHub Actions locally):"
	@echo "  make ci-test        - Quick CI check (GCC + Clang with sanitizers)"
	@echo "  make ci-all         - Full CI matrix (all compiler/sanitizer combos)"
	@echo "  make ci-gcc         - Test with GCC only"
	@echo "  make ci-clang       - Test with Clang only"
	@echo "  make ci-gcc-sanitize - Test with GCC + sanitizers"
	@echo "  make ci-clang-sanitize - Test with Clang + sanitizers"
	@echo ""
	@echo "Version Management:"
	@echo "  make version        - Show current version"
	@echo "  make show-version   - Show detailed version information"
	@echo "  make bump-patch     - Increment patch version and update README (e.g., 0.0.2 → 0.0.3)"
	@echo "  make bump-minor-version - Increment minor version and reset patch (e.g., 0.1.36 → 0.2.0)"
	@echo "  make update-version VERSION=1.0.0 - Set specific version number"
	@echo ""
	@echo "Memory Bug Scanning:"
	@echo "  make memscan           - Run comprehensive memory analysis (recommended)"
	@echo "  make comprehensive-scan - Run ALL available analysis tools (best coverage)"
	@echo "  make analyze           - Run static analysis (clang/gcc analyzer)"
	@echo "  make sanitize-all      - Build with Address + UB sanitizers (recommended)"
	@echo "  make sanitize-ub       - Build with Undefined Behavior Sanitizer only"
	@echo "  make sanitize-leak     - Build with Leak Sanitizer only"
	@echo "  make valgrind          - Run test suite under Valgrind"
	@echo "  make clang-tidy        - Run clang-tidy static analysis (if installed)"
	@echo "  make cppcheck          - Run cppcheck security analysis (if installed)"
	@echo "  make flawfinder        - Run flawfinder security analysis (if installed)"
	@echo ""
	@echo "Code Formatting:"
	@echo "  make fmt-whitespace - Remove trailing whitespaces from all source files"
	@echo ""
	@echo "Podman:"
	@echo "  make docker-sandbox - Build Podman sandbox image for isolated execution"
	@echo "  make docker-push - Transfer Podman image to filesurf-0 via save/load"
	@echo "  make docker-rotate - Rotate images on filesurf-0 (keep only last 3)"
	@echo ""
	@echo "Dependencies:"
	@echo "  - gcc or clang (or compatible C compiler)"
	@echo "  - libcurl"
	@echo "  - cJSON"
	@echo "  - sqlite3"
	@echo "  - OpenSSL (for AWS Bedrock support)"
	@echo "  - libbsd (for safer C functions)"
	@echo "  - pthread (usually included with OS)"
	@echo "  - valgrind (optional, for memory leak detection)"
	@echo ""
	@echo "macOS installation:"
	@echo "  brew install curl cjson sqlite3 openssl libbsd valgrind"
	@echo ""
	@echo "Linux installation:"
	@echo "  apt-get install libcurl4-openssl-dev libcjson-dev libsqlite3-dev libssl-dev libbsd-dev valgrind"
	@echo "  or"
	@echo "  yum install libcurl-devel cjson-devel sqlite-devel openssl-devel libbsd-devel valgrind"
	@echo ""
	@echo "Static Analysis Tools (optional):"
	@echo "  macOS: brew install llvm cppcheck flawfinder"
	@echo "  Linux: sudo apt-get install clang-tidy cppcheck flawfinder"
	@echo ""
	@echo "Memvid Integration (video-based memory storage):"
	@echo "  make MEMVID=1       - Build with memvid support enabled"
	@echo "  make MEMVID=0       - Build with memvid support disabled"
	@echo "  make memvid-ffi     - Build the Rust memvid-ffi library"
	@echo "  make memvid-ffi-clean - Clean the Rust build artifacts"
	@echo "  make check-memvid   - Check memvid-ffi library status"
	@echo "  FFI path: $(MEMVID_FFI_DIR)"
	@echo ""
	@echo "AWS Bedrock Configuration:"
	@echo "  export CLAUDE_CODE_USE_BEDROCK=true"
	@echo "  export ANTHROPIC_MODEL=us.anthropic.claude-sonnet-4-5-20250929-v1:0"
	@echo "  export AWS_REGION=us-west-2"
	@echo "  export AWS_PROFILE=your-profile"
	@echo ""
	@echo "Optional - Custom Authentication:"
	@echo "  export AWS_AUTH_COMMAND='cd /path/to/okta && bash script.sh'"

# Version management targets
version:
	@echo "$(VERSION)"

show-version: $(VERSION_H)
	@echo "Claude C - Pure C Edition"
	@echo "Version: $(VERSION)"
	@echo "Build date: $(BUILD_DATE)"
	@echo "Version file: $(VERSION_FILE)"
	@echo "Version header: $(VERSION_H)"
	@if [ -f "$(TARGET)" ]; then \
		echo "Binary: $(TARGET)"; \
		./$(TARGET) --version 2>/dev/null || echo "Binary version: not available"; \
	fi

update-version:
	@if [ -z "$(VERSION)" ]; then \
		echo "Error: VERSION parameter required"; \
		echo "Usage: make update-version VERSION=1.0.0"; \
		exit 1; \
	fi
	@echo "Updating version to $(VERSION)..."
	@echo "$(VERSION)" > $(VERSION_FILE)
	@echo "✓ Updated $(VERSION_FILE) to $(VERSION)"
	@echo "Run 'make clean && make' to rebuild with new version"

bump-patch:
	@echo "Current version: $(VERSION)"
	@CURRENT_VERSION="$(VERSION)"; \
	VERSION_MAJOR=$$(echo "$$CURRENT_VERSION" | sed 's/^\([0-9]*\)\..*/\1/'); \
	VERSION_MINOR=$$(echo "$$CURRENT_VERSION" | sed 's/^[0-9]*\.\([0-9]*\)\..*/\1/'); \
	VERSION_PATCH=$$(echo "$$CURRENT_VERSION" | sed 's/^[0-9]*\.[0-9]*\.\([0-9]*\).*/\1/'); \
	NEW_PATCH=$$((VERSION_PATCH + 1)); \
	NEW_VERSION="$$VERSION_MAJOR.$$VERSION_MINOR.$$NEW_PATCH"; \
	echo "Bumping patch version: $$CURRENT_VERSION → $$NEW_VERSION"; \
	echo "$$NEW_VERSION" > $(VERSION_FILE); \
	echo "✓ Version bumped to $$NEW_VERSION"; \
	echo ""; \
	echo "Regenerating version.h..."; \
	rm -f $(VERSION_H); \
	$(MAKE) $(VERSION_H); \
	echo ""; \
	echo "Updating README.md with new version..."; \
	sed -i.bak 's/git clone --branch v[0-9]*\.[0-9]*\.[0-9]*/git clone --branch v'"$$NEW_VERSION"'/g' README.md; \
	rm -f README.md.bak; \
	echo "✓ Updated README.md with v$$NEW_VERSION"; \
	echo ""; \
	echo "Staging version files..."; \
	git add $(VERSION_FILE) $(VERSION_H) README.md; \
	git commit -m "chore: bump version to $$NEW_VERSION"; \
	echo ""; \
	echo "Creating git tag v$$NEW_VERSION..."; \
	git tag -a "v$$NEW_VERSION" -m "Release v$$NEW_VERSION"; \
	echo ""; \
	echo "Pushing to remote..."; \
	git push origin master; \
	git push origin "v$$NEW_VERSION"; \
	echo ""; \
	echo "✓ Version $$NEW_VERSION released successfully!"; \
	echo "  - Committed: $(VERSION_FILE) and $(VERSION_H)"; \
	echo "  - Updated: README.md with new version"; \
	echo "  - Tagged: v$$NEW_VERSION"; \
	echo "  - Pushed to remote"

bump-minor-version:
	@echo "Current version: $(VERSION)"
	@CURRENT_VERSION="$(VERSION)"; \
	VERSION_MAJOR=$$(echo "$$CURRENT_VERSION" | sed 's/^\([0-9]*\)\..*/\1/'); \
	VERSION_MINOR=$$(echo "$$CURRENT_VERSION" | sed 's/^[0-9]*\.\([0-9]*\)\..*/\1/'); \
	VERSION_PATCH=$$(echo "$$CURRENT_VERSION" | sed 's/^[0-9]*\.[0-9]*\.\([0-9]*\).*/\1/'); \
	NEW_MINOR=$$((VERSION_MINOR + 1)); \
	NEW_VERSION="$$VERSION_MAJOR.$$NEW_MINOR.0"; \
	echo "Bumping minor version: $$CURRENT_VERSION → $$NEW_VERSION"; \
	echo "$$NEW_VERSION" > $(VERSION_FILE); \
	echo "✓ Version bumped to $$NEW_VERSION"; \
	echo ""; \
	echo "Regenerating version.h..."; \
	rm -f $(VERSION_H); \
	$(MAKE) $(VERSION_H); \
	echo ""; \
	echo "Updating README.md with new version..."; \
	sed -i.bak 's/git clone --branch v[0-9]*\.[0-9]*\.[0-9]*/git clone --branch v'"$$NEW_VERSION"'/g' README.md; \
	rm -f README.md.bak; \
	echo "✓ Updated README.md with v$$NEW_VERSION"; \
	echo ""; \
	echo "Staging version files..."; \
	git add $(VERSION_FILE) $(VERSION_H) README.md; \
	git commit -m "chore: bump version to $$NEW_VERSION"; \
	echo ""; \
	echo "Creating git tag v$$NEW_VERSION..."; \
	git tag -a "v$$NEW_VERSION" -m "Release v$$NEW_VERSION"; \
	echo ""; \
	echo "Pushing to remote..."; \
	git push origin master; \
	git push origin "v$$NEW_VERSION"; \
	echo ""; \
	echo "✓ Version $$NEW_VERSION released successfully!"; \
	echo "  - Committed: $(VERSION_FILE) and $(VERSION_H)"; \
	echo "  - Updated: README.md with new version"; \
	echo "  - Tagged: v$$NEW_VERSION"; \
	echo "  - Pushed to remote"

# CI-like testing targets - mirror what GitHub Actions does
ci-gcc: check-deps
	@echo "=========================================="
	@echo "CI Test: Building with GCC"
	@echo "=========================================="
	@echo ""
	@$(MAKE) clean
	@CC=gcc $(MAKE) all
	@CC=gcc $(MAKE) test
	@echo ""
	@echo "✓ GCC build and tests passed!"
	@echo ""

ci-clang: check-deps
	@echo "=========================================="
	@echo "CI Test: Building with Clang"
	@echo "=========================================="
	@echo ""
	@$(MAKE) clean
	@CC=clang $(MAKE) all
	@CC=clang $(MAKE) test
	@echo ""
	@echo "✓ Clang build and tests passed!"
	@echo ""

ci-gcc-sanitize: check-deps
	@echo "=========================================="
	@echo "CI Test: Building with GCC + Sanitizers"
	@echo "=========================================="
	@echo ""
	@$(MAKE) clean
	@CC=gcc SANITIZERS=-fsanitize=address,undefined $(MAKE) all
	@CC=gcc SANITIZERS=-fsanitize=address,undefined $(MAKE) test
	@echo ""
	@echo "✓ GCC + sanitizers build and tests passed!"
	@echo ""

ci-clang-sanitize: check-deps
	@echo "=========================================="
	@echo "CI Test: Building with Clang + Sanitizers"
	@echo "=========================================="
	@echo ""
	@$(MAKE) clean
	@CC=clang SANITIZERS=-fsanitize=address,undefined $(MAKE) all
	@CC=clang SANITIZERS=-fsanitize=address,undefined $(MAKE) test
	@echo ""
	@echo "✓ Clang + sanitizers build and tests passed!"
	@echo ""

# Run all CI test combinations (what GitHub Actions does)
ci-all: check-deps
	@echo "=========================================="
	@echo "Running Complete CI Test Matrix"
	@echo "=========================================="
	@echo ""
	@echo "This will test all compiler+sanitizer combinations like GitHub Actions"
	@echo ""
	@$(MAKE) ci-gcc
	@$(MAKE) ci-clang
	@$(MAKE) ci-gcc-sanitize
	@$(MAKE) ci-clang-sanitize
	@echo ""
	@echo "=========================================="
	@echo "✓ ALL CI TESTS PASSED!"
	@echo "=========================================="
	@echo ""
	@echo "Tested combinations:"
	@echo "  ✓ GCC build + tests"
	@echo "  ✓ Clang build + tests"
	@echo "  ✓ GCC + sanitizers build + tests"
	@echo "  ✓ Clang + sanitizers build + tests"
	@echo ""
	@echo "Your code should pass CI!"
	@echo ""

# Quick CI check - just the most important combinations
ci-test: ci-gcc ci-clang-sanitize
	@echo ""
	@echo "=========================================="
	@echo "✓ Quick CI Check Complete"
	@echo "=========================================="
	@echo ""
	@echo "Tested:"
	@echo "  ✓ GCC build + tests (default CI compiler)"
	@echo "  ✓ Clang + sanitizers (catches most issues)"
	@echo ""
	@echo "For full CI coverage, run: make ci-all"
	@echo ""

# Test target for History File functionality
$(TEST_HISTORY_FILE_TARGET): $(HISTORY_FILE_SRC) $(TEST_HISTORY_FILE_SRC) $(LOGGER_OBJ) $(DATA_DIR_OBJ)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling History File test suite..."
	@$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(BUILD_DIR)/history_file_test.o $(HISTORY_FILE_SRC)
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_history_file.o $(TEST_HISTORY_FILE_SRC)
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_HISTORY_FILE_TARGET) $(BUILD_DIR)/history_file_test.o $(BUILD_DIR)/test_history_file.o $(LOGGER_OBJ) $(DATA_DIR_OBJ) $(LDFLAGS)
	@echo ""
	@echo "✓ History File test build successful!"
	@echo ""

# Test target for TUI Input Buffer dynamic resizing (simplified standalone test)
$(TEST_TUI_INPUT_BUFFER_TARGET): $(TEST_TUI_INPUT_BUFFER_SRC)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling TUI Input Buffer test suite..."
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_tui_input_buffer.o $(TEST_TUI_INPUT_BUFFER_SRC)
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_TUI_INPUT_BUFFER_TARGET) $(BUILD_DIR)/test_tui_input_buffer.o $(LDFLAGS)
	@echo ""
	@echo "✓ TUI Input Buffer test build successful!"
	@echo ""

# Test target for TUI Auto-Scroll logic (simplified standalone test)
$(TEST_TUI_AUTO_SCROLL_TARGET): $(TEST_TUI_AUTO_SCROLL_SRC)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling TUI Auto-Scroll test suite..."
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_tui_auto_scroll.o $(TEST_TUI_AUTO_SCROLL_SRC)
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_TUI_AUTO_SCROLL_TARGET) $(BUILD_DIR)/test_tui_auto_scroll.o $(LDFLAGS)
	@echo ""
	@echo "✓ TUI Auto-Scroll test build successful!"
	@echo ""

# Test target for Bash command summarization
# Note: test_bash_summary.c file does not exist, so test-bash-summary target is removed

TEST_TOKEN_USAGE_COMPREHENSIVE_TARGET = $(BUILD_DIR)/test_token_usage_comprehensive
TEST_TOKEN_USAGE_SESSION_TOTALS_TARGET = $(BUILD_DIR)/test_token_usage_session_totals

$(TEST_TOKEN_USAGE_SESSION_TOTALS_TARGET): $(TEST_TOKEN_USAGE_SESSION_TOTALS_SRC) $(LOGGER_OBJ) $(PERSISTENCE_OBJ) $(TOKEN_USAGE_DB_OBJ) $(TOKEN_USAGE_DB_MIGRATIONS_OBJ) $(MIGRATIONS_OBJ) $(DATA_DIR_OBJ)
	@$(CC) $(CFLAGS) -o $(TEST_TOKEN_USAGE_SESSION_TOTALS_TARGET) $(TEST_TOKEN_USAGE_SESSION_TOTALS_SRC) $(LOGGER_OBJ) $(PERSISTENCE_OBJ) $(TOKEN_USAGE_DB_OBJ) $(TOKEN_USAGE_DB_MIGRATIONS_OBJ) $(MIGRATIONS_OBJ) $(DATA_DIR_OBJ) $(LDFLAGS)
# Test-specific SQLite queue object (compiled with TEST_BUILD to exclude klawed.c dependencies)
SQLITE_QUEUE_TEST_OBJ = $(BUILD_DIR)/sqlite_queue_test.o

# Test-specific API client object (compiled with TEST_BUILD to work with SQLITE_QUEUE_TEST_OBJ)
API_CLIENT_TEST_OBJ = $(BUILD_DIR)/api_client_test.o

# Test-specific tool system objects (compiled with TEST_BUILD to exclude MCP tools)
TOOL_REGISTRY_TEST_OBJ = $(BUILD_DIR)/tool_registry_test.o
TOOL_EXECUTOR_TEST_OBJ = $(BUILD_DIR)/tool_executor_test.o
TOOL_DEFINITIONS_TEST_OBJ = $(BUILD_DIR)/tool_definitions_test.o

# Common objects needed by tests that compile claude.c

# Tool registry, executor, API client, and sqlite_queue are excluded from TEST_COMMON_OBJS because they need to be
# compiled with -DTEST_BUILD to work properly with test builds. They use _TEST_OBJ variants instead.
TEST_COMMON_OBJS = $(LOGGER_OBJ) $(CONFIG_OBJ) $(PERSISTENCE_OBJ) $(TOKEN_USAGE_DB_OBJ) $(TOKEN_USAGE_DB_MIGRATIONS_OBJ) $(MIGRATIONS_OBJ) $(TODO_OBJ) $(MESSAGE_QUEUE_OBJ) $(OPENAI_MESSAGES_OBJ) $(OPENAI_RESPONSES_OBJ) $(BASE64_OBJ) $(PROVIDER_OBJ) $(OPENAI_PROVIDER_OBJ) $(BEDROCK_PROVIDER_OBJ) $(ANTHROPIC_PROVIDER_OBJ) $(DEEPSEEK_PROVIDER_OBJ) $(MOONSHOT_PROVIDER_OBJ) $(HTTP_CLIENT_OBJ) $(SESSION_OBJ) $(RETRY_LOGIC_OBJ) $(TOOL_UTILS_OBJ) $(PROCESS_UTILS_OBJ) $(SUBAGENT_MANAGER_OBJ) $(ARRAY_RESIZE_OBJ) $(HISTORY_FILE_OBJ) $(AWS_BEDROCK_OBJ) $(BEDROCK_CONVERSE_OBJ) $(TUI_OBJ) $(TUI_CORE_OBJ) $(TUI_CONVERSATION_OBJ) $(TUI_WINDOW_OBJ) $(TUI_RENDER_OBJ) $(TUI_MODES_OBJ) $(TUI_INPUT_OBJ) $(TUI_HISTORY_OBJ) $(TUI_COMPLETION_OBJ) $(TUI_PASTE_OBJ) $(TUI_SEARCH_OBJ) $(WINDOW_MANAGER_OBJ) $(COMPLETION_OBJ) $(COMMANDS_OBJ) $(THEME_EXPLORER_OBJ) $(HELP_MODAL_OBJ) $(BUILTIN_THEMES_OBJ) $(AI_WORKER_OBJ) $(VOICE_INPUT_OBJ) $(MCP_OBJ) $(FILE_SEARCH_OBJ) $(HISTORY_SEARCH_OBJ) $(DUMP_UTILS_OBJ) $(MEMVID_OBJ) $(COMPACTION_OBJ) $(BACKGROUND_INIT_OBJ) $(DATA_DIR_OBJ) $(EXPLORE_TOOLS_OBJ) $(CONTENT_TYPES_OBJ) $(CONVERSATION_STATE_OBJ) $(MESSAGE_BUILDER_OBJ) $(MESSAGE_PARSER_OBJ) $(FILE_UTILS_OBJ) $(STRING_UTILS_OBJ) $(TIMESTAMP_UTILS_OBJ) $(FORMAT_UTILS_OBJ) $(ENV_UTILS_OBJ) $(OUTPUT_UTILS_OBJ) $(DIFF_UTILS_OBJ) $(TOOL_SLEEP_OBJ) $(TOOL_TODO_OBJ) $(TOOL_IMAGE_OBJ) $(TOOL_SEARCH_OBJ) $(TOOL_FILESYSTEM_OBJ) $(TOOL_BASH_OBJ) $(TOOL_SUBAGENT_OBJ) $(CONTEXT_SYSTEM_PROMPT_OBJ) $(CONTEXT_ENVIRONMENT_OBJ) $(CONTEXT_KLAWED_MD_OBJ) $(CONTEXT_MEMORY_INJECTION_OBJ) $(UI_OUTPUT_OBJ) $(PRINT_HELPERS_OBJ) $(TOOL_OUTPUT_DISPLAY_OBJ) $(SESSION_TOKEN_USAGE_OBJ) $(SESSION_PERSISTENCE_OBJ) $(API_RESPONSE_OBJ) $(API_BUILDER_OBJ) $(API_CLIENT_TEST_OBJ) $(INTERACTIVE_LOOP_OBJ) $(INPUT_HANDLER_OBJ) $(RESPONSE_PROCESSOR_OBJ) $(COMMAND_DISPATCH_OBJ) $(ONESHOT_MODE_OBJ) $(ONESHOT_PROCESSOR_OBJ) $(ONESHOT_OUTPUT_OBJ) $(PROVIDER_COMMAND_OBJ) $(CONFIG_COMMAND_OBJ)

test-token-usage-comprehensive: check-deps $(TEST_TOKEN_USAGE_COMPREHENSIVE_TARGET)
	@echo ""
	@echo "Running Comprehensive Token Usage tests..."
	@echo ""
	@./$(TEST_TOKEN_USAGE_COMPREHENSIVE_TARGET)

$(TEST_TOKEN_USAGE_COMPREHENSIVE_TARGET): $(TEST_TOKEN_USAGE_COMPREHENSIVE_SRC)
	@$(CC) $(CFLAGS) -o $(TEST_TOKEN_USAGE_COMPREHENSIVE_TARGET) $(TEST_TOKEN_USAGE_COMPREHENSIVE_SRC) $(LDFLAGS)

$(TEST_HTTP_CLIENT_TARGET): $(TEST_HTTP_CLIENT_SRC) $(HTTP_CLIENT_OBJ) $(LOGGER_OBJ) $(RETRY_LOGIC_OBJ) $(DATA_DIR_OBJ)
	@$(CC) $(CFLAGS) -o $(TEST_HTTP_CLIENT_TARGET) $(TEST_HTTP_CLIENT_SRC) $(HTTP_CLIENT_OBJ) $(LOGGER_OBJ) $(RETRY_LOGIC_OBJ) $(DATA_DIR_OBJ) $(LDFLAGS)





$(TEST_SQLITE_QUEUE_TARGET): $(TEST_SQLITE_QUEUE_SRC) $(SQLITE_QUEUE_TEST_OBJ) $(LOGGER_OBJ) $(DATA_DIR_OBJ)
	@$(CC) $(CFLAGS) -o $(TEST_SQLITE_QUEUE_TARGET) $(TEST_SQLITE_QUEUE_SRC) $(SQLITE_QUEUE_TEST_OBJ) $(LOGGER_OBJ) $(DATA_DIR_OBJ) $(LDFLAGS)

# Test target for SQLite Queue Seeding (conversation history restoration)
$(TEST_SQLITE_QUEUE_SEEDING_TARGET): $(SRC) $(TEST_SQLITE_QUEUE_SEEDING_SRC) $(TEST_COMMON_OBJS)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling klawed.c for sqlite_queue_seeding testing (renaming main)..."
	@$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(BUILD_DIR)/klawed_sqlite_queue_seeding_test.o $(SRC)
	@echo "Compiling SQLite Queue Seeding test suite..."
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_sqlite_queue_seeding.o $(TEST_SQLITE_QUEUE_SEEDING_SRC)
	$(BUILD_SQLITE_QUEUE_TEST_OBJ)
	$(BUILD_API_CLIENT_TEST_OBJ)
	$(BUILD_TOOL_SYSTEM_TEST_OBJS)
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_SQLITE_QUEUE_SEEDING_TARGET) $(BUILD_DIR)/klawed_sqlite_queue_seeding_test.o $(BUILD_DIR)/test_sqlite_queue_seeding.o $(SQLITE_QUEUE_TEST_OBJ) $(TOOL_REGISTRY_TEST_OBJ) $(TOOL_EXECUTOR_TEST_OBJ) $(TOOL_DEFINITIONS_TEST_OBJ) $(TEST_COMMON_OBJS) $(LDFLAGS)
	@echo ""
	@echo "✓ SQLite Queue Seeding test build successful!"
	@echo ""

# Test target for File Search fuzzy matching
$(TEST_FILE_SEARCH_TARGET): $(FILE_SEARCH_SRC) $(TEST_FILE_SEARCH_SRC) $(LOGGER_OBJ) $(DATA_DIR_OBJ)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling File Search implementation for tests (TEST_BUILD)..."
	@$(CC) $(CFLAGS) -DTEST_BUILD -c -o $(BUILD_DIR)/file_search_test.o $(FILE_SEARCH_SRC)
	@echo "Compiling File Search test suite..."
	@$(CC) $(CFLAGS) -c -o $(BUILD_DIR)/test_file_search.o $(TEST_FILE_SEARCH_SRC)
	@echo "Linking test executable..."
	@$(CC) -o $(TEST_FILE_SEARCH_TARGET) $(BUILD_DIR)/file_search_test.o $(BUILD_DIR)/test_file_search.o $(LOGGER_OBJ) $(DATA_DIR_OBJ) $(LDFLAGS)
	@echo ""
	@echo "✓ File Search test build successful!"

# Socket test build rule removed - will be reimplemented with ZMQ

#
# Whisper.cpp Integration
#

# Build whisper.cpp libraries using cmake
$(WHISPER_LIB) $(WHISPER_GGML_LIB) $(WHISPER_GGML_BASE_LIB) $(WHISPER_GGML_CPU_LIB) $(WHISPER_GGML_BLAS_LIB) $(WHISPER_METAL_LIB):
	@if [ ! -d "$(WHISPER_DIR)" ]; then \
		echo ""; \
		echo "❌ Error: whisper.cpp submodule not found"; \
		echo ""; \
		echo "Run: git submodule update --init --recursive"; \
		echo ""; \
		exit 1; \
	fi
	@if [ ! -d "$(WHISPER_DIR)/.git" ]; then \
		echo ""; \
		echo "❌ Error: whisper.cpp directory exists but is not a git repository"; \
		echo ""; \
		echo "This usually means the submodule wasn't properly initialized."; \
		echo ""; \
		echo "Fix this by running:"; \
		echo "  rm -rf $(WHISPER_DIR)"; \
		echo "  git submodule update --init --recursive"; \
		echo ""; \
		echo "Or disable voice input with: make VOICE=0"; \
		echo ""; \
		exit 1; \
	fi
	@echo ""
	@echo "Building whisper.cpp $(WHISPER_VERSION) libraries..."
	@echo ""
	@cd $(WHISPER_DIR) && \
		(git fetch --tags 2>/dev/null || true) && \
		(git checkout $(WHISPER_VERSION) 2>/dev/null || echo "Already at $(WHISPER_VERSION)") && \
		cmake -B build -DCMAKE_BUILD_TYPE=Release \
			-DWHISPER_BUILD_TESTS=OFF \
			-DWHISPER_BUILD_EXAMPLES=OFF \
			-DBUILD_SHARED_LIBS=OFF \
			-DGGML_STATIC=ON \
			-DGGML_METAL=ON && \
		cmake --build build --config Release --target whisper
	@echo ""
	@echo "✓ whisper.cpp $(WHISPER_VERSION) libraries built successfully"
	@echo ""

# Download whisper model (ggml-small.en.bin by default)
.PHONY: download-model
download-model:
	@mkdir -p whisper_models
	@if [ -f "$(DEFAULT_MODEL)" ]; then \
		echo ""; \
		echo "✓ Model already exists: $(DEFAULT_MODEL)"; \
		echo ""; \
	else \
		echo ""; \
		echo "Downloading ggml-small.en.bin model (~500MB)..."; \
		echo "This may take a few minutes..."; \
		echo ""; \
		curl -L --progress-bar -o $(DEFAULT_MODEL) \
			https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.en.bin; \
		echo ""; \
		echo "✓ Model downloaded: $(DEFAULT_MODEL)"; \
		echo ""; \
	fi

# Download alternative models
.PHONY: download-model-tiny
download-model-tiny:
	@mkdir -p whisper_models
	@echo ""
	@echo "Downloading ggml-tiny.en.bin model (~75MB)..."
	@echo ""
	@curl -L --progress-bar -o whisper_models/ggml-tiny.en.bin \
		https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin
	@echo ""
	@echo "✓ Model downloaded: whisper_models/ggml-tiny.en.bin"
	@echo ""

.PHONY: download-model-base
download-model-base:
	@mkdir -p whisper_models
	@echo ""
	@echo "Downloading ggml-base.en.bin model (~150MB)..."
	@echo ""
	@curl -L --progress-bar -o whisper_models/ggml-base.en.bin \
		https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin
	@echo ""
	@echo "✓ Model downloaded: whisper_models/ggml-base.en.bin"
	@echo ""

# Clean whisper.cpp build artifacts
.PHONY: clean-whisper
clean-whisper:
	@echo "Cleaning whisper.cpp build artifacts..."
	@rm -rf $(WHISPER_BUILD_DIR)
	@echo "✓ Whisper build cleaned"

# Check whisper.cpp version
.PHONY: check-whisper-version
check-whisper-version:
	@if [ -d "$(WHISPER_DIR)" ]; then \
		echo ""; \
		echo "Whisper.cpp version information:"; \
		echo "  Pinned version: $(WHISPER_VERSION)"; \
		CURRENT=$$(cd $(WHISPER_DIR) && git describe --tags --exact-match 2>/dev/null || git describe --tags --always 2>/dev/null || echo "unknown"); \
		echo "  Current checkout: $$CURRENT"; \
		if [ "$$CURRENT" = "$(WHISPER_VERSION)" ]; then \
			echo "  ✓ On correct version"; \
		else \
			echo "  ⚠ Warning: Not on pinned version!"; \
			echo "  Run 'make setup-voice' to checkout $(WHISPER_VERSION)"; \
		fi; \
		echo ""; \
	else \
		echo ""; \
		echo "❌ whisper.cpp submodule not initialized"; \
		echo "Run: git submodule update --init --recursive"; \
		echo ""; \
	fi

# Setup voice support (build whisper + download model)
.PHONY: setup-voice
setup-voice: $(WHISPER_LIB) download-model
	@echo ""
	@echo "================================================"
	@echo "✓ Voice support setup complete!"
	@echo "================================================"
	@echo ""
	@echo "Whisper.cpp version: $(WHISPER_VERSION)"
	@echo "Build with: make VOICE=1"
	@echo ""
	@echo "Environment variables:"
	@echo "  WHISPER_MODEL_PATH - Path to custom model"
	@echo "  VOICE_DEVICE       - Audio device (macOS: 0, Linux: default)"
	@echo ""
	@echo "Available models in whisper_models/:"
	@ls -lh whisper_models/*.bin 2>/dev/null || echo "  (none yet - run make download-model)"
	@echo ""

#
# Memvid FFI Integration
#

# Build the Rust memvid-ffi library
.PHONY: memvid-ffi
memvid-ffi:
	@if [ ! -d "$(MEMVID_FFI_DIR)" ]; then \
		echo ""; \
		echo "❌ Error: memvid-ffi directory not found at $(MEMVID_FFI_DIR)"; \
		echo ""; \
		echo "The memvid-ffi vendored library may be missing."; \
		echo ""; \
		exit 1; \
	fi
	@echo ""
	@echo "Building memvid-ffi Rust library..."
	@echo ""
	@cd $(MEMVID_FFI_DIR) && cargo build --release
	@echo ""
	@echo "✓ memvid-ffi library built successfully"
	@echo "  Library: $(MEMVID_FFI_LIB)"
	@echo ""
	@echo "Now build klawed with: make MEMVID=1"
	@echo ""

# Clean the memvid-ffi build artifacts
.PHONY: memvid-ffi-clean
memvid-ffi-clean:
	@echo "Cleaning memvid-ffi build artifacts..."
	@if [ -d "$(MEMVID_FFI_DIR)" ]; then \
		cd $(MEMVID_FFI_DIR) && cargo clean; \
		echo "✓ memvid-ffi build cleaned"; \
	else \
		echo "memvid-ffi directory not found, nothing to clean"; \
	fi

# Check memvid-ffi status
.PHONY: check-memvid
check-memvid:
	@echo ""
	@echo "Memvid FFI Status:"
	@echo "  Directory: $(MEMVID_FFI_DIR)"
	@if [ -d "$(MEMVID_FFI_DIR)" ]; then \
		echo "  ✓ Directory exists"; \
		if [ -f "$(MEMVID_FFI_LIB)" ]; then \
			echo "  ✓ Library built: $(MEMVID_FFI_LIB)"; \
			ls -lh $(MEMVID_FFI_LIB); \
		else \
			echo "  ○ Library not built yet"; \
			echo "    Run: make memvid-ffi"; \
		fi; \
	else \
		echo "  ✗ Directory not found"; \
		echo "    The memvid-ffi vendored library may be missing"; \
	fi
	@echo ""
	@echo "Current MEMVID setting: $(MEMVID)"
	@echo ""

#
# Docker Sandbox Integration
#

# Build the Podman sandbox image with version tag
.PHONY: docker-sandbox
docker-sandbox: test comprehensive-scan
	@echo ""
	@echo "Building Podman sandbox image..."
	@echo "Version: $(VERSION)"
	@echo ""
	@if ! command -v podman >/dev/null 2>&1; then \
		echo "❌ Error: podman not found"; \
		echo "Install Podman from: https://podman.io/getting-started/installation"; \
		exit 1; \
	fi
	@podman build -f Dockerfile.sandbox -t klawed-sandbox:$(VERSION) -t klawed-sandbox:latest .
	@echo ""
	@echo "✓ Podman sandbox image built successfully"
	@echo "  Image: klawed-sandbox:$(VERSION)"
	@echo "  Image: klawed-sandbox:latest"
	@echo ""
	@echo "Usage examples:"
	@echo "  podman run -it --rm -e OPENAI_API_KEY=\$$OPENAI_API_KEY klawed-sandbox:$(VERSION) \"your prompt\""
	@echo "  podman run -it --rm -e OPENAI_API_KEY=\$$OPENAI_API_KEY -v \$$(pwd):/workspace klawed-sandbox:$(VERSION) \"analyze this code\""
	@echo ""
	@echo "See docs/docker-sandbox-deployment.md for deployment guide"
	@echo ""

# Transfer the Podman sandbox image to filesurf-0 host via podman save/load
.PHONY: docker-push
docker-push:
	@echo ""
	@echo "Transferring Podman sandbox image to filesurf-0..."
	@echo "Version: $(VERSION)"
	@echo ""
	@if ! command -v podman >/dev/null 2>&1; then \
		echo "❌ Error: podman not found"; \
		echo "Install Podman from: https://podman.io/getting-started/installation"; \
		exit 1; \
	fi
	@echo "Checking for podman on filesurf-0..."
	@if ! ssh filesurf-0 'command -v podman >/dev/null 2>&1'; then \
		echo "❌ Error: podman not found on filesurf-0"; \
		echo "Install podman on the remote host: https://podman.io/getting-started/installation"; \
		exit 1; \
	fi
	@echo ""
	@echo "Checking if image klawed-sandbox:$(VERSION) exists locally..."
	@if ! podman image exists klawed-sandbox:$(VERSION); then \
		echo "Image not found locally, building..."; \
		echo ""; \
		$(MAKE) docker-sandbox; \
	else \
		echo "✓ Image klawed-sandbox:$(VERSION) exists locally"; \
	fi
	@echo ""
	@echo "Transferring image klawed-sandbox:$(VERSION) to filesurf-0..."
	@podman save klawed-sandbox:$(VERSION) | ssh filesurf-0 'podman load'
	@echo ""
	@echo "✓ Image transferred successfully to filesurf-0"
	@echo "  Image: klawed-sandbox:$(VERSION)"
	@echo ""
	@echo "The image is now available on filesurf-0 and can be used with:"
	@echo "  podman run -it --rm klawed-sandbox:$(VERSION)"
	@echo ""

# Rotate klawed images on filesurf-0 (keep only last 3)
# Lists all klawed-sandbox images, sorts by creation date, and removes all but the 3 most recent
.PHONY: docker-rotate
docker-rotate:
	@echo ""
	@echo "Rotating klawed images on filesurf-0 (keeping last 3)..."
	@echo ""
	@if ! ssh filesurf-0 'command -v podman >/dev/null 2>&1'; then \
		echo "❌ Error: podman not found on filesurf-0"; \
		echo "Install podman on the remote host: https://podman.io/getting-started/installation"; \
		exit 1; \
	fi
	@echo "Fetching klawed-sandbox images from filesurf-0..."
	@ssh filesurf-0 'podman images --format "{{.ID}} {{.Repository}}:{{.Tag}} {{.CreatedAt}}" | grep "klawed-sandbox" | sort -k3 -r' | \
		tail -n +4 | awk '{print $$1}' > /tmp/klawed_images_to_remove.txt || true
	@if [ -s /tmp/klawed_images_to_remove.txt ]; then \
		echo "Removing old images..."; \
		while read -r image_id; do \
			echo "  Removing image: $$image_id"; \
			ssh filesurf-0 "podman rmi $$image_id" || echo "    (already removed or in use)"; \
		done < /tmp/klawed_images_to_remove.txt; \
		rm -f /tmp/klawed_images_to_remove.txt; \
		echo ""; \
		echo "✓ Old images removed"; \
	else \
		echo "No old images to remove (less than 4 images found)"; \
		rm -f /tmp/klawed_images_to_remove.txt; \
	fi
	@echo ""
	@echo "Remaining klawed-sandbox images on filesurf-0:"
	@ssh filesurf-0 'podman images | grep "klawed-sandbox" || echo "  (none)"'
	@echo ""
