# Makefile for Klawed Browser Controller - Go Native Messaging Host

GO      = go
HOST    = klawed_browser_controller
CLIENT  = browser_ctl
GOFILES = main.go browser_ctl.go go.mod

PREFIX        ?= $(HOME)/.local
BINDIR         = $(PREFIX)/bin

# Chrome/Chromium/Brave native messaging host directories (Linux)
CHROME_HOST_DIR   = $(HOME)/.config/google-chrome/NativeMessagingHosts
CHROMIUM_HOST_DIR = $(HOME)/.config/chromium/NativeMessagingHosts
BRAVE_HOST_DIR    = $(HOME)/.config/BraveSoftware/Brave-Browser/NativeMessagingHosts

# Extension ID — must be set after loading the extension in Chrome
EXTENSION_ID ?= EXTENSION_ID_PLACEHOLDER

.PHONY: all build clean install uninstall configure help

all: build

build: $(HOST) $(CLIENT)

$(HOST): main.go go.mod
	$(GO) build -o $(HOST) ./main.go
	@echo "Built: $(HOST)"

$(CLIENT): browser_ctl.go go.mod
	$(GO) build -o $(CLIENT) ./browser_ctl.go
	@echo "Built: $(CLIENT)"

clean:
	rm -f $(HOST) $(CLIENT)

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
	@echo "Installing binaries to $(BINDIR) ..."
	install -d "$(BINDIR)"
	install -m 755 "$(HOST)" "$(BINDIR)/"
	install -m 755 "$(CLIENT)" "$(BINDIR)/"

	@echo "Installing Chrome native messaging manifest ..."
	install -d "$(CHROME_HOST_DIR)"
	sed -e "s|HOST_PATH|$(BINDIR)/$(HOST)|g" \
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

	@install -d "$(BRAVE_HOST_DIR)"
	@cp "$(CHROME_HOST_DIR)/com.klawed.browser_controller.json" \
	    "$(BRAVE_HOST_DIR)/com.klawed.browser_controller.json"
	@echo "  → $(BRAVE_HOST_DIR)/com.klawed.browser_controller.json"

	@echo ""
	@echo "Installation complete!"
	@echo "Host binary:   $(BINDIR)/$(HOST)"
	@echo "Client binary: $(BINDIR)/$(CLIENT)"
	@echo "Extension ID:  $(EXTENSION_ID)"
	@echo ""
	@echo "Now reload Chrome and click the extension icon to connect."

uninstall:
	rm -f "$(BINDIR)/$(HOST)"
	rm -f "$(BINDIR)/$(CLIENT)"
	rm -f "$(CHROME_HOST_DIR)/com.klawed.browser_controller.json"
	rm -f "$(CHROMIUM_HOST_DIR)/com.klawed.browser_controller.json"
	@echo "Uninstalled."

help:
	@echo "Targets:"
	@echo "  build                  Build both Go binaries"
	@echo "  install EXTENSION_ID=X Build, install binaries, register native host"
	@echo "  uninstall              Remove binaries and manifests"
	@echo "  clean                  Remove built binaries"
	@echo "  configure              Show setup instructions"
