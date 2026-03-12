/*
 * tool_filesystem.c - Filesystem tool implementations
 */

#include "tool_filesystem.h"
#include "../klawed_internal.h"
#include "../util/file_utils.h"
#include "../util/diff_utils.h"
#include "../util/output_utils.h"
#include "../util/redact_utils.h"
#define COLORSCHEME_EXTERN 1
#include "../colorscheme.h"
#include "../fallback_colors.h"
#include "../logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <glob.h>
#include <limits.h>
#include <pthread.h>

// ============================================================================
// Read Tool
// ============================================================================

cJSON* tool_read(cJSON *params, ConversationState *state) {
    // Check for verbose tool logging
    int tool_verbose = 0;
    const char *tool_verbose_env = getenv("KLAWED_TOOL_VERBOSE");
    if (tool_verbose_env) {
        tool_verbose = atoi(tool_verbose_env);
        if (tool_verbose < 0) tool_verbose = 0;
        if (tool_verbose > 2) tool_verbose = 2;
    }

    const cJSON *path_json = cJSON_GetObjectItem(params, "file_path");
    if (!path_json || !cJSON_IsString(path_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing 'file_path' parameter");
        return error;
    }

    const char *file_path = path_json->valuestring;

    // Verbose logging for Read tool
    if (tool_verbose >= 1) {
        LOG_DEBUG("[TOOL VERBOSE] Read tool reading file: %s", file_path);
    }

    // Get optional line range parameters
    const cJSON *start_line_json = cJSON_GetObjectItem(params, "start_line");
    const cJSON *end_line_json = cJSON_GetObjectItem(params, "end_line");

    int start_line = -1;  // -1 means no limit
    int end_line = -1;    // -1 means no limit

    if (start_line_json && cJSON_IsNumber(start_line_json)) {
        start_line = start_line_json->valueint;
        if (start_line < 1) {
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "start_line must be >= 1");
            return error;
        }
        if (tool_verbose >= 2) {
            LOG_DEBUG("[TOOL VERBOSE] Reading from line %d", start_line);
        }
    }

    if (end_line_json && cJSON_IsNumber(end_line_json)) {
        end_line = end_line_json->valueint;
        if (end_line < 1) {
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "end_line must be >= 1");
            return error;
        }
        if (tool_verbose >= 2) {
            LOG_DEBUG("[TOOL VERBOSE] Reading to line %d", end_line);
        }
    }

    // Validate line range
    if (start_line > 0 && end_line > 0 && start_line > end_line) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "start_line must be <= end_line");
        return error;
    }

    char *resolved_path = resolve_path(path_json->valuestring, state->working_dir);
    if (!resolved_path) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to resolve path");
        return error;
    }

    char *content = read_file(resolved_path);
    free(resolved_path);

    if (!content) {
        cJSON *error = cJSON_CreateObject();
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "Failed to read file: %s", strerror(errno));
        cJSON_AddStringToObject(error, "error", err_msg);
        return error;
    }

    // If line range is specified, extract only those lines
    char *filtered_content = content;
    int total_lines = 0;

    if (start_line > 0 || end_line > 0) {
        // Count total lines and build filtered content
        char *result_buffer = NULL;
        size_t result_size = 0;
        int current_line = 1;
        char *line_start = content;
        char *pos = content;

        while (*pos) {
            // CRITICAL: Add cancellation point for large file processing
            if (current_line % 1000 == 0) {
                pthread_testcancel();
            }

            if (*pos == '\n') {
                // Found end of line
                int line_len = (int)(pos - line_start + 1);  // Include the newline

                // Check if this line should be included
                int include = 1;
                if (start_line > 0 && current_line < start_line) include = 0;
                if (end_line > 0 && current_line > end_line) include = 0;

                if (include) {
                    // Add this line to result
                    char *new_buffer = realloc(result_buffer, result_size + (size_t)line_len + 1);
                    if (!new_buffer) {
                        free(result_buffer);
                        free(content);
                        cJSON *error = cJSON_CreateObject();
                        cJSON_AddStringToObject(error, "error", "Out of memory");
                        return error;
                    }
                    result_buffer = new_buffer;
                    memcpy(result_buffer + result_size, line_start, (size_t)line_len);
                    result_size += (size_t)line_len;
                    result_buffer[result_size] = '\0';
                }

                current_line++;
                line_start = pos + 1;

                // Stop if we've reached end_line
                if (end_line > 0 && current_line > end_line) {
                    break;
                }
            }
            pos++;
        }

        // Handle last line (if file doesn't end with newline)
        if (*line_start && (end_line < 0 || current_line <= end_line) &&
            (start_line < 0 || current_line >= start_line)) {
            int line_len = (int)strlen(line_start);
            char *new_buffer = realloc(result_buffer, result_size + (size_t)line_len + 1);
            if (!new_buffer) {
                free(result_buffer);
                free(content);
                cJSON *error = cJSON_CreateObject();
                cJSON_AddStringToObject(error, "error", "Out of memory");
                return error;
            }
            result_buffer = new_buffer;
            memcpy(result_buffer + result_size, line_start, (size_t)line_len);
            result_size += (size_t)line_len;
            result_buffer[result_size] = '\0';
            current_line++;
        }

        total_lines = current_line - 1;

        if (!result_buffer) {
            result_buffer = strdup("");
        }

        free(content);
        filtered_content = result_buffer;
    } else {
        // Count total lines for the full file
        char *pos = content;
        total_lines = 0;
        while (*pos) {
            if (*pos == '\n') total_lines++;
            pos++;
        }
        if (pos > content && *(pos-1) != '\n') total_lines++;  // Last line without newline
    }

    /* Redact secrets from file content (catches .env, credentials files, etc.) */
    char *redacted_content = redact_sensitive_text(filtered_content);

    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "content", redacted_content ? redacted_content : filtered_content);
    free(redacted_content);
    cJSON_AddNumberToObject(result, "total_lines", total_lines);

    if (start_line > 0 || end_line > 0) {
        cJSON_AddNumberToObject(result, "start_line", start_line > 0 ? start_line : 1);
        cJSON_AddNumberToObject(result, "end_line", end_line > 0 ? end_line : total_lines);
    }

    free(filtered_content);

    return result;
}

