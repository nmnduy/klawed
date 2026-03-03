# Makefile for Klawed Browser Controller - Go Native Messaging Host

GO      = go
BINARY  = klawed_browser_controller
GOFILES = main.go go.mod

PREFIX        ?= $(HOME)/.local
BINDIR         = $(PREFIX)/bin

# Chrome/Chromium native messaging host directories (Linux)
CHROME_HOST_DIR   = $(HOME)/.config/google-chrome/NativeMessagingHosts
CHROMIUM_HOST_DIR = $(HOME)/.config/chromium/NativeMessagingHosts

# Extension ID — must be set after loading the extension in Chrome
EXTENSION_ID ?= EXTENSION_ID_PLACEHOLDER

.PHONY: all build clean install uninstall configure help

all: build

build: $(BINARY)

$(BINARY): $(GOFILES)
	$(GO) build -o $(BINARY) ./main.go
	@echo "Built: $(BINARY)"

clean:
	rm -f $(BINARY)

configure:
	@echo "=========================================="
	@echo " Chrome Extension Native Messaging Setup"
	@echo "=========================================="
	@echo ""
	@echo "Steps:"
	@echo "  1. Open Chrome and go to chrome://extensions/"
	@echo "  2. Enable 'Developer mode' (toggle top-right)"
	@echo "  3. Click 'Load unpacked', select the extension/ directory"
	@echo "  4. Copy the Extension ID shown (32 lowercase letters)"
	@echo "  5. Run:  make install EXTENSION_ID=<your-extension-id>"
	@echo ""
	@echo "Current EXTENSION_ID: $(EXTENSION_ID)"
	@if [ "$(EXTENSION_ID)" = "EXTENSION_ID_PLACEHOLDER" ]; then \
		echo ""; \
		echo "WARNING: EXTENSION_ID not set! Set it with:"; \
		echo "  make install EXTENSION_ID=abcdefghijklmnopqrstuvwxyzabcdef"; \
		echo ""; \
		exit 1; \
	fi

install: build configure
	@echo ""
	@echo "Installing binary to $(BINDIR)/$(BINARY) ..."
	install -d "$(BINDIR)"
	install -m 755 "$(BINARY)" "$(BINDIR)/"

	@echo "Installing Chrome native messaging manifest ..."
	install -d "$(CHROME_HOST_DIR)"
	sed -e "s|HOST_PATH|$(BINDIR)/$(BINARY)|g" \
	    -e "s|EXTENSION_ID_PLACEHOLDER|$(EXTENSION_ID)|g" \
	    com.klawed.browser_controller.json \
	    > "$(CHROME_HOST_DIR)/com.klawed.browser_controller.json"
	@echo "  → $(CHROME_HOST_DIR)/com.klawed.browser_controller.json"

	@if [ -d "$(CHROMIUM_HOST_DIR)" ]; then \
		install -d "$(CHROMIUM_HOST_DIR)"; \
		cp "$(CHROME_HOST_DIR)/com.klawed.browser_controller.json" \
		   "$(CHROMIUM_HOST_DIR)/com.klawed.browser_controller.json"; \
		echo "  → $(CHROMIUM_HOST_DIR)/com.klawed.browser_controller.json"; \
	fi

	@echo ""
	@echo "Installation complete!"
	@echo "Binary: $(BINDIR)/$(BINARY)"
	@echo "Extension ID: $(EXTENSION_ID)"
	@echo ""
	@echo "Now reload Chrome and click the extension icon to connect."

uninstall:
	rm -f "$(BINDIR)/$(BINARY)"
	rm -f "$(CHROME_HOST_DIR)/com.klawed.browser_controller.json"
	rm -f "$(CHROMIUM_HOST_DIR)/com.klawed.browser_controller.json"
	@echo "Uninstalled."

help:
	@echo "Targets:"
	@echo "  build                  Build the Go binary"
	@echo "  install EXTENSION_ID=X Build, install binary, register native host"
	@echo "  uninstall              Remove binary and manifests"
	@echo "  clean                  Remove built binary"
	@echo "  configure              Show setup instructions"
