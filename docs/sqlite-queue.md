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
| `TRIGGER_COMPACT` | Request manual context compaction | Client → Klawed |
| `TOOL` | Tool execution request | Klawed → Client |
| `TOOL_RESULT` | Tool execution result | Klawed → Client |
| `API_CALL` | API call in progress (waiting for AI response) | Klawed → Client |
| `END_AI_TURN` | AI turn completed, waiting for further instruction | Klawed → Client |
| `ERROR` | Error message | Klawed → Client |
| `AUTO_COMPACTION` | Context compaction event notification | Klawed → Client |

### Completion Detection

Clients can detect when klawed has completed processing a request by watching for the `END_AI_TURN` message. This message is sent after:

1. All `TOOL` messages have been executed and their corresponding `TOOL_RESULT` messages have been sent
2. The final `TEXT` response has been sent (if any)
3. Klawed is ready to receive the next user message

**Recommended approach:** Listen for the `END_AI_TURN` message to know when klawed is ready for the next instruction.

**Alternative approach (for advanced use cases):** Track pending `TOOL` messages by maintaining a map of `toolId` values from `TOOL` messages and mark them as completed when the corresponding `TOOL_RESULT` message is received. A turn is considered complete when all tool calls have results and no new tool messages are being sent.

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

## User Message Injection During Tool Execution

The SQLite queue mode supports **user message injection during tool execution**, similar to the interactive CLI mode. This means:

- **Clients can send messages while tools are running**: A client can insert a new TEXT message into the database while klawed is executing tools from a previous request.
- **Messages are queued for processing**: When a user message arrives during tool execution, it is added to an internal pending queue and processed after the current turn completes.
- **No blocking**: The main daemon loop continues polling for new messages while the worker thread processes the current message.

### How It Works

1. **Main Daemon Loop**: Continuously polls the SQLite database for new messages with a short timeout (100ms)
2. **Worker Thread**: Processes one message at a time, executing all tools and API calls
3. **Message Queue**: When a new message arrives while the worker is busy, it's added to a pending queue
4. **Sequential Processing**: Messages are processed in the order they are received (FIFO)
5. **Deferred Acknowledgment**: Messages are acknowledged (marked as `sent=1`) only when the worker thread starts processing them, not when they are received

### Implications for Clients

**Client-Side Queuing**: Clients can safely send messages at any time. If sent during tool execution:
- The message remains unacknowledged (`sent=0`) while queued internally
- The message is acknowledged when klawed's worker thread begins processing it
- The client can send multiple messages; they will be processed sequentially

**Example Flow with Interleaved Messages**:

```
T=0: Client sends "Read file A" → klawed acknowledges and starts processing
T=1: klawed sends TOOL request for Read
T=2: klawed sends TOOL_RESULT for Read
T=3: Client sends "Read file B" (while first request still processing)
T=4: klawed receives "Read file B", queues it internally (NOT acknowledged yet)
T=5: klawed sends API_CALL (for first request)
T=6: klawed sends TEXT response for first request
T=7: klawed sends END_AI_TURN for first request
T=8: klawed acknowledges "Read file B" and starts processing it
```

### Interruption Support

The implementation also supports interruption:

- **Interrupt Request**: Clients can request interruption by sending an appropriate signal or using the API
- **Tool Cancellation**: Long-running tools can be interrupted mid-execution
- **Graceful Shutdown**: The worker thread can be gracefully shut down with a timeout

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
| `KLAWED_SQLITE_POLL_INTERVAL` | Polling interval in milliseconds | 300 |
| `KLAWED_SQLITE_POLL_TIMEOUT` | Timeout for waiting for messages (ms) | 30000 |
| `KLAWED_SQLITE_MAX_RETRIES` | Maximum retry attempts | 3 |
| `KLAWED_SQLITE_MAX_MESSAGE_SIZE` | Maximum message size in bytes | 1048576 (1MB) |
| `KLAWED_SQLITE_MAX_QUEUE_SIZE` | Maximum pending messages | 1000 |
| `KLAWED_SQLITE_MAX_ITERATIONS` | Maximum iterations in interactive loop (0 = unlimited) | 1000 |

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

