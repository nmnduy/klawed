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

	"github.com/klawed/web-agent/pkg/ipc"
)

// Driver represents a browser driver process
type Driver struct {
	// Configuration
	sessionID   string
	socketPath  string
	headless    bool
	timeout     time.Duration

	// Process management
	cmd        *exec.Cmd
	pid        int
	ipcServer  *ipc.Server
	shutdown   chan struct{}
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

	fmt.Printf("Driver started for session %s (PID: %d, Socket: %s)\n",
		d.sessionID, os.Getpid(), d.socketPath)

	return nil
}

// Stop stops the driver process gracefully
func (d *Driver) Stop() error {
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

// RunDriverMain is the main entry point for the driver process
func RunDriverMain() error {
	// Parse command line arguments
	if len(os.Args) < 4 {
		return fmt.Errorf("usage: %s --driver <session-id> <socket-path> [--headless]", os.Args[0])
	}

	var sessionID, socketPath string
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
func StartDriverProcess(sessionID, socketPath string, headless bool) (int, error) {
	// Build command
	cmd := exec.Command(os.Args[0], "--driver", sessionID, "--socket", socketPath)
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