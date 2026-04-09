#!/bin/bash
# Debug test with trace logging

set -e

DB_PATH="/tmp/test_klawed_streaming_$$.db"
KLAWED="./build/klawed"
LOG_FILE="/tmp/klawed_trace_$$.log"

echo "========================================="
echo "Trace Test: SQLite Queue Streaming"
echo "========================================="

# Clean up on exit
cleanup() {
    echo ""
    echo "Cleaning up..."
    if [ -n "$KLAWED_PID" ]; then
        kill $KLAWED_PID 2>/dev/null || true
        wait $KLAWED_PID 2>/dev/null || true
    fi
    rm -f "$DB_PATH"
    
    echo ""
    echo "=== Log excerpts ==="
    echo "--- Streaming callback logs ---"
    grep -i "streaming_callback\|streaming_text_callback\|TEXT_STREAM_CHUNK" "$LOG_FILE" 2>/dev/null | tail -30 || echo "(No streaming callback logs found)"
    
    echo ""
    echo "--- sqlite_queue_send_streaming_chunk ---"
    grep -i "sqlite_queue_send_streaming_chunk\|TEXT_STREAM_CHUNK" "$LOG_FILE" 2>/dev/null | tail -20 || echo "(No send_streaming_chunk logs found)"
    
    rm -f "$LOG_FILE"
}
trap cleanup EXIT

# Create database
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

# Start klawed with debug logging
export KLAWED_ENABLE_STREAMING=1
export KLAWED_LOG_LEVEL=DEBUG
export KLAWED_LOG_PATH="$LOG_FILE"

$KLAWED --sqlite-queue "$DB_PATH" &
KLAWED_PID=$!

sleep 2

# Send message
sqlite3 "$DB_PATH" "INSERT INTO messages (sender, receiver, message, sent) VALUES ('client', 'klawed', '{\"messageType\":\"TEXT\",\"content\":\"Hello\"}', 0);"

# Wait and poll
for i in {1..30}; do
    if sqlite3 "$DB_PATH" "SELECT message FROM messages WHERE sender = 'klawed' AND sent = 0;" | grep -q "END_AI_TURN"; then
        break
    fi
    sqlite3 "$DB_PATH" "UPDATE messages SET sent = 1 WHERE sender = 'klawed' AND sent = 0;"
    sleep 0.3
done

echo "Done polling"