## Conversation Seeding (Currently Disabled)

> **Note**: Conversation seeding is currently disabled to prevent tool use/result mismatch errors that can occur when user messages are interleaved with tool execution in the message history. Klawed starts with a fresh conversation state in daemon mode.

Klawed previously supported seeding the conversation with pre-existing messages at boot time. This feature may be re-enabled in a future version with improved message ordering logic.

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

#### TRIGGER_COMPACT Message

Request manual context compaction to free up context window:

```json
{
  "messageType": "TRIGGER_COMPACT"
}
```

**Response:**

Klawed responds with either:
- An `AUTO_COMPACTION` message on success (containing statistics)
- A `TEXT` message if there was nothing to compact (not enough messages)
- An `ERROR` message if compaction failed

**Example flow:**

Client sends:
```json
{
  "messageType": "TRIGGER_COMPACT"
}
```

Klawed responds on success:
```json
{
  "messageType": "AUTO_COMPACTION",
  "messagesCompacted": 45,
  "tokensBefore": 92450,
  "tokensAfter": 28760,
  "tokensFreed": 63690,
  "usageBeforePct": 73.9,
  "usageAfterPct": 23.0,
  "summary": "The user asked about file organization...",
  "content": "Context compaction: 45 messages stored to memory..."
}
```

**Notes:**
- Manual compaction works independently of auto-compaction settings
- It requires at least `keep_recent + 1` messages (system message + recent messages) in the conversation
- The response format matches automatic compaction notifications

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

#### API_CALL Message

API call in progress (sent before making an API call to indicate waiting time):

```json
{
  "messageType": "API_CALL",
  "timestamp": 1735579200,
  "timestampMs": 1735579200123,
  "estimatedDurationMs": 5000,
  "model": "gpt-4",
  "provider": "openai"
}
```

**Fields:**
- `timestamp`: Unix timestamp (seconds)
- `timestampMs`: Unix timestamp with milliseconds (optional, more precise)
- `estimatedDurationMs`: Estimated duration of the API call in milliseconds (optional)
- `model`: AI model being used (optional)
- `provider`: API provider (e.g., "openai", "anthropic", "bedrock") (optional)

#### END_AI_TURN Message

Indicates that the AI has completed its turn and is now waiting for further instruction from the client:

```json
{
  "messageType": "END_AI_TURN"
}
```

This message is sent after:
- All tool calls have been executed and their results processed
- The final text response has been sent
- The AI is ready to receive the next user message

Clients can use this event to:
- Update UI state to show that klawed is ready for input
- Hide loading/processing indicators
- Enable input controls
- Trigger any post-processing logic

#### AUTO_COMPACTION Message

Indicates that automatic context compaction has occurred (when `--auto-compact` is enabled):

```json
{
  "messageType": "AUTO_COMPACTION",
  "messagesCompacted": 45,
  "tokensBefore": 92450,
  "tokensAfter": 28760,
  "tokensFreed": 63690,
  "usageBeforePct": 73.9,
  "usageAfterPct": 23.0,
  "summary": "The user asked about file organization...",
  "content": "Context compaction: 45 messages stored to memory. Tokens: 92450 → 28760 (freed ~63690 tokens). Usage: 73.9% → 23.0%."
}
```

**Fields:**
- `messagesCompacted`: Number of older messages moved to long-term memory
- `tokensBefore`: Token count before compaction
- `tokensAfter`: Token count after compaction
- `tokensFreed`: Number of tokens freed by compaction
- `usageBeforePct`: Context usage percentage before compaction
- `usageAfterPct`: Context usage percentage after compaction
- `summary`: AI-generated summary of the compacted context (what was stored to memory)
- `content`: Human-readable summary of the compaction event

This message is sent:
- When auto-compaction is enabled (`--auto-compact` flag or `KLAWED_AUTO_COMPACT=1`)
- Before an API call when the token threshold is reached
- After messages have been successfully stored in long-term memory (SQLite memory database)

