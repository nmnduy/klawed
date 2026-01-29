# Makefile for manual provider configuration tests
# These tests require actual API keys and are NOT part of the normal test suite
#
# This builds a minimal test executable that only tests provider initialization,
# without pulling in the entire TUI and tool system.

BUILD_DIR = ../build
SRC_DIR = ../src

CC = gcc
CFLAGS = -Wall -Wextra -I$(SRC_DIR) -I.. \
    $(shell pkg-config --cflags libcjson libcurl) \
    -DTEST_BUILD -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE=1

# Platform detection
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
    CFLAGS += -D_DARWIN_C_SOURCE
    HOMEBREW_PREFIX := $(shell brew --prefix 2>/dev/null || echo "/opt/homebrew")
    CFLAGS += -I$(HOMEBREW_PREFIX)/include
    LDFLAGS = -L$(HOMEBREW_PREFIX)/lib
else
    LDFLAGS =
endif

LDFLAGS += $(shell pkg-config --libs libcjson libcurl) \
    -lpthread -lbsd -lsqlite3 -lcurl -lssl -lcrypto -lm

# Minimal source files for provider testing (avoids TUI conflicts)
# These are carefully selected to avoid duplicate symbol definitions
CORE_SOURCES = \
    $(SRC_DIR)/config.c \
    $(SRC_DIR)/logger.c \
    $(SRC_DIR)/data_dir.c \
    $(SRC_DIR)/base64.c \
    $(SRC_DIR)/array_resize.c

# Test executables
TESTS = test_provider_configs

.PHONY: all clean test list check-deps

all: check-deps $(TESTS)

check-deps:
	@echo "Checking dependencies..."
	@pkg-config --exists libcjson || (echo "Error: cJSON not found. Install libcjson-dev" && exit 1)
	@pkg-config --exists libcurl || (echo "Error: libcurl not found. Install libcurl4-openssl-dev" && exit 1)

# Provider configuration test - stub out the provider functions we need
test_provider_configs: test_provider_configs.c test_provider_configs.h $(CORE_SOURCES)
	@echo "Building test_provider_configs..."
	@$(CC) $(CFLAGS) -o $@ test_provider_configs.c $(CORE_SOURCES) $(LDFLAGS)
	@echo "Build successful: $@"

# Run the test with all providers
test: test_provider_configs
	./test_provider_configs --test-all

# List providers
list: test_provider_configs
	./test_provider_configs --list

clean:
	rm -f $(TESTS)

# Help
help:
	@echo "Manual Provider Configuration Tests"
	@echo ""
	@echo "Targets:"
	@echo "  all             Build all tests (default)"
	@echo "  check-deps      Verify required dependencies are installed"
	@echo "  test            Run all provider tests"
	@echo "  list            List provider configuration status"
	@echo "  clean           Remove built files"
	@echo ""
	@echo "Usage:"
	@echo "  ./test_provider_configs [options]"
	@echo "    --list        List all providers and their configuration status"
	@echo "    --test-all    Test all configured providers"
	@echo "    --sonnet      Test sonnet-4.5 (LM Studio local)"
	@echo "    --minimax     Test minimax-2.1"
	@echo "    --kimi        Test kimi-k2.5"
	@echo "    --bedrock     Test AWS Bedrock"
	@echo ""
	@echo "Environment variables required:"
	@echo "  MINIMAX_API_KEY         (for MiniMax API)"
	@echo "  MOONSHOT_AI_API_KEY     (for Moonshot/Kimi API)"
	@echo "  AWS_ACCESS_KEY_ID       (for Bedrock)"
	@echo "  AWS_SECRET_ACCESS_KEY   (for Bedrock)"
	@echo "  AWS_REGION              (for Bedrock, optional, defaults to us-west-2)"
	@echo "  ANTHROPIC_MODEL         (for Bedrock model, optional)"
