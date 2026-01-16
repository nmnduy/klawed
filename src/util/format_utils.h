#ifndef FORMAT_UTILS_H
#define FORMAT_UTILS_H

#include <stddef.h>

/**
 * Format Utilities
 *
 * Helper functions for formatting values.
 */

/**
 * Format file size in human-readable format (B, KB, MB, GB)
 * @param size Size in bytes
 * @return Static buffer with formatted size (not thread-safe)
 *         Returns pointer to static buffer, do not free()
 */
const char* format_file_size(size_t size);

#endif // FORMAT_UTILS_H
