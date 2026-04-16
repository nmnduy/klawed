package main

import (
	"encoding/json"
	"fmt"
	"log"
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

// mainLogger is used for CLI-level logging
var mainLogger *log.Logger

func init() {
	// Initialize logger - will log to stderr by default
	// Can be redirected to file via WEB_AGENT_LOG_FILE env var
	logFile := os.Getenv("WEB_AGENT_LOG_FILE")
	if logFile != "" {
		f, err := os.OpenFile(logFile, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
		if err == nil {
			mainLogger = log.New(f, "[web-agent-cli] ", log.LstdFlags|log.Lmicroseconds)
		}
	}
	if mainLogger == nil {
		mainLogger = log.New(os.Stderr, "[web-agent-cli] ", log.LstdFlags|log.Lmicroseconds)
	}
}

var (
	sessionID         string
	jsonOutput        bool
	timeout           int
	headless          bool
	verbose           bool
	browserExecutable string
	userDataDir       string
	proxy             string
)

// Enhanced help text - displayed with --help or help command
const longHelpText = `A sessionful browser automation CLI for persistent web browsing sessions.

QUICK START
  # Start a session and open a URL
  web_browse_agent --session my-session open https://example.com

  # Get page content
  web_browse_agent --session my-session html

  # Take a screenshot (use --json to get base64 data)
  web_browse_agent --session my-session --json screenshot

  # End the session when done
  web_browse_agent --session my-session end-session

USAGE
  web_browse_agent [GLOBAL_FLAGS] <command> [args...]
  web_browse_agent commands              # List available commands

GLOBAL FLAGS
  --session, -s <id>    Session ID (required for most commands)
  --json                Output in JSON format for machine parsing
  --timeout <sec>       Per-command timeout in seconds (default: 30).
                        This applies to a single command, NOT session lifetime.
                        To control how long the browser session stays alive,
                        set WEB_AGENT_IDLE_TIMEOUT (default: 300, 0=disable).
  --headless            Run browser without visible UI (default: true)
  --headless=false      Run browser with visible UI (requires X server)
  --verbose, -v         Enable verbose output
  --proxy <url>         HTTP/SOCKS proxy URL (overrides WEB_AGENT_PROXY)
  --browser <path>      Browser executable path (auto-detects if not set)
  --user-data-dir <dir> Browser profile directory

AVAILABLE COMMANDS

  Browser Navigation:
    open <url>            Navigate to URL (async, see ASYNC NAVIGATION below)
    list-tabs             List all open tabs
    switch-tab <id>       Switch to a specific tab
    close-tab <id>        Close a specific tab

  Page Interaction:
    click <selector>      Click an element (CSS or Playwright selector)
    type <selector> <text>  Type text into element (clears first)
    upload-file <sel> <paths...>  Upload file(s) to file input
    wait-for <selector>   Wait for element to appear
    eval <javascript>     Execute JavaScript, returns {"value": ...}

  Page Inspection:
    html                  Get full page HTML
    screenshot            Take screenshot (use --json for base64 data)

  Browser Configuration:
    set-viewport <w> <h>  Set viewport size
    cookies               Get cookies (read-only)

  Session Management:
    session-info          Get session information
    end-session           Close browser and end session
    ping                  Check if session is alive
    describe-commands     Detailed command descriptions
    help                  Show this help message

  Other:
    commands              List commands (no session required)

EXAMPLES

  Web Scraping:
    web_browse_agent --session s open https://example.com
    web_browse_agent --session s wait-for "#content"
    web_browse_agent --session s --json eval "document.querySelector('h1').textContent"

  Form Interaction:
    web_browse_agent --session s open https://example.com/login
    web_browse_agent --session s wait-for "#username"
    web_browse_agent --session s type "#username" "myuser"
    web_browse_agent --session s type "#password" "mypass"
    web_browse_agent --session s click "#submit"

  Screenshot (base64 output):
    web_browse_agent --session s set-viewport 1920 1080
    web_browse_agent --session s open https://example.com
    web_browse_agent --session s wait-for "body"
    web_browse_agent --session s --json screenshot | python3 -c "
import sys,json,base64
d=json.load(sys.stdin)
open('shot.png','wb').write(base64.b64decode(d['data']))"

  File Upload:
    web_browse_agent --session s open https://example.com/upload
    web_browse_agent --session s upload-file "input[type=file]" /path/to/file.pdf
    web_browse_agent --session s click "#submit"

SELECTORS
  Commands accepting selectors support CSS and Playwright selectors:

  CSS selectors:
    #submit-button
    .nav-item:first-child
    [data-testid='login']

  Playwright selectors:
    text=Sign In
    role=button[name='Submit']

ASYNC NAVIGATION
  The 'open' command returns immediately after HTTP headers are received.
  It does NOT wait for full page load. Always follow with wait-for:

    web_browse_agent --session s open https://example.com
    web_browse_agent --session s wait-for "#main-content"

  Common wait-for targets:
    - "#main-content", "#app" - app root element
    - "body" - basic DOM exists
    - ".loading-complete" - custom indicator

COOKIES
  The 'cookies' command is read-only. To set cookies via JavaScript:

    web_browse_agent --session s eval "document.cookie = 'key=value; path=/'"

  Note: JavaScript-set cookies only work for non-HttpOnly cookies.

SESSION LIFETIME
  The browser driver shuts down automatically after a period of inactivity
  to conserve resources. This is controlled by WEB_AGENT_IDLE_TIMEOUT,
  NOT by --timeout.

  --timeout only limits how long a single command (e.g., open, html)
  may take to execute.

  To keep a session alive longer:
    export WEB_AGENT_IDLE_TIMEOUT=7200  # 2 hours
    web_browse_agent --session my-session open https://example.com

  To disable idle timeout and keep the session alive indefinitely:
    export WEB_AGENT_IDLE_TIMEOUT=0
    web_browse_agent --session my-session open https://example.com

ENVIRONMENT VARIABLES
  WEB_AGENT_PERSISTENT_STORAGE    Enable persistent storage (default: false)
  WEB_AGENT_IDLE_TIMEOUT          Idle timeout seconds (default: 300, 0=disable)
  WEB_AGENT_PROXY                 HTTP/SOCKS proxy URL
  WEB_AGENT_BROWSER               Browser executable path
  WEB_AGENT_USER_DATA_DIR         Browser profile directory
  WEB_AGENT_STEALTH               Stealth mode (default: 1, 0=disable)
  BROWSER_HEADLESS                Run headless (default: true)
  BROWSER_VIEWPORT_WIDTH          Viewport width (default: 1920)
  BROWSER_VIEWPORT_HEIGHT         Viewport height (default: 1080)
  DISPLAY                         X server display for non-headless mode

STEALTH MODE
  Enabled by default. Patches browser fingerprinting to bypass bot detection:
  - Removes navigator.webdriver
  - Replaces HeadlessChrome in User-Agent
  - Restores window.chrome object
  - Spoofs plugins array
  - Fixes permissions API
  Disable with: WEB_AGENT_STEALTH=0

NON-HEADLESS MODE (VISIBLE BROWSER)
  To see the browser window, you need an X server and DISPLAY set:

    export DISPLAY=:1
    web_browse_agent --session test --headless=false open https://example.com

  Or use xvfb-run for virtual display:
    xvfb-run web_browse_agent --session test --headless=false open https://example.com

BUILDING
  cd tools/web_browse_agent
  make build
  make install-deps          # Install Playwright browsers (first time)
  make install-datacenter    # For containers (includes system libs)

For detailed command reference, run: web_browse_agent describe-commands
`

func main() {
	rootCmd := &cobra.Command{
		Use:   "web_browse_agent [command] [args...]",
		Short: "Sessionful web browser automation CLI",
		Long:    longHelpText,
		Args:    cobra.ArbitraryArgs,
		RunE:    runCommand,
		Version: version.Version,
	}

	// Global flags
	rootCmd.PersistentFlags().StringVarP(&sessionID, "session", "s", "", "Session ID (required for most commands)")
	rootCmd.PersistentFlags().BoolVar(&jsonOutput, "json", false, "Output in JSON format")
	rootCmd.PersistentFlags().IntVar(&timeout, "timeout", 30, "Per-command timeout in seconds (does NOT control session lifetime; use WEB_AGENT_IDLE_TIMEOUT for that)")
	rootCmd.PersistentFlags().BoolVar(&headless, "headless", true, "Run browser in headless mode")
	rootCmd.PersistentFlags().BoolVarP(&verbose, "verbose", "v", false, "Enable verbose output")
	rootCmd.PersistentFlags().StringVar(&browserExecutable, "browser", "", "Browser executable path (e.g., /usr/bin/brave-browser). If not set, auto-detects system Chromium/Chrome")
	rootCmd.PersistentFlags().StringVar(&userDataDir, "user-data-dir", "", "Browser profile directory (e.g., ~/.config/BraveSoftware/Brave-Browser/Default)")
	rootCmd.PersistentFlags().StringVar(&proxy, "proxy", "", "HTTP/SOCKS proxy URL, e.g. http://host:8080 or socks5://host:1080 (overrides WEB_AGENT_PROXY env var)")

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
	mainLogger.Printf("runCommand called with args: %v", args)
	mainLogger.Printf("Session: %s, Headless: %v, JSON: %v", sessionID, headless, jsonOutput)

	// Support WEB_AGENT_BROWSER and WEB_AGENT_USER_DATA_DIR environment variables as fallback for flags
	if browserExecutable == "" {
		if envBrowser := os.Getenv("WEB_AGENT_BROWSER"); envBrowser != "" {
			mainLogger.Printf("Using browser from WEB_AGENT_BROWSER env: %s", envBrowser)
			browserExecutable = envBrowser
		}
	}
	if userDataDir == "" {
		if envUserDataDir := os.Getenv("WEB_AGENT_USER_DATA_DIR"); envUserDataDir != "" {
			mainLogger.Printf("Using user-data-dir from WEB_AGENT_USER_DATA_DIR env: %s", envUserDataDir)
			userDataDir = envUserDataDir
		}
	}
	if proxy == "" {
		if envProxy := os.Getenv("WEB_AGENT_PROXY"); envProxy != "" {
			mainLogger.Printf("Using proxy from WEB_AGENT_PROXY env: %s", envProxy)
			proxy = envProxy
		}
	}

	// Handle special case: no command provided
	if len(args) == 0 {
		return fmt.Errorf("no command provided. Use 'commands' to see available commands")
	}

	commandName := args[0]
	commandArgs := args[1:]
	mainLogger.Printf("Command: %s, Args: %v", commandName, commandArgs)

	// Handle 'help' command specially - doesn't need a running driver
	if commandName == "help" {
		return showHelp()
	}

	// Session is required for all other commands
	if sessionID == "" {
		return fmt.Errorf("--session flag is required")
	}

	// Get or create session
	mainLogger.Printf("Getting or creating session: %s", sessionID)
	sess, err := session.GetOrCreateSession(sessionID, headless)
	if err != nil {
		mainLogger.Printf("ERROR: Failed to get session: %v", err)
		return fmt.Errorf("failed to get session: %w", err)
	}
	mainLogger.Printf("Session obtained: %s, DriverPID: %d, SocketPath: %s",
		sess.ID, sess.DriverPID, sess.DriverSocketPath)

	// Ensure driver is running
	mainLogger.Printf("Ensuring driver is running...")
	if err := ensureDriverRunning(sess); err != nil {
		mainLogger.Printf("ERROR: Failed to start browser: %v", err)
		return fmt.Errorf("failed to start browser: %w", err)
	}
	mainLogger.Printf("Driver is running (PID: %d)", sess.DriverPID)

	// Execute command
	mainLogger.Printf("Executing command: %s", commandName)
	result, err := executeCommand(sess, commandName, commandArgs)
	if err != nil {
		mainLogger.Printf("ERROR: Command execution failed: %v", err)
		return err
	}

	// Output result
	mainLogger.Printf("Command executed successfully")
	if jsonOutput {
		fmt.Println(result)
	} else {
		fmt.Println(result)
	}

	return nil
}

func ensureDriverRunning(sess *session.Session) error {
	mainLogger.Printf("Checking if driver is already running (PID: %d, Socket: %s)",
		sess.DriverPID, sess.DriverSocketPath)

	// Check if driver is already running
	if sess.DriverPID > 0 && sess.DriverSocketPath != "" {
		// Try to connect
		mainLogger.Printf("Checking if existing driver is alive...")
		client, err := ipc.NewClient(ipc.ClientConfig{
			SocketPath: sess.DriverSocketPath,
			Timeout:    time.Duration(timeout) * time.Second,
		})
		if err == nil {
			if err := client.Connect(); err == nil {
				// Driver is alive
				if err := client.Ping(); err == nil {
					mainLogger.Printf("Existing driver is alive and responding")
					client.Close()
					return nil
				}
				client.Close()
			}
		}
		mainLogger.Printf("Existing driver is not responding, will start new one")
	}

	// Start new driver
	// Use CLI user-data-dir flag if provided, otherwise use session's user-data-dir
	effectiveUserDataDir := userDataDir
	if effectiveUserDataDir == "" {
		effectiveUserDataDir = sess.UserDataDir
	}
	socketPath := filepath.Join(os.TempDir(), fmt.Sprintf("web-agent-%s.sock", sess.ID))
	mainLogger.Printf("Starting new driver process (socket: %s, userDataDir: %s)", socketPath, effectiveUserDataDir)
	pid, err := browser.StartDriverProcess(sess.ID, socketPath, headless, effectiveUserDataDir, browserExecutable, proxy)
	if err != nil {
		mainLogger.Printf("ERROR: Failed to start driver process: %v", err)
		return fmt.Errorf("failed to start driver: %w", err)
	}
	mainLogger.Printf("Driver process started with PID: %d", pid)

	// Update session with driver info
	sess.SetDriverInfo(pid, socketPath)

	// Save session with driver info
	registry, err := session.GetRegistry()
	if err != nil {
		mainLogger.Printf("ERROR: Failed to get registry: %v", err)
		return fmt.Errorf("failed to get registry: %w", err)
	}

	// Save the session with driver info to disk
	sess.UpdateLastUsed()
	if err := registry.Save(sess); err != nil {
		mainLogger.Printf("ERROR: Failed to save session: %v", err)
		return fmt.Errorf("failed to save session: %w", err)
	}

	if verbose {
		fmt.Printf("Started driver (PID: %d, Socket: %s)\n", pid, socketPath)
	}

	// Wait a bit for the driver to be ready
	mainLogger.Printf("Waiting for driver to be ready...")
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
			mainLogger.Printf("Driver is ready and responding")
			client.Close()
			return nil
		}
		client.Close()
		time.Sleep(100 * time.Millisecond)
	}

	mainLogger.Printf("WARNING: Driver may not be fully ready, but proceeding anyway")
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
			Notes       string   `json:"notes,omitempty"`
		} `json:"commands"`
	}
	if err := json.Unmarshal(jsonData, &desc); err != nil {
		return "", err
	}

	var result strings.Builder
	result.WriteString("AVAILABLE COMMANDS (DETAILED)\n\n")
	for i, cmd := range desc.Commands {
		// Command name with arguments
		result.WriteString(fmt.Sprintf("  %s", cmd.Name))
		if len(cmd.Arguments) > 0 {
			result.WriteString(fmt.Sprintf(" <%s>", strings.Join(cmd.Arguments, "> <")))
		}
		result.WriteString("\n")

		// Description (wrapped at ~70 chars with indentation)
		descWrapped := wrapText(cmd.Description, 70)
		for _, line := range descWrapped {
			result.WriteString(fmt.Sprintf("    %s\n", line))
		}

		// Example
		if cmd.Example != "" {
			result.WriteString(fmt.Sprintf("    Example: %s\n", cmd.Example))
		}

		// Notes (if present)
		if cmd.Notes != "" {
			notesWrapped := wrapText(cmd.Notes, 68)
			result.WriteString(fmt.Sprintf("    Note: %s\n", notesWrapped[0]))
			for _, line := range notesWrapped[1:] {
				result.WriteString(fmt.Sprintf("          %s\n", line))
			}
		}

		// Add blank line between commands (but not after last)
		if i < len(desc.Commands)-1 {
			result.WriteString("\n")
		}
	}
	return result.String(), nil
}

// wrapText wraps text at the specified width, returning lines
func wrapText(text string, width int) []string {
	if len(text) <= width {
		return []string{text}
	}

	var lines []string
	words := strings.Fields(text)
	var currentLine strings.Builder

	for _, word := range words {
		if currentLine.Len() > 0 && currentLine.Len()+1+len(word) > width {
			lines = append(lines, currentLine.String())
			currentLine.Reset()
		}
		if currentLine.Len() > 0 {
			currentLine.WriteString(" ")
		}
		currentLine.WriteString(word)
	}

	if currentLine.Len() > 0 {
		lines = append(lines, currentLine.String())
	}

	return lines
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
	fmt.Print(longHelpText)
	return nil
}
