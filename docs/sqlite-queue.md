# SQLite Message Queue Mode

This document describes the SQLite message queue communication mode for Klawed, which enables IPC (inter-process communication) between Klawed and external clients via a shared SQLite database.

## Overview

The SQLite queue mode allows Klawed to communicate with clients through a SQLite database. Klawed polls the database for messages addressed to it, processes them, and writes responses back to the same database. Clients send requests by inserting messages into the database and poll for responses.

This mode is useful for:
- Processes that cannot use network sockets
- Systems requiring durable message storage
- Asynchronous task processing with queue persistence
- Environments where SQLite is the preferred IPC mechanism

## Database Schema

### Messages Table

```sql
CREATE TABLE IF NOT EXISTS messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    sender TEXT NOT NULL,
    receiver TEXT NOT NULL,
    message TEXT NOT NULL,
    sent INTEGER DEFAULT 0,
    created_at INTEGER DEFAULT (strftime('%s', 'now')),
    updated_at INTEGER DEFAULT (strftime('%s', 'now'))
);
```

**Indexes:**
- `idx_messages_sender ON messages(sender, sent)`
- `idx_messages_receiver ON messages(receiver, sent)`

**Field Descriptions:**
- `id`: Auto-incrementing unique message identifier
- `sender`: Name of the message sender (e.g., "client", "klawed")
- `receiver`: Name of the intended recipient (e.g., "klawed", "client")
- `message`: JSON message payload (see [Message Format](#message-format))
- `sent`: Boolean flag (0 = unsent/unread, 1 = sent/read) - used for acknowledgments
- `created_at`: Unix timestamp when message was created
- `updated_at`: Unix timestamp when message was last modified

## Message Format

All messages in the `message` field are JSON objects with a `messageType` field.

### Message Types

| Type | Description | Direction |
|------|-------------|-----------|
| `TEXT` | Text prompt or response | Client → Klawed, Klawed → Client |
| `TOOL` | Tool execution request | Klawed → Client |
| `TOOL_RESULT` | Tool execution result | Klawed → Client |
| `ERROR` | Error message | Klawed → Client |

### Completion Detection

With the removal of `COMPLETED` messages, clients must detect when processing is complete by tracking pending `TOOL` messages. A turn is considered complete when:

1. All `TOOL` messages have corresponding `TOOL_RESULT` messages
2. No new `TOOL` messages are being sent
3. The final `TEXT` response has been received

Clients should maintain a map of `toolId` values from `TOOL` messages and mark them as completed when the corresponding `TOOL_RESULT` message is received.

## Communication Flow

### 1. Client Sends Request

The client inserts a message into the database:

```sql
INSERT INTO messages (sender, receiver, message, sent)
VALUES ('client', 'klawed', '{"messageType":"TEXT","content":"Your prompt here"}', 0);
```

### 2. Klawed Receives and Processes

Klawed polls for messages where:
- `receiver = 'klawed'` (or the configured sender name)
- `sent = 0`

Once received, klawed acknowledges the message:
```sql
UPDATE messages SET sent = 1, updated_at = strftime('%s', 'now') WHERE id = ?;
```

### 3. Klawed Sends Response

Klawed inserts responses into the database:
```sql
INSERT INTO messages (sender, receiver, message, sent)
VALUES ('klawed', 'client', '{"messageType":"TEXT","content":"AI response"}', 0);
```

### 4. Client Receives Response

The client polls for messages where:
- `sender = 'klawed'`
- `sent = 0`

After reading, the client acknowledges:
```sql
UPDATE messages SET sent = 1 WHERE id = ?;
```

## Configuration

### Command Line Usage

```bash
./build/klawed --sqlite-queue /path/to/messages.db
```

### Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `KLAWED_SQLITE_DB_PATH` | Path to SQLite database file (enables daemon mode when set) | Required |
| `KLAWED_SQLITE_SENDER` | Sender name for klawed responses | "klawed" |
| `KLAWED_SQLITE_POLL_INTERVAL` | Polling interval in milliseconds | 100 |
| `KLAWED_SQLITE_POLL_TIMEOUT` | Timeout for waiting for messages (ms) | 30000 |
| `KLAWED_SQLITE_MAX_RETRIES` | Maximum retry attempts | 3 |
| `KLAWED_SQLITE_MAX_MESSAGE_SIZE` | Maximum message size in bytes | 1048576 (1MB) |
| `KLAWED_SQLITE_MAX_QUEUE_SIZE` | Maximum pending messages | 1000 |

### Starting Klawed in Daemon Mode

**Option 1: Command line**
```bash
./build/klawed --sqlite-queue /path/to/messages.db
```

**Option 2: Environment variables**
```bash
export KLAWED_SQLITE_DB_PATH=/path/to/messages.db
export KLAWED_SQLITE_SENDER="klawed"
./build/klawed
```

**With custom sender name:**
```bash
./build/klawed --sqlite-queue /path/to/messages.db
export KLAWED_SQLITE_SENDER="my_agent"
```

## Message Format Details

### Input Messages (Client → Klawed)

#### TEXT Message

Send a text prompt to klawed:

```json
{
  "messageType": "TEXT",
  "content": "Your prompt here"
}
```

**Example:**
```json
{
  "messageType": "TEXT",
  "content": "Write a hello world program in C"
}
```

### Output Messages (Klawed → Client)

#### TEXT Response

AI-generated text response:

```json
{
  "messageType": "TEXT",
  "content": "AI-generated response text"
}
```

**Example:**
```json
{
  "messageType": "TEXT",
  "content": "Here's a hello world program in C:\n\n```c\n#include <stdio.h>\n\nint main() {\n    printf(\"Hello, world!\\n\");\n    return 0;\n}\n```"
}
```

#### TOOL_RESULT Response

Tool execution result:

```json
{
  "messageType": "TOOL_RESULT",
  "toolName": "Read",
  "toolId": "tool_call_abc123",
  "toolOutput": {
    "content": "File contents...",
    "file_path": "/path/to/file.txt"
  },
  "isError": false
}
```

#### TOOL Request

Tool execution request (sent before executing a tool):

```json
{
  "messageType": "TOOL",
  "toolName": "Read",
  "toolId": "tool_call_abc123",
  "toolParameters": {
    "file_path": "/path/to/file.txt"
  }
}
```

#### ERROR Response

Error occurred:

```json
{
  "messageType": "ERROR",
  "content": "Error description"
}
```

## Client Code Examples

### C Client (Basic)

```c
#include <sqlite3.h>
#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SENDER_NAME "client"
#define RECEIVER_NAME "klawed"
#define POLL_INTERVAL_MS 100

// Send message to klawed
int send_message(sqlite3 *db, const char *receiver, 
                 const char *message_type, const char *content) {
    cJSON *json = cJSON_CreateObject();
    if (!json) return -1;
    
    cJSON_AddStringToObject(json, "messageType", message_type);
    if (content) {
        cJSON_AddStringToObject(json, "content", content);
    }
    
    char *json_str = cJSON_PrintUnformatted(json);
    char sql[4096];
    snprintf(sql, sizeof(sql),
             "INSERT INTO messages (sender, receiver, message, sent) "
             "VALUES ('%s', '%s', '%s', 0);",
             SENDER_NAME, receiver, json_str);
    
    char *errmsg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
    
    free(json_str);
    cJSON_Delete(json);
    if (errmsg) sqlite3_free(errmsg);
    
    return rc == SQLITE_OK ? 0 : -1;
}

// Receive messages from klawed
int receive_messages(sqlite3 *db, char ***messages, 
                     long long **ids, int *count) {
    const char *sql = "SELECT id, message FROM messages "
                      "WHERE sender = 'klawed' AND sent = 0 "
                      "ORDER BY created_at ASC LIMIT 10;";
    
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    char **msg_array = calloc(100, sizeof(char *));
    long long *id_array = calloc(100, sizeof(long long));
    int msg_count = 0;
    
    while (sqlite3_step(stmt) == SQLITE_ROW && msg_count < 100) {
        id_array[msg_count] = sqlite3_column_int64(stmt, 0);
        const char *msg_text = (const char *)sqlite3_column_text(stmt, 1);
        if (msg_text) {
            msg_array[msg_count] = strdup(msg_text);
            msg_count++;
        }
    }
    
    sqlite3_finalize(stmt);
    
    *messages = msg_array;
    *ids = id_array;
    *count = msg_count;
    return 0;
}

// Acknowledge message as read
int acknowledge_message(sqlite3 *db, long long msg_id) {
    char sql[256];
    snprintf(sql, sizeof(sql), 
             "UPDATE messages SET sent = 1 WHERE id = %lld;", msg_id);
    
    char *errmsg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
    if (errmsg) sqlite3_free(errmsg);
    return rc == SQLITE_OK ? 0 : -1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <database_path> [prompt]\n", argv[0]);
        return 1;
    }
    
    const char *db_path = argv[1];
    const char *prompt = argc > 2 ? argv[2] : "Hello, klawed!";
    
    // Open database
    sqlite3 *db;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    
    // Send prompt
    printf("Sending: %s\n", prompt);
    send_message(db, RECEIVER_NAME, "TEXT", prompt);
    
    // Poll for responses
    printf("Waiting for responses...\n");
    int poll_count = 0;
    while (poll_count < 100) {  // 10 second timeout
        char **messages = NULL;
        long long *ids = NULL;
        int count = 0;
        
        receive_messages(db, &messages, &ids, &count);
        if (count > 0) {
            for (int i = 0; i < count; i++) {
                cJSON *json = cJSON_Parse(messages[i]);
                if (json) {
                    cJSON *type = cJSON_GetObjectItem(json, "messageType");
                    cJSON *content = cJSON_GetObjectItem(json, "content");
                    
                    printf("[%s] ", type->valuestring);
                    if (content) {
                        printf("%s\n", content->valuestring);
                    }
                    
                    // Note: Client should track pending TOOL messages and exit when
                    // all TOOL messages have corresponding TOOL_RESULT messages
                    // For this simple example, we just process all messages
                    
                    cJSON_Delete(json);
                }
                acknowledge_message(db, ids[i]);
                free(messages[i]);
            }
            free(messages);
            free(ids);
        } else {
            usleep(POLL_INTERVAL_MS * 1000);
        }
        poll_count++;
    }
    
    sqlite3_close(db);
    return 0;
}
```

### Python Client (Simple)

```python
import sqlite3
import json
import time
import sys

