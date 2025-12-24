# ZMQ Input/Output Message Format

This document describes the JSON message format for ZeroMQ communication in Klawed.

## Overview

Klawed uses a simple JSON message format for both input (requests) and output (responses) when communicating via ZeroMQ sockets using the PAIR socket pattern (exclusive peer-to-peer communication). The format is designed to be minimal and self-describing.

## Message Structure

All messages are JSON objects with at least a `messageType` field that indicates the type of message.

### Common Fields

- `messageType` (string, required): Type of message ("TEXT", "ERROR", "TOOL_RESULT", "HEARTBEAT_PING", or "HEARTBEAT_PONG")
- `content` (string, optional): Primary content/message text

## Input Messages (Client → Klawed)

### Text Processing Request

Send a text prompt to Klawed for AI processing.

```json
{
  "messageType": "TEXT",
  "content": "Your prompt here"
}
```

**Fields:**
- `messageType`: Must be "TEXT"
- `content`: The text prompt to process

**Example:**
```json
{
  "messageType": "TEXT",
  "content": "Write a hello world program in C"
}
```

## Output Messages (Klawed → Client)

### 1. Text Response

Response to a TEXT request with AI-generated content.

```json
{
  "messageType": "TEXT",
  "content": "AI-generated response text"
}
```

**Fields:**
- `messageType`: Always "TEXT" for text responses
- `content`: The AI-generated response text

