package browser

import (
	"fmt"
	"log"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"sync"
	"syscall"
	"time"

	"github.com/klawed/tools/web_browse_agent/pkg/ipc"
)

// getLogger returns a logger for the driver
func getLogger() *log.Logger {
	return logger
}

// Driver represents a browser driver process
type Driver struct {
	// Configuration
	sessionID         string
	socketPath        string
	headless          bool
	userDataDir       string        // Path to persistent user data directory
	browserExecutable string        // Path to browser executable (empty = auto-detect)
	timeout           time.Duration
	idleTimeout       time.Duration // Auto-shutdown after this duration of inactivity (0 = disabled)
	parentPID         int           // Parent process to monitor for cleanup

	// Process management
	cmd          *exec.Cmd
	pid          int
	ipcServer    *ipc.Server
	shutdown     chan struct{}
	stopOnce     sync.Once
	shutdownOnce sync.Once // For closing shutdown channel only once
	wg           sync.WaitGroup

	// Activity tracking for idle timeout
	lastActivity time.Time
	activityMu   sync.RWMutex

	// Browser state
	context *BrowserContext
	mu      sync.RWMutex
}

// DriverConfig holds configuration for creating a new driver
type DriverConfig struct {
	SessionID         string
	SocketPath        string
	Headless          bool
	UserDataDir       string        // Path to persistent user data directory (empty = no persistence)
	BrowserExecutable string        // Path to browser executable (empty = auto-detect)
	Timeout           time.Duration
	IdleTimeout       time.Duration // Auto-shutdown after this duration of inactivity (0 = disabled)
	ParentPID         int           // Parent process to monitor - driver exits if parent dies
}

// NewDriver creates a new browser driver
func NewDriver(config DriverConfig) (*Driver, error) {
	if config.SessionID == "" {
		return nil, fmt.Errorf("session ID is required")
	}

	if config.SocketPath == "" {
		// Generate socket path in temp directory
		socketName := fmt.Sprintf("web-agent-%s.sock", config.SessionID)
		config.SocketPath = filepath.Join(os.TempDir(), socketName)
	}

	if config.Timeout == 0 {
		config.Timeout = 30 * time.Second
	}

	return &Driver{
		sessionID:         config.SessionID,
		socketPath:        config.SocketPath,
		headless:          config.Headless,
		userDataDir:       config.UserDataDir,
		browserExecutable: config.BrowserExecutable,
		timeout:           config.Timeout,
		idleTimeout:       config.IdleTimeout,
		parentPID:         config.ParentPID,
		shutdown:          make(chan struct{}),
		lastActivity:      time.Now(),
	}, nil
}

