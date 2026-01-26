/*
 * String Utilities
 * Helper functions for string manipulation
 */

#include "string_utils.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/**
 * Strip ANSI escape sequences from string
 */
char* strip_ansi_escapes(const char *input) {
    if (!input) return NULL;

    size_t len = strlen(input);
    char *result = malloc(len + 1);
    if (!result) return NULL;

    size_t j = 0;
    int in_escape = 0;

    for (size_t i = 0; i < len; i++) {
        if (in_escape) {
            // Inside escape sequence - skip until command character
            if ((input[i] >= 'A' && input[i] <= 'Z') ||
                (input[i] >= 'a' && input[i] <= 'z') ||
                input[i] == '@') {
                in_escape = 0;
            }
        } else if (input[i] == '\033') {  // ESC character
            in_escape = 1;
        } else if (i + 1 < len && input[i] == '\033' && input[i + 1] == '[') {
            // CSI (Control Sequence Introducer)
            in_escape = 1;
            i++;  // Skip the '['
        } else {
            // Normal character - copy to result
            result[j++] = input[i];
        }
    }

    result[j] = '\0';
    return result;
}

/**
 * Trim whitespace from both ends of a string (in-place)
 */
char* trim_whitespace(char *str) {
    if (!str) return NULL;

    // Trim leading whitespace
    char *start = str;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    // If the string is all whitespace
    if (*start == '\0') {
        str[0] = '\0';
        return str;
    }

    // Trim trailing whitespace
    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) {
        end--;
    }

    // Null-terminate at new end
    *(end + 1) = '\0';

    // Move trimmed string to beginning if we trimmed leading whitespace
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }

    return str;
}

/**
 * Duplicate a string and trim whitespace from both ends
 * Returns newly allocated string (caller must free), or NULL on error
 */
char* strdup_trim(const char *str) {
    if (!str) return NULL;

    char *dup = strdup(str);
    if (!dup) return NULL;

    trim_whitespace(dup);
    return dup;
}


