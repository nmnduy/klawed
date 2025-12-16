#ifndef __APPLE__
    #define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "patch_parser.h"
#include "logger.h"

// External functions from claude.c
extern char* read_file(const char *path);
extern int write_file(const char *path, const char *content);
extern char* resolve_path(const char *path, const char *base_dir);

// Helper: trim leading and trailing whitespace
static char* trim_whitespace(const char *str) {
    if (!str) return NULL;

    // Skip leading whitespace
    while (isspace((unsigned char)*str)) str++;

    if (*str == 0) return strdup("");

    // Find end and trim trailing whitespace
    const char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;

    size_t len = (size_t)(end - str + 1);
    char *result = malloc(len + 1);
    if (!result) return NULL;

    memcpy(result, str, len);
    result[len] = '\0';
    return result;
}

// Helper: extract substring between two markers
static char* extract_between(const char *str, const char *start_marker, const char *end_marker) {
    const char *start = strstr(str, start_marker);
    if (!start) return NULL;

    start += strlen(start_marker);
    const char *end = strstr(start, end_marker);
    if (!end) return NULL;

    size_t len = (size_t)(end - start);
    char *result = malloc(len + 1);
    if (!result) return NULL;

    memcpy(result, start, len);
    result[len] = '\0';
    return result;
}

// Helper: count lines up to a position in string
static int count_lines_to_position(const char *str, const char *pos) {
    if (!str || !pos || pos < str) return 0;
    
    int line_count = 1; // 1-indexed like editors
    const char *p = str;
    while (p < pos) {
        if (*p == '\n') {
            line_count++;
        }
        p++;
    }
    return line_count;
}

// Helper: create error message with line number
static char* create_error_with_line(const char *message, int line_number) {
    if (line_number > 0) {
        size_t len = strlen(message) + 32; // Room for line number
        char *error = malloc(len);
        if (!error) return strdup(message);
        snprintf(error, len, "%s at line %d", message, line_number);
        return error;
    }
    return strdup(message);
}