Clients can use this event to:
- Display a notification that context has been compacted
- Update UI to show reduced token usage
- Log compaction events for debugging
- Track conversation memory management

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
#define POLL_INTERVAL_MS 300

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
    
    def send_message(self, message_type, content=None):
        """Send a message to klawed."""
        message = {"messageType": message_type}
        if content is not None:
            message["content"] = content
        message_str = json.dumps(message)
        
        cursor = self.conn.cursor()
        cursor.execute(
            "INSERT INTO messages (sender, receiver, message, sent) VALUES (?, ?, ?, 0)",
            (self.sender, self.receiver, message_str)
        )
        self.conn.commit()
        if content:
            print(f"Sent: {content[:100]}...")
        else:
            print(f"Sent: {message_type}")
    
    def trigger_compact(self, timeout=30):
        """Trigger manual context compaction."""
        print("Requesting context compaction...")
        self.send_message("TRIGGER_COMPACT")
        
        start_time = time.time()
        while time.time() - start_time < timeout:
            messages = self.receive_messages()
            for msg_id, message in messages:
                msg_type = message.get("messageType")
                
                if msg_type == "AUTO_COMPACTION":
                    messages_compact = message.get("messagesCompacted", 0)
                    tokens_freed = message.get("tokensFreed", 0)
                    print(f"Compaction complete: {messages_compact} messages, {tokens_freed} tokens freed")
                    self.acknowledge(msg_id)
                    return True
                elif msg_type == "ERROR":
                    print(f"Compaction error: {message.get('content', '')}")
                    self.acknowledge(msg_id)
                    return False
                else:
                    self.acknowledge(msg_id)
            
            time.sleep(0.3)
        
        print("Compaction timeout")
        return False
    
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
                elif msg_type == "API_CALL":
                    print(f"Waiting for AI response...")
                elif msg_type == "END_AI_TURN":
                    print(f"AI turn completed")
                    self.acknowledge(msg_id)
                    return True
                elif msg_type == "AUTO_COMPACTION":
                    messages_compacted = message.get("messagesCompacted", 0)
                    tokens_freed = message.get("tokensFreed", 0)
                    print(f"[COMPACTION] {messages_compacted} messages compacted, {tokens_freed} tokens freed")
                elif msg_type == "ERROR":
                    print(f"Error: {content}")
                    self.acknowledge(msg_id)
                    return False
                
                self.acknowledge(msg_id)
            
            time.sleep(0.3)
        
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
    
    def send_message(self, message_type, content=None):
        """Send a message to klawed."""
        message = {"messageType": message_type}
        if content is not None:
            message["content"] = content
        message_str = json.dumps(message)
        
        cursor = self.conn.cursor()
        cursor.execute(
            "INSERT INTO messages (sender, receiver, message, sent) VALUES (?, ?, ?, 0)",
            (self.sender, self.receiver, message_str)
        )
        self.conn.commit()
        if content:
            print(f"[SEND {message_type}] {content[:100]}...")
        else:
            print(f"[SEND {message_type}]")
    
    def trigger_compact(self, timeout=30):
        """Trigger manual context compaction and wait for result."""
        print("\n[TRIGGER_COMPACT] Requesting context compaction...")
        self.send_message("TRIGGER_COMPACT")
        
        start_time = time.time()
        while time.time() - start_time < timeout:
            messages = self.receive_messages()
            for msg_id, message in messages:
                msg_type = message.get("messageType")
                
                if msg_type == "AUTO_COMPACTION":
                    messages_compacted = message.get("messagesCompacted", 0)
                    tokens_before = message.get("tokensBefore", 0)
                    tokens_after = message.get("tokensAfter", 0)
                    usage_before = message.get("usageBeforePct", 0)
                    usage_after = message.get("usageAfterPct", 0)
                    
                    print(f"[COMPACTION SUCCESS] {messages_compacted} messages compacted")
                    print(f"  Tokens: {tokens_before:.0f} → {tokens_after:.0f} (freed ~{tokens_before - tokens_after:.0f})")
                    print(f"  Usage: {usage_before:.1f}% → {usage_after:.1f}%")
                    
                    self.acknowledge(msg_id)
                    return True
                    
                elif msg_type == "TEXT":
                    content = message.get("content", "")
                    if "compaction" in content.lower() or "compact" in content.lower():
                        print(f"[COMPACTION INFO] {content}")
                        self.acknowledge(msg_id)
                        return True
                        
                elif msg_type == "ERROR":
                    content = message.get("content", "")
                    print(f"[COMPACTION FAILED] {content}")
                    self.acknowledge(msg_id)
                    return False
                    
                else:
                    # Acknowledge other message types
                    self.acknowledge(msg_id)
            
            time.sleep(0.1)
        
        print("[COMPACTION TIMEOUT] No response received")
        return False
    
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
                
            elif msg_type == "API_CALL":
                model = message.get("model", "unknown")
                print(f"[API_CALL] Waiting for AI response (model: {model})")
                
            elif msg_type == "AUTO_COMPACTION":
                messages_compacted = message.get("messagesCompacted", 0)
                tokens_before = message.get("tokensBefore", 0)
                tokens_after = message.get("tokensAfter", 0)
                usage_before = message.get("usageBeforePct", 0)
                usage_after = message.get("usageAfterPct", 0)
                print(f"[AUTO_COMPACTION] {messages_compacted} messages stored to memory")
                print(f"  Tokens: {tokens_before:.0f} → {tokens_after:.0f} (freed ~{tokens_before - tokens_after:.0f})")
                print(f"  Usage: {usage_before:.1f}% → {usage_after:.1f}%")
                
            elif msg_type == "END_AI_TURN":
                print(f"[END_AI_TURN] AI turn completed, ready for next instruction")
                completed = True
                
            elif msg_type == "ERROR":
                print(f"[ERROR] {message.get('content', '')}")
                completed = True
            
            self.acknowledge(msg_id)
        
        return completed
    
    def send_and_wait(self, prompt, timeout=120, poll_interval=0.3):
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
        
        
        # Example: Trigger manual compaction after long conversation
        client.send_and_wait("Create a detailed summary of the entire project")
        client.trigger_compact()  # Free up context space
        client.send_and_wait("Now create a list of TODO items based on the summary")
        
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

