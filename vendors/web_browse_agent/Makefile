# Go parameters
GOCMD=go
GOBUILD=$(GOCMD) build
GORUN=$(GOCMD) run
GOTEST=$(GOCMD) test
GOGET=$(GOCMD) get
GOMOD=$(GOCMD) mod
GOINSTALL=$(GOCMD) install

# Binary name
BINARY_NAME=web-browse-agent

# Main package path
MAIN_PKG=./cmd/agent

# Build flags
LDFLAGS=-ldflags "-s -w"

.PHONY: all build run test clean install lint deps playwright-install help

all: build

## build: Compile the binary
build:
	$(GOBUILD) $(LDFLAGS) -o $(BINARY_NAME) $(MAIN_PKG)

## run: Build and run the application
run: build
	./$(BINARY_NAME)

## test: Run all tests
test:
	$(GOTEST) -v ./...

## test-coverage: Run tests with coverage
test-coverage:
	$(GOTEST) -v -coverprofile=coverage.out ./...
	$(GOCMD) tool cover -html=coverage.out -o coverage.html

## clean: Remove build artifacts
clean:
	$(GOCMD) clean
	rm -f $(BINARY_NAME)
	rm -f $(BINARY_NAME).exe
	rm -f coverage.out
	rm -f coverage.html

## install: Install binary to GOPATH/bin
install:
	$(GOINSTALL) $(MAIN_PKG)

## lint: Run golangci-lint
lint:
	@which golangci-lint > /dev/null || (echo "golangci-lint not found, install with: go install github.com/golangci/golangci-lint/cmd/golangci-lint@latest" && exit 1)
	golangci-lint run ./...

## deps: Download and tidy dependencies
deps:
	$(GOMOD) download
	$(GOMOD) tidy

## playwright-install: Install Playwright browsers
playwright-install:
	@echo "Installing Playwright browsers..."
	@which playwright > /dev/null && playwright install || npx playwright install

## help: Display this help
help:
	@echo "Usage: make [target]"
	@echo ""
	@echo "Targets:"
	@sed -n 's/^##//p' $(MAKEFILE_LIST) | column -t -s ':' | sed 's/^/ /'
