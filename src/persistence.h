/*
 * Persistence Layer - SQLite3-based logging of API requests/responses
 *
 * This module provides functionality to persist all API interactions with the
 * inference backend (Anthropic/OpenAI) to a SQLite database for auditing,
 * debugging, and analysis purposes.
 */

#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include <sqlite3.h>
#include <time.h>

// Database schema for api_calls table:
//
// CREATE TABLE IF NOT EXISTS api_calls (
//     id INTEGER PRIMARY KEY AUTOINCREMENT,
//     timestamp TEXT NOT NULL,           -- ISO 8601 format (YYYY-MM-DD HH:MM:SS)
//     session_id TEXT,                   -- Unique session identifier for grouping related API calls
//     api_base_url TEXT NOT NULL,        -- API endpoint URL (e.g., "https://api.openai.com/v1/chat/completions")
//     request_json TEXT NOT NULL,        -- Full JSON request sent to API
//     headers_json TEXT,                 -- JSON representation of request headers
//     response_json TEXT,                -- Full JSON response received (NULL if error)
//     model TEXT NOT NULL,               -- Model name (e.g., "claude-sonnet-4-20250514")
//     status TEXT NOT NULL,              -- 'success' or 'error'
//     http_status INTEGER,               -- HTTP status code (200, 401, 500, etc.)
//     error_message TEXT,                -- Error message if status='error'
//     duration_ms INTEGER,               -- API call duration in milliseconds
//     tool_count INTEGER DEFAULT 0,      -- Number of tool_use blocks in response
//     created_at INTEGER NOT NULL        -- Unix timestamp for indexing/sorting
// );
//
// Database schema for token_usage table:
//
// CREATE TABLE IF NOT EXISTS token_usage (
//     id INTEGER PRIMARY KEY AUTOINCREMENT,
//     api_call_id INTEGER NOT NULL,      -- Foreign key to api_calls.id
//     session_id TEXT,                   -- Unique session identifier for grouping related token usage
//     prompt_tokens INTEGER DEFAULT 0,   -- Number of prompt tokens used
//     completion_tokens INTEGER DEFAULT 0, -- Number of completion tokens used
//     total_tokens INTEGER DEFAULT 0,    -- Total tokens used
//     cached_tokens INTEGER DEFAULT 0,   -- Number of cached tokens (from prompt_tokens_details)
//     prompt_cache_hit_tokens INTEGER DEFAULT 0, -- Number of prompt cache hit tokens
//     prompt_cache_miss_tokens INTEGER DEFAULT 0, -- Number of prompt cache miss tokens
//     created_at INTEGER NOT NULL,       -- Unix timestamp for indexing/sorting
//     FOREIGN KEY (api_call_id) REFERENCES api_calls(id) ON DELETE CASCADE
// );

// Persistence handle - opaque structure for database connection
typedef struct PersistenceDB {
    sqlite3 *db;
    char *db_path;
} PersistenceDB;

// Initialize persistence layer
// Opens/creates SQLite database and ensures schema is up to date
//
// Parameters:
//   db_path: Path to SQLite database file (NULL = use default location)
//            Default location priority:
//              1. $CLAUDE_C_DB_PATH (environment variable)
//              2. ./.claude-c/api_calls.db (project-local)
//              3. $XDG_DATA_HOME/claude-c/api_calls.db
//              4. ~/.local/share/claude-c/api_calls.db
//              5. ./api_calls.db (fallback)
//
// Returns:
//   PersistenceDB* on success, NULL on failure
PersistenceDB* persistence_init(const char *db_path);

// Log an API call to the database
//
// Parameters:
//   db: Persistence database handle
//   session_id: Unique session identifier (NULL if not available)
//   api_base_url: API endpoint URL (e.g., "https://api.openai.com/v1/chat/completions")
//   request_json: Raw JSON request string (must not be NULL)
//   headers_json: Raw JSON string of request headers (NULL if not available)
//   response_json: Raw JSON response string (NULL if error occurred)
//   model: Model name used for the request
//   status: "success" or "error"
//   http_status: HTTP status code (0 if not available)
//   error_message: Error message (NULL if status="success")
//   duration_ms: Duration of API call in milliseconds
//   tool_count: Number of tool invocations in response (0 if none or error)
//
// Returns:
//   0 on success, -1 on failure
int persistence_log_api_call(
    PersistenceDB *db,
    const char *session_id,
    const char *api_base_url,
    const char *request_json,
    const char *headers_json,
    const char *response_json,
    const char *model,
    const char *status,
    int http_status,
    const char *error_message,
    long duration_ms,
    int tool_count
);

// Close persistence layer and free resources
void persistence_close(PersistenceDB *db);

// Get default database path
// Returns: Newly allocated string with default path (caller must free)
char* persistence_get_default_path(void);

// Rotation functions for managing database size and age

// Delete records older than specified number of days
//
// Parameters:
//   db: Persistence database handle
//   days: Maximum age of records to keep (records older than this will be deleted)
//
// Returns:
//   Number of records deleted, or -1 on error
int persistence_rotate_by_age(PersistenceDB *db, int days);

// Keep only the most recent N records, delete older ones
//
// Parameters:
//   db: Persistence database handle
//   max_records: Maximum number of records to keep
//
// Returns:
//   Number of records deleted, or -1 on error
int persistence_rotate_by_count(PersistenceDB *db, int max_records);

// Get current database file size in bytes
//
// Parameters:
//   db: Persistence database handle
//
// Returns:
//   Database size in bytes, or -1 on error
long persistence_get_db_size(PersistenceDB *db);

// Run VACUUM to reclaim space after deletions
//
// Parameters:
//   db: Persistence database handle
//
// Returns:
//   0 on success, -1 on error
int persistence_vacuum(PersistenceDB *db);

// Automatically apply rotation rules based on environment variables
// Checks CLAUDE_C_DB_MAX_DAYS, CLAUDE_C_DB_MAX_RECORDS, CLAUDE_C_DB_MAX_SIZE_MB
// and applies appropriate rotation strategies
//
// Parameters:
//   db: Persistence database handle
//
// Returns:
//   0 on success, -1 on error
int persistence_auto_rotate(PersistenceDB *db);

// Get total token usage for a session
//
// Parameters:
//   db: Persistence database handle
//   session_id: Session identifier (NULL = get totals for all sessions)
//   prompt_tokens: Output parameter for total prompt tokens
//   completion_tokens: Output parameter for total completion tokens
//   cached_tokens: Output parameter for total cached tokens
//
// Returns:
//   0 on success, -1 on error
int persistence_get_session_token_usage(
    PersistenceDB *db,
    const char *session_id,
    int *prompt_tokens,
    int *completion_tokens,
    int *cached_tokens
);

// Get prompt tokens from the most recent API call in the session
//
// Parameters:
//   db: Persistence database handle
//   session_id: Session identifier
//   prompt_tokens: Output parameter for prompt tokens from last call
//
// Returns:
//   0 on success, -1 on error or if no records found
int persistence_get_last_prompt_tokens(
    PersistenceDB *db,
    const char *session_id,
    int *prompt_tokens
);

// Get cached tokens from the most recent API call in the session
//
// Parameters:
//   db: Persistence database handle
//   session_id: Session identifier
//   cached_tokens: Output parameter for cached tokens from last call
//
// Returns:
//   0 on success, -1 on error or if no records found
int persistence_get_last_cached_tokens(
    PersistenceDB *db,
    const char *session_id,
    int *cached_tokens
);

#endif // PERSISTENCE_H
