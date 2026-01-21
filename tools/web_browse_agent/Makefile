# Makefile for web_browse_agent

.PHONY: all build clean test fmt vet lint install-deps help

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
	$(GOCMD) run github.com/playwright-community/playwright-go/cmd/playwright@latest install

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
	@echo "  install-deps  - Install dependencies including Playwright"
	@echo "  install-dev-tools - Install development tools"
	@echo "  run           - Build and run with --help"
	@echo "  test-simple   - Run a simple test"
	@echo "  help          - Show this help message"

# Development workflow
dev: fmt vet build test-simple
