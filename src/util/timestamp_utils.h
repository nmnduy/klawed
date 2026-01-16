#ifndef TIMESTAMP_UTILS_H
#define TIMESTAMP_UTILS_H

#include <stddef.h>

/**
 * Timestamp Utilities
 * 
 * Helper functions for timestamp generation and formatting.
 */

/**
 * Get current timestamp in YYYY-MM-DD HH:MM:SS format
 * @param buffer Buffer to write timestamp to
 * @param buffer_size Size of buffer
 */
void get_current_timestamp(char *buffer, size_t buffer_size);

/**
 * Generate timestamped filename
 * Format: <prefix>_YYYYMMDD_HHMMSS.<extension>
 * @param buffer Buffer to write filename to
 * @param buffer_size Size of buffer
 * @param prefix Filename prefix (e.g., "file")
 * @param mime_type MIME type to determine extension (e.g., "image/png")
 */
void generate_timestamped_filename(char *buffer, size_t buffer_size,
                                   const char *prefix, const char *mime_type);

/**
 * Get current date in YYYY-MM-DD format
 * @return Dynamically allocated date string, or NULL on error
 *         Caller must free() the returned string
 */
char* get_current_date(void);

/**
 * Generate unique session ID
 * Format: sess_<unix_timestamp>_<random_hex>
 * @return Dynamically allocated session ID, or NULL on error
 *         Caller must free() the returned string
 */
char* generate_session_id(void);

#endif // TIMESTAMP_UTILS_H
