#!/bin/bash
#
# filesurf-chat-test.sh - Persistent WebSocket testing via Unix socket relay
# 
# This script creates a persistent WebSocket connection for AI agents to interact
# with the FileSurf chat system. It uses websocat to bridge WebSocket connections
# through a Unix socket, allowing for easy testing and automation.
#
# DEPENDENCIES:
#   - websocat: WebSocket client (apt-get install websocat)
#   - socat: Socket relay tool (apt-get install socat)
#
# USAGE:
#   1. First, authenticate and get a session:
#      curl -s -c cookies.txt -X POST -d "email=your-email@example.com" \
#        "http://localhost:9090/auth/login"
#      SESSION_RESPONSE=$(curl -s -b cookies.txt "http://localhost:9090/session/generate")
#      SESSION_ID=$(echo "$SESSION_RESPONSE" | grep -o '"sessionId":"[^"]*"' | cut -d'"' -f4)
#
#   2. Run the script:
#      export WS_HOST=localhost:9090
#      export WEBSOCKET_COOKIES="filesurf_userId=user-test-123"
#      ./filesurf-chat-test.sh $SESSION_ID
#
#   Or use batch mode with a command file:
#      ./filesurf-chat-test.sh $SESSION_ID /tmp/commands.txt
#
# ARGUMENTS:
#   session_id     WebSocket session ID (required)
#   command_file   Optional file containing commands (batch mode)
#
# ENVIRONMENT VARIABLES:
#   WS_HOST              WebSocket host (default: localhost:8080)
#   WEBSOCKET_COOKIES    Cookie string for authentication (e.g., "filesurf_userId=xxx")
#
# COMMANDS (interactive or batch mode):
#   send <json_message>     Send a JSON message to the chat
#   poll [timeout]          Poll for responses (default: 2 seconds)
#   sleep <seconds>         Wait between commands
#   ping                    Send ping message
#   close                   Close connection and exit
#   quit                    Same as close
#
# EXAMPLES:
#   # Interactive session
#   export WS_HOST=localhost:9090
#   export WEBSOCKET_COOKIES="filesurf_userId=user-test-123"
#   ./filesurf-chat-test.sh abc123
#   > send {"type":"message","content":"list files in current directory"}
#   > poll 5
#   > send {"type":"message","content":"what time is it"}
#   > poll 5
#   > close
#
#   # Batch mode with command file
#   cat > /tmp/commands.txt << 'EOF'
#   sleep 2
#   send {"type":"message","content":"list files"}
#   poll 5
#   sleep 1
#   send {"type":"message","content":"create a test file"}
#   poll 5
#   close
#   EOF
#   ./filesurf-chat-test.sh abc123 /tmp/commands.txt
#
# NOTES:
#   - Messages must be valid JSON
#   - Responses are delivered asynchronously via WebSocket
#   - The script automatically handles cleanup on exit
#   - Use batch mode for automation and testing

set -e

# Configuration
HOST="${WS_HOST:-localhost:8080}"
SOCKET_PATH="/tmp/ws_test_$$.sock"
PID_FILE="/tmp/ws_test_$$.pid"
WEBSOCKET_COOKIES="${WEBSOCKET_COOKIES:-}"

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
    local ws_url="ws://$HOST/app/ws/$session_id"

    log_info "Starting WebSocket bridge to: $ws_url"
    log_info "Unix socket: $SOCKET_PATH"

    # Remove stale socket file
    rm -f "$SOCKET_PATH"

    # Start websocat in background
    # -t: Line-buffered text mode
    # unix-l: Listen on Unix socket (listener on left, client on right)
    # Pass cookies for authentication if WEBSOCKET_COOKIES is set
    if [ -n "$WEBSOCKET_COOKIES" ]; then
        websocat -t "unix-l:$SOCKET_PATH" -H="Cookie: $WEBSOCKET_COOKIES" "ws://$ws_url" &
    else
        websocat -t "unix-l:$SOCKET_PATH" "ws://$ws_url" &
    fi
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
    # Send message with newline for proper WebSocket text framing
    printf '%s\n' "$message" | timeout 2 socat - "unix:$SOCKET_PATH" 2>/dev/null
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
        echo "  WS_HOST              WebSocket host (default: localhost:8080)"
        echo "  WEBSOCKET_COOKIES    Cookie string for authentication (e.g., 'filesurf_userId=xxx')"
        echo ""
        echo "EXAMPLES:"
        echo "  # 1. First authenticate and get session:"
        echo "  curl -s -c cookies.txt -X POST -d 'email=test@example.com' \\"
        echo "    'http://localhost:9090/auth/login'"
        echo "  SESSION=\$(curl -s -b cookies.txt 'http://localhost:9090/session/generate')"
        echo "  SESSION_ID=\$(echo \$SESSION | grep -o '\"sessionId\":\"[^\"]*\"' | cut -d'\"' -f4)"
        echo ""
        echo "  # 2. Run the script:"
        echo "  export WS_HOST=localhost:9090"
        echo "  export WEBSOCKET_COOKIES=\"filesurf_userId=user-test-123\""
        echo "  ./filesurf-chat-test.sh \$SESSION_ID"
        echo ""
        echo "  # Or use batch mode:"
        echo "  ./filesurf-chat-test.sh \$SESSION_ID /path/to/commands.txt"
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
