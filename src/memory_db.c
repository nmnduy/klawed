/*
 * memory_db.c - SQLite-based persistent memory system implementation
 *
 * Uses SQLite3 with FTS5 for full-text search.
 * Optional sqlite-vector extension for similarity-based vector search.
 */

#include "memory_db.h"
#include "logger.h"
#include "data_dir.h"
#include "util/alloc_utils.h"

#include <bsd/string.h>
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* Default memory database subpath within data directory */
#define MEMORY_DB_DEFAULT_SUBPATH "memory.db"
#define MEMORY_DB_MAX_PATH 4096
#define MEMORY_DB_MAX_ERROR_LEN 512

/* Schema version for migrations */
#define MEMORY_DB_SCHEMA_VERSION 1

/*
 * Memory database structure
 */
struct MemoryDB {
    sqlite3 *db;
    char *path;
    char last_error[MEMORY_DB_MAX_ERROR_LEN];
    int vector_available;  /* 1 if sqlite-vector is loaded */
    size_t vector_dim;     /* Dimension of embeddings */
};

/* Global instance management */
static MemoryDB *g_memory_db = NULL;
static pthread_once_t g_memory_db_init_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_memory_db_mutex = PTHREAD_MUTEX_INITIALIZER;
static char *g_memory_db_path = NULL;
static int g_memory_db_init_result = -1;

