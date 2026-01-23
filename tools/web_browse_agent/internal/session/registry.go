package session

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sync"
	"time"

	"github.com/spf13/viper"
)

// Registry manages persistent sessions on disk
type Registry struct {
	sessionsDir string
	lockDir     string
	mu          sync.RWMutex
}

// Config holds registry configuration
type Config struct {
	SessionsDir     string
	IdleTTL         time.Duration // Time to live for idle sessions
	CleanupInterval time.Duration // How often to run cleanup
	MaxSessions     int           // Maximum number of sessions to keep
}

// DefaultConfig returns the default registry configuration
func DefaultConfig() Config {
	return Config{
		SessionsDir:     filepath.Join(os.Getenv("HOME"), ".web-agent", "sessions"),
		IdleTTL:         7 * 24 * time.Hour, // 7 days
		CleanupInterval: 1 * time.Hour,      // Cleanup every hour
		MaxSessions:     100,                // Maximum 100 sessions
	}
}

// NewRegistry creates a new session registry
func NewRegistry(config Config) (*Registry, error) {
	// Create sessions directory
	if err := os.MkdirAll(config.SessionsDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to create sessions directory: %w", err)
	}

	// Create lock directory
	lockDir := filepath.Join(config.SessionsDir, ".locks")
	if err := os.MkdirAll(lockDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to create lock directory: %w", err)
	}

	registry := &Registry{
		sessionsDir: config.SessionsDir,
		lockDir:     lockDir,
	}

	// Start cleanup goroutine if TTL is set
	if config.IdleTTL > 0 && config.CleanupInterval > 0 {
		go registry.startCleanup(config.IdleTTL, config.CleanupInterval)
	}

	return registry, nil
}

// GetOrCreate retrieves an existing session or creates a new one
func (r *Registry) GetOrCreate(sessionID string, headless bool) (*Session, error) {
	r.mu.Lock()
	defer r.mu.Unlock()

	// Try to load existing session
	sess, err := r.load(sessionID)
	if err == nil {
		// Update last used time
		sess.UpdateLastUsed()
		if err := r.save(sess); err != nil {
			return nil, fmt.Errorf("failed to update session: %w", err)
		}
		return sess, nil
	}

	// Create new session if not found
	if os.IsNotExist(err) {
		// Check if persistent storage is enabled
		var userDataDir string
		if isPersistentStorageEnabled() {
			// Create session-specific user data directory
			userDataDir = filepath.Join(r.sessionsDir, sessionID, "user-data")
			if err := os.MkdirAll(userDataDir, 0755); err != nil {
				return nil, fmt.Errorf("failed to create user data directory: %w", err)
			}
		}

		sess, err = New(sessionID, SessionConfig{
			Headless:    headless,
			UserDataDir: userDataDir,
		})
		if err != nil {
			return nil, fmt.Errorf("failed to create session: %w", err)
		}

		// Save new session
		if err := r.save(sess); err != nil {
			return nil, fmt.Errorf("failed to save new session: %w", err)
		}

		return sess, nil
	}

	// Return other errors
	return nil, fmt.Errorf("failed to load session: %w", err)
}

// isPersistentStorageEnabled checks if persistent storage is enabled via environment variable
func isPersistentStorageEnabled() bool {
	// Check WEB_AGENT_PERSISTENT_STORAGE env var
	persistentStorage := os.Getenv("WEB_AGENT_PERSISTENT_STORAGE")
	if persistentStorage == "" {
		// Check viper config as fallback
		persistentStorage = viper.GetString("persistent_storage")
	}
	
	// Enable if set to "true", "1", "yes", or "on" (case-insensitive)
	switch persistentStorage {
	case "true", "1", "yes", "on", "True", "TRUE", "YES", "ON":
		return true
	default:
		return false
	}
}

// Get retrieves a session by ID
func (r *Registry) Get(sessionID string) (*Session, error) {
	r.mu.RLock()
	defer r.mu.RUnlock()
	return r.load(sessionID)
}

// Delete removes a session
func (r *Registry) Delete(sessionID string) error {
	r.mu.Lock()
	defer r.mu.Unlock()

	// Remove session file
	sessionPath := r.sessionPath(sessionID)
	if err := os.Remove(sessionPath); err != nil && !os.IsNotExist(err) {
		return fmt.Errorf("failed to delete session file: %w", err)
	}

	// Remove lock file if it exists
	lockPath := r.lockPath(sessionID)
	if err := os.Remove(lockPath); err != nil && !os.IsNotExist(err) {
		return fmt.Errorf("failed to delete lock file: %w", err)
	}

	return nil
}