class KlawedSQLiteClient:
    def __init__(self, db_path, sender="client", receiver="klawed"):
        self.db_path = db_path
        self.sender = sender
        self.receiver = receiver
        self.conn = sqlite3.connect(db_path)
    
    def send_message(self, message_type, content):
        """Send a message to klawed."""
        message = {"messageType": message_type, "content": content}
        message_str = json.dumps(message)
        
        cursor = self.conn.cursor()
        cursor.execute(
            "INSERT INTO messages (sender, receiver, message, sent) VALUES (?, ?, ?, 0)",
            (self.sender, self.receiver, message_str)
        )
        self.conn.commit()
        print(f"Sent: {content[:100]}...")
    
    def receive_messages(self, max_messages=10):
        """Poll for messages from klawed."""
        cursor = self.conn.cursor()
        cursor.execute(
            "SELECT id, message FROM messages "
            "WHERE sender = ? AND sent = 0 "
            "ORDER BY created_at ASC LIMIT ?",
            (self.receiver, max_messages)
        )
        
        messages = cursor.fetchall()
        results = []
        for msg_id, message_json in messages:
            message = json.loads(message_json)
            results.append((msg_id, message))
        
        return results
    
    def acknowledge(self, msg_id):
        """Mark a message as read."""
        cursor = self.conn.cursor()
        cursor.execute("UPDATE messages SET sent = 1 WHERE id = ?", (msg_id,))
        self.conn.commit()
    
    def send_and_wait(self, prompt, timeout=30):
        """Send a prompt and wait for completion."""
        self.send_message("TEXT", prompt)
        
        start_time = time.time()
        while time.time() - start_time < timeout:
            messages = self.receive_messages()
            for msg_id, message in messages:
                msg_type = message.get("messageType")
                content = message.get("content", "")
                
                if msg_type == "TEXT":
                    print(f"AI: {content}")
                elif msg_type == "TOOL_RESULT":
                    tool_name = message.get("toolName")
                    is_error = message.get("isError", False)
                    if is_error:
                        print(f"Tool {tool_name} error: {content}")
                    else:
                        print(f"Tool {tool_name} executed")
                elif msg_type == "TOOL":
                    tool_name = message.get("toolName")
                    tool_id = message.get("toolId")
                    print(f"Tool requested: {tool_name} (id: {tool_id})")
                elif msg_type == "ERROR":
                    print(f"Error: {content}")
                    self.acknowledge(msg_id)
                    return False
                
                self.acknowledge(msg_id)
            
            time.sleep(0.1)
        
        print("Timeout waiting for response")
        return False
    
    def close(self):
        self.conn.close()