2. **Klawed processes and sends responses:**
   ```sql
   -- Text response
   INSERT INTO messages (sender, receiver, message, sent)
   VALUES ('klawed', 'client', '{"messageType":"TEXT","content":"2+2 equals 4."}', 0);
   
   -- End of turn
   INSERT INTO messages (sender, receiver, message, sent)
   VALUES ('klawed', 'client', '{"messageType":"END_AI_TURN"}', 0);
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

### Manual Compaction Trigger

**Client sends compaction request:**
```sql
INSERT INTO messages (sender, receiver, message, sent)
VALUES ('client', 'klawed', '{"messageType":"TRIGGER_COMPACT"}', 0);
```

**Klawed responds with compaction result:**
```sql
-- Compaction statistics
INSERT INTO messages (sender, receiver, message, sent)
VALUES ('klawed', 'client', '{
  "messageType": "AUTO_COMPACTION",
  "messagesCompacted": 45,
  "tokensBefore": 92450,
  "tokensAfter": 28760,
  "tokensFreed": 63690,
  "usageBeforePct": 73.9,
  "usageAfterPct": 23.0,
  "summary": "The user asked about file organization...",
  "content": "Context compaction: 45 messages stored to memory..."
}', 0);

-- End of turn
INSERT INTO messages (sender, receiver, message, sent)
VALUES ('klawed', 'client', '{"messageType":"END_AI_TURN"}', 0);
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

3. End of turn:
```json
{
  "messageType": "END_AI_TURN"
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
- Poll-based communication introduces latency (default: 300ms intervals)
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