**Example:**
```json
{
  "messageType": "TEXT",
  "content": "Here's a hello world program in C:\n\n```c\n#include <stdio.h>\n\nint main() {\n    printf(\"Hello, world!\\n\");\n    return 0;\n}\n```"
}
```

### 2. Tool Result Response

Sent when a tool execution completes during interactive processing.

```json
{
  "messageType": "TOOL_RESULT",
  "toolName": "ToolName",
  "toolId": "tool_call_abc123",
  "toolOutput": {
    "result": "tool output data"
  },
  "isError": false
}
```

**Fields:**
- `messageType`: Always "TOOL_RESULT"
- `toolName`: Name of the tool that was executed
- `toolId`: ID of the tool call (matches the tool call ID from the AI)
- `toolOutput`: JSON object containing the tool execution results
- `isError`: Boolean indicating if the tool execution resulted in an error

**Example (successful tool execution):**
```json
{
  "messageType": "TOOL_RESULT",
  "toolName": "Read",
  "toolId": "tool_call_abc123",
  "toolOutput": {
    "content": "File contents here...",
    "file_path": "/path/to/file.txt"
  },
  "isError": false
}
```

**Example (tool error):**
```json
{
  "messageType": "TOOL_RESULT",
  "toolName": "Read",
  "toolId": "tool_call_abc123",
  "toolOutput": {
    "error": "File not found: /path/to/nonexistent.txt"
  },
  "isError": true
}
```


### 4. Error Response

Error messages for various failure conditions.

```json
{
  "messageType": "ERROR",
  "content": "Error description"
}
```

**Common error types:**

**JSON parse error:**
```json
{
  "messageType": "ERROR",
  "content": "Invalid JSON"
}
```

**Invalid message format:**
```json
{
  "messageType": "ERROR",
  "content": "Invalid message format"
}
```

**AI inference error:**
```json
{
  "messageType": "ERROR",
  "content": "AI inference error"
}
```

## Message Flow Examples

### Simple Text Processing

**Client sends:**
```json
{
  "messageType": "TEXT",
  "content": "What is 2+2?"
}
```

**Klawed responds:**
```json
{
  "messageType": "TEXT",
  "content": "2+2 equals 4."
}
```

### Interactive Processing with Tool Calls

**Client sends:**
```json
{
  "messageType": "TEXT",
  "content": "Read the file README.md and tell me what it says"
}
```

**Klawed responds with multiple messages:**

1. First, the AI might decide to use a tool:
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

2. Then the AI processes the tool result and responds:
```json
{
  "messageType": "TEXT",
  "content": "The README.md file contains: '# My Project\\n\\nThis is a sample README file...'"
}
```



### Multi-turn Conversation

**First client message:**
```json
{
  "messageType": "TEXT",
  "content": "What's in the current directory?"
}
```

**Klawed responds (after possible tool calls):**
```json
{
  "messageType": "TEXT",
  "content": "The current directory contains: README.md, src/, tests/"
}
```

**Client sends follow-up (conversation context is maintained):**
```json
{
  "messageType": "TEXT",
  "content": "Now read the README.md file"
}
```

**Klawed responds with tool result and text response as above.**

### Error Case

**Client sends (malformed JSON):**
```json
{
  "messageType": "TEXT"
  // Missing content field
}
```

**Klawed responds:**
```json
{
  "messageType": "ERROR",
  "content": "Invalid message format"
}
```

## Connection Management and Robustness Features

Klawed's ZMQ implementation includes several robustness features to handle network failures and ensure reliable communication.

### Heartbeat Mechanism

Klawed implements a heartbeat system to monitor connection health:

**Heartbeat Ping (Client → Klawed or Klawed → Client):**
```json
{
  "messageType": "HEARTBEAT_PING",
  "timestamp": 1703456789
}
```

**Heartbeat Pong (Response to ping):**
```json
{
  "messageType": "HEARTBEAT_PONG",
  "timestamp": 1703456790,
  "pingTimestamp": 1703456789
}
```

**Features:**
- Automatic ping/pong exchange at configurable intervals
- Connection considered dead if no pong received within timeout
- Heartbeat messages don't interfere with normal message processing

### Automatic Reconnection

Klawed can automatically reconnect when connections fail:

**Exponential Backoff:**
- Base reconnect interval: 1 second (configurable)
- Maximum reconnect interval: 30 seconds (configurable)
- Formula: `interval = base * 2^(attempt-1)` capped at maximum
- Maximum attempts: 10 (configurable)

**Reconnection Process:**
1. Detect connection failure (send/receive error or heartbeat timeout)
2. Close existing socket
3. Wait using exponential backoff
4. Create new socket and reconnect
5. Retry queued messages if message queue is enabled

### Message Queue for Reliable Delivery

When enabled, Klawed can queue messages during disconnections:

**Features:**
- Configurable queue size (default: 100 messages)
- Messages automatically queued when connection is down
- Queued messages sent when connection is restored
- Prevents message loss during transient network issues

### Connection Testing

Clients can test connection health using the `zmq_socket_test_connection()` function:

```c
// C client example
int timeout_ms = 5000; // 5 second timeout
int result = zmq_socket_test_connection(ctx, timeout_ms);
if (result == 0) {
    printf("Connection test successful\n");
} else {
    printf("Connection test failed\n");
}
```

### Configuration Environment Variables

**Heartbeat Configuration:**
- `KLAWED_ZMQ_HEARTBEAT_INTERVAL`: Ping interval in ms (default: 5000)
- `KLAWED_ZMQ_HEARTBEAT_TIMEOUT`: Pong timeout in ms (default: 15000)
- `KLAWED_ZMQ_ENABLE_HEARTBEAT`: Enable heartbeat (default: false)

**Reconnection Configuration:**
- `KLAWED_ZMQ_RECONNECT_INTERVAL`: Base reconnect interval in ms (default: 1000)
- `KLAWED_ZMQ_MAX_RECONNECT_INTERVAL`: Max reconnect interval in ms (default: 30000)
- `KLAWED_ZMQ_MAX_RECONNECT_ATTEMPTS`: Max reconnect attempts (default: 10)
- `KLAWED_ZMQ_ENABLE_RECONNECT`: Enable auto-reconnect (default: false)

**Message Queue Configuration:**
- `KLAWED_ZMQ_SEND_QUEUE_SIZE`: Send queue capacity (default: 100)
- `KLAWED_ZMQ_RECEIVE_QUEUE_SIZE`: Receive queue capacity (default: 100)
- `KLAWED_ZMQ_ENABLE_MESSAGE_QUEUE`: Enable message queues (default: false)

**Timeout Configuration:**
- `KLAWED_ZMQ_RECEIVE_TIMEOUT`: Receive timeout in ms (default: 30000)
- `KLAWED_ZMQ_SEND_TIMEOUT`: Send timeout in ms (default: 10000)
- `KLAWED_ZMQ_CONNECT_TIMEOUT`: Connect timeout in ms (default: 5000)

### Client Reconnection Best Practices

1. **Always check send/receive return codes**
   ```python
   try:
       socket.send(message)
   except zmq.ZMQError as e:
       if e.errno == zmq.EAGAIN:
           # Timeout - implement retry logic
           pass
       else:
           # Other error - attempt reconnection
           socket.close()
           socket = context.socket(zmq.PAIR)
           socket.connect(endpoint)
   ```

2. **Implement application-level retry logic**
   ```python
   max_retries = 3
   for attempt in range(max_retries):
       try:
           response = socket.recv(timeout=5000)
           break
       except zmq.Again:
           if attempt == max_retries - 1:
               raise
           time.sleep(2 ** attempt)  # Exponential backoff
   ```

3. **Use connection testing for health checks**
   ```python
   def test_connection(socket, endpoint):
       ping_msg = json.dumps({
           "messageType": "HEARTBEAT_PING",
           "timestamp": int(time.time())
       })
       try:
           socket.send(ping_msg.encode(), zmq.DONTWAIT)
           response = socket.recv(timeout=5000)
           return True
       except:
           return False
   ```

4. **Handle graceful degradation**
   - Queue messages locally when connection is down
   - Implement circuit breaker pattern
   - Provide user feedback about connection status

### Error Recovery Flow

```
Client sends message
    ↓