// Helper: parse @@ marker with optional line numbers and function context
// Format: @@ -old_start,old_count +new_start,new_count @@ function_name
// Returns 1 if parsed successfully, 0 otherwise
static int parse_at_marker(const char *line, PatchOperation *op) {
    if (!line || !op) return 0;
    
    // Initialize with defaults
    op->old_start_line = -1;
    op->old_line_count = -1; 
    op->new_start_line = -1;
    op->new_line_count = -1;
    op->function_context = NULL;
    
    // Skip initial @@
    const char *p = line;
    while (*p && *p != '@') p++;
    if (!*p) return 0;
    p++; // Skip first @
    while (*p && *p == '@') p++;
    
    // Skip whitespace
    while (*p && isspace((unsigned char)*p)) p++;
    
    // Try to parse line numbers if present
    if (*p == '-') {
        p++; // Skip -
        char *endptr;
        long old_start = strtol(p, &endptr, 10);
        if (endptr > p) {
            op->old_start_line = (int)old_start;
            p = endptr;
            
            // Check for comma and count
            if (*p == ',') {
                p++; // Skip comma
                long old_count = strtol(p, &endptr, 10);
                if (endptr > p) {
                    op->old_line_count = (int)old_count;
                    p = endptr;
                }
            } else {
                op->old_line_count = 1; // Default to 1 line if no count specified
            }
        }
        
        // Skip whitespace
        while (*p && isspace((unsigned char)*p)) p++;
        
        // Parse new line info if present
        if (*p == '+') {
            p++; // Skip +
            long new_start = strtol(p, &endptr, 10);
            if (endptr > p) {
                op->new_start_line = (int)new_start;
                p = endptr;
                
                // Check for comma and count
                if (*p == ',') {
                    p++; // Skip comma
                    long new_count = strtol(p, &endptr, 10);
                    if (endptr > p) {
                        op->new_line_count = (int)new_count;
                        p = endptr;
                    }
                } else {
                    op->new_line_count = 1; // Default to 1 line if no count specified
                }
            }
        }
        
        // Skip whitespace
        while (*p && isspace((unsigned char)*p)) p++;
    }
    
    // Skip whitespace
    while (*p && isspace((unsigned char)*p)) p++;
    
    // Look for closing @@ and function context
    // Two cases: 
    // 1. "@@ -line,count +line,count @@ function_context"
    // 2. "@@ function_context @@"
    
    // Check if we're already at @@ markers (case 1: after line number parsing)
    if (*p == '@' && *(p+1) == '@') {
        // We're at the closing @@ after line numbers, look for function context after it
        const char *check_close = p;
        while (*check_close && *check_close == '@') check_close++;
        
        // Skip whitespace after @@
        while (*check_close && isspace((unsigned char)*check_close)) check_close++;
        
        // If there's content, extract it until the final @@
        if (*check_close && *check_close != '\n' && *check_close != '\r') {
            const char *context_start = check_close;
            const char *context_end = context_start;
            
            // Find the final @@
            while (*context_end && strncmp(context_end, "@@", 2) != 0) {
                context_end++;
            }
            
            if (context_end > context_start) {
                size_t context_len = (size_t)(context_end - context_start);
                op->function_context = malloc(context_len + 1);
                if (op->function_context) {
                    memcpy(op->function_context, context_start, context_len);
                    op->function_context[context_len] = '\0';
                    
                    // Trim trailing whitespace
                    char *trim_end = op->function_context + context_len - 1;
                    while (trim_end >= op->function_context && isspace((unsigned char)*trim_end)) {
                        *trim_end = '\0';
                        trim_end--;
                    }
                }
            }
        }
    } else {
        // Find the next @@ (case 2: function context before closing @@)
        const char *next_at = p;
        while (*next_at && *next_at != '@') next_at++;
        
        if (*next_at == '@' && next_at > p) {
            // Format: "@@ function_context @@" - extract content between current p and next_at
            const char *context_start = p;
            const char *context_end = next_at;
            
            size_t context_len = (size_t)(context_end - context_start);
            if (context_len > 0) {
                op->function_context = malloc(context_len + 1);
                if (op->function_context) {
                    memcpy(op->function_context, context_start, context_len);
                    op->function_context[context_len] = '\0';
                    
                    // Trim trailing whitespace
                    char *trim_end = op->function_context + context_len - 1;
                    while (trim_end >= op->function_context && isspace((unsigned char)*trim_end)) {
                        *trim_end = '\0';
                        trim_end--;
                    }
                    
                    // Trim leading whitespace
                    char *trim_start = op->function_context;
                    while (*trim_start && isspace((unsigned char)*trim_start)) {
                        trim_start++;
                    }
                    
                    // Move trimmed content to beginning if needed
                    if (trim_start > op->function_context) {
                        size_t trimmed_len = strlen(trim_start);
                        memmove(op->function_context, trim_start, trimmed_len + 1);
                    }
                }
            }
        }
    }
    
    return 1; // Successfully parsed (even if no line numbers found)
}

// Helper: search for pattern with context and function context
// Returns position of old_content within the match, or NULL if not found
static char* search_with_context(char *content, 
                                 const char *context_before,
                                 const char *old_content,
                                 const char *context_after,
                                 const char *function_context) {
    if (!content || !old_content) return NULL;
    
    // If we have function context, try to use it for better matching
    if (function_context && function_context[0] != '\0') {
        // Look for the function context in the file first
        char *func_pos = strstr(content, function_context);
        if (func_pos) {
            // Search for old_content near the function context
            // Look within a reasonable range (e.g., 500 characters before and after)
            const size_t search_range = 500;
            char *search_start = func_pos > content + search_range ? func_pos - search_range : content;
            char *func_end = func_pos + strlen(function_context);
            size_t remaining = strlen(func_end);
            char *search_end = remaining > search_range ? func_end + search_range : func_end + remaining;
            
            // Create a temporary null-terminated substring for searching
            size_t search_len = (size_t)(search_end - search_start);
            char *search_area = malloc(search_len + 1);
            if (search_area) {
                memcpy(search_area, search_start, search_len);
                search_area[search_len] = '\0';
                
                char *found_in_area = strstr(search_area, old_content);
                if (found_in_area) {
                    char *result = search_start + (found_in_area - search_area);
                    free(search_area);
                    return result;
                }
                free(search_area);
            }
        }
        // If function context search fails, fall back to normal search
    }
    
    // If no context, just use strstr
    if ((!context_before || context_before[0] == '\0') && 
        (!context_after || context_after[0] == '\0')) {
        return strstr((char*)content, old_content);
    }
    
    // Build search pattern: context_before + old_content + context_after
    size_t before_len = context_before ? strlen(context_before) : 0;
    size_t old_len = strlen(old_content);
    size_t after_len = context_after ? strlen(context_after) : 0;
    
    // Special case: only before context
    if (before_len > 0 && after_len == 0) {
        char *pos = content;
        while ((pos = strstr(pos, context_before)) != NULL) {
            // Check if old_content follows immediately after context_before
            if (strncmp(pos + before_len, old_content, old_len) == 0) {
                return pos + before_len;
            }
            pos++;
        }
        return NULL;
    }
    
    // Special case: only after context
    if (after_len > 0 && before_len == 0) {
        char *pos = content;
        while ((pos = strstr(pos, old_content)) != NULL) {
            // Check if context_after follows immediately after old_content
            if (strncmp(pos + old_len, context_after, after_len) == 0) {
                return pos;
            }
            pos++;
        }
        return NULL;
    }
    
    // Full context: before + old + after
    if (before_len > 0 && after_len > 0) {
        char *pos = content;
        while ((pos = strstr(pos, context_before)) != NULL) {
            // Check if old_content follows immediately after context_before
            if (strncmp(pos + before_len, old_content, old_len) == 0) {
                // Check if context_after follows immediately after old_content
                if (strncmp(pos + before_len + old_len, context_after, after_len) == 0) {
                    return pos + before_len;
                }
            }
            pos++;
        }
        return NULL;
    }
    
    return NULL;
}

