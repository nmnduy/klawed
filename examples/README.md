# ZMQ Interactive Client Examples

This directory contains examples for interacting with Klawed running in ZMQ daemon mode.

## Quick Start

1. **Start Klawed in ZMQ daemon mode** (in one terminal):
   ```bash
   cd /Users/puter/git/klawedspace
   make ZMQ=1  # Build with ZMQ support
   ./build/klawed --zmq tcp://127.0.0.1:5555
   ```

2. **Build and run the C client** (in another terminal):
   ```bash
   cd /Users/puter/git/klawedspace/examples
   make
   ./zmq_client tcp://127.0.0.1:5555
   ```

## Available Examples

### 1. `zmq_client.c` - C Interactive Client
- **Language**: C
- **Dependencies**: `libzmq`, `libcjson`
- **Features**: Native C implementation, low overhead, interactive prompt with JSON parsing, handles TEXT, TOOL, TOOL_RESULT, and ERROR messages
- **Compile**: `make` or `gcc -o zmq_client zmq_client.c -lzmq -lcjson`
- **Usage**: `./zmq_client tcp://127.0.0.1:5555`

## Installation

### C Dependencies
```bash
# Ubuntu/Debian
sudo apt-get install libzmq3-dev libcjson-dev

# macOS
brew install zeromq cjson
```

## Building

Use the provided Makefile for easy compilation:

```bash
cd examples
make           # Build the C client
make clean     # Clean up built files
make test-c    # Test the C client (requires Klawed running in ZMQ mode)
```

The Makefile automatically handles platform-specific paths for macOS (Homebrew) and Linux.

## Usage Examples

### Basic Interaction
```
$ ./zmq_client tcp://127.0.0.1:5555
[INFO] Using endpoint: tcp://127.0.0.1:5555 (timeout: 120000 ms)
[INFO] Connecting to tcp://127.0.0.1:5555...
[INFO] Connected to tcp://127.0.0.1:5555

Connected to tcp://127.0.0.1:5555
Type your messages (or /help for commands)
--------------------------------------------------

> Hello Klawed!

=== AI Response ===
Hello! I'm Klawed, your AI coding assistant.
=== End of AI Response ===

> /quit
Goodbye!
```

### Available Commands
- **`/help`** - Show help message
- **`/quit`** or **`/exit`** - Exit the client
- **Any other text** - Send as a message to Klawed for processing (supports tool calls)

## Message Format

### Sending Messages
The client automatically formats messages as JSON:
```json
{
  "messageType": "TEXT",
  "content": "Your message here"
}
```

### Receiving Responses
Klawed responds with JSON messages:
```json
{
  "messageType": "TEXT",
  "content": "AI-generated response text"
}
```

For detailed message format specifications, see `docs/zmq_input_output.md`.

## Testing

Run the test target to see a complete example:
```bash
make test-c
```

**Note**: This requires Klawed to be running in ZMQ daemon mode first:
```bash
cd .. && ./build/klawed --zmq tcp://127.0.0.1:5555
```

## Notes

1. **Current Implementation**: The ZMQ implementation in Klawed uses PAIR socket pattern (peer-to-peer) for daemon mode and can process messages through the AI.

2. **Endpoints**: You can use different transport types:
   - `tcp://127.0.0.1:5555` - TCP socket (default)
   - `ipc:///tmp/klawed.sock` - IPC socket file
   - `inproc://klawed` - In-process communication

3. **Error Handling**: If you get connection errors, make sure:
   - Klawed is running with ZMQ daemon mode enabled
   - The endpoint matches exactly
   - No firewall is blocking the connection

4. **Build Options**: The main Klawed build supports ZMQ with `make ZMQ=1` (auto-detected by default if libzmq is available).

## File Structure

- `zmq_client.c` - Interactive C client for ZMQ communication
- `Makefile` - Build system for the C client with platform detection
- `.klawed/` - Example directory containing API call history and input history
  - `api_calls.db` - SQLite database of API call history
  - `input_history.txt` - History of user inputs

## Future Enhancements

Potential additions to this directory:
- Python client examples
- Shell script examples
- More advanced client implementations
- Integration examples with other languages