# Makefile for manual provider configuration tests
# These tests require actual API keys and are NOT part of the normal test suite

CC = gcc
CFLAGS = -Wall -Wextra -I../src -I.. \
    $(shell pkg-config --cflags libcjson libcurl) \
    -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE=1

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

TESTS = test_provider_configs test_provider_api_calls

.PHONY: all clean check-deps help

all: check-deps $(TESTS)

check-deps:
	@echo "Checking dependencies..."
	@pkg-config --exists libcjson || (echo "Error: cJSON not found. Install libcjson-dev" && exit 1)
	@pkg-config --exists libcurl || (echo "Error: libcurl not found. Install libcurl4-openssl-dev" && exit 1)

# Source files for config test
CONFIG_SOURCES = \
    ../src/config.c \
    ../src/logger.c \
    ../src/data_dir.c \
    ../src/base64.c \
    ../src/array_resize.c

# Configuration-only test (no API calls)
test_provider_configs: test_provider_configs.c test_provider_configs.h $(CONFIG_SOURCES)
	$(CC) $(CFLAGS) -o $@ test_provider_configs.c $(CONFIG_SOURCES) $(LDFLAGS)

# API call test (makes real API calls!)
test_provider_api_calls: test_provider_api_calls.c $(CONFIG_SOURCES)
	$(CC) $(CFLAGS) -o $@ test_provider_api_calls.c $(CONFIG_SOURCES) $(LDFLAGS)

clean:
	rm -f $(TESTS)

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
	@echo "  ./test_provider_configs --list"
	@echo "  ./test_provider_configs --test-all"
	@echo ""
	@echo "  export OPENAI_API_KEY='sk-...'"
	@echo "  ./test_provider_api_calls --test-all"
	@echo "  ./test_provider_api_calls --test-read --verbose"
