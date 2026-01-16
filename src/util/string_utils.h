#ifndef STRING_UTILS_H
#define STRING_UTILS_H

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

#endif // STRING_UTILS_H