[Connection healthy?] → No → [Queue enabled?] → No → Return error
    ↓ Yes                    ↓ Yes
Send message                Queue message
    ↓                        ↓
[Send success?] → No → [Reconnect enabled?] → No → Return error
    ↓ Yes                    ↓ Yes
Return success              Attempt reconnect
                                ↓
                            [Reconnect success?] → No → Return error
                                ↓ Yes
                            Send queued messages
                                ↓
                            Return success
```

## Implementation Details

### Code Location
- Input parsing: `src/zmq_socket.c` - `zmq_socket_process_message()` function
- Response generation: Same function, various response creation sections

### Message Type Constants
- `"TEXT"`: Text processing request and successful response
- `"ERROR"`: Error response

### Buffer Sizes
- Input buffer: `ZMQ_BUFFER_SIZE` (65536 bytes)
- Response buffer: Same size
- Messages larger than buffer will be truncated (with warning logged)

### Error Handling
- JSON parse errors return immediate error response
- Missing required fields return error response
- AI API failures return error with details
- Network/timeout errors return -1 (no response sent)

## Usage Examples

### Python Client Example (Simple)

```python
import zmq
import json

context = zmq.Context()
socket = context.socket(zmq.PAIR)
socket.connect("tcp://127.0.0.1:5555")

# Send text request
request = {
    "messageType": "TEXT",
    "content": "Write a function to calculate factorial"
}
socket.send(json.dumps(request).encode('utf-8'))

# Receive response
response = socket.recv()
response_data = json.loads(response.decode('utf-8'))

if response_data.get("messageType") == "TEXT":
    print("AI Response:", response_data.get("content"))
elif response_data.get("messageType") == "ERROR":
    print("Error:", response_data.get("content"))
```

### Python Client Example (Interactive with Tool Calls)

```python
import zmq
import json

context = zmq.Context()
socket = context.socket(zmq.PAIR)
socket.connect("tcp://127.0.0.1:5555")

