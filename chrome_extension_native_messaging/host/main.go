// Klawed Browser Controller - Go Native Messaging Host
//
// This program bridges the Chrome Native Messaging Protocol (stdin/stdout)
// with a Unix domain socket that klawed connects to, enabling klawed to
// control a real Chrome/Chromium browser.
//
// Architecture:
//
//   klawed agent
//       │  JSON over Unix socket (/tmp/klawed-browser.sock)
//       ▼
//   This Go host process
//       │  Chrome Native Messaging (4-byte LE length + JSON on stdin/stdout)
//       ▼
//   Chrome Extension (background service worker)
//       │  Chrome APIs (tabs, scripting, etc.)
//       ▼
//   Real Chrome browser
//
// The Go host is launched by Chrome when the extension calls
// chrome.runtime.connectNative("com.klawed.browser_controller").
//
// Environment variables:
//   KLAWED_BROWSER_SOCKET  - Unix socket path (default: /tmp/klawed-browser.sock)
//   KLAWED_BROWSER_LOG     - Log file path (default: /tmp/klawed-browser-host.log)
package main

import (
	"bufio"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"math/rand"
	"net"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"
)

const (
	defaultSocketPath = "/tmp/klawed-browser.sock"
	defaultLogPath    = "/tmp/klawed-browser-host.log"
	responseTimeout   = 30 * time.Second
	maxMessageSize    = 4 * 1024 * 1024 // 4MB
)

// Message is the common structure for all messages between components.
// When klawed → host: Command + Params are set (ID is assigned by host).
// When host → Chrome: ID + Command + Params are set.
// When Chrome → host: ID + Result or Error are set.
// When host → klawed: ID + Result or Error are set.
type Message struct {
	ID      string          `json:"id,omitempty"`
	Command string          `json:"command,omitempty"`
	Params  json.RawMessage `json:"params,omitempty"`
	Result  json.RawMessage `json:"result,omitempty"`
	Error   string          `json:"error,omitempty"`
	Type    string          `json:"type,omitempty"`
}

var (
	// pending maps message IDs to response channels
	pending   = make(map[string]chan Message)
	pendingMu sync.Mutex

	// writeMu serializes writes to Chrome stdout
	writeMu sync.Mutex

	logger *log.Logger
)

