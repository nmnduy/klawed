#!/bin/bash
# Manual test for SQLite queue mode

DB_PATH="/tmp/test_klawed_$$.db"
KLAWED="./build/klawed"

echo "Test database: $DB_PATH"

# Create database schema
sqlite3 "$DB_PATH" <<EOF
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

# Function to receive and acknowledge all pending messages
receive_and_ack_all() {
    sqlite3 "$DB_PATH" <<EOF
.headers off
.mode line
SELECT id, message FROM messages WHERE sender = 'klawed' AND sent = 0 ORDER BY created_at ASC;
EOF
}

# Function to acknowledge message
ack_message() {
    local id="$1"
    sqlite3 "$DB_PATH" "UPDATE messages SET sent = 1 WHERE id = $id;"
    echo "Acknowledged message $id"
}

# Start klawed in daemon mode (in background)
echo "Starting klawed daemon..."
$KLAWED --sqlite-queue "$DB_PATH" &
KLAWED_PID=$!
echo "Klawed PID: $KLAWED_PID"

# Wait for daemon to start
sleep 2

# Check if process is running
if ! kill -0 $KLAWED_PID 2>/dev/null; then
    echo "ERROR: Klawed daemon failed to start"
    rm -f "$DB_PATH"
    exit 1
fi

echo "Daemon started successfully"

# Send a test message
send_message "Hello, this is a test message"

# Wait and poll for response
echo "Waiting for response..."
for i in {1..30}; do
    # Get message IDs to acknowledge
    msg_ids=$(sqlite3 "$DB_PATH" "SELECT id FROM messages WHERE sender = 'klawed' AND sent = 0 ORDER BY created_at ASC;")
    if [ -n "$msg_ids" ]; then
        echo "=== Received messages ==="
        # Get full messages
        sqlite3 "$DB_PATH" "SELECT id, message FROM messages WHERE sender = 'klawed' AND sent = 0 ORDER BY created_at ASC;" | while IFS='|' read -r id msg; do
            echo "ID $id: $msg"
        done
        
        # Acknowledge all
        echo "$msg_ids" | while read -r id; do
            ack_message "$id"
        done
        break
    fi
    sleep 0.5
done

# Let it run a bit more to see if any duplicate processing occurs
echo "Waiting to check for duplicate processing..."
sleep 3

# Check for any additional messages
echo "=== Checking for additional messages ==="
sqlite3 "$DB_PATH" "SELECT id, sender, receiver, sent, message FROM messages WHERE sender = 'klawed' ORDER BY created_at ASC;"

# Cleanup
echo "Stopping daemon..."
kill $KLAWED_PID 2>/dev/null
wait $KLAWED_PID 2>/dev/null

# Clean up
rm -f "$DB_PATH"

echo "Test completed"
