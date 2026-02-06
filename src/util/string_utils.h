#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <stddef.h>


/**
 * String Utilities
 *
 * Helper functions for string manipulation.
 */

/**
 * Strip ANSI escape sequences from string
 * @param input String with ANSI escape sequences
 * @return Dynamically allocated string with escape sequences removed, or NULL on error
 *         Caller must free() the returned string
 */
char* strip_ansi_escapes(const char *input);

/**
 * Trim whitespace from both ends of a string (in-place)
 * @param str String to trim
 * @return Pointer to the trimmed string (same as input)
 */
char* trim_whitespace(char *str);

/**
 * Duplicate a string and trim whitespace from both ends
 * @param str String to duplicate and trim
 * @return Newly allocated trimmed string, or NULL on error
 *         Caller must free() the returned string
 */
char* strdup_trim(const char *str);



/**
 * Truncate a string to max_bytes, ensuring we don't split UTF-8 multi-byte characters.
 * Walks backwards to find a valid UTF-8 character boundary.
 *
 * @param str String to truncate
 * @param max_bytes Maximum bytes to keep (must be > 0)
 * @return Dynamically allocated truncated string, or NULL on error
 *         Caller must free() the returned string
 */
char* truncate_utf8(const char *str, size_t max_bytes);

#endif // STRING_UTILS_H
