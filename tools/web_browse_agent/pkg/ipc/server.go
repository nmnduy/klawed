package ipc

import (
	"encoding/json"
	"fmt"
	"io"
	"net"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"
)

// Server represents an IPC server that listens on a Unix domain socket
type Server struct {
	socketPath string
	listener   net.Listener
	shutdown   chan struct{}
	wg         sync.WaitGroup
	handlers   map[CommandType]CommandHandler
	mu         sync.RWMutex
}

// CommandHandler is a function that handles a command request
type CommandHandler func(*Request) (*Response, error)

// ServerConfig holds configuration for creating a new server
type ServerConfig struct {
	SocketPath string
}

// NewServer creates a new IPC server
func NewServer(config ServerConfig) (*Server, error) {
	if config.SocketPath == "" {
		return nil, fmt.Errorf("socket path is required")
	}

	// Remove existing socket if it exists
	if _, err := os.Stat(config.SocketPath); err == nil {
		if err := os.Remove(config.SocketPath); err != nil {
			return nil, fmt.Errorf("failed to remove existing socket: %w", err)
		}
	}

	return &Server{
		socketPath: config.SocketPath,
		shutdown:   make(chan struct{}),
		handlers:   make(map[CommandType]CommandHandler),
	}, nil
}

// Start starts the IPC server
func (s *Server) Start() error {
	// Create listener
	listener, err := net.Listen("unix", s.socketPath)
	if err != nil {
		return fmt.Errorf("failed to listen on socket: %w", err)
	}
	s.listener = listener

	// Set socket permissions (read/write for user and group)
	if err := os.Chmod(s.socketPath, 0660); err != nil {
		listener.Close()
		return fmt.Errorf("failed to set socket permissions: %w", err)
	}

	// Register default handlers
	s.registerDefaultHandlers()

	// Start accepting connections
	s.wg.Add(1)
	go s.acceptConnections()

	// Handle signals for graceful shutdown
	go s.handleSignals()

	return nil
}

// Stop stops the IPC server gracefully
func (s *Server) Stop() error {
	close(s.shutdown)

	// Stop accepting new connections
	if s.listener != nil {
		s.listener.Close()
	}

	// Wait for all connections to finish
	s.wg.Wait()

	// Remove socket file
	if _, err := os.Stat(s.socketPath); err == nil {
		os.Remove(s.socketPath)
	}

	return nil
}

// RegisterHandler registers a handler for a command type
func (s *Server) RegisterHandler(command CommandType, handler CommandHandler) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.handlers[command] = handler
}

// GetHandler returns the handler for a command type
func (s *Server) GetHandler(command CommandType) (CommandHandler, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	handler, ok := s.handlers[command]
	return handler, ok
}

// acceptConnections accepts incoming connections
func (s *Server) acceptConnections() {
	defer s.wg.Done()

	for {
		select {
		case <-s.shutdown:
			return
		default:
			// Accept with timeout to allow shutdown check
			if err := s.listener.(*net.UnixListener).SetDeadline(time.Now().Add(1 * time.Second)); err != nil {
				continue
			}

			conn, err := s.listener.Accept()
			if err != nil {
				if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
					continue
				}
				if !s.isShuttingDown() {
					fmt.Fprintf(os.Stderr, "Failed to accept connection: %v\n", err)
				}
				continue
			}

			// Handle connection in goroutine
			s.wg.Add(1)
			go s.handleConnection(conn)
		}
	}
}

// handleConnection handles a single client connection
func (s *Server) handleConnection(conn net.Conn) {
	defer s.wg.Done()
	defer conn.Close()

	decoder := json.NewDecoder(conn)
	encoder := json.NewEncoder(conn)

	for {
		// Set read deadline
		conn.SetReadDeadline(time.Now().Add(30 * time.Second))

		// Read request
		var req Request
		if err := decoder.Decode(&req); err != nil {
			if err == io.EOF {
				return // Client disconnected
			}
			if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
				// Send timeout response
				resp, _ := NewResponse("", false, nil, "request timeout")
				encoder.Encode(resp)
				return
			}
			// Send error response
			resp, _ := NewResponse("", false, nil, fmt.Sprintf("invalid request: %v", err))
			encoder.Encode(resp)
			return
		}

		// Handle request
		resp := s.handleRequest(&req)

		// Send response
		if err := encoder.Encode(resp); err != nil {
			fmt.Fprintf(os.Stderr, "Failed to send response: %v\n", err)
			return
		}
	}
}

// handleRequest handles a single request
func (s *Server) handleRequest(req *Request) *Response {
	// Get handler for command
	handler, ok := s.GetHandler(req.Command)
	if !ok {
		resp, _ := NewResponse(req.ID, false, nil, fmt.Sprintf("unknown command: %s", req.Command))
		return resp
	}

	// Execute handler
	resp, err := handler(req)
	if err != nil {
		resp, _ = NewResponse(req.ID, false, nil, err.Error())
		return resp
	}

	return resp
}

// registerDefaultHandlers registers the default command handlers
func (s *Server) registerDefaultHandlers() {
	s.RegisterHandler(CommandPing, s.handlePing)
	s.RegisterHandler(CommandShutdown, s.handleShutdown)
	s.RegisterHandler(CommandType("echo"), s.handleEcho)
}

// handlePing handles ping command
func (s *Server) handlePing(req *Request) (*Response, error) {
	return NewResponse(req.ID, true, map[string]string{
		"message": "pong",
		"time":    time.Now().Format(time.RFC3339),
	}, "")
}

// handleShutdown handles shutdown command
func (s *Server) handleShutdown(req *Request) (*Response, error) {
	// Start graceful shutdown in a goroutine
	go func() {
		time.Sleep(100 * time.Millisecond) // Give time to send response
		s.Stop()
	}()

	return NewResponse(req.ID, true, map[string]string{
		"message": "shutdown initiated",
	}, "")
}

// handleEcho handles echo command (for testing)
func (s *Server) handleEcho(req *Request) (*Response, error) {
	args, err := req.ParseArguments()
	if err != nil {
		return NewResponse(req.ID, false, nil, fmt.Sprintf("failed to parse arguments: %v", err))
	}

	return NewResponse(req.ID, true, map[string]string{
		"text": args.Text,
	}, "")
}

// handleSignals handles OS signals for graceful shutdown
func (s *Server) handleSignals() {
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)

	<-sigChan
	fmt.Fprintf(os.Stderr, "Received shutdown signal\n")
	s.Stop()
}

// isShuttingDown returns true if the server is shutting down
func (s *Server) isShuttingDown() bool {
	select {
	case <-s.shutdown:
		return true
	default:
		return false
	}
}

// SocketPath returns the socket path the server is listening on
func (s *Server) SocketPath() string {
	return s.socketPath
}

// IsRunning returns true if the server is running
func (s *Server) IsRunning() bool {
	return s.listener != nil && !s.isShuttingDown()
}