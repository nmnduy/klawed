#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include <stddef.h>

/**
 * File Utilities
 *
 * Helper functions for file I/O operations.
 */

/**
 * Read entire file into memory
 * @param path Path to file
 * @return Dynamically allocated string with file contents, or NULL on error
 *         Caller must free() the returned string
 */
char* read_file(const char *path);

/**
 * Write content to file
 * Creates parent directories if they don't exist
 * @param path Path to file
 * @param content Content to write
 * @return 0 on success, -1 on error
 */
int write_file(const char *path, const char *content);

/**
 * Resolve path (handle relative paths and canonicalization)
 * @param path Path to resolve
 * @param working_dir Current working directory
 * @return Dynamically allocated resolved path, or NULL on error
 *         Caller must free() the returned string
 */
char* resolve_path(const char *path, const char *working_dir);

/**
 * Create directory recursively (like mkdir -p)
 * @param path Directory path to create
 * @return 0 on success, -1 on error
 */
int mkdir_p(const char *path);

/**
 * Save binary data to file
 * Creates parent directories if they don't exist
 * @param filename Path to file
 * @param data Binary data to write
 * @param size Size of data in bytes
 * @return 0 on success, -1 on error
 */
int save_binary_file(const char *filename, const void *data, size_t size);

#endif // FILE_UTILS_H
