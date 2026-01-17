package persistence

import (
	"database/sql"
	"encoding/json"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"time"

	_ "modernc.org/sqlite"
)

// DB represents a SQLite database connection for API call logging
type DB struct {
	db *sql.DB
}

// APICall represents a logged API call
type APICall struct {
	Timestamp    time.Time `json:"timestamp"`
	SessionID    string    `json:"session_id,omitempty"`
	APIBaseURL   string    `json:"api_base_url"`
	RequestJSON  string    `json:"request_json"`
	HeadersJSON  string    `json:"headers_json,omitempty"`
	ResponseJSON string    `json:"response_json,omitempty"`
	Model        string    `json:"model"`
	Status       string    `json:"status"` // "success" or "error"
	HTTPStatus   int       `json:"http_status,omitempty"`
	ErrorMessage string    `json:"error_message,omitempty"`
	DurationMS   int64     `json:"duration_ms"`
	ToolCount    int       `json:"tool_count,omitempty"`
}

// NewDB creates or opens a SQLite database for API call logging
func NewDB(dbPath string) (*DB, error) {
	// Ensure directory exists
	dir := filepath.Dir(dbPath)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return nil, fmt.Errorf("failed to create directory %s: %w", dir, err)
	}

	db, err := sql.Open("sqlite", dbPath)
	if err != nil {
		return nil, fmt.Errorf("failed to open database %s: %w", dbPath, err)
	}

	// Enable WAL mode for better concurrency
	if _, err := db.Exec("PRAGMA journal_mode = WAL;"); err != nil {
		log.Printf("Warning: failed to enable WAL mode: %v", err)
	}

	// Create tables if they don't exist
	if err := createTables(db); err != nil {
		db.Close()
		return nil, fmt.Errorf("failed to create tables: %w", err)
	}

	return &DB{db: db}, nil
}

// createTables creates the necessary tables for API call logging
func createTables(db *sql.DB) error {
	// Create api_calls table
	_, err := db.Exec(`
		CREATE TABLE IF NOT EXISTS api_calls (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			timestamp TEXT NOT NULL,
			session_id TEXT,
			api_base_url TEXT NOT NULL,
			request_json TEXT NOT NULL,
			headers_json TEXT,
			response_json TEXT,
			model TEXT NOT NULL,
			status TEXT NOT NULL,
			http_status INTEGER,
			error_message TEXT,
			duration_ms INTEGER,
			tool_count INTEGER DEFAULT 0,
			created_at INTEGER NOT NULL
		)
	`)
	if err != nil {
		return fmt.Errorf("failed to create api_calls table: %w", err)
	}

	// Create token_usage table
	_, err = db.Exec(`
		CREATE TABLE IF NOT EXISTS token_usage (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			api_call_id INTEGER NOT NULL,
			session_id TEXT,
			prompt_tokens INTEGER DEFAULT 0,
			completion_tokens INTEGER DEFAULT 0,
			total_tokens INTEGER DEFAULT 0,
			cached_tokens INTEGER DEFAULT 0,
			prompt_cache_hit_tokens INTEGER DEFAULT 0,
			prompt_cache_miss_tokens INTEGER DEFAULT 0,
			created_at INTEGER NOT NULL,
			FOREIGN KEY (api_call_id) REFERENCES api_calls(id) ON DELETE CASCADE
		)
	`)
	if err != nil {
		return fmt.Errorf("failed to create token_usage table: %w", err)
	}

	// Create indexes for better query performance
	_, err = db.Exec("CREATE INDEX IF NOT EXISTS idx_api_calls_session_id ON api_calls(session_id)")
	if err != nil {
		return fmt.Errorf("failed to create session_id index: %w", err)
	}

	_, err = db.Exec("CREATE INDEX IF NOT EXISTS idx_api_calls_created_at ON api_calls(created_at)")
	if err != nil {
		return fmt.Errorf("failed to create created_at index: %w", err)
	}

	_, err = db.Exec("CREATE INDEX IF NOT EXISTS idx_token_usage_api_call_id ON token_usage(api_call_id)")
	if err != nil {
		return fmt.Errorf("failed to create api_call_id index: %w", err)
	}

	return nil
}

