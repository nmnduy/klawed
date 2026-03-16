# Makefile for web_browse_agent

.PHONY: all build clean test fmt vet lint install install-deps help

# Go parameters
GOCMD=go
GOBUILD=$(GOCMD) build
GOCLEAN=$(GOCMD) clean
GOTEST=$(GOCMD) test
GOMOD=$(GOCMD) mod
GOFMT=$(GOCMD) fmt
GOVET=$(GOCMD) vet
BINARY_NAME=web_browse_agent
BINARY_DIR=bin
BINARY_PATH=$(BINARY_DIR)/$(BINARY_NAME)

# Default target
all: build

# Build the project
build:
	@echo "Building $(BINARY_NAME)..."
	@mkdir -p $(BINARY_DIR)
	$(GOBUILD) -o $(BINARY_PATH) ./cmd/web_browse_agent
	@echo "Build complete: $(BINARY_PATH)"

# Clean build files
clean:
	@echo "Cleaning..."
	$(GOCLEAN)
	rm -rf $(BINARY_DIR)
	@echo "Clean complete"

# Run tests
test:
	@echo "Running tests..."
	$(GOTEST) ./...

# Format code
fmt:
	@echo "Formatting code..."
	$(GOFMT) ./...

# Vet code
vet:
	@echo "Vetting code..."
	$(GOVET) ./...

# Lint (requires golangci-lint)
lint:
	@echo "Linting..."
	golangci-lint run ./...

# Install dependencies
install-deps:
	@echo "Installing dependencies..."
	$(GOMOD) download
	@echo "Installing Playwright browsers..."
	$(GOCMD) run github.com/playwright-community/playwright-go/cmd/playwright@v0.5200.1 install chromium

# Install system libraries for Chromium in datacenter/stripped container environments
# Downloads Debian bookworm packages and extracts to ./chromium_libs/
install-system-libs:
	@echo "Installing Chromium system libraries for datacenter environments..."
	@chmod +x ./install-system-libs.sh
	./install-system-libs.sh ./chromium_libs
	@echo ""
	@echo "Libraries installed. Use 'make install-datacenter' for a full setup."

# Full datacenter install: build + playwright deps + system libs + wrapper script
install-datacenter: build install-deps install-system-libs
	@echo "Creating wrapper script that sets LD_LIBRARY_PATH automatically..."
	@cp $(BINARY_PATH) $(BINARY_DIR)/web_browse_agent.bin
	@cp web_browse_agent.sh $(BINARY_PATH)
	@chmod +x $(BINARY_PATH)
	@cp -r chromium_libs $(BINARY_DIR)/chromium_libs 2>/dev/null || true
	@echo ""
	@echo "Datacenter install complete!"
	@echo "The binary at $(BINARY_PATH) now automatically sets up library paths."
	@echo ""
	@echo "Test with:"
	@echo "  $(BINARY_PATH) --session test open https://example.com"

# Install to ~/.local/bin
install: build
	@echo "Installing $(BINARY_NAME) to ~/.local/bin..."
	@mkdir -p ~/.local/bin
	@# Remove old binary first to avoid "Text file busy" error
	@rm -f ~/.local/bin/$(BINARY_NAME)
	@cp $(BINARY_PATH) ~/.local/bin/$(BINARY_NAME)
	@echo "Install complete: ~/.local/bin/$(BINARY_NAME)"

# Install development tools
install-dev-tools:
	@echo "Installing development tools..."
	$(GOCMD) install github.com/golangci/golangci-lint/cmd/golangci-lint@latest
	$(GOCMD) install github.com/securego/gosec/v2/cmd/gosec@latest

# Run the application
run: build
	@echo "Running $(BINARY_NAME)..."
	./$(BINARY_PATH) --help

# Create a simple test
test-simple: build
	@echo "Testing basic functionality..."
	@# Create a test session
	./$(BINARY_PATH) --session test123 ping || true

# Show help
help:
	@echo "Available targets:"
	@echo "  all           - Build the project (default)"
	@echo "  build         - Build the binary"
	@echo "  clean         - Clean build files"
	@echo "  test          - Run tests"
	@echo "  fmt           - Format code"
	@echo "  vet           - Vet code"
	@echo "  lint          - Lint code (requires golangci-lint)"
	@echo "  install       - Install binary to ~/.local/bin"
	@echo "  install-deps  - Install dependencies including Playwright"
	@echo "  install-dev-tools - Install development tools"
	@echo "  run           - Build and run with --help"
	@echo "  test-simple   - Run a simple test"
	@echo "  help          - Show this help message"

# Development workflow
dev: fmt vet build test-simple