// Check if content appears to be in the "Begin Patch/End Patch" format
int is_patch_format(const char *content) {
    if (!content) return 0;

    // Look for the telltale markers
    const char *begin = strstr(content, "*** Begin Patch");
    const char *end = strstr(content, "*** End Patch");
    const char *update_file = strstr(content, "*** Update File:");

    // Must have begin, end, and at least one file update
    return (begin != NULL && end != NULL && update_file != NULL);
}

// Parse a single operation block
static PatchOperation parse_operation(const char *block, int *error_line, char **error_msg) {
    PatchOperation op = {0};
    if (error_line) *error_line = 0;
    if (error_msg) *error_msg = NULL;

    // Extract file path from "*** Update File: path"
    const char *file_marker = strstr(block, "*** Update File:");
    if (!file_marker) {
        if (error_line) *error_line = count_lines_to_position(block, block + strlen(block));
        if (error_msg) *error_msg = strdup("Missing '*** Update File:' marker");
        LOG_ERROR("Failed to find file marker in patch block");
        return op;
    }

    // Move past "*** Update File:"
    file_marker += strlen("*** Update File:");

    // Skip whitespace
    while (isspace((unsigned char)*file_marker)) file_marker++;

    // Find end of line
    const char *line_end = strchr(file_marker, '\n');
    if (!line_end) {
        if (error_line) *error_line = count_lines_to_position(block, file_marker);
        if (error_msg) *error_msg = strdup("Missing newline after file path");
        LOG_ERROR("Failed to find end of file path line");
        return op;
    }

    // Extract file path
    size_t path_len = (size_t)(line_end - file_marker);
    char *raw_path = malloc(path_len + 1);
    if (!raw_path) {
        if (error_line) *error_line = count_lines_to_position(block, file_marker);
        if (error_msg) *error_msg = strdup("Memory allocation failed for file path");
        LOG_ERROR("Failed to allocate memory for file path");
        op.file_path = NULL;
        return op;
    }
    memcpy(raw_path, file_marker, path_len);
    raw_path[path_len] = '\0';

    // Trim whitespace, free raw buffer
    op.file_path = trim_whitespace(raw_path);
    free(raw_path);

    if (!op.file_path) {
        if (error_line) *error_line = count_lines_to_position(block, file_marker);
        if (error_msg) *error_msg = strdup("Memory allocation failed for trimmed file path");
        LOG_ERROR("Failed to allocate memory for file path");
        return op;
    }

    // Find the @@ markers
    const char *old_marker = strstr(line_end, "@@");
    if (!old_marker) {
        if (error_line) *error_line = count_lines_to_position(block, line_end);
        if (error_msg) *error_msg = strdup("Missing opening '@@' marker after file path");
        LOG_ERROR("Failed to find opening @@ marker");
        free(op.file_path);
        op.file_path = NULL;
        return op;
    }

    // Parse the @@ marker for line numbers and function context
    const char *marker_line_end = strchr(old_marker, '\n');
    if (marker_line_end) {
        size_t marker_len = (size_t)(marker_line_end - old_marker);
        char *marker_line = malloc(marker_len + 1);
        if (marker_line) {
            memcpy(marker_line, old_marker, marker_len);
            marker_line[marker_len] = '\0';
            parse_at_marker(marker_line, &op);
            free(marker_line);
        }
    }

    // Skip the opening @@
    old_marker += 2;

    // Find the next line (start of old content)
    while (*old_marker && *old_marker != '\n') old_marker++;
    if (*old_marker) old_marker++; // Skip newline

    // Now extract lines until we hit the closing @@
    const char *old_start = old_marker;
    const char *closing_marker = old_marker;
    int found_closing = 0;

    // Look for line starting with @@
    while (*closing_marker) {
        if (closing_marker == old_start || *(closing_marker - 1) == '\n') {
            if (strncmp(closing_marker, "@@", 2) == 0) {
                found_closing = 1;
                break;
            }
        }
        closing_marker++;
    }

    if (!found_closing) {
        if (error_line) *error_line = count_lines_to_position(block, old_start);
        if (error_msg) *error_msg = strdup("Missing closing '@@' marker");
        LOG_ERROR("Failed to find closing @@ marker");
        free(op.file_path);
        op.file_path = NULL;
        return op;
    }

    // Extract old and new content between the @@ markers
    // The format is lines starting with - (old) or + (new)
    // Lines starting with space are context lines
    const char *line = old_start;
    size_t old_capacity = 1024;
    size_t new_capacity = 1024;
    size_t before_capacity = 1024;
    size_t after_capacity = 1024;
    size_t old_len = 0;
    size_t new_len = 0;
    size_t before_len = 0;
    size_t after_len = 0;

    char *old_buf = malloc(old_capacity);
    char *new_buf = malloc(new_capacity);
    char *before_buf = malloc(before_capacity);
    char *after_buf = malloc(after_capacity);

    if (!old_buf || !new_buf || !before_buf || !after_buf) {
        if (error_line) *error_line = count_lines_to_position(block, old_start);
        if (error_msg) *error_msg = strdup("Memory allocation failed for content buffers");
        LOG_ERROR("Failed to allocate memory for content buffers");
        free(op.file_path);
        free(old_buf);
        free(new_buf);
        free(before_buf);
        free(after_buf);
        op.file_path = NULL;
        return op;
    }

    old_buf[0] = '\0';
    new_buf[0] = '\0';
    before_buf[0] = '\0';
    after_buf[0] = '\0';

    // Track whether we've seen any - or + lines yet
    int in_change_block = 0;

    while (line < closing_marker) {
        // Find end of current line
        const char *line_end_ptr = line;
        while (*line_end_ptr && *line_end_ptr != '\n') line_end_ptr++;

        size_t line_len = (size_t)(line_end_ptr - line);

        // Skip empty lines
        if (line_len > 0) {
            char prefix = *line;
            const char *content_start = line + 1;
            size_t content_len = line_len - 1;

            if (prefix == '-') {
                in_change_block = 1;
                // Old content - remove prefix
                if (old_len + content_len + 1 >= old_capacity) {
                    old_capacity *= 2;
                    char *new_old = realloc(old_buf, old_capacity);
                    if (!new_old) {
                        free(op.file_path);
                        free(old_buf);
                        free(new_buf);
                        free(before_buf);
                        free(after_buf);
                        op.file_path = NULL;
                        return op;
                    }
                    old_buf = new_old;
                }
                memcpy(old_buf + old_len, content_start, content_len);
                old_len += content_len;
                old_buf[old_len++] = '\n';
                old_buf[old_len] = '\0';
            } else if (prefix == '+') {
                in_change_block = 1;
                // New content - remove prefix
                if (new_len + content_len + 1 >= new_capacity) {
                    new_capacity *= 2;
                    char *new_new = realloc(new_buf, new_capacity);
                    if (!new_new) {
                        free(op.file_path);
                        free(old_buf);
                        free(new_buf);
                        free(before_buf);
                        free(after_buf);
                        op.file_path = NULL;
                        return op;
                    }
                    new_buf = new_new;
                }
                memcpy(new_buf + new_len, content_start, content_len);
                new_len += content_len;
                new_buf[new_len++] = '\n';
                new_buf[new_len] = '\0';
            } else if (prefix == ' ') {
                // Context line (space prefix)
                // Check if it's before or after the change block
                char **target_buf = NULL;
                size_t *target_len = NULL;
                size_t *target_capacity = NULL;
                
                if (in_change_block) {
                    // After change block
                    target_buf = &after_buf;
                    target_len = &after_len;
                    target_capacity = &after_capacity;
                } else {
                    // Before change block
                    target_buf = &before_buf;
                    target_len = &before_len;
                    target_capacity = &before_capacity;
                }
                
                if (*target_len + content_len + 1 >= *target_capacity) {
                    *target_capacity *= 2;
                    char *new_buf_ptr = realloc(*target_buf, *target_capacity);
                    if (!new_buf_ptr) {
                        free(op.file_path);
                        free(old_buf);
                        free(new_buf);
                        free(before_buf);
                        free(after_buf);
                        op.file_path = NULL;
                        return op;
                    }
                    *target_buf = new_buf_ptr;
                }
                memcpy(*target_buf + *target_len, content_start, content_len);
                *target_len += content_len;
                (*target_buf)[(*target_len)++] = '\n';
                (*target_buf)[*target_len] = '\0';
            }
            // Ignore lines with other prefixes (could be @@ markers or other)
        }

        // Move to next line
        line = line_end_ptr;
        if (*line == '\n') line++;
    }

    // Remove trailing newlines
    if (old_len > 0 && old_buf[old_len - 1] == '\n') {
        old_buf[--old_len] = '\0';
    }
    if (new_len > 0 && new_buf[new_len - 1] == '\n') {
        new_buf[--new_len] = '\0';
    }
    if (before_len > 0 && before_buf[before_len - 1] == '\n') {
        before_buf[--before_len] = '\0';
    }
    if (after_len > 0 && after_buf[after_len - 1] == '\n') {
        after_buf[--after_len] = '\0';
    }

    op.old_content = old_buf;
    op.new_content = new_buf;
    op.context_before = before_buf;
    op.context_after = after_buf;

    LOG_DEBUG("Parsed operation: file=%s, old_len=%zu, new_len=%zu, before_len=%zu, after_len=%zu, function_context=%s", 
              op.file_path, old_len, new_len, before_len, after_len, 
              op.function_context ? op.function_context : "none");

    return op;
}

