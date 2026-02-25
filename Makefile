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
# clang-tidy, memscan, ci-*, etc.) have been removed.
# Version bump targets (bump-patch, bump-minor, bump-major) are preserved below.
# Use "zig build --help" for the full list of available build steps.

.PHONY: all build test debug install clean fmt-whitespace check-deps help \
        bump-patch bump-minor bump-major show-version update-version _apply-version

VERSION_FILE := VERSION
VERSION      := $(shell cat $(VERSION_FILE) 2>/dev/null | tr -d '[:space:]')

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
	@echo "Version management:"
	@echo "  make show-version                    Show current version"
	@echo "  make bump-patch                      Bump patch  (e.g. 0.29.33 → 0.29.34)"
	@echo "  make bump-minor                      Bump minor, reset patch (e.g. 0.29.33 → 0.30.0)"
	@echo "  make bump-major                      Bump major, reset minor+patch (e.g. 0.29.33 → 1.0.0)"
	@echo "  make update-version NEW_VERSION=x.y.z  Set an explicit version"
	@echo ""
	@echo "Quick start:"
	@echo "  export OPENAI_API_KEY=your-key"
	@echo "  make && ./zig-out/bin/klawed \"your prompt\""
	@echo ""

show-version:
	@echo "Version: $(VERSION)"
	@echo "Version file: $(VERSION_FILE)"

update-version:
	@if [ -z "$(NEW_VERSION)" ]; then \
		echo "Error: NEW_VERSION parameter required"; \
		echo "Usage: make update-version NEW_VERSION=1.2.3"; \
		exit 1; \
	fi
	@$(MAKE) _apply-version _V="$(NEW_VERSION)"

bump-patch:
	@MAJOR=$$(echo "$(VERSION)" | sed 's/\([0-9]*\)\..*/\1/'); \
	MINOR=$$(echo "$(VERSION)" | sed 's/[0-9]*\.\([0-9]*\)\..*/\1/'); \
	PATCH=$$(echo "$(VERSION)" | sed 's/[0-9]*\.[0-9]*\.\([0-9]*\).*/\1/'); \
	NEW="$$MAJOR.$$MINOR.$$((PATCH + 1))"; \
	$(MAKE) _apply-version _V="$$NEW"

bump-minor:
	@MAJOR=$$(echo "$(VERSION)" | sed 's/\([0-9]*\)\..*/\1/'); \
	MINOR=$$(echo "$(VERSION)" | sed 's/[0-9]*\.\([0-9]*\)\..*/\1/'); \
	NEW="$$MAJOR.$$((MINOR + 1)).0"; \
	$(MAKE) _apply-version _V="$$NEW"

bump-major:
	@MAJOR=$$(echo "$(VERSION)" | sed 's/\([0-9]*\)\..*/\1/'); \
	NEW="$$((MAJOR + 1)).0.0"; \
	$(MAKE) _apply-version _V="$$NEW"

# Internal helper — do not call directly.
# Updates VERSION file and all version literals in zig/version.zig.
_apply-version:
	@V="$(_V)"; \
	MAJOR=$$(echo "$$V" | sed 's/\([0-9]*\)\..*/\1/'); \
	MINOR=$$(echo "$$V" | sed 's/[0-9]*\.\([0-9]*\)\..*/\1/'); \
	PATCH=$$(echo "$$V" | sed 's/[0-9]*\.[0-9]*\.\([0-9]*\).*/\1/'); \
	echo "$(VERSION) → $$V"; \
	echo "$$V" > $(VERSION_FILE); \
	sed -i "s/pub const VERSION: \[\]const u8 = \"[^\"]*\";/pub const VERSION: []const u8 = \"$$V\";/" zig/version.zig; \
	sed -i "s/expectEqualStrings(\"[0-9.]*\", VERSION)/expectEqualStrings(\"$$V\", VERSION)/" zig/version.zig; \
	sed -i "s/test \"VERSION: matches [^\"]*\"/test \"VERSION: matches $$V\"/" zig/version.zig; \
	sed -i "s/expectEqual(@as(u32, [0-9]*), version\.major)/expectEqual(@as(u32, $$MAJOR), version.major)/" zig/version.zig; \
	sed -i "s/expectEqual(@as(u32, [0-9]*), version\.minor)/expectEqual(@as(u32, $$MINOR), version.minor)/" zig/version.zig; \
	sed -i "s/expectEqual(@as(u32, [0-9]*), version\.patch)/expectEqual(@as(u32, $$PATCH), version.patch)/" zig/version.zig; \
	echo "✓ VERSION → $$V"; \
	echo "✓ zig/version.zig updated"; \
	echo ""; \
	echo "Next: git add VERSION zig/version.zig && git commit -m \"chore: bump version to $$V\""