// ============================================================================
// Write Tool
// ============================================================================

cJSON* tool_write(cJSON *params, ConversationState *state) {
    // Check for verbose tool logging
    int tool_verbose = 0;
    const char *tool_verbose_env = getenv("KLAWED_TOOL_VERBOSE");
    if (tool_verbose_env) {
        tool_verbose = atoi(tool_verbose_env);
        if (tool_verbose < 0) tool_verbose = 0;
        if (tool_verbose > 2) tool_verbose = 2;
    }

    const cJSON *path_json = cJSON_GetObjectItem(params, "file_path");
    const cJSON *content_json = cJSON_GetObjectItem(params, "content");

    if (!path_json || !cJSON_IsString(path_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing 'file_path' parameter");
        return error;
    }

    if (!content_json || !cJSON_IsString(content_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error",
            "Missing required 'content' parameter. Write tool requires both 'file_path' and 'content' parameters.");
        return error;
    }

    const char *file_path = path_json->valuestring;
    const char *content = content_json->valuestring;

    // Verbose logging for Write tool
    if (tool_verbose >= 1) {
        LOG_DEBUG("[TOOL VERBOSE] Write tool writing to file: %s", file_path);
        if (tool_verbose >= 2) {
            size_t content_len = strlen(content);
            LOG_DEBUG("[TOOL VERBOSE] Content length: %zu bytes", content_len);
            if (content_len > 0 && content_len <= 200) {
                LOG_DEBUG("[TOOL VERBOSE] Content preview (first 200 chars):\n%.200s", content);
            } else if (content_len > 200) {
                LOG_DEBUG("[TOOL VERBOSE] Content preview (first 200 chars):\n%.200s...", content);
            }
        }
    }

    char *resolved_path = resolve_path(path_json->valuestring, state->working_dir);
    if (!resolved_path) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to resolve path");
        return error;
    }

    // Check if file exists and read original content for diff
    char *original_content = NULL;
    FILE *existing_file = fopen(resolved_path, "r");
    if (existing_file) {
        fclose(existing_file);
        original_content = read_file(resolved_path);
        if (!original_content) {
            free(resolved_path);
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "Failed to read existing file for diff comparison");
            return error;
        }
    }

    int ret = write_file(resolved_path, content_json->valuestring);

    // Show diff if write was successful
    if (ret == 0) {
        if (original_content) {
            show_diff(resolved_path, original_content);
        } else {
            // New file creation - show content as diff with all lines added
            char header[PATH_MAX + 64];
            snprintf(header, sizeof(header), "--- Created new file: %s ---", resolved_path);
            tool_emit_line(" ", header);

            // Get color for added lines
            char add_color[32];
            const char *add_color_str;
            if (get_colorscheme_color(COLORSCHEME_DIFF_ADD, add_color, sizeof(add_color)) == 0) {
                add_color_str = add_color;
            } else {
                add_color_str = ANSI_FALLBACK_DIFF_ADD;
            }

            // Show each line of the new file as an added line
            const char *line_start = content_json->valuestring;
            const char *line_end;
            char line_buf[1024];

            while (*line_start) {
                line_end = strchr(line_start, '\n');
                if (line_end) {
                    ptrdiff_t diff = line_end - line_start;
                    size_t line_len = (diff > 0) ? (size_t)diff : 0;
                    if (line_len >= sizeof(line_buf) - 2) {
                        line_len = sizeof(line_buf) - 3;  // Leave room for +, newline, and null
                    }
                    snprintf(line_buf, sizeof(line_buf), "+%.*s\n", (int)line_len, line_start);
                    emit_diff_line(line_buf, add_color_str, add_color_str);
                    line_start = line_end + 1;
                } else {
                    // Last line without newline
                    snprintf(line_buf, sizeof(line_buf), "+%s\n", line_start);
                    emit_diff_line(line_buf, add_color_str, add_color_str);
                    break;
                }
            }
        }
    }

    free(resolved_path);
    free(original_content);

    if (ret != 0) {
        cJSON *error = cJSON_CreateObject();
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "Failed to write file: %s", strerror(errno));
        cJSON_AddStringToObject(error, "error", err_msg);
        return error;
    }

    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "success");

    return result;
}