// Parse the patch format and extract operations
ParsedPatch* parse_patch_format(const char *content) {
    ParsedPatch *patch = calloc(1, sizeof(ParsedPatch));
    if (!patch) {
        LOG_ERROR("Failed to allocate memory for ParsedPatch");
        return NULL;
    }

    // Check format
    if (!is_patch_format(content)) {
        patch->is_valid = 0;
        patch->error_message = strdup("Not a valid patch format");
        return patch;
    }

    // Extract content between Begin Patch and End Patch
    char *patch_content = extract_between(content, "*** Begin Patch", "*** End Patch");
    if (!patch_content) {
        patch->is_valid = 0;
        patch->error_message = strdup("Failed to extract patch content");
        return patch;
    }

    // Count operations (count "*** Update File:" markers)
    int op_count = 0;
    const char *marker = patch_content;
    while ((marker = strstr(marker, "*** Update File:")) != NULL) {
        op_count++;
        marker += strlen("*** Update File:");
    }

    if (op_count == 0) {
        patch->is_valid = 0;
        patch->error_message = strdup("No file operations found in patch");
        free(patch_content);
        return patch;
    }

    LOG_INFO("Found %d file operations in patch", op_count);

    // Allocate operations array
    patch->operations = calloc((size_t)op_count, sizeof(PatchOperation));
    if (!patch->operations) {
        patch->is_valid = 0;
        patch->error_message = strdup("Failed to allocate memory for operations");
        free(patch_content);
        return patch;
    }

    // Parse each operation
    const char *block_start = patch_content;
    for (int i = 0; i < op_count; i++) {
        const char *next_marker = strstr(block_start, "*** Update File:");
        if (!next_marker) break;

        // Find the end of this block (start of next block or end of content)
        const char *block_end = strstr(next_marker + strlen("*** Update File:"), "*** Update File:");
        if (!block_end) {
            block_end = patch_content + strlen(patch_content);
        }

        // Extract block
        size_t block_len = (size_t)(block_end - next_marker);
        char *block = malloc(block_len + 1);
        if (!block) {
            LOG_ERROR("Failed to allocate memory for operation block");
            break;
        }
        memcpy(block, next_marker, block_len);
        block[block_len] = '\0';

        // Parse operation
        int error_line = 0;
        char *error_msg = NULL;
        patch->operations[i] = parse_operation(block, &error_line, &error_msg);
        free(block);

        // Check if parsing succeeded
        if (!patch->operations[i].file_path) {
            LOG_ERROR("Failed to parse operation %d", i);
            patch->is_valid = 0;
            if (error_msg) {
                patch->error_message = create_error_with_line(error_msg, error_line);
                free(error_msg);
            } else {
                patch->error_message = strdup("Failed to parse operation");
            }
            free(patch_content);
            return patch;
        }

        patch->operation_count++;
        block_start = block_end;
    }

    free(patch_content);
    patch->is_valid = 1;

    LOG_INFO("Successfully parsed %d operations", patch->operation_count);

    return patch;
}

