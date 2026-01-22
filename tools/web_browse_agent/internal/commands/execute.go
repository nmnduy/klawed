package commands

import (
	"encoding/json"
	"fmt"
	"strings"

	"github.com/klawed/tools/web_browse_agent/internal/session"
	"github.com/klawed/tools/web_browse_agent/pkg/ipc"
)

// Execute executes a command for a session
func Execute(sess *session.Session, commandName string, args []string) (string, error) {
	// Check if driver is running, start if needed
	if err := ensureDriverRunning(sess); err != nil {
		return "", fmt.Errorf("failed to start driver: %w", err)
	}

	// Create IPC client
	client, err := ipc.NewClient(ipc.ClientConfig{
		SocketPath: sess.DriverSocketPath,
		Timeout:    30, // seconds
	})
	if err != nil {
		return "", fmt.Errorf("failed to create IPC client: %w", err)
	}
	defer client.Close()

	// Connect to driver
	if err := client.Connect(); err != nil {
		return "", fmt.Errorf("failed to connect to driver: %w", err)
	}

	// Parse command and arguments
	ipcCommand, commandArgs, err := parseCommand(commandName, args)
	if err != nil {
		return "", fmt.Errorf("invalid command: %w", err)
	}

	// Create request
	req, err := ipc.NewRequest(ipcCommand, commandArgs)
	if err != nil {
		return "", fmt.Errorf("failed to create request: %w", err)
	}

	// Send request
	resp, err := client.SendRequest(req)
	if err != nil {
		return "", fmt.Errorf("failed to send request: %w", err)
	}

	// Format response
	return formatResponse(resp, commandName)
}

// ensureDriverRunning ensures the driver process is running for the session
func ensureDriverRunning(sess *session.Session) error {
	// Check if driver is already running
	if sess.DriverPID > 0 && sess.DriverSocketPath != "" {
		// TODO: Check if process is actually alive
		return nil
	}

	// Start driver process
	// For now, return error - driver startup will be implemented later
	return fmt.Errorf("driver not running for session %s", sess.ID)
}

// parseCommand parses CLI command into IPC command and arguments
func parseCommand(commandName string, args []string) (ipc.CommandType, interface{}, error) {
	switch strings.ToLower(commandName) {
	case "open":
		if len(args) < 1 {
			return "", nil, fmt.Errorf("open requires URL argument")
		}
		return ipc.CommandOpen, ipc.CommandArguments{
			URL: args[0],
		}, nil

	case "list-tabs":
		return ipc.CommandListTabs, nil, nil

	case "switch-tab":
		if len(args) < 1 {
			return "", nil, fmt.Errorf("switch-tab requires tab ID argument")
		}
		return ipc.CommandSwitchTab, ipc.CommandArguments{
			TabID: args[0],
		}, nil

	case "close-tab":
		if len(args) < 1 {
			return "", nil, fmt.Errorf("close-tab requires tab ID argument")
		}
		return ipc.CommandCloseTab, ipc.CommandArguments{
			TabID: args[0],
		}, nil

	case "eval":
		if len(args) < 1 {
			return "", nil, fmt.Errorf("eval requires JavaScript argument")
		}
		return ipc.CommandEval, ipc.CommandArguments{
			JavaScript: strings.Join(args, " "),
		}, nil

	case "click":
		if len(args) < 1 {
			return "", nil, fmt.Errorf("click requires selector argument")
		}
		return ipc.CommandClick, ipc.CommandArguments{
			Selector: args[0],
		}, nil

	case "type":
		if len(args) < 2 {
			return "", nil, fmt.Errorf("type requires selector and text arguments")
		}
		return ipc.CommandTypeText, ipc.CommandArguments{
			Selector: args[0],
			Text:     strings.Join(args[1:], " "),
		}, nil

	case "upload-file":
		if len(args) < 2 {
			return "", nil, fmt.Errorf("upload-file requires selector and at least one file path")
		}
		return ipc.CommandUploadFile, ipc.CommandArguments{
			Selector:  args[0],
			FilePaths: args[1:],
		}, nil

	case "wait-for":
		if len(args) < 1 {
			return "", nil, fmt.Errorf("wait-for requires selector argument")
		}
		return ipc.CommandWaitFor, ipc.CommandArguments{
			Selector: args[0],
		}, nil

	case "screenshot":
		// Optional path argument
		argsMap := make(map[string]interface{})
		if len(args) > 0 {
			argsMap["path"] = args[0]
		}
		return ipc.CommandScreenshot, argsMap, nil

	case "html":
		return ipc.CommandHTML, nil, nil

	case "set-viewport":
		if len(args) < 2 {
			return "", nil, fmt.Errorf("set-viewport requires width and height arguments")
		}
		var width, height int
		fmt.Sscanf(args[0], "%d", &width)
		fmt.Sscanf(args[1], "%d", &height)
		return ipc.CommandSetViewport, ipc.CommandArguments{
			Width:  width,
			Height: height,
		}, nil

	case "cookies":
		return ipc.CommandCookies, nil, nil

	case "session-info":
		return ipc.CommandSessionInfo, nil, nil

	case "describe-commands":
		return ipc.CommandDescribe, nil, nil

	case "end-session":
		return ipc.CommandShutdown, nil, nil

	case "ping":
		return ipc.CommandPing, nil, nil

	default:
		return "", nil, fmt.Errorf("unknown command: %s", commandName)
	}
}