/* SQL schema for memory tables */
static const char *MEMORY_SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS memories ("
    "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    entity TEXT NOT NULL,"
    "    slot TEXT NOT NULL,"
    "    value TEXT NOT NULL,"
    "    kind INTEGER NOT NULL DEFAULT 0,"
    "    relation INTEGER NOT NULL DEFAULT 0,"
    "    timestamp TEXT NOT NULL,"
    "    created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_memories_entity ON memories(entity);"
    "CREATE INDEX IF NOT EXISTS idx_memories_entity_slot ON memories(entity, slot);"
    "CREATE INDEX IF NOT EXISTS idx_memories_timestamp ON memories(timestamp);"
    "CREATE TABLE IF NOT EXISTS memory_metadata ("
    "    key TEXT PRIMARY KEY,"
    "    value TEXT NOT NULL"
    ");";

/* FTS5 virtual table for full-text search */
static const char *MEMORY_FTS_SCHEMA_SQL =
    "CREATE VIRTUAL TABLE IF NOT EXISTS memories_fts USING fts5("
    "    content,"
    "    entity,"
    "    slot,"
    "    content_rowid=rowid"
    ");"
    "CREATE TRIGGER IF NOT EXISTS memories_fts_insert AFTER INSERT ON memories BEGIN"
    "    INSERT INTO memories_fts(rowid, content, entity, slot)"
    "    VALUES (new.id, new.value, new.entity, new.slot);"
    "END;"
    "CREATE TRIGGER IF NOT EXISTS memories_fts_delete AFTER DELETE ON memories BEGIN"
    "    INSERT INTO memories_fts(memories_fts, rowid, content, entity, slot)"
    "    VALUES ('delete', old.id, old.value, old.entity, old.slot);"
    "END;";

/* Vector table schema (only created if sqlite-vector is available) */
static const char *MEMORY_VECTOR_SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS memory_embeddings ("
    "    memory_id INTEGER PRIMARY KEY,"
    "    embedding BLOB NOT NULL,"
    "    FOREIGN KEY (memory_id) REFERENCES memories(id) ON DELETE CASCADE"
    ");";

/*
 * Set error message on database handle
 */
__attribute__((format(printf, 2, 3)))
static void memory_db_set_error(MemoryDB *mdb, const char *fmt, ...) {
    if (!mdb) return;

    va_list args;
    va_start(args, fmt);
    vsnprintf(mdb->last_error, sizeof(mdb->last_error), fmt, args);
    va_end(args);

    mdb->last_error[MEMORY_DB_MAX_ERROR_LEN - 1] = '\0';
}

/*
 * Create directory recursively (like mkdir -p)
 */
static int mkdir_p(const char *path) {
    char tmp[512];
    char *p = NULL;
    size_t len;

    if (path == NULL) {
        return -1;
    }

    len = strlcpy(tmp, path, sizeof(tmp));
    if (len >= sizeof(tmp)) {
        return -1;
    }

    /* Remove trailing slash */
    if (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    /* Create directories recursively */
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

/*
 * Ensure parent directory for a file path exists.
 */
static int ensure_parent_dir_exists(const char *file_path) {
    const char *slash = NULL;
    char *dir = NULL;
    size_t dir_len;
    int rc = 0;

    if (file_path == NULL) {
        return -1;
    }

    slash = strrchr(file_path, '/');
    if (slash == NULL) {
        return 0;
    }

    dir_len = (size_t)(slash - file_path);
    if (dir_len == 0) {
        return 0;
    }

    if (dir_len >= MEMORY_DB_MAX_PATH) {
        return -1;
    }

    dir = calloc(dir_len + 1, sizeof(char));
    if (dir == NULL) {
        return -1;
    }

    memcpy(dir, file_path, dir_len);
    dir[dir_len] = '\0';

    rc = mkdir_p(dir);

    free(dir);
    return rc;
}

/*
 * Execute SQL script (multiple statements separated by semicolons)
 */
static int exec_sql_script(sqlite3 *db, const char *sql) {
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        if (err_msg) {
            LOG_ERROR("Memory DB: SQL error: %s", err_msg);
            sqlite3_free(err_msg);
        }
        return -1;
    }
    return 0;
}

/*
 * Initialize database schema
 */
static int memory_db_init_schema(MemoryDB *mdb) {
    if (exec_sql_script(mdb->db, MEMORY_SCHEMA_SQL) != 0) {
        memory_db_set_error(mdb, "Failed to create base schema");
        return -1;
    }

    if (exec_sql_script(mdb->db, MEMORY_FTS_SCHEMA_SQL) != 0) {
        memory_db_set_error(mdb, "Failed to create FTS schema (FTS5 may not be available)");
        /* FTS5 might not be available, continue without it */
    }

    /* Check if sqlite-vector is available */
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(mdb->db, "SELECT vector_version()", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            const char *version = (const char *)sqlite3_column_text(stmt, 0);
            LOG_INFO("Memory DB: sqlite-vector extension available (version: %s)", version);
            mdb->vector_available = 1;
        }
        sqlite3_finalize(stmt);
    } else {
        LOG_INFO("Memory DB: sqlite-vector extension not available");
        mdb->vector_available = 0;
    }

    /* Store schema version */
    const char *version_sql =
        "INSERT OR REPLACE INTO memory_metadata (key, value) VALUES ('schema_version', '1');";
    exec_sql_script(mdb->db, version_sql);

    return 0;
}

/*
 * Open a memory database
 */
MemoryDB* memory_db_open(const char *path) {
    if (path == NULL) {
        return NULL;
    }

    MemoryDB *mdb = calloc(1, sizeof(MemoryDB));
    if (mdb == NULL) {
        return NULL;
    }

    mdb->path = strdup(path);
    if (mdb->path == NULL) {
        free(mdb);
        return NULL;
    }

    int rc = sqlite3_open(path, &mdb->db);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Memory DB: Failed to open database at %s: %s",
                  path, sqlite3_errmsg(mdb->db));
        memory_db_set_error(mdb, "Failed to open database: %s", sqlite3_errmsg(mdb->db));
        sqlite3_close(mdb->db);
        free(mdb->path);
        free(mdb);
        return NULL;
    }

    /* Enable foreign keys */
    sqlite3_exec(mdb->db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);

    /* Initialize schema */
    if (memory_db_init_schema(mdb) != 0) {
        memory_db_close(mdb);
        return NULL;
    }

    LOG_INFO("Memory DB: Opened database at %s", path);
    return mdb;
}

/*
 * Close a memory database
 */
void memory_db_close(MemoryDB *mdb) {
    if (mdb == NULL) {
        return;
    }

    if (mdb->db) {
        sqlite3_close(mdb->db);
    }

    free(mdb->path);
    free(mdb);
}

/*
 * Internal initialization function (called via pthread_once)
 */
static void memory_db_do_init(void) {
    const char *path = g_memory_db_path;
    static char default_path[MEMORY_DB_MAX_PATH];

    /* Use default path if none provided */
    if (path == NULL) {
        if (data_dir_build_path(default_path, sizeof(default_path), MEMORY_DB_DEFAULT_SUBPATH) == 0) {
            path = default_path;
        } else {
            LOG_ERROR("Memory DB: Failed to build default path");
            g_memory_db_init_result = -1;
            return;
        }
    }

    if (ensure_parent_dir_exists(path) != 0) {
        g_memory_db_init_result = -1;
        return;
    }

    g_memory_db = memory_db_open(path);
    if (g_memory_db == NULL) {
        g_memory_db_init_result = -1;
        return;
    }

    g_memory_db_init_result = 0;
}

