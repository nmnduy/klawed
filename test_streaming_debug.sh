#!/bin/bash
# Debug test for SQLite queue streaming support

set -e

DB_PATH="/tmp/test_klawed_streaming_$$.db"
KLAWED="./build/klawed"

echo "========================================="
echo "Debug Test: SQLite Queue Streaming"
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
    
    # Show all messages in database before cleanup
    echo ""
    echo "=== All messages in database ==="
    sqlite3 "$DB_PATH" "SELECT id, sender, messageType FROM (SELECT id, sender, json_extract(message, '$.messageType') as messageType, created_at FROM messages ORDER BY created_at ASC)" 2>/dev/null || echo "(Could not query database)"
    
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

# Start klawed in daemon mode with streaming enabled
echo ""
echo "Starting klawed daemon with streaming enabled..."
export KLAWED_ENABLE_STREAMING=1
export KLAWED_LOG_LEVEL=DEBUG
export KLAWED_LOG_PATH=/tmp/klawed_streaming_test_$$.log

$KLAWED --sqlite-queue "$DB_PATH" &
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

# Send a test message
sqlite3 "$DB_PATH" "INSERT INTO messages (sender, receiver, message, sent) VALUES ('client', 'klawed', '{\"messageType\":\"TEXT\",\"content\":\"Say hi\"}', 0);"
echo "Sent message"

# Poll and show all message types
echo ""
echo "Polling for responses..."
for i in {1..30}; do
    messages=$(sqlite3 "$DB_PATH" "SELECT message FROM messages WHERE sender = 'klawed' AND sent = 0 ORDER BY created_at ASC;")
    if [ -n "$messages" ]; then
        echo "=== Received messages ==="
        echo "$messages" | while IFS= read -r msg; do
            msg_type=$(echo "$msg" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('messageType','UNKNOWN'))" 2>/dev/null || echo "UNKNOWN")
            content=$(echo "$msg" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('content','')[:50])" 2>/dev/null || echo "")
            echo "Type: $msg_type | Content: $content"
        done
        
        # Check if END_AI_TURN received
        if echo "$messages" | grep -q "END_AI_TURN"; then
            break
        fi
        
        # Acknowledge messages
        sqlite3 "$DB_PATH" "UPDATE messages SET sent = 1 WHERE sender = 'klawed' AND sent = 0;"
    fi
    sleep 0.3
done

echo ""
echo "=== Log file excerpt (grep for stream) ==="
grep -i "stream" /tmp/klawed_streaming_test_$$.log 2>/dev/null | tail -20 || echo "(No streaming-related log entries)"

echo ""
echo "Done"