// Free a ParsedPatch structure
void free_parsed_patch(ParsedPatch *patch) {
    if (!patch) return;

    if (patch->operations) {
        for (int i = 0; i < patch->operation_count; i++) {
            free(patch->operations[i].file_path);
            free(patch->operations[i].old_content);
            free(patch->operations[i].new_content);
            free(patch->operations[i].context_before);
            free(patch->operations[i].context_after);
            free(patch->operations[i].function_context);
        }
        free(patch->operations);
    }

    free(patch->error_message);
    free(patch);
}

// Apply a parsed patch to the filesystem
cJSON* apply_patch(ParsedPatch *patch, ConversationState *state) {
    if (!patch || !patch->is_valid) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error",
            patch && patch->error_message ? patch->error_message : "Invalid patch");
        return error;
    }

    LOG_INFO("Applying patch with %d operations", patch->operation_count);

    // Apply each operation
    for (int i = 0; i < patch->operation_count; i++) {
        PatchOperation *op = &patch->operations[i];

        LOG_INFO("Applying operation %d: file=%s", i + 1, op->file_path);

        // Resolve file path
        char *resolved_path = resolve_path(op->file_path, state->working_dir);
        if (!resolved_path) {
            LOG_ERROR("Failed to resolve path: %s", op->file_path);
            cJSON *error = cJSON_CreateObject();
            char err_msg[512];
            snprintf(err_msg, sizeof(err_msg), "Failed to resolve path: %s", op->file_path);
            cJSON_AddStringToObject(error, "error", err_msg);
            return error;
        }

        // Read current file content
        char *current_content = read_file(resolved_path);
        if (!current_content) {
            LOG_ERROR("Failed to read file: %s", resolved_path);
            free(resolved_path);
            cJSON *error = cJSON_CreateObject();
            char err_msg[512];
            snprintf(err_msg, sizeof(err_msg), "Failed to read file: %s", op->file_path);
            cJSON_AddStringToObject(error, "error", err_msg);
            return error;
        }

        // Find and replace old content with new content
        // Use context lines and function context if available for more robust matching
        char *pos = search_with_context(current_content, 
                                        op->context_before,
                                        op->old_content,
                                        op->context_after,
                                        op->function_context);
        if (!pos) {
            LOG_ERROR("Old content not found in file: %s", resolved_path);
            free(current_content);
            free(resolved_path);
            cJSON *error = cJSON_CreateObject();
            char err_msg[512];
            snprintf(err_msg, sizeof(err_msg),
                "Old content not found in file: %s. File may have changed.", op->file_path);
            cJSON_AddStringToObject(error, "error", err_msg);
            return error;
        }

        // Build new file content
        size_t old_len = strlen(op->old_content);
        size_t new_len = strlen(op->new_content);
        size_t current_len = strlen(current_content);
        size_t offset = (size_t)(pos - current_content);

        char *new_file_content = malloc(current_len - old_len + new_len + 1);
        if (!new_file_content) {
            LOG_ERROR("Failed to allocate memory for new content");
            free(current_content);
            free(resolved_path);
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "Out of memory");
            return error;
        }

        // Copy: before old content + new content + after old content
        memcpy(new_file_content, current_content, offset);
        memcpy(new_file_content + offset, op->new_content, new_len);
        memcpy(new_file_content + offset + new_len,
               current_content + offset + old_len,
               current_len - offset - old_len + 1);

        // Write back to file
        int ret = write_file(resolved_path, new_file_content);

        free(current_content);
        free(new_file_content);
        free(resolved_path);

        if (ret != 0) {
            LOG_ERROR("Failed to write file: %s", op->file_path);
            cJSON *error = cJSON_CreateObject();
            char err_msg[512];
            snprintf(err_msg, sizeof(err_msg), "Failed to write file: %s", op->file_path);
            cJSON_AddStringToObject(error, "error", err_msg);
            return error;
        }

        LOG_INFO("Successfully applied operation %d", i + 1);
    }

    // Success
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "success");
    cJSON_AddNumberToObject(result, "operations_applied", patch->operation_count);

    LOG_INFO("Patch applied successfully");

    return result;
}