def send_and_receive(request):
    """Send request and handle multiple response types"""
    socket.send(json.dumps(request).encode('utf-8'))
    
    # Set a timeout for receiving responses (5 seconds)
    socket.RCVTIMEO = 5000
    
    while True:
        try:
            response = socket.recv()
            response_data = json.loads(response.decode('utf-8'))
            msg_type = response_data.get("messageType")
            
            if msg_type == "TEXT":
                print(f"AI: {response_data.get('content')}")
                # After receiving a TEXT response, wait a bit more in case there are more messages
                # but reset timeout for next receive
                socket.RCVTIMEO = 2000  # Shorter timeout for subsequent messages
            elif msg_type == "TOOL_RESULT":
                tool_name = response_data.get("toolName")
                is_error = response_data.get("isError", False)
                if is_error:
                    print(f"Tool {tool_name} error: {response_data.get('toolOutput', {}).get('error', 'Unknown error')}")
                else:
                    print(f"Tool {tool_name} executed successfully")
                # Reset timeout for next message
                socket.RCVTIMEO = 2000
            elif msg_type == "ERROR":
                print(f"Error: {response_data.get('content')}")
                break
        except zmq.Again:
            # Timeout reached - no more messages
            print("Processing completed (timeout)")
            break

# Send initial request
send_and_receive({
    "messageType": "TEXT",
    "content": "Read the README.md file and summarize it"
})
```

## Best Practices

1. **Always check `messageType`** before processing responses
2. **Handle errors gracefully** - check for ERROR messageType
3. **Validate input** before sending to Klawed
4. **Use appropriate timeouts** for network operations
5. **Log message exchanges** for debugging complex interactions

### Java Client Examples

#### Simple Java Client (Minimal)

For quick testing or simple integrations:

```java
import org.zeromq.SocketType;
import org.zeromq.ZMQ;
import org.zeromq.ZContext;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.util.Map;

/**
 * Simple Java ZMQ client for Klawed - minimal implementation.
 */
public class SimpleKlawedClient {
    public static void main(String[] args) throws Exception {
        String endpoint = "tcp://127.0.0.1:5555";
        ObjectMapper mapper = new ObjectMapper();
        
        try (ZContext context = new ZContext()) {
            ZMQ.Socket socket = context.createSocket(SocketType.PAIR);
            socket.setReceiveTimeOut(30000); // 30 second timeout
            socket.connect(endpoint);
            
            // Send text request
            String request = mapper.writeValueAsString(Map.of(
                "messageType", "TEXT",
                "content", "Write a hello world program in Java"
            ));
            
            if (!socket.send(request)) {
                System.err.println("Failed to send message");
                return;
            }
            
            // Receive response
            byte[] responseBytes = socket.recv();
            if (responseBytes == null) {
                System.err.println("No response received (timeout)");
                return;
            }
            
            String responseStr = new String(responseBytes);
            Map<String, Object> response = mapper.readValue(responseStr, Map.class);
            
            String messageType = (String) response.get("messageType");
            if ("TEXT".equals(messageType)) {
                System.out.println("Response: " + response.get("content"));
            } else if ("ERROR".equals(messageType)) {
                System.err.println("Error: " + response.get("content"));
            } else {
                System.err.println("Unknown message type: " + messageType);
            }
        }
    }
}
```

#### Robust Java Client (Production-Ready with Reconnection)

```java
import org.zeromq.SocketType;
import org.zeromq.ZMQ;
import org.zeromq.ZContext;
import org.zeromq.ZMQException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.JsonNode;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.locks.ReentrantLock;

/**
 * Robust Java ZMQ client for Klawed with automatic reconnection,
 * heartbeat monitoring, and message queuing.
 */
