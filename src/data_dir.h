/*
 * data_dir.h - Data directory path utilities
 *
 * Provides centralized access to the .klawed data directory path.
 * The base directory can be customized via KLAWED_DATA_DIR environment variable.
 *
 * Priority for base directory:
 *   1. $KLAWED_DATA_DIR (environment variable)
 *   2. ./.klawed (project-local, default)
 *
 * Individual file paths can still be overridden by their specific env vars
 * (e.g., KLAWED_DB_PATH, KLAWED_LOG_PATH, KLAWED_MEMORY_PATH).
 */

#ifndef DATA_DIR_H
#define DATA_DIR_H

#include <stddef.h>

/*
 * Get the base data directory path.
 * Returns the value of KLAWED_DATA_DIR if set, otherwise ".klawed".
 *
 * @return Pointer to the data directory path (do not free)
 */
const char *data_dir_get_base(void);

/*
 * Build a path within the data directory.
 * Combines the base directory with the given subpath.
 *
 * @param buf Buffer to store the result
 * @param buf_size Size of the buffer
 * @param subpath Subpath within the data directory (e.g., "logs/klawed.log")
 * @return 0 on success, -1 if buffer too small or null arguments
 */
int data_dir_build_path(char *buf, size_t buf_size, const char *subpath);

/*
 * Ensure a directory within the data directory exists.
 * Creates the directory and all parent directories if needed.
 *
 * @param subpath Subdirectory path relative to data directory (e.g., "logs", "mcp")
 *                Pass NULL or empty string to ensure just the base directory exists
 * @return 0 on success, -1 on error
 */
int data_dir_ensure(const char *subpath);

/*
 * Check if no-storage diagnostic mode is enabled (KLAWED_NO_STORAGE=1).
 * When enabled, all SQLite database operations are skipped:
 *   - API call history database (api_calls.db)
 *   - Token usage tracking database (token_usage.db)
 *   - Memory database (memory.db)
 *   - History file
 * This helps diagnose TUI hangs on certain platforms (e.g., Mac Apple Silicon).
 *
 * @return 1 if no-storage mode is enabled, 0 otherwise
 */
int data_dir_is_no_storage_mode(void);

/*
 * Create a directory recursively (equivalent to mkdir -p).
 * Creates all parent directories as needed.
 *
 * @param path The directory path to create
 * @return 0 on success, -1 on failure (errno is set)
 */
int mkdir_recursive_path(const char *path);

#endif /* DATA_DIR_H */
