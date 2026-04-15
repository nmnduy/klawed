#!/bin/bash
# Install script for macOS

set -e

HOST_BINARY="$HOME/.local/bin/klawed_browser_controller"
CLIENT_BINARY="$HOME/.local/bin/browser_ctl"
EXTENSION_ID="${1:-}"

if [ -z "$EXTENSION_ID" ]; then
    echo "Usage: ./install-macos.sh <EXTENSION_ID>"
    echo ""
    echo "Get your Extension ID from chrome://extensions/ after loading the extension"
    exit 1
fi

CHROME_DIR="$HOME/Library/Application Support/Google/Chrome/NativeMessagingHosts"
CHROME_CANARY_DIR="$HOME/Library/Application Support/Google/Chrome Canary/NativeMessagingHosts"
CHROMIUM_DIR="$HOME/Library/Application Support/Chromium/NativeMessagingHosts"
BRAVE_DIR="$HOME/Library/Application Support/BraveSoftware/Brave-Browser/NativeMessagingHosts"

BINDIR="$HOME/.local/bin"
install -d "$BINDIR"

if [ -f "klawed_browser_controller" ]; then
    install -m 755 klawed_browser_controller "$BINDIR/"
    echo "Installed host binary: $HOST_BINARY"
fi

if [ -f "browser_ctl" ]; then
    install -m 755 browser_ctl "$BINDIR/"
    echo "Installed client binary: $CLIENT_BINARY"
fi

MANIFEST="com.klawed.browser_controller.json"

sed -e "s|HOST_PATH|$HOST_BINARY|g" \
    -e "s|EXTENSION_ID_PLACEHOLDER|$EXTENSION_ID|g" \
    "$MANIFEST" > "/tmp/$MANIFEST"

install_manifest() {
    local dir="$1"
    install -d "$dir"
    cp "/tmp/$MANIFEST" "$dir/$MANIFEST"
    echo "  → $dir/$MANIFEST"
}

echo ""
echo "Installing manifest for Extension ID: $EXTENSION_ID"
install_manifest "$CHROME_DIR"
install_manifest "$CHROME_CANARY_DIR"
install_manifest "$CHROMIUM_DIR"
install_manifest "$BRAVE_DIR"

echo ""
echo "Done! Now reload Chrome/Canary and click the extension icon to connect."