public class KlawedZMQClient {
    private static final String ENDPOINT = "tcp://127.0.0.1:5555";
    private static final int HEARTBEAT_INTERVAL_MS = 5000;
    private static final int HEARTBEAT_TIMEOUT_MS = 15000;
    private static final int RECONNECT_BASE_MS = 1000;
    private static final int MAX_RECONNECT_MS = 30000;
    private static final int MAX_RECONNECT_ATTEMPTS = 10;
    private static final int SEND_TIMEOUT_MS = 10000;
    private static final int RECV_TIMEOUT_MS = 30000;
    
    private final ZContext context;
    private ZMQ.Socket socket;
    private final ObjectMapper objectMapper;
    private final ScheduledExecutorService scheduler;
    private final AtomicBoolean connected;
    private final ReentrantLock socketLock;
    private final BlockingQueue<String> messageQueue;
    private ScheduledFuture<?> heartbeatFuture;
    private long lastHeartbeatResponse;
    
    public KlawedZMQClient() {
        this.context = new ZContext();
        this.objectMapper = new ObjectMapper();
        this.scheduler = Executors.newScheduledThreadPool(2);
        this.connected = new AtomicBoolean(false);
        this.socketLock = new ReentrantLock();
        this.messageQueue = new LinkedBlockingQueue<>(100);
        this.lastHeartbeatResponse = System.currentTimeMillis();
        
        // Start connection manager
        scheduler.scheduleAtFixedRate(this::connectionManager, 0, 1, TimeUnit.SECONDS);
        
        // Start message queue processor
        scheduler.scheduleAtFixedRate(this::processMessageQueue, 0, 100, TimeUnit.MILLISECONDS);
    }
    
    /**
     * Connection manager that handles reconnection and heartbeat monitoring
     */
    private void connectionManager() {
        try {
            if (!connected.get()) {
                attemptReconnect();
            } else {
                // Check heartbeat timeout
                long now = System.currentTimeMillis();
                if (now - lastHeartbeatResponse > HEARTBEAT_TIMEOUT_MS) {
                    System.err.println("Heartbeat timeout, reconnecting...");
                    connected.set(false);
                    closeSocket();
                    attemptReconnect();
                }
            }
        } catch (Exception e) {
            System.err.println("Connection manager error: " + e.getMessage());
        }
    }
    
