# Makefile — thin shim delegating to zig build
#
# The project has been fully migrated from C to Zig (v2.0.0-zig).
# All build logic lives in build.zig.  This Makefile is kept only for
# compatibility with scripts and contributors who type "make".
#
# Targets preserved from the original Makefile:
#   all / build    — compile the klawed binary
#   test           — run the Zig unit-test suite
#   debug          — build a debug-optimised binary
#   install        — install to ~/.local/bin (zig build install)
#   clean          — remove build artefacts
#   fmt-whitespace — format Zig sources
#   check-deps     — verify required build-time dependencies
#   help           — print this message
#
# All other targets that existed in the old C Makefile (sanitize, valgrind,
# clang-tidy, memscan, ci-*, version bump, etc.) have been removed.
# Use "zig build --help" for the full list of available build steps.

.PHONY: all build test debug install clean fmt-whitespace check-deps help

all: build

build:
	zig build

test:
	zig build test

debug:
	zig build debug

install:
	zig build install

clean:
	rm -rf zig-cache zig-out

fmt-whitespace:
	zig fmt zig/

check-deps:
	@echo "Checking build-time dependencies..."
	@which zig > /dev/null 2>&1 || (echo "ERROR: 'zig' not found in PATH. Install Zig 0.12.1." && exit 1)
	@pkg-config --exists libcurl 2>/dev/null || (echo "ERROR: libcurl not found. Install libcurl-dev." && exit 1)
	@pkg-config --exists sqlite3 2>/dev/null || (echo "ERROR: sqlite3 not found. Install libsqlite3-dev." && exit 1)
	@echo "All required dependencies found."

help:
	@echo ""
	@echo "klawed — Zig-native AI coding agent (migrated from C in v2.0.0-zig)"
	@echo ""
	@echo "Usage:"
	@echo "  make              Build the klawed binary (zig build)"
	@echo "  make test         Run the Zig unit-test suite (zig build test)"
	@echo "  make debug        Build a debug binary (zig build debug)"
	@echo "  make install      Install binary to prefix (zig build install)"
	@echo "  make clean        Remove zig-cache and zig-out"
	@echo "  make fmt-whitespace  Format Zig sources (zig fmt zig/)"
	@echo "  make check-deps   Verify zig, libcurl, sqlite3 are installed"
	@echo ""
	@echo "Quick start:"
	@echo "  export OPENAI_API_KEY=your-key"
	@echo "  make && ./zig-out/bin/klawed \"your prompt\""
	@echo ""
