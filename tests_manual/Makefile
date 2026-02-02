# Makefile for manual provider configuration tests
# These tests require actual API keys and are NOT part of the normal test suite

CC = gcc
# KLAWED_USE_BEDROCK can be set to enable AWS Bedrock support
BEDROCK_FLAG := $(if $(KLAWED_USE_BEDROCK),-DKLAWED_USE_BEDROCK)
CFLAGS = -Wall -Wextra -I../src -I.. \
    $(shell pkg-config --cflags libcjson libcurl) \
    -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE=1 -DTEST_BUILD $(BEDROCK_FLAG)

LDFLAGS = $(shell pkg-config --libs libcjson libcurl) \
    -lpthread -lcurl -lssl -lcrypto -lm

# Platform detection for macOS
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    CFLAGS += -D_DARWIN_C_SOURCE
    HOMEBREW_PREFIX := $(shell brew --prefix 2>/dev/null || echo "/opt/homebrew")
    CFLAGS += -I$(HOMEBREW_PREFIX)/include
    LDFLAGS += -L$(HOMEBREW_PREFIX)/lib
else
    LDFLAGS += -lbsd
endif

BUILD_DIR = build
TESTS = $(BUILD_DIR)/test_provider_configs $(BUILD_DIR)/test_provider_api_calls

.PHONY: all clean check-deps help

all: check-deps $(BUILD_DIR) $(TESTS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

check-deps:
	@echo "Checking dependencies..."
	@pkg-config --exists libcjson || (echo "Error: cJSON not found. Install libcjson-dev" && exit 1)
	@pkg-config --exists libcurl || (echo "Error: libcurl not found. Install libcurl4-openssl-dev" && exit 1)

# ============================================================================
# Base source files (shared)
# ============================================================================
BASE_SOURCES = \
    ../src/config.c \
    ../src/logger.c \
    ../src/data_dir.c \
    ../src/base64.c \
    ../src/array_resize.c

# ============================================================================
# Provider source files (for API call tests)
# ============================================================================
PROVIDER_SOURCES = \
    ../src/provider.c \
    ../src/openai_provider.c \
    ../src/anthropic_provider.c \
    ../src/bedrock_provider.c \
    ../src/deepseek_provider.c \
    ../src/moonshot_provider.c \
    ../src/aws_bedrock.c \
    ../src/bedrock_converse.c \
    ../src/http_client.c \
    ../src/openai_messages.c \
    ../src/openai_responses.c \
    ../src/retry_logic.c

# ============================================================================
# Conversation support files
# ============================================================================
CONVERSATION_SOURCES = \
    ../src/conversation/content_types.c \
    ../src/conversation/conversation_state.c \
    ../src/conversation/message_builder.c \
    ../src/conversation/message_parser.c

# ============================================================================
# Tool support files
# ============================================================================
TOOL_SOURCES = \
    ../src/tools/tool_definitions.c \
    ../src/tools/tool_registry.c \
    ../src/tools/tool_executor.c \
    ../src/tools/tool_filesystem.c \
    ../src/tools/tool_bash.c \
    ../src/tools/tool_search.c \
    ../src/tools/tool_sleep.c \
    ../src/tools/tool_todo.c \
    ../src/tools/tool_subagent.c \
    ../src/tools/tool_image.c \
    ../src/tool_utils.c \
    ../src/process_utils.c

# ============================================================================
# API layer files
# ============================================================================
API_SOURCES = \
    ../src/api/api_response.c \
    ../src/api/api_builder.c \
    ../src/api/api_client.c

# ============================================================================
# Utility files
# ============================================================================
UTIL_SOURCES = \
    ../src/util/file_utils.c \
    ../src/util/string_utils.c \
    ../src/util/timestamp_utils.c \
    ../src/util/format_utils.c \
    ../src/util/env_utils.c \
    ../src/util/output_utils.c \
    ../src/util/diff_utils.c

# ============================================================================
# Context files
# ============================================================================
CONTEXT_SOURCES = \
    ../src/context/system_prompt.c \
    ../src/context/environment.c \
    ../src/context/klawed_md.c

# ============================================================================
# UI stub files (minimal)
# ============================================================================
UI_SOURCES = \
    ../src/ui/ui_output.c \
    ../src/ui/print_helpers.c

# ============================================================================
# Arena implementation (needed for header-only arena.h)
# We define ARENA_IMPLEMENTATION in the test file itself
# ============================================================================
TEST_STUBS = test_stubs.c

# Combined sources for API call test
API_CALL_SOURCES = \
    $(BASE_SOURCES) \
    $(PROVIDER_SOURCES) \
    $(CONVERSATION_SOURCES) \
    $(TOOL_SOURCES) \
    $(API_SOURCES) \
    $(UTIL_SOURCES) \
    $(CONTEXT_SOURCES) \
    $(UI_SOURCES) \
    $(TEST_STUBS)

# Configuration-only test (no API calls)
$(BUILD_DIR)/test_provider_configs: test_provider_configs.c test_provider_configs.h $(BASE_SOURCES) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ test_provider_configs.c $(BASE_SOURCES) $(LDFLAGS)

# API call test (makes real API calls using actual provider code!)
$(BUILD_DIR)/test_provider_api_calls: test_provider_api_calls.c $(API_CALL_SOURCES) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ test_provider_api_calls.c $(API_CALL_SOURCES) $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "Manual Provider Test Suite"
	@echo ""
	@echo "Targets:"
	@echo "  all                    Build all tests"
	@echo "  test_provider_configs  Build config validation test (safe)"
	@echo "  test_provider_api_calls Build API call test (costs money!)"
	@echo "  clean                  Remove built files"
	@echo ""
	@echo "Usage:"
	@echo "  $(BUILD_DIR)/test_provider_configs --list"
	@echo "  $(BUILD_DIR)/test_provider_configs --test-all"
	@echo ""
	@echo "  export OPENAI_API_KEY='sk-...'"
	@echo "  $(BUILD_DIR)/test_provider_api_calls --test-all"
	@echo "  $(BUILD_DIR)/test_provider_api_calls --test-read --verbose"