// ============================================================================
// Edit Tool
// ============================================================================

cJSON* tool_edit(cJSON *params, ConversationState *state) {
    const cJSON *path_json = cJSON_GetObjectItem(params, "file_path");
    const cJSON *old_json = cJSON_GetObjectItem(params, "old_string");
    const cJSON *new_json = cJSON_GetObjectItem(params, "new_string");

    if (!path_json || !cJSON_IsString(path_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error",
            "Missing required 'file_path' parameter. Edit tool requires 'file_path', 'old_string', and 'new_string'.");
        return error;
    }

    if (!old_json || !cJSON_IsString(old_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error",
            "Missing required 'old_string' parameter. Edit tool requires 'file_path', 'old_string', and 'new_string'.");
        return error;
    }

    if (!new_json || !cJSON_IsString(new_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error",
            "Missing required 'new_string' parameter. Edit tool requires 'file_path', 'old_string', and 'new_string'.");
        return error;
    }

    char *resolved_path = resolve_path(path_json->valuestring, state->working_dir);
    if (!resolved_path) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to resolve path");
        return error;
    }

    char *content = read_file(resolved_path);
    if (!content) {
        free(resolved_path);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to read file");
        return error;
    }

    // Save original content for diff comparison
    char *original_content = strdup(content);
    if (!original_content) {
        free(content);
        free(resolved_path);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to allocate memory for diff");
        return error;
    }

    const char *old_str = old_json && cJSON_IsString(old_json) ? old_json->valuestring : NULL;
    const char *new_str = new_json->valuestring;
    char *new_content = NULL;
    int replace_count = 0;

    // Simple string single replace (only mode supported now)
    char *pos = old_str ? strstr(content, old_str) : NULL;
    if (pos) {
        replace_count = 1;
        size_t old_len = strlen(old_str);
        size_t new_len = strlen(new_str);
        size_t content_len = strlen(content);
        size_t offset = (size_t)(pos - content);

        new_content = malloc(content_len - old_len + new_len + 1);
        if (new_content) {
            memcpy(new_content, content, offset);
            memcpy(new_content + offset, new_str, new_len);
            memcpy(new_content + offset + new_len, content + offset + old_len,
                   content_len - offset - old_len + 1);
        }
    }

    if (!new_content) {
        free(content);
        free(original_content);
        free(resolved_path);
        cJSON *error = cJSON_CreateObject();
        if (replace_count == 0) {
            cJSON_AddStringToObject(error, "error", "String not found in file");
        } else {
            cJSON_AddStringToObject(error, "error", "Out of memory");
        }
        return error;
    }

    int ret = write_file(resolved_path, new_content);

    // Show diff if edit was successful
    if (ret == 0) {
        show_diff(resolved_path, original_content);
    }

    free(content);
    free(new_content);
    free(resolved_path);
    free(original_content);

    if (ret != 0) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to write file");
        return error;
    }

    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "success");
    cJSON_AddNumberToObject(result, "replacements", replace_count);
    return result;
}

// ============================================================================
// MultiEdit Tool
// ============================================================================

