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
CSS_OUTPUT := src/main/resources/META-INF/resources/assets/main.css
ASSETS_DIR := src/main/resources/META-INF/resources/assets
TARGET_DIR := target

.PHONY: help build-dist dev css clean install-deps

# Default target
help:
	@echo "Available targets:"
	@echo "  build-dist  - Build CSS + Quarkus production JAR"
	@echo "  dev         - Start Quarkus development server (mvn quarkus:dev)"
	@echo "  css         - Build Tailwind CSS output (npm run build)"
	@echo "  install-deps - Install npm dependencies"
	@echo "  clean       - Clean build artifacts"

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
dev: css
	@echo "Starting Quarkus development server..."
	@$(MAVEN) quarkus:dev

# Build Tailwind CSS output
css: install-deps
	@echo "Building Tailwind CSS with cache busting..."
	@mkdir -p $(ASSETS_DIR)
	@$(NPM) run build
	@echo "CSS built with hash to $(ASSETS_DIR)"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(ASSETS_DIR)/*
	@echo "Clean complete!"