// Start starts the driver process and IPC server
func (d *Driver) Start() error {
	logger.Printf("Starting driver for session %s", d.sessionID)
	logger.Printf("Socket path: %s", d.socketPath)
	logger.Printf("Headless: %v, UserDataDir: %s, BrowserExecutable: %s", d.headless, d.userDataDir, d.browserExecutable)

	// Create IPC server with activity tracking for idle timeout
	ipcConfig := ipc.ServerConfig{
		SocketPath: d.socketPath,
		OnActivity: d.TouchActivity,
	}
	server, err := ipc.NewServer(ipcConfig)
	if err != nil {
		logger.Printf("ERROR: Failed to create IPC server: %v", err)
		return fmt.Errorf("failed to create IPC server: %w", err)
	}
	logger.Printf("IPC server created")
	d.ipcServer = server

	// Initialize browser context
	logger.Printf("Initializing browser context...")
	ctx, err := NewBrowserContextWithConfig(BrowserContextConfig{
		Headless:         d.headless,
		UserDataDir:      d.userDataDir,
		BrowserExecutable: d.browserExecutable,
	})
	if err != nil {
		logger.Printf("ERROR: Failed to create browser context: %v", err)
		return fmt.Errorf("failed to create browser context: %w", err)
	}
	logger.Printf("Browser context initialized successfully")
	d.context = ctx

	// Register command handlers
	d.registerHandlers(server)

	// Start IPC server
	logger.Printf("Starting IPC server on socket: %s", d.socketPath)
	if err := server.Start(); err != nil {
		logger.Printf("ERROR: Failed to start IPC server: %v", err)
		return fmt.Errorf("failed to start IPC server: %w", err)
	}
	logger.Printf("IPC server started successfully")

	// Start cleanup goroutine
	d.wg.Add(1)
	go d.cleanupMonitor()

	// Monitor IPC server for shutdown (e.g., from end-session command)
	d.wg.Add(1)
	go d.ipcShutdownMonitor()

	// Start idle timeout monitor if configured
	if d.idleTimeout > 0 {
		d.wg.Add(1)
		go d.idleTimeoutMonitor()
	}

	// NOTE: Parent process monitoring is disabled for now.
	// The driver will run until explicitly stopped via end-session command,
	// idle timeout, or when the process receives SIGINT/SIGTERM.
	// if d.parentPID > 0 {
	// 	d.wg.Add(1)
	// 	go d.parentProcessMonitor()
	// }

	idleInfo := ""
	if d.idleTimeout > 0 {
		idleInfo = fmt.Sprintf(", IdleTimeout: %s", d.idleTimeout)
	}
	fmt.Printf("Driver started for session %s (PID: %d, Socket: %s%s)\n",
		d.sessionID, os.Getpid(), d.socketPath, idleInfo)

	return nil
}

// Stop stops the driver process gracefully
func (d *Driver) Stop() error {
	d.stopOnce.Do(func() {
		// Close shutdown channel to signal all goroutines
		d.shutdownOnce.Do(func() {
			close(d.shutdown)
		})

		// Stop IPC server
		if d.ipcServer != nil {
			d.ipcServer.Stop()
		}

		// Close browser context
		if d.context != nil {
			d.context.Close()
		}

		// Wait for cleanup
		d.wg.Wait()

		// Remove socket file
		if _, err := os.Stat(d.socketPath); err == nil {
			os.Remove(d.socketPath)
		}
	})

	return nil
}

// SocketPath returns the socket path for IPC communication
func (d *Driver) SocketPath() string {
	return d.socketPath
}

// PID returns the driver process ID
func (d *Driver) PID() int {
	return os.Getpid()
}

// IsAlive returns true if the driver is running
func (d *Driver) IsAlive() bool {
	select {
	case <-d.shutdown:
		return false
	default:
		return true
	}
}

// registerHandlers registers all command handlers with the IPC server
func (d *Driver) registerHandlers(server *ipc.Server) {
	// Browser control commands
	server.RegisterHandler(ipc.CommandOpen, d.handleOpen)
	server.RegisterHandler(ipc.CommandListTabs, d.handleListTabs)
	server.RegisterHandler(ipc.CommandSwitchTab, d.handleSwitchTab)
	server.RegisterHandler(ipc.CommandCloseTab, d.handleCloseTab)

	// Page interaction commands
	server.RegisterHandler(ipc.CommandEval, d.handleEval)
	server.RegisterHandler(ipc.CommandClick, d.handleClick)
	server.RegisterHandler(ipc.CommandTypeText, d.handleType)
	server.RegisterHandler(ipc.CommandWaitFor, d.handleWaitFor)
	server.RegisterHandler(ipc.CommandUploadFile, d.handleUploadFile)

	// Page inspection commands
	server.RegisterHandler(ipc.CommandScreenshot, d.handleScreenshot)
	server.RegisterHandler(ipc.CommandHTML, d.handleHTML)

	// Browser configuration commands
	server.RegisterHandler(ipc.CommandSetViewport, d.handleSetViewport)
	server.RegisterHandler(ipc.CommandCookies, d.handleCookies)

	// Session management commands
	server.RegisterHandler(ipc.CommandSessionInfo, d.handleSessionInfo)
	server.RegisterHandler(ipc.CommandDescribe, d.handleDescribeCommands)
}

