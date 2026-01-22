package ipc

import (
	"encoding/json"
	"fmt"
	"time"
)

// CommandType represents the type of command to execute
type CommandType string

const (
	// Browser control commands
	CommandOpen      CommandType = "open"
	CommandListTabs  CommandType = "list-tabs"
	CommandSwitchTab CommandType = "switch-tab"
	CommandCloseTab  CommandType = "close-tab"

	// Page interaction commands
	CommandEval       CommandType = "eval"
	CommandClick      CommandType = "click"
	CommandTypeText   CommandType = "type"
	CommandWaitFor    CommandType = "wait-for"
	CommandUploadFile CommandType = "upload-file"

	// Page inspection commands
	CommandScreenshot CommandType = "screenshot"
	CommandHTML       CommandType = "html"

	// Browser configuration commands
	CommandSetViewport CommandType = "set-viewport"
	CommandCookies     CommandType = "cookies"

	// Session management commands
	CommandSessionInfo CommandType = "session-info"
	CommandDescribe    CommandType = "describe-commands"

	// System commands
	CommandPing     CommandType = "ping"
	CommandShutdown CommandType = "shutdown"
)

// Request represents an IPC request from client to server
type Request struct {
	ID        string          `json:"id"`
	Command   CommandType     `json:"command"`
	Arguments json.RawMessage `json:"arguments,omitempty"`
	Timestamp time.Time       `json:"timestamp"`
}

// Response represents an IPC response from server to client
type Response struct {
	ID        string          `json:"id"`
	Success   bool            `json:"success"`
	Data      json.RawMessage `json:"data,omitempty"`
	Error     string          `json:"error,omitempty"`
	Timestamp time.Time       `json:"timestamp"`
}

// CommandArguments holds arguments for various commands
type CommandArguments struct {
	// Common fields
	TabID string `json:"tab_id,omitempty"`

	// Command-specific fields
	URL        string `json:"url,omitempty"`
	Selector   string `json:"selector,omitempty"`
	Text       string `json:"text,omitempty"`
	JavaScript string `json:"javascript,omitempty"`
	Width      int    `json:"width,omitempty"`
	Height     int    `json:"height,omitempty"`

	// WaitFor specific
	WaitType string `json:"wait_type,omitempty"` // "selector", "timeout", "navigation"
	Timeout  int    `json:"timeout,omitempty"`   // milliseconds

	// Upload specific
	FilePaths []string `json:"file_paths,omitempty"` // Paths to files to upload
}

// TabInfo represents information about a browser tab
type TabInfo struct {
	ID     string `json:"id"`
	URL    string `json:"url"`
	Title  string `json:"title"`
	Active bool   `json:"active"`
}

// SessionInfo represents information about the browser session
type SessionInfo struct {
	SessionID   string    `json:"session_id"`
	PID         int       `json:"pid"`
	SocketPath  string    `json:"socket_path"`
	StartedAt   time.Time `json:"started_at"`
	TabCount    int       `json:"tab_count"`
	ActiveTabID string    `json:"active_tab_id,omitempty"`
}

// ScreenshotResult contains screenshot data
type ScreenshotResult struct {
	Data []byte `json:"data"` // Base64 encoded image data
	Type string `json:"type"` // "png" or "jpeg"
}

// EvalResult contains JavaScript evaluation result
type EvalResult struct {
	Value interface{} `json:"value"`
	Type  string      `json:"type,omitempty"`
}

// CookiesResult contains cookie information
type CookiesResult struct {
	Cookies []CookieInfo `json:"cookies"`
}

// CookieInfo represents a single cookie
type CookieInfo struct {
	Name     string    `json:"name"`
	Value    string    `json:"value"`
	Domain   string    `json:"domain"`
	Path     string    `json:"path"`
	Expires  time.Time `json:"expires,omitempty"`
	HTTPOnly bool      `json:"http_only"`
	Secure   bool      `json:"secure"`
	SameSite string    `json:"same_site,omitempty"`
}

// CommandDescription describes a command for the describe-commands response
type CommandDescription struct {
	Name        string   `json:"name"`
	Description string   `json:"description"`
	Arguments   []string `json:"arguments"`
	Example     string   `json:"example,omitempty"`
}

// DescribeCommandsResult contains descriptions of all available commands
type DescribeCommandsResult struct {
	Commands []CommandDescription `json:"commands"`
}

// NewRequest creates a new request with the given command and arguments
func NewRequest(command CommandType, args interface{}) (*Request, error) {
	id := fmt.Sprintf("req_%d", time.Now().UnixNano())

	var argsJSON json.RawMessage
	if args != nil {
		bytes, err := json.Marshal(args)
		if err != nil {
			return nil, fmt.Errorf("failed to marshal arguments: %w", err)
		}
		argsJSON = json.RawMessage(bytes)
	}

	return &Request{
		ID:        id,
		Command:   command,
		Arguments: argsJSON,
		Timestamp: time.Now(),
	}, nil
}

// NewResponse creates a new response
func NewResponse(requestID string, success bool, data interface{}, errMsg string) (*Response, error) {
	var dataJSON json.RawMessage
	if data != nil {
		bytes, err := json.Marshal(data)
		if err != nil {
			return nil, fmt.Errorf("failed to marshal data: %w", err)
		}
		dataJSON = json.RawMessage(bytes)
	}

	return &Response{
		ID:        requestID,
		Success:   success,
		Data:      dataJSON,
		Error:     errMsg,
		Timestamp: time.Now(),
	}, nil
}

// ParseArguments parses the arguments JSON into the CommandArguments struct
func (r *Request) ParseArguments() (*CommandArguments, error) {
	if len(r.Arguments) == 0 {
		return &CommandArguments{}, nil
	}

	var args CommandArguments
	if err := json.Unmarshal(r.Arguments, &args); err != nil {
		return nil, fmt.Errorf("failed to unmarshal arguments: %w", err)
	}

	return &args, nil
}

// ParseData parses the response data into the specified type
func (r *Response) ParseData(target interface{}) error {
	if len(r.Data) == 0 {
		return nil
	}

	if err := json.Unmarshal(r.Data, target); err != nil {
		return fmt.Errorf("failed to unmarshal data: %w", err)
	}

	return nil
}