/*
 * Initialize global memory database
 */
int memory_db_init_global(const char *path) {
    const char *effective_path = NULL;
    size_t path_len = 0;

    pthread_mutex_lock(&g_memory_db_mutex);

    /* If already initialized, return previous result */
    if (g_memory_db != NULL) {
        pthread_mutex_unlock(&g_memory_db_mutex);
        return 0;
    }

    /* Precedence: explicit argument > env var > default */
    effective_path = path;
    if (effective_path == NULL || effective_path[0] == '\0') {
        const char *env_path = getenv("KLAWED_MEMORY_PATH");
        if (env_path != NULL && env_path[0] != '\0') {
            effective_path = env_path;
        }
    }

    if (effective_path != NULL && effective_path[0] != '\0') {
        path_len = strnlen(effective_path, MEMORY_DB_MAX_PATH + 1);
        if (path_len == 0 || path_len > MEMORY_DB_MAX_PATH) {
            LOG_ERROR("Memory DB: Memory path too long (max %d)", MEMORY_DB_MAX_PATH);
            pthread_mutex_unlock(&g_memory_db_mutex);
            return -1;
        }

        g_memory_db_path = strdup(effective_path);
        if (g_memory_db_path == NULL) {
            LOG_ERROR("Memory DB: Failed to allocate memory for path");
            pthread_mutex_unlock(&g_memory_db_mutex);
            return -1;
        }
    }

    /* Perform one-time initialization */
    pthread_once(&g_memory_db_init_once, memory_db_do_init);

    pthread_mutex_unlock(&g_memory_db_mutex);
    return g_memory_db_init_result;
}

/*
 * Clean up global memory database
 */
void memory_db_cleanup_global(void) {
    pthread_mutex_lock(&g_memory_db_mutex);

    if (g_memory_db != NULL) {
        LOG_INFO("Memory DB: Closing database");
        memory_db_close(g_memory_db);
        g_memory_db = NULL;
    }

    if (g_memory_db_path != NULL) {
        free(g_memory_db_path);
        g_memory_db_path = NULL;
    }

    /* Reset pthread_once so init can be called again if needed */
    g_memory_db_init_once = (pthread_once_t)PTHREAD_ONCE_INIT;
    g_memory_db_init_result = -1;

    pthread_mutex_unlock(&g_memory_db_mutex);
}

/*
 * Get global memory database handle
 */
MemoryDB* memory_db_get_global(void) {
    MemoryDB *handle = NULL;

    pthread_mutex_lock(&g_memory_db_mutex);
    handle = g_memory_db;
    pthread_mutex_unlock(&g_memory_db_mutex);

    return handle;
}

/*
 * Check if memory database is available
 */
int memory_db_is_available(void) {
    return 1;  /* Always available with SQLite */
}

/*
 * Open memory database for specific path
 */
MemoryDB* memory_db_open_for_path(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return memory_db_get_global();
    }

    if (ensure_parent_dir_exists(path) != 0) {
        LOG_ERROR("Memory DB: Failed to create parent directory for %s", path);
        return NULL;
    }

    LOG_INFO("Memory DB: Opening database at %s (custom path)", path);

    MemoryDB *mdb = memory_db_open(path);
    if (mdb == NULL) {
        LOG_ERROR("Memory DB: Failed to open database at %s", path);
        return NULL;
    }

    return mdb;
}

/*
 * Store a memory card
 */
int64_t memory_db_store(MemoryDB *mdb, const char *entity,
                        const char *slot, const char *value,
                        MemoryKind kind, MemoryRelation relation) {
    if (mdb == NULL || entity == NULL || slot == NULL || value == NULL) {
        if (mdb) memory_db_set_error(mdb, "Invalid parameters");
        return -1;
    }

    /* Get current timestamp */
    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);

    const char *sql =
        "INSERT INTO memories (entity, slot, value, kind, relation, timestamp)"
        " VALUES (?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(mdb->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        memory_db_set_error(mdb, "Failed to prepare statement: %s", sqlite3_errmsg(mdb->db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, entity, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, slot, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, value, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, (int)kind);
    sqlite3_bind_int(stmt, 5, (int)relation);
    sqlite3_bind_text(stmt, 6, timestamp, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        memory_db_set_error(mdb, "Failed to insert memory: %s", sqlite3_errmsg(mdb->db));
        return -1;
    }

    return sqlite3_last_insert_rowid(mdb->db);
}

