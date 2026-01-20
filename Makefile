# Makefile for FileSurf v2
# 
# Available targets:
#   build-dist  - Build production distribution
#   dev         - Start development server
#   css         - Build Tailwind CSS output

# Variables
NPM := npm
MAVEN := mvn
NODE_MODULES := node_modules
VITE := $(NPM) run
TAILWIND_CONFIG := tailwind.config.js
CSS_SOURCE := src/main/resources/css/index.css
CSS_OUTPUT := src/main/resources/META-INF/resources/dist/main.css
DIST_DIR := src/main/resources/META-INF/resources/dist
TARGET_DIR := target

.PHONY: help build-dist dev css css-dev clean install-deps install-search-tools check-search-tools check-colors

# Default target
help:
	@echo "Available targets:"
	@echo "  build-dist          - Build CSS + Quarkus production JAR"
	@echo "  dev                 - Start Quarkus development server (mvn quarkus:dev)"
	@echo "  css                 - Build Tailwind CSS output (production with cache busting)"
	@echo "  css-dev             - Build Tailwind CSS output (development, no cache busting)"
	@echo "  check-colors        - Check templates for hardcoded color values (warning only)"
	@echo "  install-deps        - Install npm dependencies"
	@echo "  install-search-tools - Install fd and ripgrep for fast file search"
	@echo "  check-search-tools  - Check if fast search tools are installed"
	@echo "  clean               - Clean build artifacts"

# Install npm dependencies if node_modules doesn't exist
install-deps: $(NODE_MODULES)

$(NODE_MODULES): package.json
	@echo "Installing npm dependencies..."
	@$(NPM) install
	@touch $(NODE_MODULES)

# Build production distribution (CSS + Quarkus JAR)
build-dist: css
	@echo "Building Quarkus production JAR..."
	@$(MAVEN) clean package -DskipTests
	@echo "Production build complete! JAR file available in $(TARGET_DIR)/"

# Start Quarkus development server
dev: css-dev
	@echo "Starting Quarkus development server..."
	@$(MAVEN) quarkus:dev

# Build Tailwind CSS output (production with cache busting)
css: install-deps
	@echo "Building Tailwind CSS with cache busting (production)..."
	@mkdir -p $(DIST_DIR)
	@$(NPM) run build
	@echo "CSS built with hash to $(DIST_DIR)"

# Build Tailwind CSS output (development, no hashing)
css-dev: install-deps
	@echo "Building Tailwind CSS (development, no cache busting)..."
	@mkdir -p $(DIST_DIR)
	@$(NPM) run build:dev
	@echo "CSS built to $(DIST_DIR)/main.css"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(DIST_DIR)/*
	@echo "Clean complete!"

# Check templates for hardcoded colors
check-colors:
	@echo "Checking templates for hardcoded color values..."
	@bash scripts/check-hardcoded-colors.sh || true
	@echo ""

# Check if fast search tools are installed
check-search-tools:
	@echo "Checking for fast file search tools..."
	@echo ""
	@echo "fd (fd-find):"
	@which fd 2>/dev/null && fd --version || which fdfind 2>/dev/null && fdfind --version || echo "  NOT INSTALLED"
	@echo ""
	@echo "ripgrep (rg):"
	@which rg 2>/dev/null && rg --version | head -1 || echo "  NOT INSTALLED"
	@echo ""
	@echo "find (fallback):"
	@which find 2>/dev/null && find --version 2>/dev/null | head -1 || echo "  Available (GNU find or BSD find)"
	@echo ""
	@echo "Recommendation: Install fd-find for best search performance"
	@echo "  Debian/Ubuntu: apt install fd-find ripgrep"
	@echo "  Note: On Debian/Ubuntu, the binary is named 'fdfind' (not 'fd')"

# Install fast search tools (fd and ripgrep)
# Note: Requires sudo on Linux
install-search-tools:
	@echo "Installing fast file search tools..."
	@if [ "$$(uname)" = "Darwin" ]; then \
		echo "macOS detected, using Homebrew..."; \
		brew install fd ripgrep; \
	elif [ -f /etc/debian_version ]; then \
		echo "Debian/Ubuntu detected, using apt..."; \
		echo "Note: On Debian/Ubuntu, fd is named 'fdfind'. A symlink will be created."; \
		sudo apt-get update && sudo apt-get install -y fd-find ripgrep; \
		if [ ! -f /usr/local/bin/fd ] && [ -f /usr/bin/fdfind ]; then \
			sudo ln -sf /usr/bin/fdfind /usr/local/bin/fd; \
			echo "Created symlink: /usr/local/bin/fd -> /usr/bin/fdfind"; \
		fi; \
	elif [ -f /etc/fedora-release ]; then \
		echo "Fedora detected, using dnf..."; \
		sudo dnf install -y fd-find ripgrep; \
	elif [ -f /etc/arch-release ]; then \
		echo "Arch detected, using pacman..."; \
		sudo pacman -S --noconfirm fd ripgrep; \
	else \
		echo "Unknown OS. Please install fd and ripgrep manually:"; \
		echo "  fd: https://github.com/sharkdp/fd#installation"; \
		echo "  ripgrep: https://github.com/BurntSushi/ripgrep#installation"; \
		exit 1; \
	fi
	@echo ""
	@echo "Installation complete! Verifying..."
	@$(MAKE) check-search-tools