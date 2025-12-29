# ZMQ Clients for Klawed

This directory contains client implementations for communicating with Klawed's ZMQ daemon.

## Available Clients

### Java Client
Location: `java/zmq-client/`

A Java client library that implements the Klawed ZMQ protocol with reliable message delivery.

**Features:**
- Full implementation of the message ID/ACK system
- Thread-safe design
- Automatic retry for unacknowledged messages
- JSON message format support
- Connection management with automatic reconnection

**Quick Start:**
```java
import com.klawed.zmq.KlawedZMQClient;
import com.klawed.zmq.Message;

public class Example {
    public static void main(String[] args) {
        try (KlawedZMQClient client = new KlawedZMQClient("tcp://127.0.0.1:5555")) {
            Message response = client.sendText("What is 2+2?");
            System.out.println(response.getContent());
        }
    }
}
```

See the [Java Client README](java/zmq-client/README.md) for more details.

## Protocol Documentation

The ZMQ protocol uses JSON messages with the following structure:

### Message Types
- `TEXT`: Text processing requests and responses
- `TOOL`: Tool execution requests
- `TOOL_RESULT`: Tool execution results
- `ACK`: Acknowledgment messages
- `NACK`: Negative acknowledgments
- `ERROR`: Error messages

### Example Message Flow

1. **Client sends TEXT request:**
```json
{
  "messageType": "TEXT",
  "content": "What is 2+2?",
  "messageId": "client_msg_001"
}
```

2. **Klawed responds with ACK:**
```json
{
  "messageType": "ACK",
  "messageId": "client_msg_001"
}
```

3. **Klawed sends TEXT response:**
```json
{
  "messageType": "TEXT",
  "content": "2+2 equals 4.",
  "messageId": "klawed_msg_001"
}
```

4. **Client sends ACK:**
```json
{
  "messageType": "ACK",
  "messageId": "klawed_msg_001"
}
```

### Reliable Delivery

The protocol implements reliable message delivery with:
- Unique message IDs (32-character hex strings)
- ACK/NACK system for confirmation
- Automatic retry for unacknowledged messages
- Pending message queue with configurable limits

## Running Klawed in ZMQ Mode

Before using any client, start Klawed in ZMQ daemon mode:

```bash
# Start the daemon
./build/klawed --zmq tcp://127.0.0.1:5555

# Or using environment variable
export KLAWED_ZMQ_ENDPOINT="tcp://127.0.0.1:5555"
export KLAWED_ZMQ_MODE="daemon"
./build/klawed
```

## Testing

Test the Java client with the provided examples:

```bash
cd zmq-clients/java/zmq-client

# Build the project
mvn clean compile

# Run simple example
mvn exec:java -Dexec.mainClass="com.klawed.zmq.examples.SimpleClient" \
    -Dexec.args="tcp://127.0.0.1:5555 \"What is 2+2?\""

# Run interactive example
mvn exec:java -Dexec.mainClass="com.klawed.zmq.examples.InteractiveClient" \
    -Dexec.args="tcp://127.0.0.1:5555"
```

## Adding New Clients

To add a client in another language:

1. Create a new directory for your language (e.g., `python/`, `javascript/`, etc.)
2. Implement the ZMQ protocol as described in the documentation
3. Include examples and tests
4. Update this README with information about your client

### Protocol Requirements

All clients should implement:
- ZMQ_PAIR socket connection
- JSON message parsing/serialization
- Message ID generation (or acceptance)
- ACK/NACK handling
- Pending message tracking with retry logic
- Error handling and reconnection

## Configuration Reference

### Klawed Environment Variables
- `KLAWED_ZMQ_ENDPOINT`: ZMQ endpoint (default: none)
- `KLAWED_ZMQ_MODE`: ZMQ mode ("daemon")

### Client Configuration
- **Endpoint**: ZMQ endpoint to connect to
- **Max Pending Messages**: Default 50
- **ACK Timeout**: Default 3000ms
- **Max Retries**: Default 5
- **Receive Timeout**: Default 10000ms

## Troubleshooting

### Common Issues

1. **Connection refused**: Ensure Klawed daemon is running
2. **No response**: Check if Klawed has API key configured
3. **Message timeouts**: Increase ACK timeout or check network
4. **JSON parsing errors**: Verify message format matches protocol

### Debugging

Enable debug logging in clients to see message flow:
- Java: Set log level to DEBUG
- Check Klawed logs for daemon-side issues
- Use `zmq_dump` or similar tools to inspect ZMQ traffic

## License

[Specify license for client implementations]