/*
 * Get current value for entity:slot
 */
MemoryCard* memory_db_get_current(MemoryDB *mdb, const char *entity, const char *slot) {
    if (mdb == NULL || entity == NULL || slot == NULL) {
        return NULL;
    }

    const char *sql =
        "SELECT id, entity, slot, value, kind, relation, timestamp"
        " FROM memories"
        " WHERE entity = ? AND slot = ?"
        " ORDER BY id DESC"
        " LIMIT 1;";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(mdb->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        memory_db_set_error(mdb, "Failed to prepare statement: %s", sqlite3_errmsg(mdb->db));
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, entity, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, slot, -1, SQLITE_STATIC);

    MemoryCard *card = NULL;
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        /* Check if the memory has been retracted */
        int relation_val = sqlite3_column_int(stmt, 5);
        if (relation_val == MEMORY_RELATION_RETRACTS) {
            /* Memory was retracted - return NULL */
            sqlite3_finalize(stmt);
            return NULL;
        }

        card = calloc(1, sizeof(MemoryCard));
        if (card != NULL) {
            card->card_id = sqlite3_column_int64(stmt, 0);
            card->entity = strdup((const char *)sqlite3_column_text(stmt, 1));
            card->slot = strdup((const char *)sqlite3_column_text(stmt, 2));
            card->value = strdup((const char *)sqlite3_column_text(stmt, 3));
            int kind_val = sqlite3_column_int(stmt, 4);
            card->kind = strdup(memory_db_kind_to_string((MemoryKind)kind_val));
            card->relation = strdup(memory_db_relation_to_string((MemoryRelation)relation_val));
            card->timestamp = strdup((const char *)sqlite3_column_text(stmt, 6));
            card->score = 0.0;
        }
    }

    sqlite3_finalize(stmt);
    return card;
}

/*
 * Search memories using FTS5
 */
/*
 * Enhance a query for better FTS5 matching.
 * Adds prefix wildcards to terms that don't already have FTS5 operators.
 * For example: "staging build" -> "staging* build*"
 * Preserves existing FTS5 syntax: quotes, AND/OR, column:prefix, etc.
 */
static char* enhance_fts5_query(const char *query) {
    if (!query || !query[0]) {
        return strdup("");
    }

    size_t query_len = strlen(query);
    /* Allocate enough space for the enhanced query (can grow by ~2x with wildcards) */
    char *enhanced = malloc(query_len * 3 + 1);
    if (!enhanced) {
        return strdup(query);
    }

    size_t out_pos = 0;
    size_t i = 0;
    int in_quotes = 0;
    int term_start = -1;

    while (i <= query_len) {
        char c = query[i];

        /* Check for quote boundaries */
        if (c == '"') {
            in_quotes = !in_quotes;
            /* Copy the quote and any preceding term */
            if (term_start >= 0 && !in_quotes) {
                /* End of quoted term - copy it as-is */
                size_t term_len = i - (size_t)term_start;
                memcpy(enhanced + out_pos, query + term_start, term_len);
                out_pos += term_len;
                term_start = -1;
            }
            enhanced[out_pos++] = c;
            i++;
            continue;
        }

        if (in_quotes) {
            /* Inside quotes - just track position, copy later */
            if (term_start < 0) {
                term_start = (int)i;
            }
            i++;
            continue;
        }

        /* Check for word boundaries outside quotes */
        if (c == '\0' || c == ' ' || c == '\t' || c == '\n') {
            if (term_start >= 0) {
                /* End of a term */
                size_t term_len = i - (size_t)term_start;

                /* Check if term already has FTS5 operators */
                int has_operator = 0;
                if (term_len > 0) {
                    /* Check for column: prefix, wildcards, or boolean operators */
                    for (size_t j = 0; j < term_len; j++) {
                        char tc = query[term_start + (int)j];
                        if (tc == ':' || tc == '*' || tc == '^') {
                            has_operator = 1;
                            break;
                        }
                    }
                    /* Check for boolean operators (case-insensitive) */
                    if (term_len == 2 || term_len == 3) {
                        char buf[4] = {0};
                        for (size_t j = 0; j < term_len && j < 3; j++) {
                            buf[j] = (char)tolower((unsigned char)query[term_start + (int)j]);
                        }
                        if (strcmp(buf, "and") == 0 || strcmp(buf, "or") == 0 || strcmp(buf, "not") == 0) {
                            has_operator = 1;
                        }
                    }
                }

                /* Copy the term */
                memcpy(enhanced + out_pos, query + term_start, term_len);
                out_pos += term_len;

                /* Add wildcard if no operator present and term is alphanumeric */
                if (!has_operator && term_len > 0) {
                    char last_char = query[term_start + (int)term_len - 1];
                    if (isalnum((unsigned char)last_char)) {
                        enhanced[out_pos++] = '*';
                    }
                }

                term_start = -1;
            }
            /* Copy the whitespace */
            if (c != '\0') {
                enhanced[out_pos++] = c;
            }
        } else {
            if (term_start < 0) {
                term_start = (int)i;
            }
        }
        i++;
    }

    enhanced[out_pos] = '\0';
    return enhanced;
}