# Usage
if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <database_path> [prompt]")
        sys.exit(1)
    
    db_path = sys.argv[1]
    prompt = sys.argv[2] if len(sys.argv) > 2 else "Hello, klawed!"
    
    client = KlawedSQLiteClient(db_path)
    client.send_and_wait(prompt, timeout=60)
    client.close()
```

### Python Client (Advanced with Tool Call Support)

```python
import sqlite3
import json
import time
import sys

class KlawedSQLiteClient:
    def __init__(self, db_path, sender="client", receiver="klawed"):
        self.db_path = db_path
        self.sender = sender
        self.receiver = receiver
        self.conn = sqlite3.connect(db_path)
    
    def send_message(self, message_type, content):
        message = {"messageType": message_type, "content": content}
        message_str = json.dumps(message)
        
        cursor = self.conn.cursor()
        cursor.execute(
            "INSERT INTO messages (sender, receiver, message, sent) VALUES (?, ?, ?, 0)",
            (self.sender, self.receiver, message_str)
        )
        self.conn.commit()
        print(f"[SEND {message_type}] {content[:100]}...")
    
    def receive_messages(self):
        cursor = self.conn.cursor()
        cursor.execute(
            "SELECT id, message FROM messages "
            "WHERE sender = ? AND sent = 0 "
            "ORDER BY created_at ASC LIMIT 10",
            (self.receiver,)
        )
        return cursor.fetchall()
    
    def acknowledge(self, msg_id):
        cursor = self.conn.cursor()
        cursor.execute("UPDATE messages SET sent = 1 WHERE id = ?", (msg_id,))
        self.conn.commit()
    
    def process_messages(self):
        """Process all pending messages from klawed."""
        messages = self.receive_messages()
        completed = False
        
        for msg_id, message_json in messages:
            message = json.loads(message_json)
            msg_type = message.get("messageType")
            
            if msg_type == "TEXT":
                content = message.get("content", "")
                print(f"[TEXT AI] {content}")
                
            elif msg_type == "TOOL_RESULT":
                tool_name = message.get("toolName")
                tool_id = message.get("toolId")
                tool_output = message.get("toolOutput", {})
                is_error = message.get("isError", False)
                
                if is_error:
                    print(f"[TOOL {tool_name}] ERROR: {tool_output.get('error', 'Unknown')}")
                else:
                    print(f"[TOOL {tool_name}] OK")
                    # Pretty print tool output if needed
                    if tool_output:
                        print(f"  Output keys: {list(tool_output.keys())}")
                
            elif msg_type == "TOOL":
                tool_name = message.get("toolName")
                tool_id = message.get("toolId")
                tool_params = message.get("toolParameters")
                print(f"[TOOL REQUEST] {tool_name} (id: {tool_id})")
                if tool_params:
                    print(f"  Parameters: {list(tool_params.keys())}")
                
            elif msg_type == "ERROR":
                print(f"[ERROR] {message.get('content', '')}")
                completed = True
            
            self.acknowledge(msg_id)
        
        return completed
    
    def send_and_wait(self, prompt, timeout=120, poll_interval=0.1):
        """Send a prompt and wait for completion with tool call support."""
        print(f"\n{'='*60}")
        print(f"Sending: {prompt}")
        print(f"{'='*60}\n")
        
        self.send_message("TEXT", prompt)
        
        start_time = time.time()
        while time.time() - start_time < timeout:
            completed = self.process_messages()
            if completed:
                print(f"\n{'='*60}")
                print("Processing completed")
                print(f"{'='*60}\n")
                return True
            
            time.sleep(poll_interval)
        
        print(f"\nTimeout after {timeout} seconds")
        return False
    
    def close(self):
        self.conn.close()

