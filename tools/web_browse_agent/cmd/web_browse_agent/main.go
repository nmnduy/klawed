package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/klawed/tools/web_browse_agent/internal/session"
	"github.com/klawed/tools/web_browse_agent/pkg/browser"
	"github.com/klawed/tools/web_browse_agent/pkg/ipc"
	"github.com/klawed/tools/web_browse_agent/pkg/version"
	"github.com/spf13/cobra"
)

var (
	sessionID  string
	jsonOutput bool
	timeout    int
	headless   bool
	verbose    bool
)

func main() {
	rootCmd := &cobra.Command{
		Use:   "web_browse_agent [command] [args...]",
		Short: "Sessionful web browser automation CLI",
		Long: `A CLI tool for persistent browser sessions with web automation.

Each session maintains browser state (tabs, cookies, navigation history)
across invocations, allowing you to issue one command at a time while
maintaining session state.

Examples:
  # Start a session and open a URL
  web_browse_agent --session my-session open https://example.com

  # List tabs in the session
  web_browse_agent --session my-session list-tabs

  # Get available commands
  web_browse_agent --session my-session help

  # End a session
  web_browse_agent --session my-session end-session`,
		Args:    cobra.ArbitraryArgs,
		RunE:    runCommand,
		Version: version.Version,
	}

	// Global flags
	rootCmd.PersistentFlags().StringVarP(&sessionID, "session", "s", "", "Session ID (required for most commands)")
	rootCmd.PersistentFlags().BoolVar(&jsonOutput, "json", false, "Output in JSON format")
	rootCmd.PersistentFlags().IntVar(&timeout, "timeout", 30, "Command timeout in seconds")
	rootCmd.PersistentFlags().BoolVar(&headless, "headless", true, "Run browser in headless mode")
	rootCmd.PersistentFlags().BoolVarP(&verbose, "verbose", "v", false, "Enable verbose output")

	// Don't mark session as required globally - check in runCommand instead

	// Add driver subcommand (hidden, used internally)
	// DisableFlagParsing allows RunDriverMain() to do its own arg parsing
	driverCmd := &cobra.Command{
		Use:                "driver",
		Hidden:             true,
		DisableFlagParsing: true,
		RunE: func(cmd *cobra.Command, args []string) error {
			return browser.RunDriverMain()
		},
	}
	rootCmd.AddCommand(driverCmd)

	// Add commands subcommand (doesn't require session)
	commandsCmd := &cobra.Command{
		Use:   "commands",
		Short: "List available browser commands",
		RunE: func(cmd *cobra.Command, args []string) error {
			return showHelp()
		},
	}
	rootCmd.AddCommand(commandsCmd)

	if err := rootCmd.Execute(); err != nil {
		os.Exit(1)
	}
}

func runCommand(cmd *cobra.Command, args []string) error {
	// Handle special case: no command provided
	if len(args) == 0 {
		return fmt.Errorf("no command provided. Use 'commands' to see available commands")
	}

	commandName := args[0]
	commandArgs := args[1:]

	// Handle 'help' command specially - doesn't need a running driver
	if commandName == "help" {
		return showHelp()
	}

	// Session is required for all other commands
	if sessionID == "" {
		return fmt.Errorf("--session flag is required")
	}

	// Get or create session
	sess, err := session.GetOrCreateSession(sessionID, headless)
	if err != nil {
		return fmt.Errorf("failed to get session: %w", err)
	}

	// Ensure driver is running
	if err := ensureDriverRunning(sess); err != nil {
		return fmt.Errorf("failed to start browser: %w", err)
	}

	// Execute command
	result, err := executeCommand(sess, commandName, commandArgs)
	if err != nil {
		return err
	}

	// Output result
	if jsonOutput {
		fmt.Println(result)
	} else {
		fmt.Println(result)
	}

	return nil
}

func ensureDriverRunning(sess *session.Session) error {
	// Check if driver is already running
	if sess.DriverPID > 0 && sess.DriverSocketPath != "" {
		// Try to connect
		client, err := ipc.NewClient(ipc.ClientConfig{
			SocketPath: sess.DriverSocketPath,
			Timeout:    time.Duration(timeout) * time.Second,
		})
		if err == nil {
			if err := client.Connect(); err == nil {
				// Driver is alive
				if err := client.Ping(); err == nil {
					client.Close()
					return nil
				}
				client.Close()
			}
		}
	}

	// Start new driver
	socketPath := filepath.Join(os.TempDir(), fmt.Sprintf("web-agent-%s.sock", sess.ID))
	pid, err := browser.StartDriverProcess(sess.ID, socketPath, headless)
	if err != nil {
		return fmt.Errorf("failed to start driver: %w", err)
	}

	// Update session with driver info
	sess.SetDriverInfo(pid, socketPath)

	// Save session with driver info
	registry, err := session.GetRegistry()
	if err != nil {
		return fmt.Errorf("failed to get registry: %w", err)
	}

	// Save the session with driver info to disk
	sess.UpdateLastUsed()
	if err := registry.Save(sess); err != nil {
		return fmt.Errorf("failed to save session: %w", err)
	}

	if verbose {
		fmt.Printf("Started driver (PID: %d, Socket: %s)\n", pid, socketPath)
	}

	// Wait a bit for the driver to be ready
	for i := 0; i < 20; i++ {
		client, err := ipc.NewClient(ipc.ClientConfig{
			SocketPath: socketPath,
			Timeout:    time.Duration(timeout) * time.Second,
		})
		if err != nil {
			time.Sleep(100 * time.Millisecond)
			continue
		}
		if err := client.Connect(); err != nil {
			time.Sleep(100 * time.Millisecond)
			continue
		}
		if err := client.Ping(); err == nil {
			client.Close()
			return nil
		}
		client.Close()
		time.Sleep(100 * time.Millisecond)
	}

	// If we get here, try to proceed anyway
	_ = registry
	return nil
}