// cleanupMonitor monitors for shutdown signal and cleans up resources
func (d *Driver) cleanupMonitor() {
	defer d.wg.Done()

	<-d.shutdown
	// Additional cleanup if needed
}

// ipcShutdownMonitor monitors for IPC server shutdown and triggers driver shutdown
func (d *Driver) ipcShutdownMonitor() {
	defer d.wg.Done()

	select {
	case <-d.shutdown:
		// Already shutting down from another source
		return
	case <-d.ipcServer.Done():
		// IPC server shut down (e.g., from end-session command)
		// Signal shutdown by closing the channel (only if not already closed)
		d.shutdownOnce.Do(func() {
			close(d.shutdown)
		})
	}
}

// idleTimeoutMonitor monitors for inactivity and shuts down the driver
// after the configured idle timeout period
func (d *Driver) idleTimeoutMonitor() {
	defer d.wg.Done()

	// Check every 30 seconds or 1/4 of idle timeout, whichever is smaller
	checkInterval := 30 * time.Second
	if d.idleTimeout/4 < checkInterval {
		checkInterval = d.idleTimeout / 4
	}
	if checkInterval < time.Second {
		checkInterval = time.Second
	}

	ticker := time.NewTicker(checkInterval)
	defer ticker.Stop()

	for {
		select {
		case <-d.shutdown:
			return
		case <-ticker.C:
			d.activityMu.RLock()
			idleDuration := time.Since(d.lastActivity)
			d.activityMu.RUnlock()

			if idleDuration >= d.idleTimeout {
				fmt.Printf("Session %s idle for %s (timeout: %s), shutting down driver\n",
					d.sessionID, idleDuration.Round(time.Second), d.idleTimeout)
				// Trigger shutdown - use a goroutine to avoid deadlock
				go d.Stop()
				return
			}
		}
	}
}

// TouchActivity updates the last activity timestamp to prevent idle timeout
func (d *Driver) TouchActivity() {
	d.activityMu.Lock()
	d.lastActivity = time.Now()
	d.activityMu.Unlock()
}

// LastActivity returns the time of the last activity
func (d *Driver) LastActivity() time.Time {
	d.activityMu.RLock()
	defer d.activityMu.RUnlock()
	return d.lastActivity
}

// parentProcessMonitor checks if the parent process is still alive
// and triggers shutdown if it's not. This ensures the driver doesn't
// become orphaned when klawed is killed.
//
// NOTE: The parent PID should be klawed's PID (from KLAWED_PID env var),
// not the CLI process PID. The CLI is ephemeral - it spawns the driver
// and exits after each command. The driver should persist across CLI
// invocations until klawed itself exits or end-session is called.
func (d *Driver) parentProcessMonitor() {
	defer d.wg.Done()

	ticker := time.NewTicker(5 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-d.shutdown:
			return
		case <-ticker.C:
			if !isProcessAlive(d.parentPID) {
				fmt.Printf("Parent process %d died, shutting down driver\n", d.parentPID)
				// Trigger shutdown - use a goroutine to avoid deadlock
				go d.Stop()
				return
			}
		}
	}
}

// isProcessAlive checks if a process with the given PID is still running
func isProcessAlive(pid int) bool {
	if pid <= 0 {
		return false
	}
	process, err := os.FindProcess(pid)
	if err != nil {
		return false
	}
	// On Unix, sending signal 0 checks if process exists
	// EPERM means the process exists but we don't have permission to signal it
	// ESRCH means the process doesn't exist
	err = process.Signal(syscall.Signal(0))
	if err == nil {
		return true
	}
	// Check if it's a permission error (process exists but we can't signal it)
	if err == syscall.EPERM {
		return true
	}
	return false
}

// DefaultIdleTimeout is the default idle timeout for browser sessions (5 minutes)
const DefaultIdleTimeout = 5 * time.Minute

