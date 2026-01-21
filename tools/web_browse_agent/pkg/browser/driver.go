package browser

import (
	"fmt"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"sync"
	"syscall"
	"time"

	"github.com/klawed/tools/web_browse_agent/pkg/ipc"
)

// Driver represents a browser driver process
type Driver struct {
	// Configuration
	sessionID   string
	socketPath  string
	headless    bool
	timeout     time.Duration
	parentPID   int // Parent process to monitor for cleanup

	// Process management
	cmd        *exec.Cmd
	pid        int
	ipcServer  *ipc.Server
	shutdown   chan struct{}
	stopOnce   sync.Once
	wg         sync.WaitGroup

	// Browser state
	context    *BrowserContext
	mu         sync.RWMutex
}

// DriverConfig holds configuration for creating a new driver
type DriverConfig struct {
	SessionID  string
	SocketPath string
	Headless   bool
	Timeout    time.Duration
	ParentPID  int // Parent process to monitor - driver exits if parent dies
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
		sessionID:  config.SessionID,
		socketPath: config.SocketPath,
		headless:   config.Headless,
		timeout:    config.Timeout,
		parentPID:  config.ParentPID,
		shutdown:   make(chan struct{}),
	}, nil
}

// Start starts the driver process and IPC server
func (d *Driver) Start() error {
	// Create IPC server
	ipcConfig := ipc.ServerConfig{
		SocketPath: d.socketPath,
	}
	server, err := ipc.NewServer(ipcConfig)
	if err != nil {
		return fmt.Errorf("failed to create IPC server: %w", err)
	}
	d.ipcServer = server

	// Initialize browser context
	ctx, err := NewBrowserContext(d.headless)
	if err != nil {
		return fmt.Errorf("failed to create browser context: %w", err)
	}
	d.context = ctx

	// Register command handlers
	d.registerHandlers(server)

	// Start IPC server
	if err := server.Start(); err != nil {
		return fmt.Errorf("failed to start IPC server: %w", err)
	}

	// Start cleanup goroutine
	d.wg.Add(1)
	go d.cleanupMonitor()

	// Start parent process monitor if parent PID is set
	if d.parentPID > 0 {
		d.wg.Add(1)
		go d.parentProcessMonitor()
	}

	fmt.Printf("Driver started for session %s (PID: %d, Socket: %s)\n",
		d.sessionID, os.Getpid(), d.socketPath)

	return nil
}

// Stop stops the driver process gracefully
func (d *Driver) Stop() error {
	d.stopOnce.Do(func() {
		close(d.shutdown)

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

// parentProcessMonitor checks if the parent process is still alive
// and triggers shutdown if it's not. This ensures the driver doesn't
// become orphaned when klawed is killed.
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
	err = process.Signal(syscall.Signal(0))
	return err == nil
}

// RunDriverMain is the main entry point for the driver process
func RunDriverMain() error {
	// Parse command line arguments
	if len(os.Args) < 4 {
		return fmt.Errorf("usage: %s --driver <session-id> --socket <socket-path> [--parent-pid <pid>] [--no-headless]", os.Args[0])
	}

	var sessionID, socketPath string
	var parentPID int
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
		case "--no-headless":
			headless = false
		}
	}

	if sessionID == "" {
		return fmt.Errorf("session ID is required")
	}

	// Create and start driver
	config := DriverConfig{
		SessionID:  sessionID,
		SocketPath: socketPath,
		Headless:   headless,
		Timeout:    30 * time.Second,
		ParentPID:  parentPID,
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
func StartDriverProcess(sessionID, socketPath string, headless bool) (int, error) {
	// Get current process PID to pass to driver for orphan detection
	parentPID := os.Getpid()

	// Build command
	cmd := exec.Command(os.Args[0], "driver",
		"--driver", sessionID,
		"--socket", socketPath,
		"--parent-pid", fmt.Sprintf("%d", parentPID))
	if !headless {
		cmd.Args = append(cmd.Args, "--no-headless")
	}

	// Set up process attributes
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.SysProcAttr = getSysProcAttr()

	// Start process
	if err := cmd.Start(); err != nil {
		return 0, fmt.Errorf("failed to start driver process: %w", err)
	}

	// Wait a bit for the process to start
	time.Sleep(500 * time.Millisecond)

	// Check if process is still running
	if cmd.Process == nil {
		return 0, fmt.Errorf("driver process failed to start")
	}

	// Wait for socket to be created
	for i := 0; i < 10; i++ {
		if _, err := os.Stat(socketPath); err == nil {
			break
		}
		time.Sleep(100 * time.Millisecond)
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