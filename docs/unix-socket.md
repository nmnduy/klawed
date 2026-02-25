# Unix Domain Socket (UDS) Mode

This document describes how to use the Unix domain socket mode for communicating with Klawed programmatically.

## Overview

Klawed can run as a daemon that accepts connections via Unix domain sockets. This provides a simple, efficient IPC mechanism for integrating Klawed into other applications. The UDS mode is ideal for:

- Local application integration (same machine)
- Language-agnostic client implementations
- Lower latency than TCP (no network stack overhead)
- Simple length-prefixed framing protocol

## Starting UDS Daemon Mode

### Command Line

```bash
./build/klawed -u /tmp/klawed.sock
# or
./build/klawed --uds /tmp/klawed.sock
```

### Environment Variable

```bash
export KLAWED_UNIX_SOCKET_PATH=/tmp/klawed.sock
./build/klawed
```

The socket file is created at the specified path. If the file already exists, it will be removed and recreated.

## Configuration

| Environment Variable | Default | Description |
|---------------------|---------|-------------|
| `KLAWED_UNIX_SOCKET_PATH` | - | Path to Unix socket file |
| `KLAWED_UNIX_SOCKET_RETRIES` | 5 | Max reconnection attempts |
| `KLAWED_UNIX_SOCKET_TIMEOUT` | 30 | Timeout for operations (seconds) |

## Wire Protocol

### Message Framing

Messages use a simple length-prefixed framing protocol:

```
+------------------+------------------+
| Length (4 bytes) | JSON Payload     |
| (network order)  | (UTF-8)          |
+------------------+------------------+
```

- **Length**: 4-byte unsigned integer in network byte order (big-endian)
- **Payload**: JSON-encoded message (UTF-8)
- **Max message size**: 64 MB

### Connection Model

- Klawed acts as the **server** (binds and listens)
- Your application acts as the **client** (connects)
- Single client connection at a time
- Synchronous request-response pattern

## JSON Message Format

### Client → Klawed Messages

#### TEXT Message (Send Prompt)

```json
{
  "messageType": "TEXT",
  "content": "Your prompt or question here"
}
```

#### TERMINATE Message (Stop Daemon)

```json
{
  "messageType": "TERMINATE"
}
```

### Klawed → Client Messages

#### TEXT Response

```json
{
  "messageType": "TEXT",
  "content": "AI-generated response text"
}
```

#### ERROR Response

```json
{
  "messageType": "ERROR",
  "content": "Error description"
}
```

## Message Flow

A typical interaction involves:

1. Client connects to the Unix socket
2. Client sends a `TEXT` message with a prompt
3. Klawed processes the request (may involve multiple tool calls internally)
4. Klawed sends one or more `TEXT` responses
5. If an error occurs, Klawed sends an `ERROR` message
6. Client can send another request or disconnect

**Note**: A single user prompt may result in multiple `TEXT` responses if the AI generates output at different stages (e.g., before and after tool execution).

## Example: Java Connector

Here's a complete Java implementation for connecting to Klawed via Unix domain socket:

