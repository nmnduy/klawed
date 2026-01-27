/*
 * Token Usage Database Migration System
 *
 * Handles schema evolution for the token usage database.
 */

#ifndef TOKEN_USAGE_DB_MIGRATIONS_H
#define TOKEN_USAGE_DB_MIGRATIONS_H

#include <sqlite3.h>

// Apply all pending migrations to the token usage database
// Returns 0 on success, -1 on failure
int token_usage_db_migrations_apply(sqlite3 *db);

// Get current schema version
// Returns version number, or 0 if schema_version table doesn't exist
int token_usage_db_migrations_get_version(sqlite3 *db);

#endif // TOKEN_USAGE_DB_MIGRATIONS_H
