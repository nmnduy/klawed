# Java ZMQ Client for Klawed

A Java client library for communicating with Klawed's ZMQ daemon.

## Overview

This Java package provides a client implementation for the Klawed ZMQ protocol, enabling Java applications to send requests to and receive responses from a Klawed daemon running in ZMQ mode.

## Features

- **Reliable Message Delivery**: Implements the message ID/ACK system for reliable communication
- **Thread-Safe**: Designed for concurrent use in multi-threaded applications
- **JSON Message Format**: Uses the standard Klawed JSON message format
- **Automatic Retry**: Handles message retransmission for unacknowledged messages
- **Connection Management**: Automatic reconnection and error handling

## Dependencies

- **ZeroMQ (JZMQ)**: ZeroMQ Java bindings
- **JSON Processing**: Jackson or Gson for JSON serialization
- **Logging**: SLF4J for logging

## Quick Start

### Running the Interactive Example
The `pom.xml` is configured to compile `InteractiveClient` as the default main class.

```bash
# Build the project
mvn clean compile

# Run the interactive example
mvn exec:java -Dexec.args="tcp://127.0.0.1:5555"

# Or create a runnable JAR
mvn package
java -jar target/zmq-client-1.0.0.jar tcp://127.0.0.1:5555
```

### Code Example
```java
import com.klawed.zmq.KlawedZMQClient;
import com.klawed.zmq.Message;
import com.klawed.zmq.MessageType;

public class Example {
    public static void main(String[] args) {
        try (KlawedZMQClient client = new KlawedZMQClient("tcp://127.0.0.1:5555")) {
            // Send a text request
            Message response = client.sendText("What is 2+2?");
            System.out.println("Response: " + response.getContent());
        }
    }
}
```

## Message Types

The client supports all Klawed message types:
- `TEXT`: Text processing requests and responses
- `TOOL`: Tool execution requests
- `TOOL_RESULT`: Tool execution results
- `ACK`: Acknowledgment messages
- `NACK`: Negative acknowledgments
- `ERROR`: Error messages

## Configuration

### Connection Settings
- **Endpoint**: ZMQ endpoint (e.g., `tcp://127.0.0.1:5555` or `ipc:///tmp/klawed.sock`)
- **Socket Type**: ZMQ_PAIR for peer-to-peer communication
- **Timeout**: Configurable receive and send timeouts

### Reliability Settings
- **Max Pending Messages**: Maximum number of unacknowledged messages (default: 50)
- **ACK Timeout**: Time to wait for ACK before retry (default: 3000ms)
- **Max Retries**: Maximum retry attempts (default: 5)

## Usage Examples

### Basic Text Request
```java
KlawedZMQClient client = new KlawedZMQClient("tcp://127.0.0.1:5555");
Message response = client.sendText("What is the capital of France?");
System.out.println(response.getContent());
client.close();
```

### Interactive Processing with Tool Calls
```java
KlawedZMQClient client = new KlawedZMQClient("tcp://127.0.0.1:5555");

// Send initial request
Message initialResponse = client.sendText("Read the file README.md");

// Process responses (may include TOOL and TOOL_RESULT messages)
while (true) {
    Message response = client.receive();
    if (response == null) break;
    
    switch (response.getMessageType()) {
        case TEXT:
            System.out.println("AI Response: " + response.getContent());
            break;
        case TOOL:
            System.out.println("Tool request: " + response.getToolName());
            // Execute tool and send result
            break;
        case TOOL_RESULT:
            System.out.println("Tool result: " + response.getToolOutput());
            break;
        case ERROR:
            System.err.println("Error: " + response.getContent());
            break;
    }
}

client.close();
```

### Custom Message Creation
```java
Message message = new Message.Builder()
    .messageType(MessageType.TEXT)
    .content("Your prompt here")
    .messageId("custom_id_001")
    .build();

Message response = client.send(message);
```

## Error Handling

```java
try {
    Message response = client.sendText("Some prompt");
    // Process response
} catch (ZMQException e) {
    System.err.println("ZMQ error: " + e.getMessage());
} catch (TimeoutException e) {
    System.err.println("Timeout waiting for response");
} catch (IOException e) {
    System.err.println("IO error: " + e.getMessage());
}
```

## Building

```bash
# Build with Maven
mvn clean compile

# Build with Gradle
gradle build
```

## Testing

```bash
# Run unit tests
mvn test

# Run integration tests (requires Klawed daemon running)
mvn verify -Pintegration
```

## License

[Specify license]

## Contributing

[Contribution guidelines]