MemorySearchResult* memory_db_search(MemoryDB *mdb, const char *query, uint32_t top_k) {
    if (mdb == NULL || query == NULL) {
        return NULL;
    }

    MemorySearchResult *result = calloc(1, sizeof(MemorySearchResult));
    if (result == NULL) {
        return NULL;
    }

    /* Check if FTS5 table exists */
    const char *check_sql =
        "SELECT name FROM sqlite_master WHERE type='table' AND name='memories_fts';";
    sqlite3_stmt *check_stmt;
    int rc = sqlite3_prepare_v2(mdb->db, check_sql, -1, &check_stmt, NULL);
    if (rc != SQLITE_OK) {
        free(result);
        return NULL;
    }

    int has_fts = (sqlite3_step(check_stmt) == SQLITE_ROW);
    sqlite3_finalize(check_stmt);

    const char *sql;
    if (has_fts) {
        /* Use FTS5 for search, filtering out retracted memories.
         * A memory is considered retracted if:
         * 1. Its own relation is 'retracts', OR
         * 2. There exists a newer memory for the same entity:slot with relation 'retracts'
         */
        sql =
            "SELECT m.id, m.entity, m.slot, m.value, m.kind, m.relation, m.timestamp,"
            "       rank as score"
            " FROM memories_fts"
            " JOIN memories m ON memories_fts.rowid = m.id"
            " WHERE memories_fts MATCH ?"
            "   AND m.relation != 3"
            "   AND NOT EXISTS ("
            "       SELECT 1 FROM memories m2"
            "       WHERE m2.entity = m.entity"
            "         AND m2.slot = m.slot"
            "         AND m2.relation = 3"
            "         AND m2.id > m.id"
            "   )"
            " ORDER BY rank"
            " LIMIT ?;";
    } else {
        /* Fallback to simple LIKE search, filtering out retracted memories */
        sql =
            "SELECT id, entity, slot, value, kind, relation, timestamp, 0.0 as score"
            " FROM memories m"
            " WHERE (value LIKE ? OR entity LIKE ? OR slot LIKE ?)"
            "   AND relation != 3"
            "   AND NOT EXISTS ("
            "       SELECT 1 FROM memories m2"
            "       WHERE m2.entity = m.entity"
            "         AND m2.slot = m.slot"
            "         AND m2.relation = 3"
            "         AND m2.id > m.id"
            "   )"
            " ORDER BY id DESC"
            " LIMIT ?;";
    }

    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(mdb->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        memory_db_set_error(mdb, "Failed to prepare search: %s", sqlite3_errmsg(mdb->db));
        free(result);
        return NULL;
    }

    if (has_fts) {
        /* FTS5 search - enhance query with prefix wildcards */
        char *enhanced_query = enhance_fts5_query(query);
        sqlite3_bind_text(stmt, 1, enhanced_query, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, (int)top_k);
        free(enhanced_query);
    } else {
        /* LIKE search - bind pattern with wildcards */
        char *pattern = malloc(strlen(query) + 3);
        if (pattern) {
            snprintf(pattern, strlen(query) + 3, "%%%s%%", query);
            sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, pattern, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 4, (int)top_k);
            free(pattern);
        }
    }

    /* Execute search and collect results */
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (result->count >= result->capacity) {
            size_t new_capacity = result->capacity == 0 ? 8 : result->capacity * 2;
            MemoryCard *new_cards = reallocarray(result->cards, new_capacity, sizeof(MemoryCard));
            if (new_cards == NULL) {
                break;
            }
            result->cards = new_cards;
            result->capacity = new_capacity;
        }

        MemoryCard *card = &result->cards[result->count];
        card->card_id = sqlite3_column_int64(stmt, 0);
        card->entity = strdup((const char *)sqlite3_column_text(stmt, 1));
        card->slot = strdup((const char *)sqlite3_column_text(stmt, 2));
        card->value = strdup((const char *)sqlite3_column_text(stmt, 3));
        int kind_int = sqlite3_column_int(stmt, 4);
        int relation_int = sqlite3_column_int(stmt, 5);
        card->kind = strdup(memory_db_kind_to_string((MemoryKind)kind_int));
        card->relation = strdup(memory_db_relation_to_string((MemoryRelation)relation_int));
        card->timestamp = strdup((const char *)sqlite3_column_text(stmt, 6));
        card->score = sqlite3_column_double(stmt, 7);

        result->count++;
    }

    sqlite3_finalize(stmt);
    return result;
}