// LogAPICall logs an API call to the database
func (d *DB) LogAPICall(call *APICall) error {
	if call == nil {
		return fmt.Errorf("call cannot be nil")
	}

	// Convert timestamp to ISO 8601 format
	timestampStr := call.Timestamp.Format("2006-01-02 15:04:05")
	createdAt := call.Timestamp.Unix()

	// Insert into api_calls table
	result, err := d.db.Exec(`
		INSERT INTO api_calls (
			timestamp, session_id, api_base_url, request_json, headers_json,
			response_json, model, status, http_status, error_message,
			duration_ms, tool_count, created_at
		) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
		timestampStr, call.SessionID, call.APIBaseURL, call.RequestJSON,
		call.HeadersJSON, call.ResponseJSON, call.Model, call.Status,
		call.HTTPStatus, call.ErrorMessage, call.DurationMS, call.ToolCount,
		createdAt,
	)
	if err != nil {
		return fmt.Errorf("failed to insert API call: %w", err)
	}

	// If we have a response JSON, try to extract and log token usage
	if call.Status == "success" && call.ResponseJSON != "" {
		apiCallID, err := result.LastInsertId()
		if err != nil {
			log.Printf("Warning: failed to get last insert ID for token usage: %v", err)
			return nil
		}

		// Try to extract token usage from response
		if tokenUsage, err := extractTokenUsage(call.ResponseJSON); err == nil {
			err = d.logTokenUsage(apiCallID, call.SessionID, tokenUsage, createdAt)
			if err != nil {
				log.Printf("Warning: failed to log token usage: %v", err)
			}
		}
	}

	return nil
}

// TokenUsage represents token usage statistics from an API response
type TokenUsage struct {
	PromptTokens          int `json:"prompt_tokens"`
	CompletionTokens      int `json:"completion_tokens"`
	TotalTokens           int `json:"total_tokens"`
	CachedTokens          int `json:"cached_tokens"`
	PromptCacheHitTokens  int `json:"prompt_cache_hit_tokens"`
	PromptCacheMissTokens int `json:"prompt_cache_miss_tokens"`
}

// extractTokenUsage extracts token usage from API response JSON
func extractTokenUsage(responseJSON string) (*TokenUsage, error) {
	var data map[string]interface{}
	if err := json.Unmarshal([]byte(responseJSON), &data); err != nil {
		return nil, fmt.Errorf("failed to parse response JSON: %w", err)
	}

	// Check for usage object
	usageObj, ok := data["usage"].(map[string]interface{})
	if !ok {
		return nil, fmt.Errorf("no usage object in response")
	}

	usage := &TokenUsage{}

	// Try different field names for token counts
	if val, ok := usageObj["prompt_tokens"].(float64); ok {
		usage.PromptTokens = int(val)
	} else if val, ok := usageObj["input_tokens"].(float64); ok {
		usage.PromptTokens = int(val)
	}

	if val, ok := usageObj["completion_tokens"].(float64); ok {
		usage.CompletionTokens = int(val)
	} else if val, ok := usageObj["output_tokens"].(float64); ok {
		usage.CompletionTokens = int(val)
	}

	if val, ok := usageObj["total_tokens"].(float64); ok {
		usage.TotalTokens = int(val)
	}

	// Check for cached tokens (OpenAI-specific)
	if val, ok := usageObj["cached_tokens"].(float64); ok {
		usage.CachedTokens = int(val)
	}

	// Check for prompt cache details
	if cacheDetails, ok := usageObj["prompt_tokens_details"].(map[string]interface{}); ok {
		if val, ok := cacheDetails["cached_tokens"].(float64); ok {
			usage.CachedTokens = int(val)
		}
		if val, ok := cacheDetails["prompt_cache_hit_tokens"].(float64); ok {
			usage.PromptCacheHitTokens = int(val)
		}
		if val, ok := cacheDetails["prompt_cache_miss_tokens"].(float64); ok {
			usage.PromptCacheMissTokens = int(val)
		}
	}

	return usage, nil
}

// logTokenUsage logs token usage to the database
func (d *DB) logTokenUsage(apiCallID int64, sessionID string, usage *TokenUsage, createdAt int64) error {
	_, err := d.db.Exec(`
		INSERT INTO token_usage (
			api_call_id, session_id, prompt_tokens, completion_tokens,
			total_tokens, cached_tokens, prompt_cache_hit_tokens,
			prompt_cache_miss_tokens, created_at
		) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)`,
		apiCallID, sessionID, usage.PromptTokens, usage.CompletionTokens,
		usage.TotalTokens, usage.CachedTokens, usage.PromptCacheHitTokens,
		usage.PromptCacheMissTokens, createdAt,
	)
	return err
}

// Close closes the database connection
func (d *DB) Close() error {
	if d.db != nil {
		return d.db.Close()
	}
	return nil
}

// GetDefaultDBPath returns the default path for the SQLite database
func GetDefaultDBPath() string {
	// Check environment variables first
	if path := os.Getenv("WEB_BROWSE_AGENT_DB_PATH"); path != "" {
		return path
	}
	// Also check KLAWED_DB_PATH for consistency with klawed
	if path := os.Getenv("KLAWED_DB_PATH"); path != "" {
		return path
	}

	// Use project-local directory
	homeDir, err := os.UserHomeDir()
	if err != nil {
		homeDir = "."
	}

	// Try .klawed directory in current working directory first
	if cwd, err := os.Getwd(); err == nil {
		localPath := filepath.Join(cwd, ".klawed", "web_browse_agent_api_calls.db")
		if _, err := os.Stat(filepath.Dir(localPath)); err == nil {
			return localPath
		}
	}

	// Fall back to user's home directory
	return filepath.Join(homeDir, ".klawed", "web_browse_agent_api_calls.db")
}