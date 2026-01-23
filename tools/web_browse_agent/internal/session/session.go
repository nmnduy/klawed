package session

import (
	"fmt"
	"os"
	"sync"
	"time"

	"github.com/google/uuid"
)

// Session represents a persistent browser session
type Session struct {
	// Metadata
	ID          string    `json:"session_id"`
	CreatedAt   time.Time `json:"created_at"`
	LastUsed    time.Time `json:"last_used"`
	Headless    bool      `json:"headless"`
	UserDataDir string    `json:"user_data_dir,omitempty"` // Path to persistent browser data
	ActiveTabID string    `json:"active_tab_id,omitempty"`

	// Driver process info
	DriverPID        int    `json:"driver_pid,omitempty"`
	DriverSocketPath string `json:"driver_socket_path,omitempty"`

	// Internal state
	mu       sync.RWMutex
	filePath string
	lockFile *os.File
}

// SessionConfig holds configuration for creating a new session
type SessionConfig struct {
	Headless    bool
	UserDataDir string // Path to persistent browser data directory (empty = no persistence)
}

// SessionInfo is a simplified view of session metadata for display
type SessionInfo struct {
	ID          string    `json:"session_id"`
	CreatedAt   time.Time `json:"created_at"`
	LastUsed    time.Time `json:"last_used"`
	Headless    bool      `json:"headless"`
	ActiveTabID string    `json:"active_tab_id,omitempty"`
	DriverPID   int       `json:"driver_pid,omitempty"`
	IsAlive     bool      `json:"is_alive"`
	Age         string    `json:"age"`
	LastUsedAgo string    `json:"last_used_ago"`
}

// New creates a new session with the given ID and configuration
func New(id string, config SessionConfig) (*Session, error) {
	if id == "" {
		id = generateSessionID()
	}

	sess := &Session{
		ID:          id,
		CreatedAt:   time.Now(),
		LastUsed:    time.Now(),
		Headless:    config.Headless,
		UserDataDir: config.UserDataDir,
	}

	return sess, nil
}

// GetOrCreateSession retrieves an existing session or creates a new one
// This is a wrapper around the registry's GetOrCreate for backward compatibility
func GetOrCreateSession(sessionID string, headless bool) (*Session, error) {
	return GetOrCreate(sessionID, headless)
}

// Close cleans up session resources
func (s *Session) Close() error {
	// TODO: Implement cleanup
	return nil
}

// UpdateLastUsed updates the last used timestamp
func (s *Session) UpdateLastUsed() {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.LastUsed = time.Now()
}

// SetActiveTab sets the active tab ID
func (s *Session) SetActiveTab(tabID string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.ActiveTabID = tabID
}

// SetDriverInfo sets the driver process information
func (s *Session) SetDriverInfo(pid int, socketPath string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.DriverPID = pid
	s.DriverSocketPath = socketPath
}

// Info returns a simplified view of session metadata
func (s *Session) Info() SessionInfo {
	s.mu.RLock()
	defer s.mu.RUnlock()

	now := time.Now()
	age := now.Sub(s.CreatedAt)
	lastUsedAgo := now.Sub(s.LastUsed)

	return SessionInfo{
		ID:          s.ID,
		CreatedAt:   s.CreatedAt,
		LastUsed:    s.LastUsed,
		Headless:    s.Headless,
		ActiveTabID: s.ActiveTabID,
		DriverPID:   s.DriverPID,
		IsAlive:     s.isDriverAlive(),
		Age:         formatDuration(age),
		LastUsedAgo: formatDuration(lastUsedAgo),
	}
}

// isDriverAlive checks if the driver process is still running
func (s *Session) isDriverAlive() bool {
	if s.DriverPID == 0 {
		return false
	}

	// Check if process exists
	process, err := os.FindProcess(s.DriverPID)
	if err != nil {
		return false
	}

	// Send signal 0 to check if process exists
	err = process.Signal(os.Signal(nil))
	return err == nil
}

// generateSessionID generates a new session ID
func generateSessionID() string {
	return uuid.New().String()[:8]
}

// formatDuration formats a duration in a human-readable way
func formatDuration(d time.Duration) string {
	if d < time.Minute {
		return fmt.Sprintf("%ds", int(d.Seconds()))
	}
	if d < time.Hour {
		return fmt.Sprintf("%dm", int(d.Minutes()))
	}
	if d < 24*time.Hour {
		return fmt.Sprintf("%dh", int(d.Hours()))
	}
	return fmt.Sprintf("%dd", int(d.Hours()/24))
}