    /**
     * Attempt reconnection with exponential backoff
     */
    private void attemptReconnect() {
        int attempt = 0;
        while (attempt < MAX_RECONNECT_ATTEMPTS && !connected.get()) {
            try {
                socketLock.lock();
                try {
                    if (socket != null) {
                        socket.close();
                    }
                    
                    socket = context.createSocket(SocketType.PAIR);
                    socket.setSendTimeOut(SEND_TIMEOUT_MS);
                    socket.setReceiveTimeOut(RECV_TIMEOUT_MS);
                    socket.setLinger(0); // Don't linger on close
                    
                    System.out.println("Attempting connection to " + ENDPOINT + " (attempt " + (attempt + 1) + ")");
                    socket.connect(ENDPOINT);
                    
                    // Test connection with a heartbeat ping
                    if (sendHeartbeatPing()) {
                        connected.set(true);
                        System.out.println("Connected successfully to " + ENDPOINT);
                        
                        // Start heartbeat monitoring
                        startHeartbeat();
                        return;
                    }
                } finally {
                    socketLock.unlock();
                }
            } catch (Exception e) {
                System.err.println("Connection attempt " + (attempt + 1) + " failed: " + e.getMessage());
            }
            
            // Exponential backoff
            int backoffMs = Math.min(RECONNECT_BASE_MS * (1 << attempt), MAX_RECONNECT_MS);
            attempt++;
            
            if (attempt < MAX_RECONNECT_ATTEMPTS) {
                try {
                    Thread.sleep(backoffMs);
                } catch (InterruptedException ie) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
        }
        
        if (!connected.get()) {
            System.err.println("Failed to connect after " + MAX_RECONNECT_ATTEMPTS + " attempts");
        }
    }
    
    /**
     * Send a heartbeat ping to test connection
     */
    private boolean sendHeartbeatPing() {
        try {
            String pingMessage = objectMapper.writeValueAsString(
                java.util.Map.of(
                    "messageType", "HEARTBEAT_PING",
                    "timestamp", System.currentTimeMillis()
                )
            );
            
            socketLock.lock();
            try {
                if (socket.send(pingMessage, ZMQ.DONTWAIT)) {
                    byte[] response = socket.recv(ZMQ.DONTWAIT);
                    if (response != null) {
                        JsonNode responseJson = objectMapper.readTree(new String(response));
                        if ("HEARTBEAT_PONG".equals(responseJson.get("messageType").asText())) {
                            lastHeartbeatResponse = System.currentTimeMillis();
                            return true;
                        }
                    }
                }
            } finally {
                socketLock.unlock();
            }
        } catch (Exception e) {
            System.err.println("Heartbeat ping failed: " + e.getMessage());
        }
        return false;
    }
    
    /**
     * Start periodic heartbeat monitoring
     */
    private void startHeartbeat() {
        if (heartbeatFuture != null) {
            heartbeatFuture.cancel(false);
        }
        
        heartbeatFuture = scheduler.scheduleAtFixedRate(() -> {
            if (connected.get()) {
                if (!sendHeartbeatPing()) {
                    System.err.println("Heartbeat failed, marking as disconnected");
                    connected.set(false);
                }
            }
        }, HEARTBEAT_INTERVAL_MS, HEARTBEAT_INTERVAL_MS, TimeUnit.MILLISECONDS);
    }
    
    /**
     * Send a text message to Klawed
     */
    public CompletableFuture<String> sendTextMessage(String content) {
        CompletableFuture<String> future = new CompletableFuture<>();
        
        try {
            String message = objectMapper.writeValueAsString(
                java.util.Map.of(
                    "messageType", "TEXT",
                    "content", content
                )
            );
            
            // Queue the message for sending
            if (!messageQueue.offer(message)) {
                future.completeExceptionally(new RuntimeException("Message queue full"));
                return future;
            }
            
            // Process the response asynchronously
            scheduler.submit(() -> {
                try {
                    String response = waitForTextResponse();
                    future.complete(response);
                } catch (Exception e) {
                    future.completeExceptionally(e);
                }
            });
            
        } catch (Exception e) {
            future.completeExceptionally(e);
        }
        
        return future;
    }
    
    /**
     * Process messages from the queue
     */
    private void processMessageQueue() {
        if (!connected.get() || messageQueue.isEmpty()) {
            return;
        }
        
        String message = messageQueue.peek();
        if (message == null) {
            return;
        }
        
        try {
            socketLock.lock();
            try {
                if (socket.send(message, ZMQ.DONTWAIT)) {
                    // Message sent successfully, remove from queue
                    messageQueue.poll();
                    System.out.println("Message sent successfully");
                } else {
                    System.err.println("Failed to send message, will retry");
                    connected.set(false);
                }
            } finally {
                socketLock.unlock();
            }
        } catch (Exception e) {
            System.err.println("Error sending message: " + e.getMessage());
            connected.set(false);
        }
    }
    
    /**
     * Wait for a text response from Klawed
     */
    private String waitForTextResponse() throws Exception {
        long startTime = System.currentTimeMillis();
        StringBuilder fullResponse = new StringBuilder();
        boolean receivedTextResponse = false;
        
        while (System.currentTimeMillis() - startTime < RECV_TIMEOUT_MS) {
            if (!connected.get()) {
                throw new RuntimeException("Disconnected while waiting for response");
            }
            
            byte[] responseBytes = null;
            socketLock.lock();
            try {
                responseBytes = socket.recv(ZMQ.DONTWAIT);
            } finally {
                socketLock.unlock();
            }
            
            if (responseBytes == null) {
                // No message available, check if we've received a text response
                if (receivedTextResponse) {
                    // We got a text response and now there are no more messages
                    break;
                }
                
                // Wait a bit before checking again
                Thread.sleep(100);
                continue;
            }
            
            String responseStr = new String(responseBytes);
            JsonNode responseJson = objectMapper.readTree(responseStr);
            String messageType = responseJson.get("messageType").asText();
            
            switch (messageType) {
                case "TEXT":
                    String content = responseJson.get("content").asText();
                    fullResponse.append(content);
                    receivedTextResponse = true;
                    // Don't break immediately - there might be more messages
                    break;
                    
                case "TOOL_RESULT":
                    String toolName = responseJson.get("toolName").asText();
                    boolean isError = responseJson.get("isError").asBoolean();
                    if (isError) {
                        String error = responseJson.get("toolOutput").get("error").asText();
                        System.out.println("Tool " + toolName + " error: " + error);
                    } else {
                        System.out.println("Tool " + toolName + " executed successfully");
                    }
                    break;
                    
                case "ERROR":
                    String errorContent = responseJson.get("content").asText();
                    throw new RuntimeException("Klawed error: " + errorContent);
                    
                case "HEARTBEAT_PONG":
                    lastHeartbeatResponse = System.currentTimeMillis();
                    break;
                    
                default:
                    System.err.println("Unknown message type: " + messageType);
            }
        }
        
        if (!receivedTextResponse) {
            throw new RuntimeException("No text response received within timeout");
        }
        
        return fullResponse.toString();
    }
    
    /**
     * Close the socket and cleanup
     */
    private void closeSocket() {
        socketLock.lock();
        try {
            if (socket != null) {
                socket.close();
                socket = null;
            }
        } finally {
            socketLock.unlock();
        }
    }
    
    /**
     * Shutdown the client
     */
    public void shutdown() {
        connected.set(false);
        
        if (heartbeatFuture != null) {
            heartbeatFuture.cancel(false);
        }
        
        scheduler.shutdown();
        try {
            if (!scheduler.awaitTermination(5, TimeUnit.SECONDS)) {
                scheduler.shutdownNow();
            }
        } catch (InterruptedException e) {
            scheduler.shutdownNow();
            Thread.currentThread().interrupt();
        }
        
        closeSocket();
        context.close();
    }
    
    /**
     * Example usage
     */
    public static void main(String[] args) throws Exception {
        KlawedZMQClient client = new KlawedZMQClient();
        
        // Wait for connection
        Thread.sleep(2000);
        
        if (!client.connected.get()) {
            System.err.println("Failed to connect, exiting");
            client.shutdown();
            return;
        }
        
        // Send a message and get response
        CompletableFuture<String> future = client.sendTextMessage(
            "Write a hello world program in Java"
        );
        
        try {
            String response = future.get(60, TimeUnit.SECONDS);
            System.out.println("Response received:");
            System.out.println(response);
        } catch (TimeoutException e) {
            System.err.println("Request timed out");
        } catch (Exception e) {
            System.err.println("Error: " + e.getMessage());
        }
        
        // Send another message
        future = client.sendTextMessage(
            "Now write a function to calculate factorial"
        );
        
        try {
            String response = future.get(60, TimeUnit.SECONDS);
            System.out.println("Second response:");
            System.out.println(response);
        } catch (Exception e) {
            System.err.println("Error: " + e.getMessage());
        }
        
        client.shutdown();
    }
}
```

**Dependencies (Maven pom.xml):**
```xml
<dependencies>
    <dependency>
        <groupId>org.zeromq</groupId>
        <artifactId>jeromq</artifactId>
        <version>0.5.3</version>
    </dependency>
    <dependency>
        <groupId>com.fasterxml.jackson.core</groupId>
        <artifactId>jackson-databind</artifactId>
        <version>2.15.2</version>
    </dependency>
</dependencies>
```

**Key Features of the Java Client:**

1. **Automatic Reconnection**: Exponential backoff with configurable limits
2. **Heartbeat Monitoring**: Periodic pings to detect dead connections
3. **Message Queuing**: Messages are queued when disconnected and sent when reconnected
4. **Thread-Safe Operations**: Proper locking for socket operations
5. **Comprehensive Error Handling**: Graceful degradation and recovery
6. **Asynchronous API**: `CompletableFuture` for non-blocking operations
7. **Multiple Message Type Support**: Handles TEXT, TOOL_RESULT, ERROR, and HEARTBEAT messages
8. **Timeout Management**: Configurable send/receive timeouts

**Best Practices Implemented:**

1. **Circuit Breaker Pattern**: Stops sending when connection is down
2. **Exponential Backoff**: Prevents overwhelming the server during reconnection
3. **Connection Health Monitoring**: Heartbeat system detects stale connections
4. **Resource Cleanup**: Proper shutdown sequence
5. **Non-Blocking Operations**: Uses ZMQ.DONTWAIT for better control
6. **Message Persistence**: Queue prevents message loss during disconnections

**Ensuring Reliable Message Delivery:**

To ensure client messages are received by Klawed and Klawed's messages are received by the client:

1. **Client → Klawed (Sending):**
   - Use `send()` with timeout and check return value
   - Queue messages when disconnected
   - Implement retry logic with exponential backoff
   - Verify delivery with acknowledgment (heartbeat response)

2. **Klawed → Client (Receiving):**
   - Use non-blocking `recv()` with timeout
   - Handle multiple message types in a loop
   - Process TOOL_RESULT messages even when waiting for TEXT
   - Reset timeout after each received message

3. **Bidirectional Reliability:**
   - Heartbeat system monitors both directions
   - Automatic reconnection on failure
   - Message queuing during disconnections
   - Thread-safe socket operations

## Java Client Implementation Notes

### Building and Running

**Compilation:**
```bash
# With Maven
mvn compile

# Manual compilation
javac -cp "jeromq-0.5.3.jar:jackson-databind-2.15.2.jar" *.java
```

**Running:**
```bash
# Simple client
java -cp ".:jeromq-0.5.3.jar:jackson-databind-2.15.2.jar" SimpleKlawedClient

# Robust client
java -cp ".:jeromq-0.5.3.jar:jackson-databind-2.15.2.jar" RobustKlawedClient
```

### Testing Message Flow

To verify that messages are properly sent and received:

1. **Start Klawed in ZMQ daemon mode:**
   ```bash
   ./build/klawed --zmq tcp://127.0.0.1:5555
   ```

2. **Run the Java client:**
   ```bash
   cd examples/java
   java -cp ".:dependencies/*" RobustKlawedClient
   ```

3. **Monitor the output:**
   - Connection established message
   - Request sent confirmation
   - Response received and displayed
   - Any error messages

### Debugging Tips

1. **Enable verbose logging** in the Java client by setting system properties:
   ```java
   System.setProperty("org.zeromq.ZMQ.debug", "true");
   ```

2. **Check ZMQ version compatibility:**
   ```java
   System.out.println("ZMQ version: " + ZMQ.getVersionString());
   ```

3. **Monitor network traffic** with tools like `tcpdump` or Wireshark.

4. **Test with a simple echo server** to verify ZMQ setup:
   ```java
   // Simple ZMQ echo server for testing
   try (ZContext context = new ZContext()) {
       ZMQ.Socket socket = context.createSocket(SocketType.PAIR);
       socket.bind("tcp://127.0.0.1:5556");
       while (!Thread.currentThread().isInterrupted()) {
           byte[] msg = socket.recv();
           if (msg != null) {
               socket.send(msg);
           }
       }
   }
   ```

## See Also

- [ZMQ Socket Implementation](../src/zmq_socket.c)
- [ZMQ Header File](../src/zmq_socket.h)
- [Main ZMQ Integration](../src/klawed.c) (lines ~8134-8143)
- [Java Examples](../examples/java/) - Complete working examples
- [Java README](../examples/java/README.md) - Build and usage instructions
