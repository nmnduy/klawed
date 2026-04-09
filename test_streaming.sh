#!/bin/bash
# Test script for SQLite queue streaming support

set -e

DB_PATH="/tmp/test_klawed_streaming_$$.db"
KLAWED="./build/klawed"

echo "========================================="
echo "Testing SQLite Queue Streaming Support"
echo "========================================="
echo "Database: $DB_PATH"

# Clean up on exit
cleanup() {
    echo ""
    echo "Cleaning up..."
    if [ -n "$KLAWED_PID" ]; then
        kill $KLAWED_PID 2>/dev/null || true
        wait $KLAWED_PID 2>/dev/null || true
    fi
    rm -f "$DB_PATH"
}
trap cleanup EXIT

# Create database schema
sqlite3 "$DB_PATH" <<'EOF'
CREATE TABLE IF NOT EXISTS messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    sender TEXT NOT NULL,
    receiver TEXT NOT NULL,
    message TEXT NOT NULL,
    sent INTEGER DEFAULT 0,
    created_at INTEGER DEFAULT (strftime('%s', 'now')),
    updated_at INTEGER DEFAULT (strftime('%s', 'now'))
);
CREATE INDEX IF NOT EXISTS idx_messages_sender ON messages(sender, sent);
CREATE INDEX IF NOT EXISTS idx_messages_receiver ON messages(receiver, sent);
EOF

echo "Database initialized"

# Function to send a message
send_message() {
    local content="$1"
    sqlite3 "$DB_PATH" "INSERT INTO messages (sender, receiver, message, sent) VALUES ('client', 'klawed', '{\"messageType\":\"TEXT\",\"content\":\"$content\"}', 0);"
    echo "Sent: $content"
}

# Function to poll for messages
poll_messages() {
    sqlite3 "$DB_PATH" "SELECT message FROM messages WHERE sender = 'klawed' AND sent = 0 ORDER BY created_at ASC;"
}

# Function to acknowledge all messages
ack_all() {
    sqlite3 "$DB_PATH" "UPDATE messages SET sent = 1 WHERE sender = 'klawed' AND sent = 0;"
}

# Build klawed if needed
if [ ! -f "$KLAWED" ]; then
    echo "Building klawed..."
    make
fi

# Start klawed in daemon mode with streaming enabled
echo ""
echo "Starting klawed daemon with streaming enabled..."
KLAWED_ENABLE_STREAMING=1 "$KLAWED" --sqlite-queue "$DB_PATH" &
KLAWED_PID=$!
echo "Klawed PID: $KLAWED_PID"

# Wait for daemon to start
sleep 2

# Check if process is running
if ! kill -0 $KLAWED_PID 2>/dev/null; then
    echo "ERROR: Klawed daemon failed to start"
    exit 1
fi

echo "Daemon started successfully"

# Send a test message that should generate a streaming response
send_message "Say hello and list 3 numbers"

# Poll for responses
echo ""
echo "Polling for responses (streaming chunks should appear)..."
chunk_count=0
text_received=0
end_turn_received=0

for i in {1..60}; do
    messages=$(poll_messages)
    if [ -n "$messages" ]; then
        # Process each message
        while IFS= read -r msg; do
            msg_type=$(echo "$msg" | python3 -c "import sys,json; print(json.load(sys.stdin).get('messageType','UNKNOWN'))" 2>/dev/null || echo "UNKNOWN")
            
            case "$msg_type" in
                "TEXT_STREAM_CHUNK")
                    chunk_count=$((chunk_count + 1))
                    content=$(echo "$msg" | python3 -c "import sys,json; print(json.load(sys.stdin).get('content',''))" 2>/dev/null || echo "")
                    echo -n "$content"
                    ;;
                "TEXT")
                    text_received=1
                    content=$(echo "$msg" | python3 -c "import sys,json; print(json.load(sys.stdin).get('content',''))" 2>/dev/null || echo "")
                    echo ""
                    echo "[FINAL TEXT] $content"
                    ;;
                "END_AI_TURN")
                    end_turn_received=1
                    echo ""
                    echo "[END_AI_TURN received]"
                    break 2
                    ;;
                "TOOL")
                    tool_name=$(echo "$msg" | python3 -c "import sys,json; print(json.load(sys.stdin).get('toolName',''))" 2>/dev/null || echo "")
                    echo "[TOOL: $tool_name]"
                    ;;
                "TOOL_RESULT")
                    echo "[TOOL_RESULT]"
                    ;;
                *)
                    echo "[$msg_type]"
                    ;;
            esac
        done <<< "$messages"
        
        # Acknowledge messages
        ack_all
    fi
    
    sleep 0.3
done

echo ""
echo "========================================="
echo "Results:"
echo "  Streaming chunks received: $chunk_count"
echo "  Final TEXT received: $text_received"
echo "  END_AI_TURN received: $end_turn_received"
echo "========================================="

if [ "$end_turn_received" -eq 1 ]; then
    echo "SUCCESS: Conversation completed"
    exit 0
else
    echo "FAILURE: Conversation did not complete"
    exit 1
fi