// formatResponse formats IPC response for CLI output
func formatResponse(resp *ipc.Response, commandName string) (string, error) {
	if !resp.Success {
		return "", fmt.Errorf("command failed: %s", resp.Error)
	}

	// For JSON output, return raw JSON
	// For now, return simple text representation
	if len(resp.Data) == 0 {
		return "OK", nil
	}

	// Try to parse and format data
	var data interface{}
	if err := json.Unmarshal(resp.Data, &data); err != nil {
		return string(resp.Data), nil
	}

	// Format based on command type
	switch commandName {
	case "list-tabs":
		return formatTabList(data)
	case "eval":
		return formatEvalResult(data)
	case "session-info":
		return formatSessionInfo(data)
	case "describe-commands":
		return formatCommandDescriptions(data)
	default:
		// Default JSON formatting
		formatted, _ := json.MarshalIndent(data, "", "  ")
		return string(formatted), nil
	}
}

// formatTabList formats tab list for display
func formatTabList(data interface{}) (string, error) {
	// Parse tabs data
	var tabs []struct {
		ID     string `json:"id"`
		URL    string `json:"url"`
		Title  string `json:"title"`
		Active bool   `json:"active"`
	}

	jsonData, _ := json.Marshal(data)
	if err := json.Unmarshal(jsonData, &tabs); err != nil {
		return "", err
	}

	var result strings.Builder
	result.WriteString("Tabs:\n")
	for i, tab := range tabs {
		activeMarker := " "
		if tab.Active {
			activeMarker = "*"
		}
		result.WriteString(fmt.Sprintf("  %s [%d] %s\n", activeMarker, i+1, tab.ID))
		if tab.Title != "" {
			result.WriteString(fmt.Sprintf("      Title: %s\n", tab.Title))
		}
		if tab.URL != "" {
			result.WriteString(fmt.Sprintf("      URL: %s\n", tab.URL))
		}
	}
	return result.String(), nil
}

// formatEvalResult formats JavaScript evaluation result
func formatEvalResult(data interface{}) (string, error) {
	var result struct {
		Value interface{} `json:"value"`
		Type  string      `json:"type,omitempty"`
	}

	jsonData, _ := json.Marshal(data)
	if err := json.Unmarshal(jsonData, &result); err != nil {
		return "", err
	}

	return fmt.Sprintf("%v", result.Value), nil
}

// formatSessionInfo formats session information
func formatSessionInfo(data interface{}) (string, error) {
	var info struct {
		SessionID   string `json:"session_id"`
		PID         int    `json:"pid"`
		SocketPath  string `json:"socket_path"`
		TabCount    int    `json:"tab_count"`
		ActiveTabID string `json:"active_tab_id,omitempty"`
	}

	jsonData, _ := json.Marshal(data)
	if err := json.Unmarshal(jsonData, &info); err != nil {
		return "", err
	}

	var result strings.Builder
	result.WriteString(fmt.Sprintf("Session: %s\n", info.SessionID))
	result.WriteString(fmt.Sprintf("  Driver PID: %d\n", info.PID))
	result.WriteString(fmt.Sprintf("  Socket: %s\n", info.SocketPath))
	result.WriteString(fmt.Sprintf("  Tabs: %d\n", info.TabCount))
	if info.ActiveTabID != "" {
		result.WriteString(fmt.Sprintf("  Active Tab: %s\n", info.ActiveTabID))
	}
	return result.String(), nil
}

// formatCommandDescriptions formats command descriptions
func formatCommandDescriptions(data interface{}) (string, error) {
	var desc struct {
		Commands []struct {
			Name        string   `json:"name"`
			Description string   `json:"description"`
			Arguments   []string `json:"arguments"`
			Example     string   `json:"example,omitempty"`
		} `json:"commands"`
	}

	jsonData, _ := json.Marshal(data)
	if err := json.Unmarshal(jsonData, &desc); err != nil {
		return "", err
	}

	var result strings.Builder
	result.WriteString("Available commands:\n")
	for _, cmd := range desc.Commands {
		result.WriteString(fmt.Sprintf("  %s", cmd.Name))
		if len(cmd.Arguments) > 0 {
			result.WriteString(fmt.Sprintf(" %s", strings.Join(cmd.Arguments, " ")))
		}
		result.WriteString(fmt.Sprintf("\n      %s\n", cmd.Description))
		if cmd.Example != "" {
			result.WriteString(fmt.Sprintf("      Example: %s\n", cmd.Example))
		}
	}
	return result.String(), nil
}
