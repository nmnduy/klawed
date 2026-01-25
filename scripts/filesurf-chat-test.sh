#!/bin/bash
#
# ws-persistent-test.sh - Persistent WebSocket testing via Unix socket relay
#
# Usage:
#   ./ws-persistent-test.sh <session_id> [command_file]
#   ./ws-persistent-test.sh session123                    # Interactive mode
#   ./ws-persistent-test.sh session123 /tmp/cmds.txt      # Batch mode
#
# Commands (in command file or interactive):
#   send <json_message>     Send a JSON message
#   poll [timeout]          Poll for responses (default: 2 seconds)
#   sleep <seconds>         Wait between commands
#   ping                    Send ping message
#   close                   Close connection and exit
#   quit                    Same as close
#
# Examples:
#   # Interactive session
#   ./ws-persistent-test.sh abc123
#   > send {"type":"message","content":"hello"}
#   > poll
#   > send {"type":"message","content":"world"}
#   > close
#
#   # Batch mode with command file
#   ./ws-persistent-test.sh abc123 /tmp/my_commands.txt
#
#   # Command file example:
#   send {"type":"message","content":"hello"}
#   poll 3
#   sleep 1
#   send {"type":"message","content":"world"}
#   poll
#   close

set -e

# Configuration
HOST="${WS_HOST:-localhost:8080}"
SOCKET_PATH="/tmp/ws_test_$$.sock"
PID_FILE="/tmp/ws_test_$$.pid"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[OK]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Cleanup function
cleanup() {
    log_info "Cleaning up..."

    # Kill websocat if running
    if [ -f "$PID_FILE" ]; then
        PID=$(cat "$PID_FILE")
        if kill -0 "$PID" 2>/dev/null; then
            kill "$PID" 2>/dev/null || true
            sleep 0.5
            kill -9 "$PID" 2>/dev/null || true
        fi
        rm -f "$PID_FILE"
    fi

    # Kill any remaining socat processes for this socket
    pkill -f "socat.*$SOCKET_PATH" 2>/dev/null || true

    # Remove socket file
    rm -f "$SOCKET_PATH"

    log_success "Cleanup complete"
}

trap cleanup EXIT INT TERM

# Check dependencies
check_dependencies() {
    local missing=()

    for cmd in websocat socat; do
        if ! command -v "$cmd" &>/dev/null; then
            missing+=("$cmd")
        fi
    done

    if [ ${#missing[@]} -gt 0 ]; then
        log_error "Missing dependencies: ${missing[*]}"
        echo "Install with: apt-get install websocat socat"
        exit 1
    fi
}

# Start the WebSocket to Unix socket bridge
start_bridge() {
    local session_id="$1"
    local ws_url="ws://$HOST/file-chat/ws/$session_id"

    log_info "Starting WebSocket bridge to: $ws_url"
    log_info "Unix socket: $SOCKET_PATH"

    # Remove stale socket file
    rm -f "$SOCKET_PATH"

    # Start websocat in background
    # -t: Line-buffered text mode
    # unix-l: Listen on Unix socket
    websocat -t "ws://$ws_url" "unix-l:$SOCKET_PATH" &
    BRIDGE_PID=$!

    echo "$BRIDGE_PID" > "$PID_FILE"

    # Wait for socket to be ready
    local retries=10
    while [ $retries -gt 0 ]; do
        if [ -S "$SOCKET_PATH" ]; then
            # Verify the process is actually running
            if kill -0 "$BRIDGE_PID" 2>/dev/null; then
                sleep 0.5  # Give websocat time to establish WebSocket connection
                log_success "Bridge started (PID: $BRIDGE_PID)"
                return 0
            fi
        fi
        sleep 0.2
        retries=$((retries - 1))
    done

    log_error "Failed to start bridge"
    exit 1
}

# Send a message through the Unix socket
ws_send() {
    local message="$1"
    # Use timeout to prevent hanging if socket is broken
    echo "$message" | timeout 2 socat - "unix:$SOCKET_PATH" 2>/dev/null
}

# Poll for responses
ws_poll() {
    local timeout="${1:-2}"

    # Read with timeout - will block until data available or timeout
    timeout "$timeout" cat "$SOCKET_PATH" 2>/dev/null || echo ""
}

# Interactive mode
run_interactive() {
    echo ""
    echo "=========================================="
    echo "  WebSocket Persistent Test - Interactive"
    echo "=========================================="
    echo ""
    echo "Commands:"
    echo "  send <json>    Send a JSON message"
    echo "  poll [sec]     Poll for responses (default: 2s)"
    echo "  sleep <sec>    Wait between commands"
    echo "  ping           Send ping message"
    echo "  close/quit     Close and exit"
    echo ""
    echo -e "${GREEN}Connected. Type commands below:${NC}"
    echo ""

    while true; do
        read -r -p "> " line

        if [ -z "$line" ]; then
            continue
        fi

        cmd=$(echo "$line" | awk '{print $1}')

        case "$cmd" in
            send)
                # Extract message (everything after first space)
                message=$(echo "$line" | sed 's/^send //')
                if [ -z "$message" ]; then
                    log_error "Missing message. Usage: send {\"type\":\"message\"}"
                    continue
                fi
                log_info "Sending: $message"
                ws_send "$message"
                ;;
            poll)
                timeout_sec=$(echo "$line" | awk '{print $2}')
                timeout_sec=${timeout_sec:-2}
                log_info "Polling for $timeout_sec seconds..."
                response=$(ws_poll "$timeout_sec")
                if [ -n "$response" ]; then
                    echo -e "${YELLOW}Response:${NC} $response"
                else
                    echo "(no response within ${timeout_sec}s)"
                fi
                ;;
            sleep)
                sec=$(echo "$line" | awk '{print $2}')
                sec=${sec:-1}
                log_info "Sleeping ${sec}s..."
                sleep "$sec"
                ;;
            ping)
                log_info "Sending ping..."
                ws_send '{"type":"ping"}'
                response=$(ws_poll 2)
                if [ -n "$response" ]; then
                    echo -e "${YELLOW}Response:${NC} $response"
                fi
                ;;
            close|quit|exit)
                log_info "Closing connection..."
                break
                ;;
            *)
                log_error "Unknown command: $cmd"
                echo "Available: send, poll, sleep, ping, close"
                ;;
        esac
    done
}

