/*
 * Database Migration System Implementation
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "migrations.h"
#include "logger.h"

// ============================================================================
// Migration Functions
// ============================================================================

// Migration 1: Add session_id column to api_calls table
static int migration_001_add_session_id(sqlite3 *db) {
    const char *sql =
        "ALTER TABLE api_calls ADD COLUMN session_id TEXT;";

    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);

    if (rc != SQLITE_OK) {
        // Check if column already exists (idempotent migration)
        if (strstr(err_msg, "duplicate column name")) {
            sqlite3_free(err_msg);
            return 0;  // Column already exists, consider it success
        }
        LOG_ERROR("Migration 001 failed: %s", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }

    // Create index on session_id
    const char *index_sql =
        "CREATE INDEX IF NOT EXISTS idx_api_calls_session_id ON api_calls(session_id);";

    rc = sqlite3_exec(db, index_sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_WARN("Migration 001 index warning: %s", err_msg);
        sqlite3_free(err_msg);
        // Non-fatal, continue
    }

    return 0;
}

// Migration 2: Add headers_json column to api_calls table
static int migration_002_add_headers_json(sqlite3 *db) {
    const char *sql =
        "ALTER TABLE api_calls ADD COLUMN headers_json TEXT;";

    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);

    if (rc != SQLITE_OK) {
        // Check if column already exists (idempotent migration)
        if (strstr(err_msg, "duplicate column name")) {
            sqlite3_free(err_msg);
            return 0;  // Column already exists, consider it success
        }
        LOG_ERROR("Migration 002 failed: %s", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }

    return 0;
}

// Migration 3: Add session_id column to token_usage table
static int migration_003_add_session_id_to_token_usage(sqlite3 *db) {
    const char *sql =
        "ALTER TABLE token_usage ADD COLUMN session_id TEXT;";

    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);

    if (rc != SQLITE_OK) {
        // Check if column already exists (idempotent migration)
        if (strstr(err_msg, "duplicate column name")) {
            sqlite3_free(err_msg);
            return 0;  // Column already exists, consider it success
        }
        LOG_ERROR("Migration 003 failed: %s", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }

    // Create index on session_id
    const char *index_sql =
        "CREATE INDEX IF NOT EXISTS idx_token_usage_session_id ON token_usage(session_id);";

    rc = sqlite3_exec(db, index_sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_WARN("Migration 003 index warning: %s", err_msg);
        sqlite3_free(err_msg);
        // Non-fatal, continue
    }

    return 0;
}

// ============================================================================
// Migration Registry
// ============================================================================

static const Migration MIGRATIONS[] = {
    {
        .version = 1,
        .description = "Add session_id column to api_calls table",
        .up = migration_001_add_session_id
    },
    {
        .version = 2,
        .description = "Add headers_json column to api_calls table",
        .up = migration_002_add_headers_json
    },
    {
        .version = 3,
        .description = "Add session_id column to token_usage table",
        .up = migration_003_add_session_id_to_token_usage
    },
    // Add new migrations here with incrementing version numbers
};

static const int NUM_MIGRATIONS = sizeof(MIGRATIONS) / sizeof(Migration);

// ============================================================================
// Version Management
// ============================================================================

// Get current schema version from database
int migrations_get_version(sqlite3 *db) {
    // Check if schema_version table exists
    const char *check_sql =
        "SELECT name FROM sqlite_master WHERE type='table' AND name='schema_version';";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, check_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return 0;
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_ROW) {
        // Table doesn't exist, version is 0
        return 0;
    }

    // Get version from table
    const char *version_sql = "SELECT version FROM schema_version ORDER BY version DESC LIMIT 1;";
    rc = sqlite3_prepare_v2(db, version_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return 0;
    }

    rc = sqlite3_step(stmt);
    int version = 0;
    if (rc == SQLITE_ROW) {
        version = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return version;
}

// Update schema version in database
static int migrations_set_version(sqlite3 *db, int version, const char *description) {
    // Create schema_version table if it doesn't exist
    const char *create_sql =
        "CREATE TABLE IF NOT EXISTS schema_version ("
        "    version INTEGER PRIMARY KEY,"
        "    description TEXT NOT NULL,"
        "    applied_at INTEGER NOT NULL"
        ");";

    char *err_msg = NULL;
    int rc = sqlite3_exec(db, create_sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to create schema_version table: %s", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }

    // Insert version record
    const char *insert_sql =
        "INSERT OR REPLACE INTO schema_version (version, description, applied_at) VALUES (?, ?, ?);";

    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare version insert: %s", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, version);
    sqlite3_bind_text(stmt, 2, description, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, (long long)time(NULL));

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LOG_ERROR("Failed to insert version: %s", sqlite3_errmsg(db));
        return -1;
    }

    return 0;
}

// ============================================================================
// Migration Application
// ============================================================================

// Apply all pending migrations
int migrations_apply(sqlite3 *db) {
    if (!db) {
        LOG_ERROR("migrations_apply: NULL database handle");
        return -1;
    }

    int current_version = migrations_get_version(db);

    // Apply each pending migration in order
    for (int i = 0; i < NUM_MIGRATIONS; i++) {
        const Migration *m = &MIGRATIONS[i];

        if (m->version <= current_version) {
            // Migration already applied
            continue;
        }

        LOG_INFO("[Migration] Applying v%d: %s", m->version, m->description);

        // Begin transaction
        char *err_msg = NULL;
        int rc = sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, &err_msg);
        if (rc != SQLITE_OK) {
            LOG_ERROR("Failed to begin transaction: %s", err_msg);
            sqlite3_free(err_msg);
            return -1;
        }

        // Apply migration
        rc = m->up(db);
        if (rc != 0) {
            LOG_ERROR("Migration v%d failed, rolling back", m->version);
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            return -1;
        }

        // Update version
        rc = migrations_set_version(db, m->version, m->description);
        if (rc != 0) {
            LOG_ERROR("Failed to update schema version, rolling back");
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            return -1;
        }

        // Commit transaction
        rc = sqlite3_exec(db, "COMMIT;", NULL, NULL, &err_msg);
        if (rc != SQLITE_OK) {
            LOG_ERROR("Failed to commit migration: %s", err_msg);
            sqlite3_free(err_msg);
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            return -1;
        }

        LOG_INFO("[Migration] Successfully applied v%d", m->version);
        current_version = m->version;
    }

    return 0;
}