func initLogger() {
	logPath := os.Getenv("KLAWED_BROWSER_LOG")
	if logPath == "" {
		logPath = defaultLogPath
	}
	f, err := os.OpenFile(logPath, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
	if err != nil {
		// Cannot log — use discard to avoid any output on stderr/stdout
		logger = log.New(io.Discard, "", 0)
		return
	}
	logger = log.New(f, "[klawed-browser-host] ", log.LstdFlags)
}

// readNativeMessage reads one Chrome native messaging frame from r.
// Format: 4-byte little-endian uint32 length, then that many bytes of JSON.
func readNativeMessage(r io.Reader) ([]byte, error) {
	var length uint32
	if err := binary.Read(r, binary.LittleEndian, &length); err != nil {
		return nil, err
	}
	if length > maxMessageSize {
		return nil, fmt.Errorf("message too large: %d bytes", length)
	}
	buf := make([]byte, length)
	if _, err := io.ReadFull(r, buf); err != nil {
		return nil, err
	}
	return buf, nil
}

// writeNativeMessage writes one Chrome native messaging frame to w.
func writeNativeMessage(w io.Writer, data []byte) error {
	writeMu.Lock()
	defer writeMu.Unlock()
	length := uint32(len(data))
	if err := binary.Write(w, binary.LittleEndian, length); err != nil {
		return err
	}
	_, err := w.Write(data)
	return err
}

// sendToChrome serializes msg and sends it to the Chrome extension.
func sendToChrome(msg Message) error {
	data, err := json.Marshal(msg)
	if err != nil {
		return fmt.Errorf("marshal error: %w", err)
	}
	logger.Printf("→ Chrome: %s", string(data))
	return writeNativeMessage(os.Stdout, data)
}

// generateID creates a unique message ID.
func generateID() string {
	return fmt.Sprintf("%d-%d", time.Now().UnixNano(), rand.Int63())
}

// chromeReader reads responses from Chrome native messaging (stdin) in a loop.
// It dispatches each response to the waiting goroutine via pending map.
// When stdin closes (Chrome disconnected), it closes the done channel.
func chromeReader(done chan struct{}) {
	stdin := bufio.NewReader(os.Stdin)
	for {
		data, err := readNativeMessage(stdin)
		if err != nil {
			if err != io.EOF {
				logger.Printf("error reading from Chrome: %v", err)
			}
			logger.Printf("Chrome stdin closed, triggering shutdown")
			close(done)
			return
		}
		logger.Printf("← Chrome: %s", string(data))

		var msg Message
		if err := json.Unmarshal(data, &msg); err != nil {
			logger.Printf("error parsing Chrome message: %v", err)
			continue
		}

		if msg.ID == "" {
			logger.Printf("Chrome message has no ID, ignoring")
			continue
		}

		pendingMu.Lock()
		ch, ok := pending[msg.ID]
		if ok {
			delete(pending, msg.ID)
		}
		pendingMu.Unlock()

		if ok {
			// Non-blocking send — the receiver might have timed out
			select {
			case ch <- msg:
			default:
				logger.Printf("response channel full or closed for id %s", msg.ID)
			}
		} else {
			logger.Printf("no pending request for id %s", msg.ID)
		}
	}
}

// handleKlawedConn handles one klawed client connection on the Unix socket.
// It reads newline-delimited JSON commands, forwards each to Chrome,
// waits for a response, and writes back the JSON response + newline.
func handleKlawedConn(conn net.Conn) {
	defer conn.Close()
	logger.Printf("klawed client connected")

	scanner := bufio.NewScanner(conn)
	scanner.Buffer(make([]byte, maxMessageSize), maxMessageSize)

	for scanner.Scan() {
		line := scanner.Bytes()
		if len(line) == 0 {
			continue
		}

		var req Message
		if err := json.Unmarshal(line, &req); err != nil {
			logger.Printf("error parsing klawed request: %v", err)
			resp := Message{Error: fmt.Sprintf("invalid JSON: %v", err)}
			writeJSONLine(conn, resp)
			continue
		}

		if req.Command == "" {
			resp := Message{Error: "missing 'command' field"}
			writeJSONLine(conn, resp)
			continue
		}

		// Assign a unique ID for this request
		req.ID = generateID()
		ch := make(chan Message, 1)

		pendingMu.Lock()
		pending[req.ID] = ch
		pendingMu.Unlock()

		// Forward command to Chrome extension
		if err := sendToChrome(req); err != nil {
			logger.Printf("error sending to Chrome: %v", err)
			pendingMu.Lock()
			delete(pending, req.ID)
			pendingMu.Unlock()
			resp := Message{ID: req.ID, Error: fmt.Sprintf("failed to send to Chrome: %v", err)}
			writeJSONLine(conn, resp)
			continue
		}

		// Wait for Chrome's response with a timeout
		var resp Message
		select {
		case resp = <-ch:
			logger.Printf("got Chrome response for id %s", req.ID)
		case <-time.After(responseTimeout):
			pendingMu.Lock()
			delete(pending, req.ID)
			pendingMu.Unlock()
			resp = Message{ID: req.ID, Error: "timeout waiting for Chrome response (30s)"}
			logger.Printf("timeout waiting for Chrome response for id %s", req.ID)
		}

		if err := writeJSONLine(conn, resp); err != nil {
			logger.Printf("error writing response to klawed: %v", err)
			return
		}
	}

	if err := scanner.Err(); err != nil {
		logger.Printf("scanner error: %v", err)
	}
	logger.Printf("klawed client disconnected")
}

// writeJSONLine marshals msg to JSON and writes it followed by a newline.
func writeJSONLine(w io.Writer, msg Message) error {
	data, err := json.Marshal(msg)
	if err != nil {
		logger.Printf("error marshaling message: %v", err)
		return err
	}
	_, err = w.Write(append(data, '\n'))
	return err
}

func main() {
	initLogger()
	logger.Printf("starting (pid=%d)", os.Getpid())

	socketPath := os.Getenv("KLAWED_BROWSER_SOCKET")
	if socketPath == "" {
		socketPath = defaultSocketPath
	}

	// Remove any stale socket file from a previous run
	os.Remove(socketPath)

	ln, err := net.Listen("unix", socketPath)
	if err != nil {
		logger.Fatalf("failed to listen on socket %s: %v", socketPath, err)
	}
	defer func() {
		ln.Close()
		os.Remove(socketPath)
		logger.Printf("socket removed, bye")
	}()

	logger.Printf("listening for klawed on Unix socket: %s", socketPath)

	// Handle OS signals for graceful shutdown
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGTERM, syscall.SIGINT)

	// Start reading from Chrome native messaging
	chromeDone := make(chan struct{})
	go chromeReader(chromeDone)

	// Accept klawed connections in a goroutine
	acceptDone := make(chan struct{})
	go func() {
		defer close(acceptDone)
		for {
			conn, err := ln.Accept()
			if err != nil {
				select {
				case <-chromeDone:
					return
				default:
					logger.Printf("accept error: %v", err)
					return
				}
			}
			go handleKlawedConn(conn)
		}
	}()

	// Block until shutdown is triggered
	select {
	case sig := <-sigCh:
		logger.Printf("received signal %v, shutting down", sig)
	case <-chromeDone:
		logger.Printf("Chrome disconnected, shutting down")
	}
}
