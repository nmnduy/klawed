package ipc

import (
	"encoding/json"
	"fmt"
	"io"
	"net"
	"os"
	"time"
)

// Client represents an IPC client that connects to a Unix domain socket server
type Client struct {
	socketPath string
	timeout    time.Duration
	conn       net.Conn
}

// ClientConfig holds configuration for creating a new client
type ClientConfig struct {
	SocketPath string
	Timeout    time.Duration // Connection and request timeout
}

// NewClient creates a new IPC client
func NewClient(config ClientConfig) (*Client, error) {
	if config.SocketPath == "" {
		return nil, fmt.Errorf("socket path is required")
	}

	if config.Timeout == 0 {
		config.Timeout = 30 * time.Second
	}

	return &Client{
		socketPath: config.SocketPath,
		timeout:    config.Timeout,
	}, nil
}

// Connect establishes a connection to the IPC server
func (c *Client) Connect() error {
	// Check if socket exists
	if _, err := os.Stat(c.socketPath); os.IsNotExist(err) {
		return fmt.Errorf("socket does not exist: %s", c.socketPath)
	}

	// Connect with timeout
	dialer := net.Dialer{Timeout: c.timeout}
	conn, err := dialer.Dial("unix", c.socketPath)
	if err != nil {
		return fmt.Errorf("failed to connect to socket: %w", err)
	}

	c.conn = conn
	return nil
}

// Close closes the connection
func (c *Client) Close() error {
	if c.conn != nil {
		return c.conn.Close()
	}
	return nil
}

// SendRequest sends a request to the server and returns the response
func (c *Client) SendRequest(req *Request) (*Response, error) {
	if c.conn == nil {
		return nil, fmt.Errorf("not connected")
	}

	// Set deadline for the entire request
	if err := c.conn.SetDeadline(time.Now().Add(c.timeout)); err != nil {
		return nil, fmt.Errorf("failed to set deadline: %w", err)
	}

	// Marshal request to JSON
	reqJSON, err := json.Marshal(req)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal request: %w", err)
	}

	// Send request (add newline delimiter)
	if _, err := fmt.Fprintf(c.conn, "%s\n", reqJSON); err != nil {
		return nil, fmt.Errorf("failed to send request: %w", err)
	}

	// Read response
	decoder := json.NewDecoder(c.conn)
	var resp Response
	if err := decoder.Decode(&resp); err != nil {
		if err == io.EOF {
			return nil, fmt.Errorf("connection closed by server")
		}
		return nil, fmt.Errorf("failed to decode response: %w", err)
	}

	return &resp, nil
}

// Ping sends a ping command to test connectivity
func (c *Client) Ping() error {
	req, err := NewRequest(CommandPing, nil)
	if err != nil {
		return fmt.Errorf("failed to create ping request: %w", err)
	}

	resp, err := c.SendRequest(req)
	if err != nil {
		return fmt.Errorf("ping failed: %w", err)
	}

	if !resp.Success {
		return fmt.Errorf("ping failed: %s", resp.Error)
	}

	return nil
}

// Shutdown sends a shutdown command to the server
func (c *Client) Shutdown() error {
	req, err := NewRequest(CommandShutdown, nil)
	if err != nil {
		return fmt.Errorf("failed to create shutdown request: %w", err)
	}

	resp, err := c.SendRequest(req)
	if err != nil {
		return fmt.Errorf("shutdown failed: %w", err)
	}

	if !resp.Success {
		return fmt.Errorf("shutdown failed: %s", resp.Error)
	}

	return nil
}

// Echo sends a test command with the given text and returns the echoed text
func (c *Client) Echo(text string) (string, error) {
	args := CommandArguments{
		Text: text,
	}

	req, err := NewRequest(CommandType("echo"), args)
	if err != nil {
		return "", fmt.Errorf("failed to create echo request: %w", err)
	}

	resp, err := c.SendRequest(req)
	if err != nil {
		return "", fmt.Errorf("echo failed: %w", err)
	}

	if !resp.Success {
		return "", fmt.Errorf("echo failed: %s", resp.Error)
	}

	var result struct {
		Text string `json:"text"`
	}
	if err := resp.ParseData(&result); err != nil {
		return "", fmt.Errorf("failed to parse echo response: %w", err)
	}

	return result.Text, nil
}

// IsConnected returns true if the client is connected to the server
func (c *Client) IsConnected() bool {
	return c.conn != nil
}

// SocketPath returns the socket path the client is configured to use
func (c *Client) SocketPath() string {
	return c.socketPath
}