```java
import java.io.*;
import java.net.StandardProtocolFamily;
import java.net.UnixDomainSocketAddress;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.concurrent.TimeUnit;

/**
 * Java client for communicating with Klawed via Unix domain socket.
 * Requires Java 16+ for Unix domain socket support.
 */
public class KlawedClient implements AutoCloseable {
    
    private static final int MAX_MESSAGE_SIZE = 64 * 1024 * 1024; // 64 MB
    private static final int DEFAULT_TIMEOUT_MS = 30000;
    
    private final SocketChannel channel;
    private final ByteBuffer lengthBuffer;
    private final int timeoutMs;
    
    /**
     * Creates a new Klawed client connected to the specified socket path.
     *
     * @param socketPath Path to the Unix domain socket
     * @throws IOException if connection fails
     */
    public KlawedClient(String socketPath) throws IOException {
        this(socketPath, DEFAULT_TIMEOUT_MS);
    }
    
    /**
     * Creates a new Klawed client with custom timeout.
     *
     * @param socketPath Path to the Unix domain socket
     * @param timeoutMs  Timeout in milliseconds for read operations
     * @throws IOException if connection fails
     */
    public KlawedClient(String socketPath, int timeoutMs) throws IOException {
        this.timeoutMs = timeoutMs;
        this.lengthBuffer = ByteBuffer.allocate(4);
        this.lengthBuffer.order(ByteOrder.BIG_ENDIAN);
        
        UnixDomainSocketAddress address = UnixDomainSocketAddress.of(Path.of(socketPath));
        this.channel = SocketChannel.open(StandardProtocolFamily.UNIX);
        this.channel.configureBlocking(true);
        this.channel.connect(address);
    }
    
    /**
     * Sends a text prompt to Klawed.
     *
     * @param prompt The prompt to send
     * @throws IOException if send fails
     */
    public void sendPrompt(String prompt) throws IOException {
        String json = String.format("{\"messageType\":\"TEXT\",\"content\":%s}", 
                                    escapeJson(prompt));
        sendMessage(json);
    }
    
    /**
     * Sends a terminate command to stop the Klawed daemon.
     *
     * @throws IOException if send fails
     */
    public void sendTerminate() throws IOException {
        sendMessage("{\"messageType\":\"TERMINATE\"}");
    }
    
    /**
     * Receives a response from Klawed.
     *
     * @return KlawedResponse containing the message type and content
     * @throws IOException if receive fails or times out
     */
    public KlawedResponse receive() throws IOException {
        String json = receiveMessage();
        return parseResponse(json);
    }
    
    /**
     * Sends a prompt and collects all responses until completion.
     * This handles the case where Klawed sends multiple TEXT messages.
     *
     * @param prompt The prompt to send
     * @return Combined response text
     * @throws IOException if communication fails
     * @throws KlawedException if Klawed returns an error
     */
    public String chat(String prompt) throws IOException, KlawedException {
        sendPrompt(prompt);
        
        StringBuilder result = new StringBuilder();
        
        // Collect responses - Klawed may send multiple TEXT messages
        // We use a short timeout to detect end of responses
        while (true) {
            try {
                KlawedResponse response = receive();
                
                if (response.isError()) {
                    throw new KlawedException(response.getContent());
                }
                
                if (response.isText()) {
                    if (result.length() > 0) {
                        result.append("\n");
                    }
                    result.append(response.getContent());
                }
                
            } catch (IOException e) {
                // Timeout likely means no more messages
                if (result.length() > 0) {
                    break;
                }
                throw e;
            }
        }
        
        return result.toString();
    }
    
    private void sendMessage(String message) throws IOException {
        byte[] payload = message.getBytes(StandardCharsets.UTF_8);
        
        if (payload.length > MAX_MESSAGE_SIZE) {
            throw new IOException("Message too large: " + payload.length + " bytes");
        }
        
        // Prepare length header
        lengthBuffer.clear();
        lengthBuffer.putInt(payload.length);
        lengthBuffer.flip();
        
        // Send length header
        while (lengthBuffer.hasRemaining()) {
            channel.write(lengthBuffer);
        }
        
        // Send payload
        ByteBuffer payloadBuffer = ByteBuffer.wrap(payload);
        while (payloadBuffer.hasRemaining()) {
            channel.write(payloadBuffer);
        }
    }
    
    private String receiveMessage() throws IOException {
        // Read length header
        lengthBuffer.clear();
        readFully(lengthBuffer);
        lengthBuffer.flip();
        int length = lengthBuffer.getInt();
        
        if (length <= 0 || length > MAX_MESSAGE_SIZE) {
            throw new IOException("Invalid message length: " + length);
        }
        
        // Read payload
        ByteBuffer payloadBuffer = ByteBuffer.allocate(length);
        readFully(payloadBuffer);
        payloadBuffer.flip();
        
        return StandardCharsets.UTF_8.decode(payloadBuffer).toString();
    }
    
    private void readFully(ByteBuffer buffer) throws IOException {
        long deadline = System.currentTimeMillis() + timeoutMs;
        
        while (buffer.hasRemaining()) {
            if (System.currentTimeMillis() > deadline) {
                throw new IOException("Read timeout");
            }
            
            int read = channel.read(buffer);
            if (read < 0) {
                throw new IOException("Connection closed");
            }
            if (read == 0) {
                // Brief sleep to avoid busy-waiting
                try {
                    Thread.sleep(10);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    throw new IOException("Interrupted", e);
                }
            }
        }
    }
    
    private KlawedResponse parseResponse(String json) throws IOException {
        // Simple JSON parsing - consider using a proper JSON library in production
        String messageType = extractJsonString(json, "messageType");
        String content = extractJsonString(json, "content");
        return new KlawedResponse(messageType, content);
    }
    
    private String extractJsonString(String json, String key) {
        String searchKey = "\"" + key + "\":";
        int keyIndex = json.indexOf(searchKey);
        if (keyIndex < 0) {
            return null;
        }
        
        int valueStart = keyIndex + searchKey.length();
        // Skip whitespace
        while (valueStart < json.length() && Character.isWhitespace(json.charAt(valueStart))) {
            valueStart++;
        }
        
        if (valueStart >= json.length() || json.charAt(valueStart) != '"') {
            return null;
        }
        
        valueStart++; // Skip opening quote
        StringBuilder value = new StringBuilder();
        boolean escaped = false;
        
        for (int i = valueStart; i < json.length(); i++) {
            char c = json.charAt(i);
            if (escaped) {
                switch (c) {
                    case 'n': value.append('\n'); break;
                    case 'r': value.append('\r'); break;
                    case 't': value.append('\t'); break;
                    case '"': value.append('"'); break;
                    case '\\': value.append('\\'); break;
                    default: value.append(c);
                }
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                break;
            } else {
                value.append(c);
            }
        }
        
        return value.toString();
    }
    
    private String escapeJson(String str) {
        StringBuilder sb = new StringBuilder("\"");
        for (char c : str.toCharArray()) {
            switch (c) {
                case '"': sb.append("\\\""); break;
                case '\\': sb.append("\\\\"); break;
                case '\n': sb.append("\\n"); break;
                case '\r': sb.append("\\r"); break;
                case '\t': sb.append("\\t"); break;
                default:
                    if (c < 0x20) {
                        sb.append(String.format("\\u%04x", (int) c));
                    } else {
                        sb.append(c);
                    }
            }
        }
        sb.append("\"");
        return sb.toString();
    }
    
    @Override
    public void close() throws IOException {
        channel.close();
    }
    
    /**
     * Response from Klawed.
     */
    public static class KlawedResponse {
        private final String messageType;
        private final String content;
        
        public KlawedResponse(String messageType, String content) {
            this.messageType = messageType;
            this.content = content;
        }
        
        public String getMessageType() { return messageType; }
        public String getContent() { return content; }
        public boolean isText() { return "TEXT".equals(messageType); }
        public boolean isError() { return "ERROR".equals(messageType); }
        
        @Override
        public String toString() {
            return String.format("KlawedResponse{type=%s, content=%s}", messageType, content);
        }
    }
    
    /**
     * Exception thrown when Klawed returns an error.
     */
    public static class KlawedException extends Exception {
        public KlawedException(String message) {
            super(message);
        }
    }
    
    // Example usage
    public static void main(String[] args) {
        String socketPath = args.length > 0 ? args[0] : "/tmp/klawed.sock";
        
        try (KlawedClient client = new KlawedClient(socketPath)) {
            System.out.println("Connected to Klawed at " + socketPath);
            
            // Send a simple prompt
            client.sendPrompt("What is 2 + 2?");
            
            // Receive response(s)
            KlawedResponse response = client.receive();
            System.out.println("Response: " + response.getContent());
            
            // Or use the convenience method for a complete conversation turn
            // String answer = client.chat("Explain what Unix domain sockets are.");
            // System.out.println("Answer: " + answer);
            
        } catch (IOException e) {
            System.err.println("Communication error: " + e.getMessage());
            e.printStackTrace();
        }
    }
}
```