// RunDriverMain is the main entry point for the driver process
func RunDriverMain() error {
	// Parse command line arguments
	if len(os.Args) < 4 {
		return fmt.Errorf("usage: %s --driver <session-id> --socket <socket-path> [--parent-pid <pid>] [--idle-timeout <seconds>] [--user-data-dir <path>] [--browser <path>] [--no-headless]", os.Args[0])
	}

	var sessionID, socketPath, userDataDir, browserExecutable string
	var parentPID int
	var idleTimeoutSec int
	headless := true

	for i := 1; i < len(os.Args); i++ {
		switch os.Args[i] {
		case "--driver":
			if i+1 < len(os.Args) {
				sessionID = os.Args[i+1]
				i++
			}
		case "--socket":
			if i+1 < len(os.Args) {
				socketPath = os.Args[i+1]
				i++
			}
		case "--parent-pid":
			if i+1 < len(os.Args) {
				fmt.Sscanf(os.Args[i+1], "%d", &parentPID)
				i++
			}
		case "--idle-timeout":
			if i+1 < len(os.Args) {
				fmt.Sscanf(os.Args[i+1], "%d", &idleTimeoutSec)
				i++
			}
		case "--user-data-dir":
			if i+1 < len(os.Args) {
				userDataDir = os.Args[i+1]
				i++
			}
		case "--browser":
			if i+1 < len(os.Args) {
				browserExecutable = os.Args[i+1]
				i++
			}
		case "--no-headless":
			headless = false
		}
	}

	if sessionID == "" {
		return fmt.Errorf("session ID is required")
	}

	// Determine idle timeout: CLI arg > env var > default
	idleTimeout := DefaultIdleTimeout
	if idleTimeoutSec > 0 {
		idleTimeout = time.Duration(idleTimeoutSec) * time.Second
	} else if envTimeout := os.Getenv("WEB_AGENT_IDLE_TIMEOUT"); envTimeout != "" {
		var envSec int
		if _, err := fmt.Sscanf(envTimeout, "%d", &envSec); err == nil && envSec > 0 {
			idleTimeout = time.Duration(envSec) * time.Second
		} else if envTimeout == "0" {
			idleTimeout = 0 // Disable idle timeout
		}
	}

	// Create and start driver
	config := DriverConfig{
		SessionID:         sessionID,
		SocketPath:        socketPath,
		Headless:          headless,
		UserDataDir:       userDataDir,
		BrowserExecutable: browserExecutable,
		Timeout:           30 * time.Second,
		IdleTimeout:       idleTimeout,
		ParentPID:         parentPID,
	}

	driver, err := NewDriver(config)
	if err != nil {
		return fmt.Errorf("failed to create driver: %w", err)
	}

	if err := driver.Start(); err != nil {
		return fmt.Errorf("failed to start driver: %w", err)
	}

	// Wait for shutdown signal
	select {
	case <-driver.shutdown:
		// Graceful shutdown
	case <-waitForInterrupt():
		// Interrupt signal received
	}

	// Stop driver
	return driver.Stop()
}

// waitForInterrupt waits for interrupt signal
func waitForInterrupt() chan os.Signal {
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
	return sigChan
}

