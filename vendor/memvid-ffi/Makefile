.PHONY: help build build-release test test-verbose clean fmt fmt-check clippy clippy-fix doc doc-open check install run-example docker-build docker-test

# Default target
.DEFAULT_GOAL := help

# Variables
CARGO := cargo
RUST_VERSION := 1.85.0
FEATURES := lex,pdf_extract

# Colors for output
CYAN := \033[0;36m
GREEN := \033[0;32m
YELLOW := \033[0;33m
NC := \033[0m # No Color

help: ## Show this help message
	@echo "$(CYAN)Memvid Makefile Commands:$(NC)"
	@echo ""
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "  $(GREEN)%-20s$(NC) %s\n", $$1, $$2}'
	@echo ""

install: ## Install Rust toolchain and dependencies
	@echo "$(CYAN)Installing Rust toolchain...$(NC)"
	@rustup toolchain install $(RUST_VERSION) || true
	@rustup default $(RUST_VERSION) || true
	@echo "$(CYAN)Installing cargo dependencies...$(NC)"
	@$(CARGO) fetch

check: ## Check code without building
	@echo "$(CYAN)Checking code...$(NC)"
	@$(CARGO) check --features $(FEATURES)

build: ## Build in debug mode
	@echo "$(CYAN)Building in debug mode...$(NC)"
	@$(CARGO) build --features $(FEATURES)

build-release: ## Build in release mode (optimized)
	@echo "$(CYAN)Building in release mode...$(NC)"
	@$(CARGO) build --release --features $(FEATURES)

build-all-features: ## Build with all features enabled
	@echo "$(CYAN)Building with all features...$(NC)"
	@$(CARGO) build --release --all-features

test: ## Run tests
	@echo "$(CYAN)Running tests...$(NC)"
	@$(CARGO) test --features $(FEATURES)

test-verbose: ## Run tests with output
	@echo "$(CYAN)Running tests with output...$(NC)"
	@$(CARGO) test --features $(FEATURES) -- --nocapture

test-integration: ## Run integration tests only
	@echo "$(CYAN)Running integration tests...$(NC)"
	@$(CARGO) test --test lifecycle --test search --test mutation --test crash_recovery --test doctor_recovery --test encryption_capsule --test replay_integrity --test single_file --features $(FEATURES)

test-unit: ## Run unit tests only
	@echo "$(CYAN)Running unit tests...$(NC)"
	@$(CARGO) test --lib --features $(FEATURES)

fmt: ## Format code
	@echo "$(CYAN)Formatting code...$(NC)"
	@$(CARGO) fmt

fmt-check: ## Check code formatting
	@echo "$(CYAN)Checking code formatting...$(NC)"
	@$(CARGO) fmt -- --check

clippy: ## Run clippy linter
	@echo "$(CYAN)Running clippy...$(NC)"
	@$(CARGO) clippy --features $(FEATURES) -- -D warnings

clippy-fix: ## Run clippy and auto-fix issues
	@echo "$(CYAN)Running clippy with auto-fix...$(NC)"
	@$(CARGO) clippy --fix --features $(FEATURES) -- -D warnings

doc: ## Generate documentation
	@echo "$(CYAN)Generating documentation...$(NC)"
	@$(CARGO) doc --features $(FEATURES) --no-deps

doc-open: ## Generate and open documentation
	@echo "$(CYAN)Generating and opening documentation...$(NC)"
	@$(CARGO) doc --features $(FEATURES) --no-deps --open

clean: ## Clean build artifacts
	@echo "$(CYAN)Cleaning build artifacts...$(NC)"
	@$(CARGO) clean

clean-all: clean ## Clean everything including target directory
	@echo "$(CYAN)Cleaning all artifacts...$(NC)"
	@rm -rf target/

run-example-basic: ## Run basic_usage example
	@echo "$(CYAN)Running basic_usage example...$(NC)"
	@$(CARGO) run --example basic_usage --features $(FEATURES)

run-example-pdf: ## Run pdf_ingestion example
	@echo "$(CYAN)Running pdf_ingestion example...$(NC)"
	@$(CARGO) run --example pdf_ingestion --features $(FEATURES)

run-example-clip: ## Run clip_visual_search example (requires clip feature)
	@echo "$(CYAN)Running clip_visual_search example...$(NC)"
	@$(CARGO) run --example clip_visual_search --features $(FEATURES),clip

run-example-whisper: ## Run test_whisper example (requires whisper feature)
	@echo "$(CYAN)Running test_whisper example...$(NC)"
	@$(CARGO) run --example test_whisper --features $(FEATURES),whisper

lint: fmt-check clippy ## Run all linting checks

verify: check lint test ## Run all verification checks (check, lint, test)

ci: verify build-release ## Run CI pipeline (verify + release build)

docker-build: ## Build Docker image
	@echo "$(CYAN)Building Docker image...$(NC)"
	@cd docker/cli && docker build -t memvid/cli:latest .

docker-test: ## Test Docker image
	@echo "$(CYAN)Testing Docker image...$(NC)"
	@cd docker/cli && ./test.sh

bench: ## Run benchmarks (if available)
	@echo "$(CYAN)Running benchmarks...$(NC)"
	@$(CARGO) bench --features $(FEATURES) || echo "$(YELLOW)No benchmarks found$(NC)"

update: ## Update dependencies
	@echo "$(CYAN)Updating dependencies...$(NC)"
	@$(CARGO) update

audit: ## Audit dependencies for security vulnerabilities
	@echo "$(CYAN)Auditing dependencies...$(NC)"
	@$(CARGO) audit || echo "$(YELLOW)cargo-audit not installed. Install with: cargo install cargo-audit$(NC)"

version: ## Show version information
	@echo "$(CYAN)Version Information:$(NC)"
	@$(CARGO) --version
	@rustc --version
	@echo ""
	@echo "$(CYAN)Project version:$(NC)"
	@grep "^version" Cargo.toml