/*
 * Get all memories for an entity
 */
MemorySearchResult* memory_db_get_entity_memories(MemoryDB *mdb, const char *entity) {
    if (mdb == NULL || entity == NULL) {
        return NULL;
    }

    MemorySearchResult *result = calloc(1, sizeof(MemorySearchResult));
    if (result == NULL) {
        return NULL;
    }

    /* Filter out retracted memories. A memory is retracted if:
     * 1. Its own relation is 'retracts', OR
     * 2. There exists a newer memory for the same entity:slot with relation 'retracts'
     */
    const char *sql =
        "SELECT m.id, m.entity, m.slot, m.value, m.kind, m.relation, m.timestamp"
        " FROM memories m"
        " WHERE m.entity = ?"
        "   AND m.relation != 3"
        "   AND NOT EXISTS ("
        "       SELECT 1 FROM memories m2"
        "       WHERE m2.entity = m.entity"
        "         AND m2.slot = m.slot"
        "         AND m2.relation = 3"
        "         AND m2.id > m.id"
        "   )"
        " ORDER BY m.id DESC;";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(mdb->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        memory_db_set_error(mdb, "Failed to prepare query: %s", sqlite3_errmsg(mdb->db));
        free(result);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, entity, -1, SQLITE_STATIC);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (result->count >= result->capacity) {
            size_t new_capacity = result->capacity == 0 ? 8 : result->capacity * 2;
            MemoryCard *new_cards = reallocarray(result->cards, new_capacity, sizeof(MemoryCard));
            if (new_cards == NULL) {
                break;
            }
            result->cards = new_cards;
            result->capacity = new_capacity;
        }

        MemoryCard *card = &result->cards[result->count];
        card->card_id = sqlite3_column_int64(stmt, 0);
        card->entity = strdup((const char *)sqlite3_column_text(stmt, 1));
        card->slot = strdup((const char *)sqlite3_column_text(stmt, 2));
        card->value = strdup((const char *)sqlite3_column_text(stmt, 3));
        int kind_val = sqlite3_column_int(stmt, 4);
        int relation_val = sqlite3_column_int(stmt, 5);
        card->kind = strdup(memory_db_kind_to_string((MemoryKind)kind_val));
        card->relation = strdup(memory_db_relation_to_string((MemoryRelation)relation_val));
        card->timestamp = strdup((const char *)sqlite3_column_text(stmt, 6));
        card->score = 0.0;

        result->count++;
    }

    sqlite3_finalize(stmt);
    return result;
}

/*
 * Free a memory card
 */
void memory_db_free_card(MemoryCard *card) {
    if (card == NULL) {
        return;
    }

    free(card->entity);
    free(card->slot);
    free(card->value);
    free(card->kind);
    free(card->relation);
    free(card->timestamp);
    free(card);
}

/*
 * Free a search result
 */
void memory_db_free_result(MemorySearchResult *result) {
    if (result == NULL) {
        return;
    }

    for (size_t i = 0; i < result->count; i++) {
        MemoryCard *card = &result->cards[i];
        free(card->entity);
        free(card->slot);
        free(card->value);
        free(card->kind);
        free(card->relation);
        free(card->timestamp);
    }

    free(result->cards);
    free(result);
}

/*
 * Get last error message
 */
const char* memory_db_last_error(MemoryDB *mdb) {
    if (mdb == NULL) {
        return "Memory DB not initialized";
    }
    return mdb->last_error[0] != '\0' ? mdb->last_error : "Unknown error";
}

