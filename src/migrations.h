/*
 * Database Migration System
 *
 * Handles schema evolution for the persistence layer.
 * Tracks schema version and applies migrations incrementally.
 */

#ifndef MIGRATIONS_H
#define MIGRATIONS_H

#include <sqlite3.h>

// Migration function type
// Returns 0 on success, -1 on failure
typedef int (*migration_func_t)(sqlite3 *db);

// Migration descriptor
typedef struct {
    int version;                    // Schema version number
    const char *description;        // Human-readable description
    migration_func_t up;           // Migration function
} Migration;

// Apply all pending migrations to the database
// Returns 0 on success, -1 on failure
int migrations_apply(sqlite3 *db);

// Get current schema version
// Returns version number, or 0 if schema_version table doesn't exist
int migrations_get_version(sqlite3 *db);

#endif // MIGRATIONS_H