cJSON* tool_multiedit(cJSON *params, ConversationState *state) {
    const cJSON *path_json = cJSON_GetObjectItem(params, "file_path");
    const cJSON *edits_json = cJSON_GetObjectItem(params, "edits");

    if (!path_json || !cJSON_IsString(path_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error",
            "Missing required 'file_path' parameter. MultiEdit requires 'file_path' (string) and 'edits' (array).");
        return error;
    }

    if (!edits_json || !cJSON_IsArray(edits_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error",
            "Missing required 'edits' parameter. MultiEdit requires 'file_path' (string) and 'edits' (array of {old_string, new_string}).");
        return error;
    }

    char *resolved_path = resolve_path(path_json->valuestring, state->working_dir);
    if (!resolved_path) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to resolve path");
        return error;
    }

    char *content = read_file(resolved_path);
    if (!content) {
        free(resolved_path);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to read file");
        return error;
    }

    // Save original content for diff comparison
    char *original_content = strdup(content);
    if (!original_content) {
        free(content);
        free(resolved_path);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to allocate memory for diff");
        return error;
    }

    char *current_content = content;
    int total_replacements = 0;
    int failed_edits = 0;
    cJSON *edit_item = NULL;
    cJSON_ArrayForEach(edit_item, edits_json) {
        if (!cJSON_IsObject(edit_item)) {
            failed_edits++;
            continue;
        }

        const cJSON *old_json = cJSON_GetObjectItem(edit_item, "old_string");
        const cJSON *new_json = cJSON_GetObjectItem(edit_item, "new_string");

        if (!old_json || !new_json || !cJSON_IsString(old_json) || !cJSON_IsString(new_json)) {
            failed_edits++;
            continue;
        }

        const char *old_str = old_json->valuestring;
        const char *new_str = new_json->valuestring;

        char *pos = strstr(current_content, old_str);
        if (!pos) {
            failed_edits++;
            continue;
        }

        size_t old_len = strlen(old_str);
        size_t new_len = strlen(new_str);
        size_t content_len = strlen(current_content);
        size_t offset = (size_t)(pos - current_content);

        char *new_content = malloc(content_len - old_len + new_len + 1);
        if (!new_content) {
            failed_edits++;
            continue;
        }

        memcpy(new_content, current_content, offset);
        memcpy(new_content + offset, new_str, new_len);
        memcpy(new_content + offset + new_len, current_content + offset + old_len,
               content_len - offset - old_len + 1);

        free(current_content);
        current_content = new_content;
        total_replacements++;
    }

    int ret = write_file(resolved_path, current_content);

    // Show diff if edit was successful
    if (ret == 0) {
        show_diff(resolved_path, original_content);
    }

    free(current_content);
    free(original_content);
    free(resolved_path);

    if (ret != 0) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to write file");
        return error;
    }

    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "success");
    cJSON_AddNumberToObject(result, "total_replacements", total_replacements);
    cJSON_AddNumberToObject(result, "failed_edits", failed_edits);
    return result;
}

// ============================================================================
// Glob Tool
// ============================================================================

cJSON* tool_glob(cJSON *params, ConversationState *state) {
    const cJSON *pattern_json = cJSON_GetObjectItem(params, "pattern");
    if (!pattern_json || !cJSON_IsString(pattern_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing 'pattern' parameter");
        return error;
    }

    const char *pattern = pattern_json->valuestring;
    cJSON *result = cJSON_CreateObject();
    cJSON *files = cJSON_CreateArray();
    int total_count = 0;

    // Search in main working directory
    char full_pattern[PATH_MAX];
    snprintf(full_pattern, sizeof(full_pattern), "%s/%s", state->working_dir, pattern);

    glob_t glob_result;
    int ret = glob(full_pattern, GLOB_TILDE, NULL, &glob_result);

    if (ret == 0) {
        for (size_t i = 0; i < glob_result.gl_pathc; i++) {
            cJSON_AddItemToArray(files, cJSON_CreateString(glob_result.gl_pathv[i]));
            total_count++;
        }
        globfree(&glob_result);
    }

    // Search in additional working directories
    for (int dir_idx = 0; dir_idx < state->additional_dirs_count; dir_idx++) {
        snprintf(full_pattern, sizeof(full_pattern), "%s/%s",
                 state->additional_dirs[dir_idx], pattern);

        ret = glob(full_pattern, GLOB_TILDE, NULL, &glob_result);

        if (ret == 0) {
            for (size_t i = 0; i < glob_result.gl_pathc; i++) {
                cJSON_AddItemToArray(files, cJSON_CreateString(glob_result.gl_pathv[i]));
                total_count++;
            }
            globfree(&glob_result);
        }
    }

    cJSON_AddItemToObject(result, "files", files);
    cJSON_AddNumberToObject(result, "count", total_count);

    return result;
}