# Batch mode (from file)
run_batch() {
    local cmd_file="$1"

    if [ ! -f "$cmd_file" ]; then
        log_error "Command file not found: $cmd_file"
        exit 1
    fi

    log_info "Running commands from: $cmd_file"
    echo ""

    while IFS= read -r line || [ -n "$line" ]; do
        # Skip empty lines and comments
        [[ -z "$line" || "$line" =~ ^[[:space:]]*# ]] && continue

        # Trim leading/trailing whitespace
        line=$(echo "$line" | xargs)

        cmd=$(echo "$line" | awk '{print $1}')

        case "$cmd" in
            send)
                message=$(echo "$line" | sed 's/^send //')
                if [ -z "$message" ]; then
                    log_warn "Skipping empty send command"
                    continue
                fi
                log_info "Sending: $message"
                ws_send "$message"
                ;;
            poll)
                timeout_sec=$(echo "$line" | awk '{print $2}')
                timeout_sec=${timeout_sec:-2}
                log_info "Polling for $timeout_sec seconds..."
                response=$(ws_poll "$timeout_sec")
                if [ -n "$response" ]; then
                    echo -e "${YELLOW}Response:${NC} $response"
                else
                    echo "(no response within ${timeout_sec}s)"
                fi
                ;;
            sleep)
                sec=$(echo "$line" | awk '{print $2}')
                sec=${sec:-1}
                log_info "Sleeping ${sec}s..."
                sleep "$sec"
                ;;
            ping)
                log_info "Sending ping..."
                ws_send '{"type":"ping"}'
                response=$(ws_poll 2)
                if [ -n "$response" ]; then
                    echo -e "${YELLOW}Response:${NC} $response"
                fi
                ;;
            close|quit|exit)
                log_info "Closing connection..."
                break
                ;;
            *)
                log_warn "Unknown command: $cmd (skipping)"
                ;;
        esac
    done < "$cmd_file"
}

# Main
main() {
    echo ""
    echo "=========================================="
    echo "  WebSocket Persistent Test Script"
    echo "=========================================="
    echo ""

    check_dependencies

    if [ -z "$1" ]; then
        echo "Usage: $0 <session_id> [command_file]"
        echo ""
        echo "Arguments:"
        echo "  session_id     WebSocket session ID"
        echo "  command_file   Optional file with commands (batch mode)"
        echo ""
        echo "Environment variables:"
        echo "  WS_HOST        WebSocket host (default: localhost:8080)"
        echo ""
        exit 1
    fi

    local session_id="$1"
    local cmd_file="$2"

    start_bridge "$session_id"

    if [ -n "$cmd_file" ]; then
        run_batch "$cmd_file"
    else
        run_interactive
    fi

    log_success "Test completed"
}

main "$@"
