/*
 * Token Usage Database Implementation - Dedicated SQLite3-based token tracking
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <cjson/cJSON.h>
#include "token_usage_db.h"
#include "token_usage_db_migrations.h"
#include "logger.h"
#include "data_dir.h"

// SQL schema for the token_usage table
static const char *SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS token_usage ("
    "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    api_call_id INTEGER,"
    "    session_id TEXT,"
    "    prompt_tokens INTEGER DEFAULT 0,"
    "    completion_tokens INTEGER DEFAULT 0,"
    "    total_tokens INTEGER DEFAULT 0,"
    "    cached_tokens INTEGER DEFAULT 0,"
    "    prompt_cache_hit_tokens INTEGER DEFAULT 0,"
    "    prompt_cache_miss_tokens INTEGER DEFAULT 0,"
    "    created_at INTEGER NOT NULL"
    ");";

// SQL for creating indexes
static const char *INDEX_SQL =
    "CREATE INDEX IF NOT EXISTS idx_token_usage_api_call_id ON token_usage(api_call_id);"
    "CREATE INDEX IF NOT EXISTS idx_token_usage_session_id ON token_usage(session_id);"
    "CREATE INDEX IF NOT EXISTS idx_token_usage_created_at ON token_usage(created_at);";

// Extract token usage from API response JSON
int token_usage_extract_from_response(
    const char *response_json,
    int *prompt_tokens,
    int *completion_tokens,
    int *total_tokens,
    int *cached_tokens,
    int *prompt_cache_hit_tokens,
    int *prompt_cache_miss_tokens
) {
    if (!response_json) {
        LOG_DEBUG("token_usage_extract: response_json is NULL");
        return -1;
    }

    cJSON *json = cJSON_Parse(response_json);
    if (!json) {
        LOG_DEBUG("token_usage_extract: failed to parse JSON response");
        return -1;
    }

    // Extract usage object
    cJSON *usage = cJSON_GetObjectItem(json, "usage");
    if (!usage) {
        LOG_DEBUG("token_usage_extract: no 'usage' object found in response");
        cJSON_Delete(json);
        return -1;
    }

    // Initialize all output parameters to 0
    *prompt_tokens = 0;
    *completion_tokens = 0;
    *total_tokens = 0;
    *cached_tokens = 0;
    *prompt_cache_hit_tokens = 0;
    *prompt_cache_miss_tokens = 0;

    // Try input_tokens first (Anthropic style)
    cJSON *prompt_tokens_json = cJSON_GetObjectItem(usage, "input_tokens");
    if (!prompt_tokens_json) {
        // Fall back to generic prompt_tokens
        prompt_tokens_json = cJSON_GetObjectItem(usage, "prompt_tokens");
    }

    cJSON *completion_tokens_json = cJSON_GetObjectItem(usage, "output_tokens");
    if (!completion_tokens_json) {
        // Fall back to generic completion_tokens
        completion_tokens_json = cJSON_GetObjectItem(usage, "completion_tokens");
    }

    cJSON *total_tokens_json = cJSON_GetObjectItem(usage, "total_tokens");

    if (prompt_tokens_json && cJSON_IsNumber(prompt_tokens_json)) {
        *prompt_tokens = prompt_tokens_json->valueint;
        LOG_DEBUG("token_usage_extract: found prompt_tokens = %d", *prompt_tokens);
    }

    if (completion_tokens_json && cJSON_IsNumber(completion_tokens_json)) {
        *completion_tokens = completion_tokens_json->valueint;
        LOG_DEBUG("token_usage_extract: found completion_tokens = %d", *completion_tokens);
    }

    if (total_tokens_json && cJSON_IsNumber(total_tokens_json)) {
        *total_tokens = total_tokens_json->valueint;
        LOG_DEBUG("token_usage_extract: found total_tokens = %d", *total_tokens);
    }

    // Extract cache-related token counts with provider-specific detection
    // Priority order: Moonshot > DeepSeek > Anthropic > General

    // 1. Moonshot-style: direct cached_tokens field
    cJSON *direct_cached_tokens = cJSON_GetObjectItem(usage, "cached_tokens");
    if (direct_cached_tokens && cJSON_IsNumber(direct_cached_tokens)) {
        *cached_tokens = direct_cached_tokens->valueint;
        LOG_DEBUG("token_usage_extract: found Moonshot-style cached_tokens = %d", *cached_tokens);
    }

    // 2. DeepSeek-style: cached_tokens inside prompt_tokens_details
    if (*cached_tokens == 0) {
        cJSON *prompt_tokens_details = cJSON_GetObjectItem(usage, "prompt_tokens_details");
        if (prompt_tokens_details) {
            cJSON *cached_tokens_json = cJSON_GetObjectItem(prompt_tokens_details, "cached_tokens");
            if (cached_tokens_json && cJSON_IsNumber(cached_tokens_json)) {
                *cached_tokens = cached_tokens_json->valueint;
                LOG_DEBUG("token_usage_extract: found DeepSeek-style cached_tokens = %d", *cached_tokens);
            }
        }
    }

    // 3. Anthropic-style: cache_read_input_tokens (counts cache hits)
    if (*cached_tokens == 0) {
        cJSON *cache_read_input_tokens = cJSON_GetObjectItem(usage, "cache_read_input_tokens");
        if (cache_read_input_tokens && cJSON_IsNumber(cache_read_input_tokens)) {
            *cached_tokens = cache_read_input_tokens->valueint;
            LOG_DEBUG("token_usage_extract: using Anthropic-style cache_read_input_tokens = %d", *cached_tokens);
        }
    }

    // Extract detailed cache metrics (DeepSeek-style)
    cJSON *cache_hit_tokens_json = cJSON_GetObjectItem(usage, "prompt_cache_hit_tokens");
    cJSON *cache_miss_tokens_json = cJSON_GetObjectItem(usage, "prompt_cache_miss_tokens");

    if (cache_hit_tokens_json && cJSON_IsNumber(cache_hit_tokens_json)) {
        *prompt_cache_hit_tokens = cache_hit_tokens_json->valueint;
        LOG_DEBUG("token_usage_extract: found prompt_cache_hit_tokens = %d", *prompt_cache_hit_tokens);
    }

    if (cache_miss_tokens_json && cJSON_IsNumber(cache_miss_tokens_json)) {
        *prompt_cache_miss_tokens = cache_miss_tokens_json->valueint;
        LOG_DEBUG("token_usage_extract: found prompt_cache_miss_tokens = %d", *prompt_cache_miss_tokens);
    }

    cJSON_Delete(json);

    LOG_DEBUG("token_usage_extract: completed (prompt=%d, completion=%d, total=%d)",
             *prompt_tokens, *completion_tokens, *total_tokens);
    return 0;
}

// Get default database path
char* token_usage_db_get_default_path(void) {
    char *path = NULL;

    // Check environment variable first
    const char *env_path = getenv("KLAWED_TOKEN_USAGE_DB_PATH");
    if (env_path && env_path[0] != '\0') {
        path = strdup(env_path);
        return path;
    }

    // Try project-local data directory (respects KLAWED_DATA_DIR)
    if (data_dir_ensure(NULL) == 0) {
        char db_path[PATH_MAX];
        if (data_dir_build_path(db_path, sizeof(db_path), "token_usage.db") == 0) {
            return strdup(db_path);
        }
    }

    // Fallback to current directory
    return strdup("./token_usage.db");
}

// Create directory path recursively
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

// Initialize token usage database
TokenUsageDB* token_usage_db_init(const char *db_path) {
    TokenUsageDB *tdb = calloc(1, sizeof(TokenUsageDB));
    if (!tdb) {
        LOG_ERROR("Failed to allocate TokenUsageDB");
        return NULL;
    }

    // Initialize mutex for thread-safe access
    if (pthread_mutex_init(&tdb->mutex, NULL) != 0) {
        LOG_ERROR("Failed to initialize TokenUsageDB mutex");
        free(tdb);
        return NULL;
    }

    // Determine database path
    if (db_path && db_path[0] != '\0') {
        tdb->db_path = strdup(db_path);
    } else {
        tdb->db_path = token_usage_db_get_default_path();
    }

    if (!tdb->db_path) {
        LOG_ERROR("Failed to determine token usage database path");
        pthread_mutex_destroy(&tdb->mutex);
        free(tdb);
        return NULL;
    }

    // Create directory structure if it doesn't exist
    char dir_path[PATH_MAX];
    snprintf(dir_path, sizeof(dir_path), "%s", tdb->db_path);
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        if (mkdir_recursive(dir_path) != 0) {
            LOG_WARN("Failed to create directory %s: %s", dir_path, strerror(errno));
        }
    }

    // Open/create database
    int rc = sqlite3_open(tdb->db_path, &tdb->db);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to open token usage database %s: %s",
                  tdb->db_path, sqlite3_errmsg(tdb->db));
        free(tdb->db_path);
        pthread_mutex_destroy(&tdb->mutex);
        free(tdb);
        return NULL;
    }

    char *err_msg = NULL;

    // Enable WAL mode for better concurrency
    rc = sqlite3_exec(tdb->db, "PRAGMA journal_mode=WAL;", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_WARN("Failed to enable WAL mode: %s", err_msg);
        sqlite3_free(err_msg);
        err_msg = NULL;
    }

    // Set synchronous mode to NORMAL for better performance
    rc = sqlite3_exec(tdb->db, "PRAGMA synchronous=NORMAL;", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_WARN("Failed to set synchronous mode: %s", err_msg);
        sqlite3_free(err_msg);
        err_msg = NULL;
    }

    // Set busy timeout to 5 seconds
    sqlite3_busy_timeout(tdb->db, 5000);

    // Create schema
    rc = sqlite3_exec(tdb->db, SCHEMA_SQL, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to create token usage schema: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(tdb->db);
        free(tdb->db_path);
        pthread_mutex_destroy(&tdb->mutex);
        free(tdb);
        return NULL;
    }

    // Create indexes
    rc = sqlite3_exec(tdb->db, INDEX_SQL, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_WARN("Failed to create indexes: %s", err_msg);
        sqlite3_free(err_msg);
    }

    // Apply any pending migrations
    if (token_usage_db_migrations_apply(tdb->db) != 0) {
        LOG_ERROR("Failed to apply token usage database migrations");
        sqlite3_close(tdb->db);
        free(tdb->db_path);
        pthread_mutex_destroy(&tdb->mutex);
        free(tdb);
        return NULL;
    }

    // Apply automatic rotation rules
    token_usage_db_auto_rotate(tdb);

    LOG_DEBUG("Token usage database initialized: %s", tdb->db_path);
    return tdb;
}

// Close token usage database
void token_usage_db_close(TokenUsageDB *db) {
    if (!db) return;

    pthread_mutex_destroy(&db->mutex);

    if (db->db) {
        sqlite3_close(db->db);
    }

    if (db->db_path) {
        free(db->db_path);
    }

    free(db);
}

// Log token usage to database
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
) {
    if (!db || !db->db) {
        LOG_ERROR("Invalid parameters to token_usage_db_log");
        return -1;
    }

    pthread_mutex_lock(&db->mutex);

    const char *sql =
        "INSERT INTO token_usage "
        "(api_call_id, session_id, prompt_tokens, completion_tokens, total_tokens, "
        "cached_tokens, prompt_cache_hit_tokens, prompt_cache_miss_tokens, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare token usage insert: %s", sqlite3_errmsg(db->db));
        pthread_mutex_unlock(&db->mutex);
        return -1;
    }

    time_t now = time(NULL);

    if (api_call_id > 0) {
        sqlite3_bind_int64(stmt, 1, api_call_id);
    } else {
        sqlite3_bind_null(stmt, 1);
    }

    if (session_id) {
        sqlite3_bind_text(stmt, 2, session_id, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 2);
    }

    sqlite3_bind_int(stmt, 3, prompt_tokens);
    sqlite3_bind_int(stmt, 4, completion_tokens);
    sqlite3_bind_int(stmt, 5, total_tokens);
    sqlite3_bind_int(stmt, 6, cached_tokens);
    sqlite3_bind_int(stmt, 7, prompt_cache_hit_tokens);
    sqlite3_bind_int(stmt, 8, prompt_cache_miss_tokens);
    sqlite3_bind_int64(stmt, 9, now);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        LOG_ERROR("Failed to insert token usage record: %s", sqlite3_errmsg(db->db));
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db->mutex);
        return -1;
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);

    LOG_DEBUG("Token usage logged: prompt=%d, completion=%d, cached=%d",
              prompt_tokens, completion_tokens, cached_tokens);
    return 0;
}

// Get session token usage
int token_usage_db_get_session_usage(
    TokenUsageDB *db,
    const char *session_id,
    int *prompt_tokens,
    int *completion_tokens,
    int *cached_tokens
) {
    if (!db || !db->db || !prompt_tokens || !completion_tokens || !cached_tokens) {
        LOG_ERROR("Invalid parameters to token_usage_db_get_session_usage");
        return -1;
    }

    *prompt_tokens = 0;
    *completion_tokens = 0;
    *cached_tokens = 0;

    pthread_mutex_lock(&db->mutex);

    const char *sql;
    sqlite3_stmt *stmt;
    int rc;

    // Get the latest token usage record (cumulative mode)
    // Order by id DESC as a secondary sort for deterministic ordering
    // when multiple records have the same created_at timestamp
    if (session_id) {
        sql = "SELECT prompt_tokens, completion_tokens, cached_tokens "
              "FROM token_usage "
              "WHERE session_id = ? "
              "ORDER BY created_at DESC, id DESC "
              "LIMIT 1;";
    } else {
        sql = "SELECT prompt_tokens, completion_tokens, cached_tokens "
              "FROM token_usage "
              "ORDER BY created_at DESC, id DESC "
              "LIMIT 1;";
    }

    rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare token usage query: %s", sqlite3_errmsg(db->db));
        pthread_mutex_unlock(&db->mutex);
        return -1;
    }

    if (session_id) {
        sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_TRANSIENT);
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        *prompt_tokens = sqlite3_column_int(stmt, 0);
        *completion_tokens = sqlite3_column_int(stmt, 1);
        *cached_tokens = sqlite3_column_int(stmt, 2);

        LOG_FINE("Retrieved token usage for session %s: prompt=%d, completion=%d, cached=%d",
                 session_id ? session_id : "all",
                 *prompt_tokens, *completion_tokens, *cached_tokens);
    } else if (rc == SQLITE_DONE) {
        LOG_FINE("No token usage records found for session %s",
                 session_id ? session_id : "all");
    } else {
        LOG_ERROR("Failed to execute token usage query: %s", sqlite3_errmsg(db->db));
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db->mutex);
        return -1;
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);
    return 0;
}

// Get last prompt tokens
int token_usage_db_get_last_prompt_tokens(
    TokenUsageDB *db,
    const char *session_id,
    int *prompt_tokens
) {
    if (!db || !db->db || !prompt_tokens) {
        LOG_ERROR("Invalid parameters to token_usage_db_get_last_prompt_tokens");
        return -1;
    }

    *prompt_tokens = 0;

    pthread_mutex_lock(&db->mutex);

    const char *sql;
    sqlite3_stmt *stmt;
    int rc;

    if (session_id) {
        sql = "SELECT prompt_tokens "
              "FROM token_usage "
              "WHERE session_id = ? "
              "ORDER BY created_at DESC, id DESC "
              "LIMIT 1;";
    } else {
        sql = "SELECT prompt_tokens "
              "FROM token_usage "
              "ORDER BY created_at DESC, id DESC "
              "LIMIT 1;";
    }

    rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare token usage query: %s", sqlite3_errmsg(db->db));
        pthread_mutex_unlock(&db->mutex);
        return -1;
    }

    if (session_id) {
        sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_TRANSIENT);
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        *prompt_tokens = sqlite3_column_int(stmt, 0);
        LOG_DEBUG("Retrieved last prompt tokens for session %s: %d",
                  session_id ? session_id : "all", *prompt_tokens);
    } else if (rc == SQLITE_DONE) {
        LOG_DEBUG("No token usage records found for session %s",
                  session_id ? session_id : "all");
    } else {
        LOG_ERROR("Failed to execute token usage query: %s", sqlite3_errmsg(db->db));
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db->mutex);
        return -1;
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);
    return 0;
}

// Get last cached tokens
int token_usage_db_get_last_cached_tokens(
    TokenUsageDB *db,
    const char *session_id,
    int *cached_tokens
) {
    if (!db || !db->db || !cached_tokens) {
        LOG_ERROR("Invalid parameters to token_usage_db_get_last_cached_tokens");
        return -1;
    }

    *cached_tokens = 0;

    pthread_mutex_lock(&db->mutex);

    const char *sql;
    sqlite3_stmt *stmt;
    int rc;

    if (session_id) {
        sql = "SELECT cached_tokens, prompt_cache_hit_tokens "
              "FROM token_usage "
              "WHERE session_id = ? "
              "ORDER BY created_at DESC, id DESC "
              "LIMIT 1;";
    } else {
        sql = "SELECT cached_tokens, prompt_cache_hit_tokens "
              "FROM token_usage "
              "ORDER BY created_at DESC, id DESC "
              "LIMIT 1;";
    }

    rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare cached tokens query: %s", sqlite3_errmsg(db->db));
        pthread_mutex_unlock(&db->mutex);
        return -1;
    }

    if (session_id) {
        sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_TRANSIENT);
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        *cached_tokens = sqlite3_column_int(stmt, 0);
        // Fallback to prompt_cache_hit_tokens if cached_tokens is 0
        if (*cached_tokens == 0) {
            *cached_tokens = sqlite3_column_int(stmt, 1);
        }
        LOG_DEBUG("Retrieved last cached tokens for session %s: %d",
                  session_id ? session_id : "all", *cached_tokens);
    } else if (rc == SQLITE_DONE) {
        LOG_DEBUG("No token usage records found for session %s",
                  session_id ? session_id : "all");
    } else {
        LOG_ERROR("Failed to execute cached tokens query: %s", sqlite3_errmsg(db->db));
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db->mutex);
        return -1;
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);
    return 0;
}

// Rotate by age
int token_usage_db_rotate_by_age(TokenUsageDB *db, int days) {
    if (!db || !db->db || days < 0) {
        LOG_ERROR("Invalid parameters to token_usage_db_rotate_by_age");
        return -1;
    }

    if (days == 0) {
        return 0;  // 0 means unlimited
    }

    pthread_mutex_lock(&db->mutex);

    time_t now = time(NULL);
    time_t cutoff = now - (days * 86400);

    const char *sql = "DELETE FROM token_usage WHERE created_at < ?;";
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
        LOG_INFO("Token usage DB rotated: deleted %d records older than %d days", deleted, days);
    }

    return deleted;
}

// Rotate by count
int token_usage_db_rotate_by_count(TokenUsageDB *db, int max_records) {
    if (!db || !db->db || max_records < 0) {
        LOG_ERROR("Invalid parameters to token_usage_db_rotate_by_count");
        return -1;
    }

    if (max_records == 0) {
        return 0;  // 0 means unlimited
    }

    pthread_mutex_lock(&db->mutex);

    // Get total count
    const char *count_sql = "SELECT COUNT(*) FROM token_usage;";
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
        pthread_mutex_unlock(&db->mutex);
        return 0;
    }

    // Delete oldest records
    const char *delete_sql =
        "DELETE FROM token_usage WHERE id NOT IN "
        "(SELECT id FROM token_usage ORDER BY created_at DESC LIMIT ?);";

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
        LOG_INFO("Token usage DB rotated: deleted %d records, keeping %d most recent",
                 deleted, max_records);
    }

    return deleted;
}

// Get database size
long token_usage_db_get_size(TokenUsageDB *db) {
    if (!db || !db->db_path) {
        LOG_ERROR("Invalid parameters to token_usage_db_get_size");
        return -1;
    }

    struct stat st;
    if (stat(db->db_path, &st) != 0) {
        LOG_ERROR("Failed to stat token usage database: %s", strerror(errno));
        return -1;
    }

    return st.st_size;
}

// Vacuum database
int token_usage_db_vacuum(TokenUsageDB *db) {
    if (!db || !db->db) {
        LOG_ERROR("Invalid parameters to token_usage_db_vacuum");
        return -1;
    }

    pthread_mutex_lock(&db->mutex);

    char *err_msg = NULL;
    int rc = sqlite3_exec(db->db, "VACUUM;", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to vacuum token usage database: %s", err_msg);
        sqlite3_free(err_msg);
        pthread_mutex_unlock(&db->mutex);
        return -1;
    }

    pthread_mutex_unlock(&db->mutex);

    LOG_INFO("Token usage database vacuum completed");
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

// Auto-rotate based on environment variables
int token_usage_db_auto_rotate(TokenUsageDB *db) {
    if (!db || !db->db) {
        LOG_ERROR("Invalid parameters to token_usage_db_auto_rotate");
        return -1;
    }

    // Check if auto-rotation is enabled (uses same env var as main DB)
    const char *auto_rotate_env = getenv("KLAWED_DB_AUTO_ROTATE");
    if (auto_rotate_env && strcmp(auto_rotate_env, "0") == 0) {
        LOG_DEBUG("Auto-rotation disabled by KLAWED_DB_AUTO_ROTATE=0");
        return 0;
    }

    int total_deleted = 0;
    int need_vacuum = 0;

    // Rotate by age (default: 30 days)
    int max_days = get_env_int("KLAWED_TOKEN_USAGE_DB_MAX_DAYS", 30);
    if (max_days == 0) {
        // Fall back to general DB setting
        max_days = get_env_int("KLAWED_DB_MAX_DAYS", 30);
    }
    if (max_days > 0) {
        int deleted = token_usage_db_rotate_by_age(db, max_days);
        if (deleted > 0) {
            total_deleted += deleted;
            need_vacuum = 1;
        } else if (deleted < 0) {
            return -1;
        }
    }

    // Rotate by count (default: 5000 records - higher than API calls since these are smaller)
    int max_records = get_env_int("KLAWED_TOKEN_USAGE_DB_MAX_RECORDS", 5000);
    if (max_records == 0) {
        // Fall back to general DB setting * 5 since token records are smaller
        max_records = get_env_int("KLAWED_DB_MAX_RECORDS", 1000) * 5;
    }
    if (max_records > 0) {
        int deleted = token_usage_db_rotate_by_count(db, max_records);
        if (deleted > 0) {
            total_deleted += deleted;
            need_vacuum = 1;
        } else if (deleted < 0) {
            return -1;
        }
    }

    if (need_vacuum) {
        token_usage_db_vacuum(db);
    }

    if (total_deleted > 0) {
        LOG_INFO("Token usage DB auto-rotation completed: deleted %d total records", total_deleted);
    }

    return 0;
}