# Example usage
if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <database_path> [prompt]")
        sys.exit(1)
    
    db_path = sys.argv[1]
    prompt = sys.argv[2] if len(sys.argv) > 2 else "What files are in the current directory?"
    
    client = KlawedSQLiteClient(db_path)
    
    try:
        # Example: Ask klawed to list files
        client.send_and_wait("List all files in the current directory")
        
        # Example: Read a file (this will trigger tool calls)
        client.send_and_wait("Read the README.md file and summarize it")
        
        # Example: Multi-turn conversation
        client.send_and_wait("Now tell me about the src/ directory")
        
    finally:
        client.close()
```

## Message Flow Examples

### Simple Request-Response

1. **Client inserts message:**
   ```sql
   INSERT INTO messages (sender, receiver, message, sent)
   VALUES ('client', 'klawed', '{"messageType":"TEXT","content":"What is 2+2?"}', 0);
   ```

2. **Klawed processes and sends response:**
   ```sql
   INSERT INTO messages (sender, receiver, message, sent)
   VALUES ('klawed', 'client', '{"messageType":"TEXT","content":"2+2 equals 4."}', 0);
   ```

3. **Client polls and receives:**
   ```sql
   SELECT id, message FROM messages 
   WHERE sender = 'klawed' AND sent = 0;
   ```

4. **Client acknowledges:**
   ```sql
   UPDATE messages SET sent = 1 WHERE id = <message_id>;
   ```

### Interactive Processing with Tool Calls

**Client sends:**
```json
{"messageType":"TEXT","content":"Read the file README.md and tell me what it says"}
```

**Klawed responds with multiple messages:**

1. Tool execution result:
```json
{
  "messageType": "TOOL_RESULT",
  "toolName": "Read",
  "toolId": "tool_call_abc123",
  "toolOutput": {
    "content": "# My Project\n\nThis is a sample README file...",
    "file_path": "README.md"
  },
  "isError": false
}
```

2. AI response:
```json
{
  "messageType": "TEXT",
  "content": "The README.md file contains a project overview..."
}
```

3. Completion:
```json
{
  "messageType": "COMPLETED",
  "content": "Interactive processing completed successfully"
}
```

## Restrictions and Limitations

### Database Concurrency
- SQLite uses file-based locking - ensure the database file is accessible by both klawed and the client
- Multiple concurrent writers may cause `SQLITE_BUSY` errors
- The implementation includes retry logic but very high concurrency may cause issues

### Message Size
- Default maximum message size: 1MB (configurable via `KLAWED_SQLITE_MAX_MESSAGE_SIZE`)
- Messages larger than the limit will be rejected with error code `SQLITE_QUEUE_ERROR_MESSAGE_TOO_LONG`

### Queue Size
- Default maximum pending messages: 1000 (configurable via `KLAWED_SQLITE_MAX_QUEUE_SIZE`)
- New messages may be rejected when queue is full

### Polling Overhead
- Poll-based communication introduces latency (default: 100ms intervals)
- Not suitable for real-time or low-latency requirements
- Consider using ZMQ socket mode for real-time communication

### Data Retention
- Acknowledged messages (`sent=1`) remain in the database
- Clients should implement cleanup strategies for old messages
- Use `DELETE FROM messages WHERE sent = 1 AND created_at < <timestamp>` for cleanup

### Security Considerations
- Database file permissions determine access control
- Message content is stored as plain text in the database
- No built-in encryption - use encrypted filesystem if needed
- Validate all JSON before processing

### Error Handling
- Malformed JSON messages are acknowledged but not processed
- Invalid message formats result in ERROR responses
- Client should check `messageType` field before processing

## Troubleshooting

### Klawed not processing messages

**Check message format:**
```sql
SELECT * FROM messages WHERE receiver = 'klawed' AND sent = 0;
```

**Verify sender name matches:**
```bash
# Default sender name is 'klawed'
# If you set KLAWED_SQLITE_SENDER, use that value in receiver field
```

**Check klawed logs for errors:**
```bash
# Logs are typically in .klawed/logs/klawed.log
tail -f .klawed/logs/klawed.log
```

### Client not receiving responses

**Check klawed's sender name:**
```bash
# Make sure you're polling for the correct sender
# Default: sender = 'klawed'
```

**Verify polling query:**
```sql
SELECT * FROM messages WHERE sender = 'klawed' AND sent = 0;
```

### Database locked errors

- Reduce polling frequency on client side
- Check for other processes accessing the database
- Consider using SQLite WAL mode: `PRAGMA journal_mode=WAL;`

### Message timeout errors

- Increase `KLAWED_SQLITE_POLL_TIMEOUT` if messages take long to process
- Check if klawed is running in daemon mode
- Verify database file path is correct

## Testing

See the test scripts for examples:
- `test_sqlite_simple.sh` - Basic test
- `test_sqlite_integration.sh` - Integration test
- `test_sqlite_debug.sh` - Debug test
- `examples/sqlite_queue_client.c` - Full C client example

## See Also

- [SQLite Queue Implementation](../src/sqlite_queue.c)
- [SQLite Queue Header](../src/sqlite_queue.h)
- [ZMQ Socket Mode](zmq_input_output.md) - Alternative IPC mechanism