// List returns all sessions
func (r *Registry) List() ([]SessionInfo, error) {
	r.mu.RLock()
	defer r.mu.RUnlock()

	// Read session files
	files, err := filepath.Glob(filepath.Join(r.sessionsDir, "*.json"))
	if err != nil {
		return nil, fmt.Errorf("failed to list session files: %w", err)
	}

	var sessions []SessionInfo
	for _, file := range files {
		// Skip lock files
		if filepath.Ext(file) == ".lock" {
			continue
		}

		// Load session
		sess, err := r.loadFromFile(file)
		if err != nil {
			// Skip corrupted files
			continue
		}

		sessions = append(sessions, sess.Info())
	}

	return sessions, nil
}

// CleanupOldSessions removes sessions older than the TTL
func (r *Registry) CleanupOldSessions(ttl time.Duration) (int, error) {
	r.mu.Lock()
	defer r.mu.Unlock()

	files, err := filepath.Glob(filepath.Join(r.sessionsDir, "*.json"))
	if err != nil {
		return 0, fmt.Errorf("failed to list session files: %w", err)
	}

	now := time.Now()
	removed := 0

	for _, file := range files {
		// Skip lock files
		if filepath.Ext(file) == ".lock" {
			continue
		}

		// Load session
		sess, err := r.loadFromFile(file)
		if err != nil {
			// Remove corrupted files
			os.Remove(file)
			removed++
			continue
		}

		// Check if session is older than TTL
		if now.Sub(sess.LastUsed) > ttl {
			// Delete session
			sessionID := filepath.Base(file)
			sessionID = sessionID[:len(sessionID)-5] // Remove .json extension

			if err := r.Delete(sessionID); err == nil {
				removed++
			}
		}
	}

	return removed, nil
}

// load loads a session from disk
func (r *Registry) load(sessionID string) (*Session, error) {
	sessionPath := r.sessionPath(sessionID)
	return r.loadFromFile(sessionPath)
}

// loadFromFile loads a session from a specific file
func (r *Registry) loadFromFile(path string) (*Session, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}

	var sess Session
	if err := json.Unmarshal(data, &sess); err != nil {
		return nil, fmt.Errorf("failed to unmarshal session: %w", err)
	}

	// Set file path
	sess.filePath = path

	return &sess, nil
}

// Save saves a session to disk (exported)
func (r *Registry) Save(sess *Session) error {
	r.mu.Lock()
	defer r.mu.Unlock()
	return r.save(sess)
}

// save saves a session to disk (internal, caller must hold lock)
func (r *Registry) save(sess *Session) error {
	sess.mu.RLock()
	defer sess.mu.RUnlock()

	// Marshal session data
	data, err := json.MarshalIndent(sess, "", "  ")
	if err != nil {
		return fmt.Errorf("failed to marshal session: %w", err)
	}

	// Ensure directory exists
	if err := os.MkdirAll(r.sessionsDir, 0755); err != nil {
		return fmt.Errorf("failed to create sessions directory: %w", err)
	}

	// Write to file
	sessionPath := r.sessionPath(sess.ID)
	if err := os.WriteFile(sessionPath, data, 0644); err != nil {
		return fmt.Errorf("failed to write session file: %w", err)
	}

	// Update file path
	sess.filePath = sessionPath

	return nil
}

// sessionPath returns the path to a session file
func (r *Registry) sessionPath(sessionID string) string {
	return filepath.Join(r.sessionsDir, sessionID+".json")
}

// lockPath returns the path to a lock file
func (r *Registry) lockPath(sessionID string) string {
	return filepath.Join(r.lockDir, sessionID+".lock")
}

// startCleanup starts a goroutine to periodically clean up old sessions
func (r *Registry) startCleanup(ttl, interval time.Duration) {
	ticker := time.NewTicker(interval)
	defer ticker.Stop()

	for range ticker.C {
		if removed, err := r.CleanupOldSessions(ttl); err == nil && removed > 0 {
			// Log cleanup activity (could be configured to use a logger)
			fmt.Printf("Cleaned up %d old sessions\n", removed)
		}
	}
}

// Global registry instance
var (
	globalRegistry *Registry
	registryOnce   sync.Once
)

// GetRegistry returns the global registry instance
func GetRegistry() (*Registry, error) {
	var err error
	registryOnce.Do(func() {
		config := DefaultConfig()

		// Override with viper config if available
		if viper.IsSet("sessions.dir") {
			config.SessionsDir = viper.GetString("sessions.dir")
		}
		if viper.IsSet("sessions.idle_ttl") {
			config.IdleTTL = viper.GetDuration("sessions.idle_ttl")
		}
		if viper.IsSet("sessions.cleanup_interval") {
			config.CleanupInterval = viper.GetDuration("sessions.cleanup_interval")
		}
		if viper.IsSet("sessions.max_sessions") {
			config.MaxSessions = viper.GetInt("sessions.max_sessions")
		}

		globalRegistry, err = NewRegistry(config)
	})

	return globalRegistry, err
}

// GetOrCreate uses the global registry
func GetOrCreate(sessionID string, headless bool) (*Session, error) {
	registry, err := GetRegistry()
	if err != nil {
		return nil, err
	}
	return registry.GetOrCreate(sessionID, headless)
}
