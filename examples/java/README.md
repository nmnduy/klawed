# Java ZMQ Client Examples for Klawed

This directory contains Java client examples for connecting to Klawed via ZeroMQ sockets.

## Prerequisites

1. **Java 8 or higher**
2. **Maven** for dependency management
3. **Klawed running in ZMQ daemon mode**:
   ```bash
   ./build/klawed --zmq tcp://127.0.0.1:5555
   ```
   Or using environment variables:
   ```bash
   export KLAWED_ZMQ_ENDPOINT="tcp://127.0.0.1:5555"
   export KLAWED_ZMQ_MODE="daemon"
   ./build/klawed
   ```

## Dependencies

Add these dependencies to your `pom.xml`:

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

## Examples

### 1. SimpleKlawedClient.java

**Purpose**: Minimal implementation for quick testing.

**Features**:
- Basic connection setup
- Single request/response cycle
- Simple error handling

**Usage**:
```bash
javac -cp "jeromq-0.5.3.jar:jackson-databind-2.15.2.jar:jackson-core-2.15.2.jar:jackson-annotations-2.15.2.jar" SimpleKlawedClient.java
java -cp ".:jeromq-0.5.3.jar:jackson-databind-2.15.2.jar:jackson-core-2.15.2.jar:jackson-annotations-2.15.2.jar" SimpleKlawedClient
```

### 2. RobustKlawedClient.java

**Purpose**: Production-ready client with reliability features.

**Features**:
- Automatic reconnection with retry logic
- Connection testing via heartbeat
- Timeout handling
- Basic error recovery
- Simple message queuing

**Usage**:
```bash
javac -cp "jeromq-0.5.3.jar:jackson-databind-2.15.2.jar:jackson-core-2.15.2.jar:jackson-annotations-2.15.2.jar" RobustKlawedClient.java
java -cp ".:jeromq-0.5.3.jar:jackson-databind-2.15.2.jar:jackson-core-2.15.2.jar:jackson-annotations-2.15.2.jar" RobustKlawedClient
```

## Message Flow

### Client → Klawed (Sending)
1. Create JSON message with `messageType: "TEXT"` and `content`
2. Send via ZMQ socket
3. Check send success, retry if failed

### Klawed → Client (Receiving)
1. Wait for response with timeout
2. Parse JSON response
3. Handle different message types:
   - `TEXT`: AI-generated response
   - `TOOL_RESULT`: Tool execution result
   - `ERROR`: Error message
   - `HEARTBEAT_PONG`: Connection test response

## Ensuring Reliable Communication

### 1. Connection Management
- Always test connection before sending
- Implement exponential backoff for reconnection
- Monitor connection health with heartbeats

### 2. Message Delivery
- Verify send() return value
- Implement retry logic for failed sends
- Queue messages during disconnection

### 3. Error Handling
- Handle ZMQ exceptions gracefully
- Provide meaningful error messages
- Implement circuit breaker pattern

### 4. Resource Management
- Close sockets properly
- Clean up ZContext on shutdown
- Handle interrupts correctly

## Common Issues and Solutions

### 1. Connection Refused
- Ensure Klawed is running in ZMQ daemon mode
- Check endpoint address and port
- Verify firewall settings

### 2. Timeout Errors
- Increase timeout values for slow networks
- Implement retry logic
- Check Klawed processing time

### 3. Message Format Errors
- Ensure valid JSON format
- Include required fields (`messageType`, `content`)
- Use proper UTF-8 encoding

### 4. Memory Leaks
- Always close ZContext
- Use try-with-resources where possible
- Monitor socket lifecycle

## Testing

1. **Start Klawed in ZMQ mode**:
   ```bash
   cd /path/to/klawedspace
   make ZMQ=1
   ./build/klawed --zmq tcp://127.0.0.1:5555
   ```

2. **Run Java client**:
   ```bash
   cd examples/java
   javac -cp "path/to/dependencies/*" *.java
   java -cp ".:path/to/dependencies/*" RobustKlawedClient
   ```

3. **Expected output**:
   - Connection established message
   - AI responses to prompts
   - Proper error handling if connection fails

## Integration Tips

### 1. Async Operations
For non-blocking operations, use `CompletableFuture`:
```java
CompletableFuture<String> future = CompletableFuture.supplyAsync(() -> {
    return client.sendMessage("Your prompt");
});
```

### 2. Connection Pooling
For high-throughput applications:
- Create a pool of ZMQ sockets
- Implement connection pooling
- Use thread-safe operations

### 3. Monitoring
- Log connection attempts and failures
- Monitor message throughput
- Track response times

## See Also

- [ZMQ Input/Output Documentation](../../docs/zmq_input_output.md)
- [ZMQ Socket Implementation](../../src/zmq_socket.c)
- [Klawed ZMQ Configuration](../../KLAWED.md#zmq-socket-mode)