### Compilation and Usage

```bash
# Compile (requires Java 16+)
javac KlawedClient.java

# Start Klawed daemon in another terminal
./build/klawed -u /tmp/klawed.sock

# Run the Java client
java KlawedClient /tmp/klawed.sock
```

## Error Handling

### Common Errors

| Error | Cause | Solution |
|-------|-------|----------|
| `Connection refused` | Klawed not running or wrong path | Start Klawed with `-u` option |
| `Read timeout` | Klawed taking too long to respond | Increase timeout or check Klawed logs |
| `Invalid message length` | Protocol mismatch | Ensure correct byte order (big-endian) |
| `Message too large` | Response exceeds 64 MB | Check for runaway tool output |

### Reconnection

If the connection is lost, clients should:

1. Close the existing socket
2. Wait briefly (e.g., 1 second)
3. Attempt to reconnect
4. Retry up to `KLAWED_UNIX_SOCKET_RETRIES` times

## Comparison with ZMQ Mode

| Feature | UDS Mode | ZMQ Mode |
|---------|----------|----------|
| Dependencies | None (built-in) | libzmq required |
| Transport | Unix socket only | TCP, IPC, in-process |
| Framing | Manual (length-prefix) | Automatic |
| Message ACK | Not required | Built-in reliability |
| Complexity | Simple | Feature-rich |
| Best for | Simple integrations | Complex distributed systems |

## Building with UDS Support

UDS support is included by default. To verify it's enabled:

```bash
./build/klawed --help | grep uds
```

If UDS support is missing, rebuild with:

```bash
make clean
make
```

## See Also

- [ZMQ Input/Output](zmq_input_output.md) - Alternative socket mode with more features
- [SQLite Queue](sqlite-queue.md) - Database-backed message queue
