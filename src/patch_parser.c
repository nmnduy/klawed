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
static PatchOperation parse_operation(const char *block) {
    PatchOperation op = {0};

    // Extract file path from "*** Update File: path"
    const char *file_marker = strstr(block, "*** Update File:");
    if (!file_marker) {
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
        LOG_ERROR("Failed to find end of file path line");
        return op;
    }

    // Extract file path
    size_t path_len = (size_t)(line_end - file_marker);
    char *raw_path = malloc(path_len + 1);
    if (!raw_path) {
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
        LOG_ERROR("Failed to allocate memory for file path");
        return op;
    }

    // Find the @@ markers
    const char *old_marker = strstr(line_end, "@@");
    if (!old_marker) {
        LOG_ERROR("Failed to find opening @@ marker");
        free(op.file_path);
        op.file_path = NULL;
        return op;
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
        LOG_ERROR("Failed to find closing @@ marker");
        free(op.file_path);
        op.file_path = NULL;
        return op;
    }

    // Extract old and new content between the @@ markers
    // The format is lines starting with - (old) or + (new)
    const char *line = old_start;
    size_t old_capacity = 1024;
    size_t new_capacity = 1024;
    size_t old_len = 0;
    size_t new_len = 0;

    char *old_buf = malloc(old_capacity);
    char *new_buf = malloc(new_capacity);

    if (!old_buf || !new_buf) {
        LOG_ERROR("Failed to allocate memory for content buffers");
        free(op.file_path);
        free(old_buf);
        free(new_buf);
        op.file_path = NULL;
        return op;
    }

    old_buf[0] = '\0';
    new_buf[0] = '\0';

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
                // Old content - remove prefix
                if (old_len + content_len + 1 >= old_capacity) {
                    old_capacity *= 2;
                    char *new_old = realloc(old_buf, old_capacity);
                    if (!new_old) {
                        free(op.file_path);
                        free(old_buf);
                        free(new_buf);
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
                // New content - remove prefix
                if (new_len + content_len + 1 >= new_capacity) {
                    new_capacity *= 2;
                    char *new_new = realloc(new_buf, new_capacity);
                    if (!new_new) {
                        free(op.file_path);
                        free(old_buf);
                        free(new_buf);
                        op.file_path = NULL;
                        return op;
                    }
                    new_buf = new_new;
                }
                memcpy(new_buf + new_len, content_start, content_len);
                new_len += content_len;
                new_buf[new_len++] = '\n';
                new_buf[new_len] = '\0';
            }
            // Ignore lines without +/- prefix
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

    op.old_content = old_buf;
    op.new_content = new_buf;

    LOG_DEBUG("Parsed operation: file=%s, old_len=%zu, new_len=%zu", op.file_path, old_len, new_len);

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
        patch->operations[i] = parse_operation(block);
        free(block);

        // Check if parsing succeeded
        if (!patch->operations[i].file_path) {
            LOG_ERROR("Failed to parse operation %d", i);
            patch->is_valid = 0;
            patch->error_message = strdup("Failed to parse operation");
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
        char *pos = strstr(current_content, op->old_content);
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