/*
 * Convert kind enum to string
 */
const char* memory_db_kind_to_string(MemoryKind kind) {
    switch (kind) {
        case MEMORY_KIND_FACT: return "fact";
        case MEMORY_KIND_PREFERENCE: return "preference";
        case MEMORY_KIND_EVENT: return "event";
        case MEMORY_KIND_PROFILE: return "profile";
        case MEMORY_KIND_RELATIONSHIP: return "relationship";
        case MEMORY_KIND_GOAL: return "goal";
        default: return "fact";
    }
}

/*
 * Convert string to kind enum
 */
MemoryKind memory_db_string_to_kind(const char *str) {
    if (str == NULL) return MEMORY_KIND_FACT;
    if (strcmp(str, "fact") == 0) return MEMORY_KIND_FACT;
    if (strcmp(str, "preference") == 0) return MEMORY_KIND_PREFERENCE;
    if (strcmp(str, "event") == 0) return MEMORY_KIND_EVENT;
    if (strcmp(str, "profile") == 0) return MEMORY_KIND_PROFILE;
    if (strcmp(str, "relationship") == 0) return MEMORY_KIND_RELATIONSHIP;
    if (strcmp(str, "goal") == 0) return MEMORY_KIND_GOAL;
    return MEMORY_KIND_FACT;
}

/*
 * Convert relation enum to string
 */
const char* memory_db_relation_to_string(MemoryRelation relation) {
    switch (relation) {
        case MEMORY_RELATION_SETS: return "sets";
        case MEMORY_RELATION_UPDATES: return "updates";
        case MEMORY_RELATION_EXTENDS: return "extends";
        case MEMORY_RELATION_RETRACTS: return "retracts";
        default: return "sets";
    }
}

/*
 * Convert string to relation enum
 */
MemoryRelation memory_db_string_to_relation(const char *str) {
    if (str == NULL) return MEMORY_RELATION_SETS;
    if (strcmp(str, "sets") == 0) return MEMORY_RELATION_SETS;
    if (strcmp(str, "updates") == 0) return MEMORY_RELATION_UPDATES;
    if (strcmp(str, "extends") == 0) return MEMORY_RELATION_EXTENDS;
    if (strcmp(str, "retracts") == 0) return MEMORY_RELATION_RETRACTS;
    return MEMORY_RELATION_SETS;
}

/*
 * Check if vector extension is available
 */
int memory_db_vector_available(MemoryDB *mdb) {
    if (mdb == NULL) return 0;
    return mdb->vector_available;
}

/*
 * Initialize vector search (placeholder - requires sqlite-vector extension)
 */
int memory_db_vector_init(MemoryDB *mdb, size_t embedding_dim) {
    if (mdb == NULL) return -1;
    if (!mdb->vector_available) {
        memory_db_set_error(mdb, "sqlite-vector extension not available");
        return -1;
    }

    mdb->vector_dim = embedding_dim;

    /* Create vector table if not exists */
    if (exec_sql_script(mdb->db, MEMORY_VECTOR_SCHEMA_SQL) != 0) {
        memory_db_set_error(mdb, "Failed to create vector table");
        return -1;
    }

    /* Initialize the vector column using sqlite-vector */
    char init_sql[256];
    snprintf(init_sql, sizeof(init_sql),
             "SELECT vector_init('memory_embeddings', 'embedding', 'type=FLOAT32,dimension=%zu');",
             embedding_dim);

    if (exec_sql_script(mdb->db, init_sql) != 0) {
        memory_db_set_error(mdb, "Failed to initialize vector extension");
        return -1;
    }

    return 0;
}

/*
 * Store memory with embedding (placeholder - requires sqlite-vector extension)
 */
