/*
 * json_repair.c - Simple JSON repair utilities
 * 
 * Tries to repair truncated JSON strings, especially for DeepSeek API
 * when finish_reason is "length" and Write tool arguments are cut off.
 */

#include "json_repair.h"
#include <string.h>
#include <ctype.h>

/**
 * Try to repair truncated JSON string
 * 
 * This is a simple heuristic that:
 * 1. Closes any open strings
 * 2. Closes any open braces/brackets
 * 3. Adds missing closing quotes/braces
 * 
 * @param json The truncated JSON string
 * @param max_len Maximum length for repaired JSON (including null terminator)
 * @param repaired Buffer to store repaired JSON (must be at least max_len bytes)
 * @return 1 if repair attempted, 0 if JSON appears valid or can't be repaired
 */
int repair_truncated_json(const char *json, size_t max_len, char *repaired) {
    if (!json || !repaired || max_len < 2) {
        return 0;
    }
    
    size_t len = strlen(json);
    if (len == 0) {
        return 0;
    }
    
    // First, copy the original string
    size_t copy_len = len < max_len - 1 ? len : max_len - 1;
    memcpy(repaired, json, copy_len);
    repaired[copy_len] = '\0';
    size_t current_len = copy_len;
    
    // Check if we need to close a string
    int in_string = 0;
    char string_char = 0;
    int escape = 0;
    
    for (size_t i = 0; i < len; i++) {
        if (escape) {
            escape = 0;
            continue;
        }
        
        if (in_string) {
            if (repaired[i] == '\\') {
                escape = 1;
            } else if (repaired[i] == string_char) {
                in_string = 0;
                string_char = 0;
            }
        } else {
            if (repaired[i] == '"' || repaired[i] == '\'') {
                in_string = 1;
                string_char = repaired[i];
            }
        }
    }
    
    // If we're still in a string, close it
    if (in_string && current_len + 1 < max_len) {
        repaired[current_len] = string_char;
        current_len++;
        repaired[current_len] = '\0';
    }
    
    // Count braces and brackets
    int brace_depth = 0;
    int bracket_depth = 0;
    in_string = 0;
    string_char = 0;
    escape = 0;
    
    for (size_t i = 0; i < current_len; i++) {
        if (escape) {
            escape = 0;
            continue;
        }
        
        if (in_string) {
            if (repaired[i] == '\\') {
                escape = 1;
            } else if (repaired[i] == string_char) {
                in_string = 0;
                string_char = 0;
            }
            continue;
        }
        
        switch (repaired[i]) {
            case '{':
                brace_depth++;
                break;
            case '}':
                if (brace_depth > 0) brace_depth--;
                break;
            case '[':
                bracket_depth++;
                break;
            case ']':
                if (bracket_depth > 0) bracket_depth--;
                break;
            case '"':
            case '\'':
                in_string = 1;
                string_char = repaired[i];
                break;
            default:
                // Other characters are fine
                break;
        }
    }
    
    // Add closing braces/brackets
    while ((brace_depth > 0 || bracket_depth > 0) && current_len + 1 < max_len) {
        if (brace_depth > 0) {
            repaired[current_len] = '}';
            brace_depth--;
        } else if (bracket_depth > 0) {
            repaired[current_len] = ']';
            bracket_depth--;
        }
        current_len++;
    }
    
    if (current_len > len) {
        repaired[current_len] = '\0';
        return 1;  // Repair attempted
    }
    
    return 0;  // No repair needed or possible
}

/**
 * Check if JSON appears to be a truncated Write tool arguments
 * 
 * Write tool arguments have the format: {"file_path": "...", "content": "..."}
 * If truncated, it's often in the middle of the content string.
 * 
 * @param json The JSON string to check
 * @return 1 if it looks like truncated Write tool args, 0 otherwise
 */
int is_truncated_write_args(const char *json) {
    if (!json || !*json) {
        return 0;
    }
    
    // Check if it starts with Write tool pattern
    if (strncmp(json, "{\"file_path\"", 12) != 0) {
        return 0;
    }
    
    // Check if it contains "content" field
    if (strstr(json, "\"content\"") == NULL) {
        return 0;
    }
    
    // Check if it ends with incomplete JSON
    size_t len = strlen(json);
    if (len < 2) {
        return 1;
    }
    
    // Check for common truncation patterns
    if (json[len - 1] == '\\') {
        return 1;  // Ends with escape character
    }
    
    // Count quotes from the end
    int quote_count = 0;
    for (size_t i = len; i > 0; i--) {
        if (json[i - 1] == '"') {
            // Check if it's escaped
            if (i > 1 && json[i - 2] == '\\') {
                continue;
            }
            quote_count++;
        }
    }
    
    // Odd number of quotes means we're in a string
    if (quote_count % 2 == 1) {
        return 1;
    }
    
    // Check if ends with comma or colon (incomplete object)
    char last_char = json[len - 1];
    if (last_char == ',' || last_char == ':') {
        return 1;
    }
    
    return 0;
}

