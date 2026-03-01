/*
 * Token Usage Database - Dedicated SQLite3-based token usage tracking
 *
 * This module provides functionality to persist token usage statistics
 * in a separate SQLite database file from the main API call logs.
 * This separation allows for:
 *   - Independent lifecycle management of token data
 *   - Smaller database files for faster queries
 *   - Easier backup/export of token usage data
 */

#ifndef TOKEN_USAGE_DB_H
#define TOKEN_USAGE_DB_H

#include <sqlite3.h>
#include <pthread.h>

// Database schema for token_usage table:
//
// CREATE TABLE IF NOT EXISTS token_usage (
//     id INTEGER PRIMARY KEY AUTOINCREMENT,
//     api_call_id INTEGER,               -- Optional reference (not a foreign key)
//     session_id TEXT,                   -- Unique session identifier
//     prompt_tokens INTEGER DEFAULT 0,   -- Number of prompt tokens used
//     completion_tokens INTEGER DEFAULT 0, -- Number of completion tokens used
//     total_tokens INTEGER DEFAULT 0,    -- Total tokens used
//     cached_tokens INTEGER DEFAULT 0,   -- Number of cached tokens
//     prompt_cache_hit_tokens INTEGER DEFAULT 0,
//     prompt_cache_miss_tokens INTEGER DEFAULT 0,
//     created_at INTEGER NOT NULL        -- Unix timestamp
// );

// Token usage database handle
typedef struct TokenUsageDB {
    sqlite3 *db;
    char *db_path;
    pthread_mutex_t mutex;  // Mutex for thread-safe SQLite access
} TokenUsageDB;

// Initialize token usage database
// Opens/creates SQLite database and ensures schema is up to date
//
// Parameters:
//   db_path: Path to SQLite database file (NULL = use default location)
//            Default location priority:
//              1. $KLAWED_TOKEN_USAGE_DB_PATH (environment variable)
//              2. $KLAWED_DATA_DIR/token_usage.db
//              3. ./.klawed/token_usage.db
//
// Returns:
//   TokenUsageDB* on success, NULL on failure
TokenUsageDB* token_usage_db_init(const char *db_path);

// Close token usage database and free resources
void token_usage_db_close(TokenUsageDB *db);

// Get default database path
// Returns: Newly allocated string with default path (caller must free)
char* token_usage_db_get_default_path(void);

// Log token usage to the database
//
// Parameters:
//   db: Token usage database handle
//   api_call_id: Associated API call ID (can be 0 if not tracked)
//   session_id: Session identifier (NULL if not available)
//   prompt_tokens: Number of prompt tokens
//   completion_tokens: Number of completion tokens
//   total_tokens: Total tokens
//   cached_tokens: Number of cached tokens
//   prompt_cache_hit_tokens: Number of cache hit tokens
//   prompt_cache_miss_tokens: Number of cache miss tokens
//
// Returns:
//   0 on success, -1 on failure
int token_usage_db_log(
    TokenUsageDB *db,
    long long api_call_id,
    const char *session_id,
    int prompt_tokens,
    int completion_tokens,
    int total_tokens,
    int cached_tokens,
    int prompt_cache_hit_tokens,
    int prompt_cache_miss_tokens
);

// Get total token usage for a session
//
// Parameters:
//   db: Token usage database handle
//   session_id: Session identifier (NULL = get totals for all sessions)
//   prompt_tokens: Output parameter for total prompt tokens
//   completion_tokens: Output parameter for total completion tokens
//   cached_tokens: Output parameter for total cached tokens
//
// Returns:
//   0 on success, -1 on error
int token_usage_db_get_session_usage(
    TokenUsageDB *db,
    const char *session_id,
    int *prompt_tokens,
    int *completion_tokens,
    int *cached_tokens
);

// Get cumulative token totals for a session (sums all records)
// Use this for session summary reports, not for current context size
//
// Parameters:
//   db: Token usage database handle
//   session_id: Session identifier (NULL = get totals for all sessions)
//   prompt_tokens: Output parameter for cumulative prompt tokens
//   completion_tokens: Output parameter for cumulative completion tokens
//   cached_tokens: Output parameter for cumulative cached tokens
//
// Returns:
//   0 on success, -1 on error
int token_usage_db_get_session_totals(
    TokenUsageDB *db,
    const char *session_id,
    int *prompt_tokens,
    int *completion_tokens,
    int *cached_tokens
);

// Get prompt tokens from the most recent API call in the session
//
// Parameters:
//   db: Token usage database handle
//   session_id: Session identifier
//   prompt_tokens: Output parameter for prompt tokens from last call
//
// Returns:
//   0 on success, -1 on error
int token_usage_db_get_last_prompt_tokens(
    TokenUsageDB *db,
    const char *session_id,
    int *prompt_tokens
);

// Get cached tokens from the most recent API call in the session
//
// Parameters:
//   db: Token usage database handle
//   session_id: Session identifier
//   cached_tokens: Output parameter for cached tokens from last call
//
// Returns:
//   0 on success, -1 on error
int token_usage_db_get_last_cached_tokens(
    TokenUsageDB *db,
    const char *session_id,
    int *cached_tokens
);

// Rotation functions for managing database size and age

// Delete records older than specified number of days
//
// Returns: Number of records deleted, or -1 on error
int token_usage_db_rotate_by_age(TokenUsageDB *db, int days);

// Keep only the most recent N records
//
// Returns: Number of records deleted, or -1 on error
int token_usage_db_rotate_by_count(TokenUsageDB *db, int max_records);

// Get current database file size in bytes
//
// Returns: Database size in bytes, or -1 on error
long token_usage_db_get_size(TokenUsageDB *db);

// Run VACUUM to reclaim space
//
// Returns: 0 on success, -1 on error
int token_usage_db_vacuum(TokenUsageDB *db);

// Automatically apply rotation rules based on environment variables
// Checks KLAWED_TOKEN_USAGE_DB_MAX_DAYS, KLAWED_TOKEN_USAGE_DB_MAX_RECORDS
//
// Returns: 0 on success, -1 on error
int token_usage_db_auto_rotate(TokenUsageDB *db);

// Extract token usage from API response JSON
//
// Parameters:
//   response_json: Raw JSON response string from API
//   prompt_tokens: Output parameter for prompt tokens
//   completion_tokens: Output parameter for completion tokens
//   total_tokens: Output parameter for total tokens
//   cached_tokens: Output parameter for cached tokens
//   prompt_cache_hit_tokens: Output parameter for cache hit tokens
//   prompt_cache_miss_tokens: Output parameter for cache miss tokens
//
// Returns:
//   0 on success, -1 if no token usage data found
int token_usage_extract_from_response(
    const char *response_json,
    int *prompt_tokens,
    int *completion_tokens,
    int *total_tokens,
    int *cached_tokens,
    int *prompt_cache_hit_tokens,
    int *prompt_cache_miss_tokens
);

#endif // TOKEN_USAGE_DB_H