// StartDriverProcess starts the driver as a separate process
// The driver will monitor the parent process and exit if it dies.
//
// Parent PID resolution order:
// 1. KLAWED_PID env var (set by klawed itself)
// 2. Parent PID of this process (works when CLI is run via popen from klawed)
// 3. 0 (disables parent monitoring if neither available)
//
// Idle timeout is inherited from WEB_AGENT_IDLE_TIMEOUT env var if set,
// otherwise defaults to DefaultIdleTimeout (5 minutes).
func StartDriverProcess(sessionID, socketPath string, headless bool, userDataDir string, browserExecutable string) (int, error) {
	logger.Printf("StartDriverProcess called for session %s", sessionID)
	logger.Printf("Socket path: %s, Headless: %v, UserDataDir: %s, BrowserExecutable: %s", socketPath, headless, userDataDir, browserExecutable)

	// Get parent PID for orphan detection
	// Prefer KLAWED_PID env var, then fall back to our parent PID
	var parentPID int
	if klawedPID := os.Getenv("KLAWED_PID"); klawedPID != "" {
		fmt.Sscanf(klawedPID, "%d", &parentPID)
		logger.Printf("Using KLAWED_PID: %d", parentPID)
	} else {
		// Use parent PID of this CLI process (should be klawed when run via popen)
		parentPID = os.Getppid()
		logger.Printf("Using parent PID: %d", parentPID)
	}

	// Build command
	cmd := exec.Command(os.Args[0], "driver",
		"--driver", sessionID,
		"--socket", socketPath,
		"--parent-pid", fmt.Sprintf("%d", parentPID))
	if !headless {
		cmd.Args = append(cmd.Args, "--no-headless")
	}

	// Pass user data directory if specified
	if userDataDir != "" {
		cmd.Args = append(cmd.Args, "--user-data-dir", userDataDir)
	}

	// Pass browser executable if specified
	if browserExecutable != "" {
		cmd.Args = append(cmd.Args, "--browser", browserExecutable)
	}

	// Pass idle timeout from environment if set
	// The driver will use WEB_AGENT_IDLE_TIMEOUT env var directly,
	// but we can also pass it as a CLI arg for explicit control
	if envTimeout := os.Getenv("WEB_AGENT_IDLE_TIMEOUT"); envTimeout != "" {
		var idleTimeoutSec int
		if _, err := fmt.Sscanf(envTimeout, "%d", &idleTimeoutSec); err == nil {
			cmd.Args = append(cmd.Args, "--idle-timeout", fmt.Sprintf("%d", idleTimeoutSec))
		}
	}

	logger.Printf("Driver command: %v", cmd.Args)

	// Set up environment for the driver
	cmd.Env = os.Environ()

	// Redirect driver's stdout/stderr to a log file if WEB_AGENT_LOG_FILE is set
	// Otherwise redirect to /dev/null
	logFile := os.Getenv("WEB_AGENT_LOG_FILE")
	if logFile != "" {
		// Use the same log file but with driver prefix
		f, err := os.OpenFile(logFile, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
		if err == nil {
			cmd.Stdout = f
			cmd.Stderr = f
			logger.Printf("Driver output will be logged to: %s", logFile)
		}
	} else {
		// Redirect to /dev/null
		devNull, err := os.OpenFile(os.DevNull, os.O_WRONLY, 0)
		if err == nil {
			cmd.Stdout = devNull
			cmd.Stderr = devNull
			// Note: devNull will be closed when cmd exits
		}
	}

	cmd.SysProcAttr = getSysProcAttr()

	// Start process
	logger.Printf("Starting driver process...")
	if err := cmd.Start(); err != nil {
		logger.Printf("ERROR: Failed to start driver process: %v", err)
		return 0, fmt.Errorf("failed to start driver process: %w", err)
	}
	logger.Printf("Driver process started, PID: %d", cmd.Process.Pid)

	// Wait a bit for the process to start
	time.Sleep(500 * time.Millisecond)

	// Check if process is still running
	if cmd.Process == nil {
		logger.Printf("ERROR: Driver process failed to start (process is nil)")
		return 0, fmt.Errorf("driver process failed to start")
	}

	// Wait for socket to be created
	logger.Printf("Waiting for driver socket to be created...")
	for i := 0; i < 30; i++ {
		if _, err := os.Stat(socketPath); err == nil {
			logger.Printf("Driver socket created: %s", socketPath)
			break
		}
		time.Sleep(200 * time.Millisecond)
	}

	// Check if socket exists
	if _, err := os.Stat(socketPath); err != nil {
		logger.Printf("WARNING: Driver socket was not created: %s", socketPath)
	} else {
		logger.Printf("Driver is ready (PID: %d)", cmd.Process.Pid)
	}

	return cmd.Process.Pid, nil
}

// getSysProcAttr returns process attributes for the driver
func getSysProcAttr() *syscall.SysProcAttr {
	return &syscall.SysProcAttr{
		// Set process group to allow killing child processes
		Setpgid: true,
	}
}