int64_t memory_db_store_with_embedding(MemoryDB *mdb, const char *entity,
                                       const char *slot, const char *value,
                                       MemoryKind kind, MemoryRelation relation,
                                       const float *embedding, size_t embedding_dim) {
    if (mdb == NULL || !mdb->vector_available) {
        /* Fall back to regular store without embedding */
        return memory_db_store(mdb, entity, slot, value, kind, relation);
    }

    /* Store the base memory first */
    int64_t card_id = memory_db_store(mdb, entity, slot, value, kind, relation);
    if (card_id < 0) {
        return -1;
    }

    if (embedding != NULL && embedding_dim > 0) {
        /* Insert the embedding */
        const char *sql =
            "INSERT INTO memory_embeddings (memory_id, embedding)"
            " VALUES (?, vector_as_f32(?));";

        sqlite3_stmt *stmt;
        int rc = sqlite3_prepare_v2(mdb->db, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            return card_id;  /* Return card_id even if embedding fails */
        }

        sqlite3_bind_int64(stmt, 1, card_id);
        /* Convert embedding array to JSON string for vector_as_f32 */
        char *json_embedding = malloc(embedding_dim * 20 + 3);  /* Rough estimate */
        if (json_embedding) {
            json_embedding[0] = '[';
            json_embedding[1] = '\0';
            for (size_t i = 0; i < embedding_dim; i++) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%s%.6f", i > 0 ? "," : "", embedding[i]);
                strlcat(json_embedding, buf, embedding_dim * 20 + 3);
            }
            strlcat(json_embedding, "]", embedding_dim * 20 + 3);
            sqlite3_bind_text(stmt, 2, json_embedding, -1, SQLITE_TRANSIENT);
            free(json_embedding);
        }

        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    return card_id;
}

/*
 * Vector search (placeholder - requires sqlite-vector extension)
 */
MemorySearchResult* memory_db_vector_search(MemoryDB *db, const float *query_embedding,
                                            size_t embedding_dim, uint32_t top_k) {
    if (db == NULL || !db->vector_available || query_embedding == NULL) {
        return NULL;
    }

    /* Check if vector_quantize has been called */
    const char *check_sql = "SELECT count(*) FROM memory_embeddings;";
    sqlite3_stmt *check_stmt;
    int rc = sqlite3_prepare_v2(db->db, check_sql, -1, &check_stmt, NULL);
    if (rc != SQLITE_OK) {
        return NULL;
    }

    rc = sqlite3_step(check_stmt);
    int has_embeddings = (rc == SQLITE_ROW && sqlite3_column_int(check_stmt, 0) > 0);
    sqlite3_finalize(check_stmt);

    if (!has_embeddings) {
        /* No embeddings stored, fall back to text search */
        return NULL;
    }

    MemorySearchResult *result = calloc(1, sizeof(MemorySearchResult));
    if (result == NULL) {
        return NULL;
    }

    /* Convert query embedding to JSON */
    char *json_query = malloc(embedding_dim * 20 + 3);
    if (!json_query) {
        free(result);
        return NULL;
    }

    json_query[0] = '[';
    json_query[1] = '\0';
    for (size_t i = 0; i < embedding_dim; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s%.6f", i > 0 ? "," : "", query_embedding[i]);
        strlcat(json_query, buf, embedding_dim * 20 + 3);
    }
    strlcat(json_query, "]", embedding_dim * 20 + 3);

    /* Use vector_quantize_scan for similarity search */
    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT m.id, m.entity, m.slot, m.value, m.kind, m.relation, m.timestamp,"
             "       v.distance"
             " FROM vector_quantize_scan('memory_embeddings', 'embedding', vector_as_f32(?), %u) AS v"
             " JOIN memories m ON v.rowid = m.id;",
             top_k);

    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        free(json_query);
        free(result);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, json_query, -1, SQLITE_TRANSIENT);
    free(json_query);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (result->count >= result->capacity) {
            size_t new_capacity = result->capacity == 0 ? 8 : result->capacity * 2;
            MemoryCard *new_cards = reallocarray(result->cards, new_capacity, sizeof(MemoryCard));
            if (new_cards == NULL) {
                break;
            }
            result->cards = new_cards;
            result->capacity = new_capacity;
        }

        MemoryCard *card = &result->cards[result->count];
        card->card_id = sqlite3_column_int64(stmt, 0);
        card->entity = strdup((const char *)sqlite3_column_text(stmt, 1));
        card->slot = strdup((const char *)sqlite3_column_text(stmt, 2));
        card->value = strdup((const char *)sqlite3_column_text(stmt, 3));
        int kind_val = sqlite3_column_int(stmt, 4);
        int relation_val = sqlite3_column_int(stmt, 5);
        card->kind = strdup(memory_db_kind_to_string((MemoryKind)kind_val));
        card->relation = strdup(memory_db_relation_to_string((MemoryRelation)relation_val));
        card->timestamp = strdup((const char *)sqlite3_column_text(stmt, 6));
        card->score = sqlite3_column_double(stmt, 7);

        result->count++;
    }

    sqlite3_finalize(stmt);
    return result;
}