func executeCommand(sess *session.Session, commandName string, args []string) (string, error) {
	// Create IPC client
	client, err := ipc.NewClient(ipc.ClientConfig{
		SocketPath: sess.DriverSocketPath,
		Timeout:    time.Duration(timeout) * time.Second,
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
			WaitType: "selector",
		}, nil

	case "screenshot":
		return ipc.CommandScreenshot, nil, nil

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
		return "", nil, fmt.Errorf("unknown command: %s. Use 'help' to see available commands", commandName)
	}
}

func formatResponse(resp *ipc.Response, commandName string) (string, error) {
	if !resp.Success {
		return "", fmt.Errorf("command failed: %s", resp.Error)
	}

	if jsonOutput {
		if len(resp.Data) == 0 {
			return "{\"success\": true}", nil
		}
		return string(resp.Data), nil
	}

	// Human-readable output
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
	case "session-info":
		return formatSessionInfo(data)
	case "describe-commands":
		return formatCommandDescriptions(data)
	case "screenshot":
		return formatScreenshot(data)
	default:
		// Default: pretty print JSON
		formatted, _ := json.MarshalIndent(data, "", "  ")
		return string(formatted), nil
	}
}

func formatTabList(data interface{}) (string, error) {
	jsonData, _ := json.Marshal(data)
	var tabs []struct {
		ID     string `json:"id"`
		URL    string `json:"url"`
		Title  string `json:"title"`
		Active bool   `json:"active"`
	}
	if err := json.Unmarshal(jsonData, &tabs); err != nil {
		return "", err
	}

	if len(tabs) == 0 {
		return "No open tabs", nil
	}

	var result strings.Builder
	result.WriteString("Tabs:\n")
	for i, tab := range tabs {
		marker := " "
		if tab.Active {
			marker = "*"
		}
		result.WriteString(fmt.Sprintf("  %s [%d] %s\n", marker, i+1, tab.ID))
		if tab.Title != "" {
			result.WriteString(fmt.Sprintf("      Title: %s\n", tab.Title))
		}
		if tab.URL != "" {
			result.WriteString(fmt.Sprintf("      URL: %s\n", tab.URL))
		}
	}
	return result.String(), nil
}

func formatSessionInfo(data interface{}) (string, error) {
	jsonData, _ := json.Marshal(data)
	var info struct {
		SessionID   string `json:"session_id"`
		PID         int    `json:"pid"`
		SocketPath  string `json:"socket_path"`
		TabCount    int    `json:"tab_count"`
		ActiveTabID string `json:"active_tab_id,omitempty"`
	}
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

func formatCommandDescriptions(data interface{}) (string, error) {
	jsonData, _ := json.Marshal(data)
	var desc struct {
		Commands []struct {
			Name        string   `json:"name"`
			Description string   `json:"description"`
			Arguments   []string `json:"arguments"`
			Example     string   `json:"example,omitempty"`
		} `json:"commands"`
	}
	if err := json.Unmarshal(jsonData, &desc); err != nil {
		return "", err
	}

	var result strings.Builder
	result.WriteString("Available commands:\n\n")
	for _, cmd := range desc.Commands {
		result.WriteString(fmt.Sprintf("  %s", cmd.Name))
		if len(cmd.Arguments) > 0 {
			result.WriteString(fmt.Sprintf(" <%s>", strings.Join(cmd.Arguments, "> <")))
		}
		result.WriteString(fmt.Sprintf("\n      %s\n", cmd.Description))
		if cmd.Example != "" {
			result.WriteString(fmt.Sprintf("      Example: %s\n", cmd.Example))
		}
		result.WriteString("\n")
	}
	return result.String(), nil
}

func formatScreenshot(data interface{}) (string, error) {
	jsonData, _ := json.Marshal(data)
	var screenshot struct {
		Data string `json:"data"`
		Type string `json:"type"`
	}
	if err := json.Unmarshal(jsonData, &screenshot); err != nil {
		return "", err
	}

	return fmt.Sprintf("Screenshot captured (%s, %d bytes base64)", screenshot.Type, len(screenshot.Data)), nil
}

func showHelp() error {
	help := `Available commands:

  open <url>              Navigate to a URL in the browser
  list-tabs               List all open browser tabs
  switch-tab <tab_id>     Switch to a different browser tab
  close-tab <tab_id>      Close a browser tab
  eval <javascript>       Execute JavaScript in the browser
  click <selector>        Click on an element
  type <selector> <text>  Type text into an input element
  upload-file <selector> <file_path...>
                          Upload file(s) to a file input element
  wait-for <selector>     Wait for an element to appear
  screenshot              Take a screenshot of the current page
  html                    Get the HTML content of the page
  set-viewport <w> <h>    Set browser viewport size
  cookies                 Get browser cookies
  session-info            Get session information
  describe-commands       Get detailed command descriptions
  end-session             End the browser session
  ping                    Check if the session is alive
  help                    Show this help message

Use --json for machine-readable output.
Use --session <id> to specify or create a session.

Examples:
  web_browse_agent --session test open https://example.com
  web_browse_agent --session test eval "document.title"
  web_browse_agent --session test screenshot
  web_browse_agent --session test end-session
`
	fmt.Print(help)
	return nil
}
