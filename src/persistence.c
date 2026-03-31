/*
 * Persistence Layer Implementation - SQLite3-based API logging
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <cjson/cJSON.h>
#include "persistence.h"
#include "token_usage_db.h"
#include "migrations.h"
#include "logger.h"
#include "data_dir.h"
#include "macos_sqlite_fix.h"

// Get total token usage for a session
// Delegates to the separate token_usage database
int persistence_get_session_token_usage(
    PersistenceDB *db,
    const char *session_id,
    int64_t *prompt_tokens,
    int64_t *completion_tokens,
    int64_t *cached_tokens
) {
    if (!db || !prompt_tokens || !completion_tokens || !cached_tokens) {
        LOG_ERROR("Invalid parameters to persistence_get_session_token_usage");
        return -1;
    }

    // Delegate to the separate token usage database
    if (db->token_usage_db) {
        return token_usage_db_get_session_usage(
            db->token_usage_db, session_id, prompt_tokens, completion_tokens, cached_tokens);
    }

    // No token usage database available
    *prompt_tokens = 0;
    *completion_tokens = 0;
    *cached_tokens = 0;
    LOG_DEBUG("Token usage database not initialized");
    return 0;
}

// Get cumulative token totals for a session (sums all records)
// Delegates to the separate token_usage database
int persistence_get_session_token_totals(
    PersistenceDB *db,
    const char *session_id,
    int64_t *prompt_tokens,
    int64_t *completion_tokens,
    int64_t *cached_tokens
) {
    if (!db || !prompt_tokens || !completion_tokens || !cached_tokens) {
        LOG_ERROR("Invalid parameters to persistence_get_session_token_totals");
        return -1;
    }

    // Delegate to the separate token usage database
    if (db->token_usage_db) {
        return token_usage_db_get_session_totals(
            db->token_usage_db, session_id, prompt_tokens, completion_tokens, cached_tokens);
    }

    // No token usage database available
    *prompt_tokens = 0;
    *completion_tokens = 0;
    *cached_tokens = 0;
    LOG_DEBUG("Token usage database not initialized");
    return 0;
}

// Get prompt tokens from the most recent API call in the session
// Delegates to the separate token_usage database
int persistence_get_last_prompt_tokens(
    PersistenceDB *db,
    const char *session_id,
    int64_t *prompt_tokens
) {
    if (!db || !prompt_tokens) {
        LOG_ERROR("Invalid parameters to persistence_get_last_prompt_tokens");
        return -1;
    }

    // Delegate to the separate token usage database
    if (db->token_usage_db) {
        return token_usage_db_get_last_prompt_tokens(db->token_usage_db, session_id, prompt_tokens);
    }

    // No token usage database available
    *prompt_tokens = 0;
    LOG_DEBUG("Token usage database not initialized");
    return 0;
}

// SQL schema for the api_calls table (token_usage is now in a separate database)
static const char *SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS api_calls ("
    "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    timestamp TEXT NOT NULL,"
    "    session_id TEXT,"
    "    api_base_url TEXT NOT NULL,"
    "    request_json TEXT NOT NULL,"
    "    headers_json TEXT,"
    "    response_json TEXT,"
    "    model TEXT NOT NULL,"
    "    status TEXT NOT NULL,"
    "    http_status INTEGER,"
    "    error_message TEXT,"
    "    duration_ms INTEGER,"
    "    tool_count INTEGER DEFAULT 0,"
    "    created_at INTEGER NOT NULL"
    ");";

// SQL for creating indexes for faster queries
static const char *INDEX_SQL =
    "CREATE INDEX IF NOT EXISTS idx_api_calls_timestamp ON api_calls(timestamp);"
    "CREATE INDEX IF NOT EXISTS idx_api_calls_session_id ON api_calls(session_id);";

// Get default database path
// Priority: $KLAWED_DB_PATH > $KLAWED_DATA_DIR/api_calls.db > ./.klawed/api_calls.db > $XDG_DATA_HOME/klawed/api_calls.db > ~/.local/share/klawed/api_calls.db
char* persistence_get_default_path(void) {
    char *path = NULL;

    // Check environment variable first
    const char *env_path = getenv("KLAWED_DB_PATH");
    if (env_path && env_path[0] != '\0') {
        path = strdup(env_path);
        return path;
    }

    // Try project-local data directory (respects KLAWED_DATA_DIR)
    if (data_dir_ensure(NULL) == 0) {
        char db_path[PATH_MAX];
        if (data_dir_build_path(db_path, sizeof(db_path), "api_calls.db") == 0) {
            return strdup(db_path);
        }
    }

    // Try XDG_DATA_HOME
    const char *xdg_data = getenv("XDG_DATA_HOME");
    if (xdg_data && xdg_data[0] != '\0') {
        path = malloc(PATH_MAX);
        if (path) {
            snprintf(path, PATH_MAX, "%s/klawed/api_calls.db", xdg_data);
            return path;
        }
    }

    // Fallback to ~/.local/share/klawed
    const char *home = getenv("HOME");
    if (home && home[0] != '\0') {
        path = malloc(PATH_MAX);
        if (path) {
            snprintf(path, PATH_MAX, "%s/.local/share/klawed/api_calls.db", home);
            return path;
        }
    }

    // Last resort: current directory
    return strdup("./api_calls.db");
}

// Create directory path recursively (mkdir -p)
static int mkdir_recursive(const char *path) {
    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);

    // Remove trailing slash
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    // Create each directory in the path
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    // Create final directory
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

// Initialize persistence layer
PersistenceDB* persistence_init(const char *db_path) {
    PersistenceDB *pdb = calloc(1, sizeof(PersistenceDB));
    if (!pdb) {
        LOG_ERROR("Failed to allocate PersistenceDB");
        return NULL;
    }

    // Initialize mutex for thread-safe access
    if (pthread_mutex_init(&pdb->mutex, NULL) != 0) {
        LOG_ERROR("Failed to initialize PersistenceDB mutex");
        free(pdb);
        return NULL;
    }

    // Determine database path
    if (db_path && db_path[0] != '\0') {
        pdb->db_path = strdup(db_path);
    } else {
        pdb->db_path = persistence_get_default_path();
    }

    if (!pdb->db_path) {
        LOG_ERROR("Failed to determine database path");
        free(pdb);
        return NULL;
    }

    // Create directory structure if it doesn't exist
    char dir_path[PATH_MAX];
    snprintf(dir_path, sizeof(dir_path), "%s", pdb->db_path);
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        if (mkdir_recursive(dir_path) != 0) {
            LOG_WARN("Failed to create directory %s: %s", dir_path, strerror(errno));
            // Continue anyway - maybe the directory exists or the path is valid
        }
    }

    // Open/create database with macOS-specific flags if needed
    int open_flags = macos_sqlite_open_flags();
    int rc = sqlite3_open_v2(pdb->db_path, &pdb->db, open_flags, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to open database %s: %s", pdb->db_path, sqlite3_errmsg(pdb->db));
        free(pdb->db_path);
        free(pdb);
        return NULL;
    }

    // Apply macOS-specific fixes
    if (macos_sqlite_needs_fixes()) {
        macos_sqlite_apply_fixes(pdb->db);
    }

    // Configure database for better concurrency and performance
    char *err_msg = NULL;

    // Enable WAL mode for better concurrency
    rc = sqlite3_exec(pdb->db, "PRAGMA journal_mode=WAL;", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_WARN("Failed to enable WAL mode: %s", err_msg);
        sqlite3_free(err_msg);
        err_msg = NULL;
        // Non-fatal, continue anyway
    }

    // Set synchronous mode to NORMAL for better performance
    rc = sqlite3_exec(pdb->db, "PRAGMA synchronous=NORMAL;", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_WARN("Failed to set synchronous mode: %s", err_msg);
        sqlite3_free(err_msg);
        err_msg = NULL;
        // Non-fatal, continue anyway
    }

    // Set busy timeout (shorter on macOS to prevent hangs)
    int busy_timeout_ms = macos_sqlite_busy_timeout_ms();
    sqlite3_busy_timeout(pdb->db, busy_timeout_ms);
    LOG_DEBUG("[Persistence] SQLite busy timeout set to %d ms", busy_timeout_ms);

    // Create schema
    rc = sqlite3_exec(pdb->db, SCHEMA_SQL, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to create schema: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(pdb->db);
        free(pdb->db_path);
        free(pdb);
        return NULL;
    }

    // Create index
    rc = sqlite3_exec(pdb->db, INDEX_SQL, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_WARN("Failed to create index: %s", err_msg);
        sqlite3_free(err_msg);
        // Non-fatal, continue
    }

    // Apply any pending migrations
    if (migrations_apply(pdb->db) != 0) {
        LOG_ERROR("Failed to apply migrations");
        sqlite3_close(pdb->db);
        free(pdb->db_path);
        free(pdb);
        return NULL;
    }

    // Initialize the separate token usage database
    pdb->token_usage_db = token_usage_db_init(NULL);
    if (!pdb->token_usage_db) {
        LOG_WARN("Failed to initialize token usage database - token tracking will be disabled");
        // Non-fatal: continue without token usage tracking
    }

    // Apply automatic rotation rules if enabled
    persistence_auto_rotate(pdb);

    return pdb;
}

// Get current timestamp in ISO 8601 format
static char* get_iso_timestamp(void) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    char *timestamp = malloc(32);
    if (!timestamp) return NULL;

    strftime(timestamp, 32, "%Y-%m-%d %H:%M:%S", tm_info);
    return timestamp;
}

// Log an API call to the database
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
) {
    if (!db || !db->db || !api_base_url || !request_json || !model || !status) {
        LOG_ERROR("Invalid parameters to persistence_log_api_call");
        return -1;
    }

    // Get timestamp (before locking, as it doesn't use the DB)
    char *timestamp = get_iso_timestamp();
    if (!timestamp) {
        LOG_ERROR("Failed to get timestamp");
        return -1;
    }

    // Lock mutex for thread-safe SQLite access
    pthread_mutex_lock(&db->mutex);

    // Prepare SQL statement
    const char *sql =
        "INSERT INTO api_calls "
        "(timestamp, session_id, api_base_url, request_json, headers_json, response_json, model, status, "
        "http_status, error_message, duration_ms, tool_count, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare statement: %s", sqlite3_errmsg(db->db));
        pthread_mutex_unlock(&db->mutex);
        free(timestamp);
        return -1;
    }

    // Bind parameters
    time_t now = time(NULL);
    sqlite3_bind_text(stmt, 1, timestamp, -1, SQLITE_TRANSIENT);

    if (session_id) {
        sqlite3_bind_text(stmt, 2, session_id, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 2);
    }

    sqlite3_bind_text(stmt, 3, api_base_url, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, request_json, -1, SQLITE_TRANSIENT);

    if (headers_json) {
        sqlite3_bind_text(stmt, 5, headers_json, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 5);
    }

    if (response_json) {
        sqlite3_bind_text(stmt, 6, response_json, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 6);
    }

    sqlite3_bind_text(stmt, 7, model, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, status, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 9, http_status);

    if (error_message) {
        sqlite3_bind_text(stmt, 10, error_message, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 10);
    }

    sqlite3_bind_int64(stmt, 11, duration_ms);
    sqlite3_bind_int(stmt, 12, tool_count);
    sqlite3_bind_int64(stmt, 13, now);

    // Execute
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        LOG_ERROR("Failed to insert record: %s", sqlite3_errmsg(db->db));
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db->mutex);
        free(timestamp);
        return -1;
    }

    sqlite3_finalize(stmt);
    free(timestamp);

    // Get the last inserted API call ID (for reference in token usage)
    sqlite3_int64 api_call_id = sqlite3_last_insert_rowid(db->db);

    pthread_mutex_unlock(&db->mutex);

    // If this was a successful API call with response JSON, also log token usage
    // to the separate token usage database
    if (strcmp(status, "success") == 0 && response_json && db->token_usage_db) {
        LOG_DEBUG("Attempting to extract token usage from successful API response");

        int64_t prompt_tokens = 0;
        int64_t completion_tokens = 0;
        int64_t total_tokens = 0;
        int64_t cached_tokens = 0;
        int64_t prompt_cache_hit_tokens = 0;
        int64_t prompt_cache_miss_tokens = 0;

        // Extract token usage from response JSON
        int extract_result = token_usage_extract_from_response(
            response_json,
            &prompt_tokens,
            &completion_tokens,
            &total_tokens,
            &cached_tokens,
            &prompt_cache_hit_tokens,
            &prompt_cache_miss_tokens
        );

        LOG_DEBUG("Token extraction result: %d (0=success, -1=no usage field)", extract_result);

        if (extract_result == 0) {
            LOG_DEBUG("Token usage extracted: prompt=%ld, completion=%ld, total=%ld, cached=%ld",
                     (long)prompt_tokens, (long)completion_tokens, (long)total_tokens, (long)cached_tokens);

            // Warn if session_id is NULL - this should not happen in normal operation
            if (!session_id) {
                LOG_WARN("Creating token usage record with NULL session_id for api_call_id=%lld. "
                         "This indicates a potential bug in session tracking.", (long long)api_call_id);
            }

            // Log to the separate token usage database
            int log_result = token_usage_db_log(
                db->token_usage_db,
                api_call_id,
                session_id,
                prompt_tokens,
                completion_tokens,
                total_tokens,
                cached_tokens,
                prompt_cache_hit_tokens,
                prompt_cache_miss_tokens
            );

            if (log_result != 0) {
                LOG_WARN("Failed to log token usage to separate database");
            } else {
                LOG_DEBUG("Token usage logged to separate database for API call ID %lld",
                         (long long)api_call_id);
            }
        } else {
            LOG_DEBUG("No token usage data found in API response or extraction failed");
        }
    } else if (strcmp(status, "success") == 0 && response_json && !db->token_usage_db) {
        LOG_DEBUG("Skipping token usage logging - token usage database not initialized");
    } else {
        LOG_DEBUG("Skipping token usage logging - status=%s, response_json=%s",
                 status, response_json ? "present" : "NULL");
    }

    return 0;
}

// Close persistence layer
void persistence_close(PersistenceDB *db) {
    if (!db) return;

    // Close the token usage database first
    if (db->token_usage_db) {
        token_usage_db_close(db->token_usage_db);
        db->token_usage_db = NULL;
    }

    // Destroy mutex
    pthread_mutex_destroy(&db->mutex);

    if (db->db) {
        sqlite3_close(db->db);
    }

    if (db->db_path) {
        free(db->db_path);
    }

    free(db);
}

// Rotation functions implementation

// Delete records older than specified number of days
int persistence_rotate_by_age(PersistenceDB *db, int days) {
    if (!db || !db->db || days < 0) {
        LOG_ERROR("Invalid parameters to persistence_rotate_by_age");
        return -1;
    }

    if (days == 0) {
        // 0 means unlimited, don't delete anything
        return 0;
    }

    // Lock mutex for thread-safe SQLite access
    pthread_mutex_lock(&db->mutex);

    // Calculate cutoff timestamp (current time - days * 86400 seconds)
    time_t now = time(NULL);
    time_t cutoff = now - (days * 86400);

    const char *sql = "DELETE FROM api_calls WHERE created_at < ?;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare delete statement: %s", sqlite3_errmsg(db->db));
        pthread_mutex_unlock(&db->mutex);
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, cutoff);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        LOG_ERROR("Failed to delete old records: %s", sqlite3_errmsg(db->db));
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db->mutex);
        return -1;
    }

    int deleted = sqlite3_changes(db->db);
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&db->mutex);

    if (deleted > 0) {
        LOG_INFO("Rotated database: deleted %d records older than %d days", deleted, days);
    }

    return deleted;
}

// Keep only the most recent N records
int persistence_rotate_by_count(PersistenceDB *db, int max_records) {
    if (!db || !db->db || max_records < 0) {
        LOG_ERROR("Invalid parameters to persistence_rotate_by_count");
        return -1;
    }

    if (max_records == 0) {
        // 0 means unlimited, don't delete anything
        return 0;
    }

    // Lock mutex for thread-safe SQLite access
    pthread_mutex_lock(&db->mutex);

    // First, get the total count
    const char *count_sql = "SELECT COUNT(*) FROM api_calls;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->db, count_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare count statement: %s", sqlite3_errmsg(db->db));
        pthread_mutex_unlock(&db->mutex);
        return -1;
    }

    int total_records = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        total_records = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (total_records <= max_records) {
        // Nothing to delete
        pthread_mutex_unlock(&db->mutex);
        return 0;
    }

    // Delete oldest records, keeping only max_records
    // Strategy: Delete records where id is not in the top N most recent records
    const char *delete_sql =
        "DELETE FROM api_calls WHERE id NOT IN "
        "(SELECT id FROM api_calls ORDER BY created_at DESC LIMIT ?);";

    rc = sqlite3_prepare_v2(db->db, delete_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare delete statement: %s", sqlite3_errmsg(db->db));
        pthread_mutex_unlock(&db->mutex);
        return -1;
    }

    sqlite3_bind_int(stmt, 1, max_records);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        LOG_ERROR("Failed to delete excess records: %s", sqlite3_errmsg(db->db));
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db->mutex);
        return -1;
    }

    int deleted = sqlite3_changes(db->db);
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&db->mutex);

    if (deleted > 0) {
        LOG_INFO("Rotated database: deleted %d records, keeping %d most recent", deleted, max_records);
    }

    return deleted;
}

// Get current database file size in bytes
long persistence_get_db_size(PersistenceDB *db) {
    if (!db || !db->db_path) {
        LOG_ERROR("Invalid parameters to persistence_get_db_size");
        return -1;
    }

    struct stat st;
    if (stat(db->db_path, &st) != 0) {
        LOG_ERROR("Failed to stat database file %s: %s", db->db_path, strerror(errno));
        return -1;
    }

    return st.st_size;
}

// Run VACUUM to reclaim space
int persistence_vacuum(PersistenceDB *db) {
    if (!db || !db->db) {
        LOG_ERROR("Invalid parameters to persistence_vacuum");
        return -1;
    }

    // Lock mutex for thread-safe SQLite access
    pthread_mutex_lock(&db->mutex);

    char *err_msg = NULL;
    int rc = sqlite3_exec(db->db, "VACUUM;", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to vacuum database: %s", err_msg);
        sqlite3_free(err_msg);
        pthread_mutex_unlock(&db->mutex);
        return -1;
    }

    pthread_mutex_unlock(&db->mutex);

    LOG_INFO("Database vacuum completed successfully");
    return 0;
}

// Parse integer from environment variable with default
static int get_env_int(const char *name, int default_value) {
    const char *value = getenv(name);
    if (!value || value[0] == '\0') {
        return default_value;
    }

    char *endptr;
    long result = strtol(value, &endptr, 10);
    if (*endptr != '\0' || result < 0 || result > INT_MAX) {
        LOG_WARN("Invalid value for %s: '%s', using default %d", name, value, default_value);
        return default_value;
    }

    return (int)result;
}

// Automatically apply rotation rules based on environment variables
int persistence_auto_rotate(PersistenceDB *db) {
    if (!db || !db->db) {
        LOG_ERROR("Invalid parameters to persistence_auto_rotate");
        return -1;
    }

    // Check if auto-rotation is enabled (default: yes)
    const char *auto_rotate_env = getenv("KLAWED_DB_AUTO_ROTATE");
    if (auto_rotate_env && strcmp(auto_rotate_env, "0") == 0) {
        LOG_DEBUG("Auto-rotation disabled by KLAWED_DB_AUTO_ROTATE=0");
        return 0;
    }

    int total_deleted = 0;
    int need_vacuum = 0;

    // Rotate by age (default: 30 days, 0 = unlimited)
    int max_days = get_env_int("KLAWED_DB_MAX_DAYS", 30);
    if (max_days > 0) {
        int deleted = persistence_rotate_by_age(db, max_days);
        if (deleted > 0) {
            total_deleted += deleted;
            need_vacuum = 1;
        } else if (deleted < 0) {
            return -1;
        }
    }

    // Rotate by count (default: 1000 records, 0 = unlimited)
    int max_records = get_env_int("KLAWED_DB_MAX_RECORDS", 1000);
    if (max_records > 0) {
        int deleted = persistence_rotate_by_count(db, max_records);
        if (deleted > 0) {
            total_deleted += deleted;
            need_vacuum = 1;
        } else if (deleted < 0) {
            return -1;
        }
    }

    // Check size limit (default: 100 MB, 0 = unlimited)
    int max_size_mb = get_env_int("KLAWED_DB_MAX_SIZE_MB", 100);
    if (max_size_mb > 0) {
        long size_bytes = persistence_get_db_size(db);
        long max_size_bytes = max_size_mb * 1024L * 1024L;

        if (size_bytes > max_size_bytes) {
            LOG_WARN("Database size (%ld bytes) exceeds maximum (%ld bytes)",
                     size_bytes, max_size_bytes);

            // If size is exceeded, try more aggressive count-based rotation
            // Delete oldest 25% of records
            const char *count_sql = "SELECT COUNT(*) FROM api_calls;";
            sqlite3_stmt *stmt;

            // Lock mutex for thread-safe SQLite access
            pthread_mutex_lock(&db->mutex);
            int rc = sqlite3_prepare_v2(db->db, count_sql, -1, &stmt, NULL);
            int current_count = 0;
            if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
                current_count = sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
            pthread_mutex_unlock(&db->mutex);

            int target_count = (current_count * 3) / 4; // Keep 75%

            if (target_count > 0) {
                int deleted = persistence_rotate_by_count(db, target_count);
                if (deleted > 0) {
                    total_deleted += deleted;
                    need_vacuum = 1;
                }
            }
        }
    }

    // Run VACUUM if we deleted anything
    if (need_vacuum) {
        persistence_vacuum(db);
    }

    if (total_deleted > 0) {
        LOG_INFO("Auto-rotation completed: deleted %d total records", total_deleted);
    }

    return 0;
}
