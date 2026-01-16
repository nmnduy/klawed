/*
 * String Utilities
 * Helper functions for string manipulation
 */

#include "string_utils.h"
#include <stdlib.h>
#include <string.h>

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
