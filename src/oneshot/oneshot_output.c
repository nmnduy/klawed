/*
 * Oneshot Output Formatting
 *
 * Handles output formatting for oneshot mode execution.
 * Supports both human-readable and machine-readable (HTML+JSON) formats.
 */

#include "oneshot_output.h"
#include "oneshot_ui.h"
#include "../logger.h"
#include "../ui/tool_output_display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <time.h>

/**
 * Parse output format from environment variable
 */
OneshotFormat oneshot_get_output_format(void) {
    const char *output_format = getenv("KLAWED_ONESHOT_FORMAT");
    if (!output_format) {
        LOG_DEBUG("Oneshot mode: using default human-readable output format");
        return ONESHOT_FORMAT_HUMAN;
    }

    if (strcmp(output_format, "json") == 0 || strcmp(output_format, "machine") == 0) {
        LOG_DEBUG("Oneshot mode: using machine-readable output format");
        return ONESHOT_FORMAT_MACHINE;
    } else if (strcmp(output_format, "human") == 0 || strcmp(output_format, "clean") == 0) {
        LOG_DEBUG("Oneshot mode: using human-readable output format");
        return ONESHOT_FORMAT_HUMAN;
    } else {
        LOG_WARN("Unknown KLAWED_ONESHOT_FORMAT value: %s, using default (human-readable)",
                 output_format);
        return ONESHOT_FORMAT_HUMAN;
    }
}

/**
 * Get current timestamp as a formatted string
 */
void oneshot_get_timestamp(char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return;
    }

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    if (tm_info) {
        strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", tm_info);
    } else {
        buffer[0] = '\0';
    }
}

/**
 * Print tool execution in machine-readable format (HTML+JSON)
 */
void oneshot_print_machine_format(const char *tool_name,
                                   const char *tool_details,
                                   cJSON *tool_result) {
    if (!tool_name) {
        LOG_ERROR("oneshot_print_machine_format: tool_name is NULL");
        return;
    }

    // Build details with timestamp
    char details_with_timestamp[384] = {0}; // 256 for details + 128 for timestamp
    if (tool_details && tool_details[0] != '\0') {
        char timestamp[32] = {0};
        oneshot_get_timestamp(timestamp, sizeof(timestamp));
        snprintf(details_with_timestamp, sizeof(details_with_timestamp),
                 "%s (%s)", tool_details, timestamp);
    } else {
        char timestamp[32] = {0};
        oneshot_get_timestamp(timestamp, sizeof(timestamp));
        snprintf(details_with_timestamp, sizeof(details_with_timestamp),
                 "(%s)", timestamp);
    }

    // Print opening HTML-style tag with tool name
    printf("<tool name=\"%s\"", tool_name);

    // Add details attribute if present
    if (details_with_timestamp[0] != '\0') {
        // Escape XML special characters in details
        // Worst case: every character could be & or " requiring 6 chars each
        size_t max_escaped_len = strlen(details_with_timestamp) * 6 + 1;
        char *escaped_details = malloc(max_escaped_len);
        if (escaped_details) {
            size_t j = 0;
            for (size_t k = 0; details_with_timestamp[k] != '\0'; k++) {
                char c = details_with_timestamp[k];
                if (c == '"') {
                    // &quot;
                    escaped_details[j++] = '&';
                    escaped_details[j++] = 'q';
                    escaped_details[j++] = 'u';
                    escaped_details[j++] = 'o';
                    escaped_details[j++] = 't';
                    escaped_details[j++] = ';';
                } else if (c == '&') {
                    // &amp;
                    escaped_details[j++] = '&';
                    escaped_details[j++] = 'a';
                    escaped_details[j++] = 'm';
                    escaped_details[j++] = 'p';
                    escaped_details[j++] = ';';
                } else if (c == '<') {
                    // &lt;
                    escaped_details[j++] = '&';
                    escaped_details[j++] = 'l';
                    escaped_details[j++] = 't';
                    escaped_details[j++] = ';';
                } else if (c == '>') {
                    // &gt;
                    escaped_details[j++] = '&';
                    escaped_details[j++] = 'g';
                    escaped_details[j++] = 't';
                    escaped_details[j++] = ';';
                } else {
                    escaped_details[j++] = c;
                }
            }
            escaped_details[j] = '\0';
            printf(" details=\"%s\"", escaped_details);
            free(escaped_details);
        }
    }

    printf(">\n");
    fflush(stdout);

    // Print tool result as JSON content inside the tag
    if (tool_result) {
        char *result_str = cJSON_Print(tool_result);
        if (result_str) {
            printf("%s\n", result_str);
            free(result_str);
        }
    }

    // Print closing HTML-style tag
    printf("</tool>\n");
    fflush(stdout);
}

/**
 * Print tool execution in human-readable format with enhanced theming
 */
void oneshot_print_human_format(const char *tool_name,
                                 const char *tool_details,
                                 cJSON *tool_result) {
    // Initialize UI system if not already done
    static int ui_initialized = 0;
    if (!ui_initialized) {
        oneshot_ui_init();
        ui_initialized = 1;
    }

    OneshotUIStyle style = oneshot_ui_get_style();
    int is_compact = (style == ONESHOT_UI_STYLE_COMPACT || style == ONESHOT_UI_STYLE_MINIMAL);

    // Print tool header with themed styling
    oneshot_ui_print_tool_header(tool_name, tool_details, is_compact);

    // Determine status and print result
    OneshotStatus status = ONESHOT_STATUS_SUCCESS;
    char summary[256] = {0};

    if (tool_result) {
        // Check for error in result
        cJSON *error = cJSON_GetObjectItem(tool_result, "error");
        cJSON *exit_code = cJSON_GetObjectItem(tool_result, "exit_code");

        if (error && !cJSON_IsNull(error)) {
            status = ONESHOT_STATUS_ERROR;
        } else if (exit_code && cJSON_IsNumber(exit_code) && exit_code->valueint != 0) {
            status = ONESHOT_STATUS_ERROR;
        }

        // Print tool-specific formatted output
        print_tool_output_formatted(tool_name, tool_result, summary, sizeof(summary), is_compact);
    }

    // Print footer with status
    oneshot_ui_print_tool_footer(status, summary[0] ? summary : NULL, is_compact);
}

/**
 * Helper: Print formatted output based on tool type
 * Extracted from print_human_readable_tool_output for consistency
 */
void print_tool_output_formatted(const char *tool_name, cJSON *tool_result,
                                  char *summary, size_t summary_size, int is_compact) {
    if (!tool_name || !tool_result || !summary) return;

    summary[0] = '\0';

    if (strcmp(tool_name, "Bash") == 0) {
        cJSON *exit_code = cJSON_GetObjectItem(tool_result, "exit_code");
        cJSON *output = cJSON_GetObjectItem(tool_result, "output");

        if (exit_code && cJSON_IsNumber(exit_code)) {
            if (exit_code->valueint != 0) {
                snprintf(summary, summary_size, "exit code %d", exit_code->valueint);
            } else if (output && cJSON_IsString(output)) {
                // Count lines for summary
                const char *out = output->valuestring;
                int lines = 0;
                for (const char *p = out; *p; p++) {
                    if (*p == '\n') lines++;
                }
                if (lines > 0 || strlen(out) > 0) {
                    snprintf(summary, summary_size, "%d lines", lines > 0 ? lines : 1);
                }
            }
        }

        if (output && cJSON_IsString(output)) {
            const char *output_str = output->valuestring;
            if (output_str && strlen(output_str) > 0) {
                oneshot_ui_print_content(output_str, 1);
            }
        }
    }
    else if (strcmp(tool_name, "Read") == 0) {
        cJSON *content = cJSON_GetObjectItem(tool_result, "content");
        cJSON *start_line = cJSON_GetObjectItem(tool_result, "start_line");
        cJSON *end_line = cJSON_GetObjectItem(tool_result, "end_line");

        if (content && cJSON_IsString(content)) {
            const char *content_str = content->valuestring;
            if (content_str && strlen(content_str) > 0) {
                // Count lines
                int lines = 0;
                for (const char *p = content_str; *p; p++) {
                    if (*p == '\n') lines++;
                }

                if (start_line && end_line && cJSON_IsNumber(start_line) && cJSON_IsNumber(end_line)) {
                    snprintf(summary, summary_size, "lines %d-%d", start_line->valueint, end_line->valueint);
                } else {
                    snprintf(summary, summary_size, "%d lines", lines > 0 ? lines : 1);
                }

                oneshot_ui_print_content(content_str, 0);
            }
        }
    }
    else if (strcmp(tool_name, "Grep") == 0) {
        cJSON *match_count = cJSON_GetObjectItem(tool_result, "match_count");
        cJSON *total_matches = cJSON_GetObjectItem(tool_result, "total_matches");
        cJSON *matches = cJSON_GetObjectItem(tool_result, "matches");

        int shown = 0, total = 0;
        if (match_count && cJSON_IsNumber(match_count)) {
            shown = match_count->valueint;
        }
        if (total_matches && cJSON_IsNumber(total_matches)) {
            total = total_matches->valueint;
        } else {
            total = shown;
        }

        if (shown < total) {
            snprintf(summary, summary_size, "%d/%d matches (showing first %d)", shown, total, shown);
        } else {
            snprintf(summary, summary_size, "%d match%s", total, total == 1 ? "" : "es");
        }

        if (matches && cJSON_IsArray(matches)) {
            int array_size = cJSON_GetArraySize(matches);
            if (array_size > 0) {
                int display_limit = 20;
                const char *display_env = getenv("KLAWED_GREP_DISPLAY_LIMIT");
                if (display_env) {
                    int val = atoi(display_env);
                    if (val > 0) display_limit = val;
                }

                for (int i = 0; i < array_size && i < display_limit; i++) {
                    cJSON *match = cJSON_GetArrayItem(matches, i);
                    if (match && cJSON_IsString(match)) {
                        char line[1024];
                        snprintf(line, sizeof(line), "  %s\n", match->valuestring);
                        oneshot_ui_print_content(line, 0);
                    }
                }
                if (array_size > display_limit) {
                    char more[64];
                    snprintf(more, sizeof(more), "  ... and %d more\n", array_size - display_limit);
                    oneshot_ui_print_content(more, 0);
                }
            }
        }
    }
    else if (strcmp(tool_name, "Glob") == 0) {
        cJSON *files = cJSON_GetObjectItem(tool_result, "files");

        if (files && cJSON_IsArray(files)) {
            int array_size = cJSON_GetArraySize(files);
            snprintf(summary, summary_size, "%d file%s", array_size, array_size == 1 ? "" : "s");

            if (array_size > 0) {
                int display_limit = 10;
                const char *display_env = getenv("KLAWED_GLOB_DISPLAY_LIMIT");
                if (display_env) {
                    int val = atoi(display_env);
                    if (val > 0) display_limit = val;
                }

                for (int i = 0; i < array_size && i < display_limit; i++) {
                    cJSON *file = cJSON_GetArrayItem(files, i);
                    if (file && cJSON_IsString(file)) {
                        char line[512];
                        snprintf(line, sizeof(line), "  %s\n", file->valuestring);
                        oneshot_ui_print_content(line, 0);
                    }
                }
                if (array_size > display_limit) {
                    char more[64];
                    snprintf(more, sizeof(more), "  ... and %d more\n", array_size - display_limit);
                    oneshot_ui_print_content(more, 0);
                }
            }
        }
    }
    else if (strcmp(tool_name, "TodoWrite") == 0) {
        cJSON *status = cJSON_GetObjectItem(tool_result, "status");
        cJSON *total = cJSON_GetObjectItem(tool_result, "total");

        if (status && cJSON_IsString(status) && strcmp(status->valuestring, "success") == 0) {
            if (total && cJSON_IsNumber(total)) {
                snprintf(summary, summary_size, "%d task%s", total->valueint,
                        total->valueint == 1 ? "" : "s");
            }
        }
    }
    else if (strcmp(tool_name, "Edit") == 0 || strcmp(tool_name, "MultiEdit") == 0) {
        cJSON *status = cJSON_GetObjectItem(tool_result, "status");
        cJSON *replacements = cJSON_GetObjectItem(tool_result, "replacements");

        if (status && cJSON_IsString(status) && strcmp(status->valuestring, "success") == 0) {
            if (replacements && cJSON_IsNumber(replacements)) {
                snprintf(summary, summary_size, "%d change%s", replacements->valueint,
                        replacements->valueint == 1 ? "" : "s");
            } else {
                snprintf(summary, summary_size, "success");
            }
        }
    }
    else if (strcmp(tool_name, "Subagent") == 0) {
        cJSON *exit_code = cJSON_GetObjectItem(tool_result, "exit_code");
        cJSON *output = cJSON_GetObjectItem(tool_result, "output");

        if (exit_code && cJSON_IsNumber(exit_code)) {
            snprintf(summary, summary_size, "exit code %d", exit_code->valueint);
        }

        if (output && cJSON_IsString(output)) {
            const char *output_str = output->valuestring;
            if (output_str && strlen(output_str) > 0 && !is_compact) {
                // Show last few lines
                char *output_copy = strdup(output_str);
                if (output_copy) {
                    int line_count = 0;
                    char *p = output_copy;
                    while (*p) {
                        if (*p == '\n') line_count++;
                        p++;
                    }

                    if (line_count > 3) {
                        oneshot_ui_print_content("  ...\n", 0);
                        int lines_to_skip = line_count - 3;
                        p = output_copy;
                        while (lines_to_skip > 0 && *p) {
                            if (*p == '\n') lines_to_skip--;
                            p++;
                        }
                        oneshot_ui_print_content(p, 0);
                    } else {
                        oneshot_ui_print_content(output_str, 0);
                    }
                    free(output_copy);
                }
            }
        }
    }
    else {
        // Generic fallback
        char *result_str = cJSON_Print(tool_result);
        if (result_str) {
            // Truncate if too long
            size_t len = strlen(result_str);
            if (len > 100) {
                result_str[100] = '\0';
                snprintf(summary, summary_size, "%.100s...", result_str);
            } else {
                strlcpy(summary, result_str, summary_size);
            }

            if (!is_compact) {
                oneshot_ui_print_content(result_str, 0);
            }
            free(result_str);
        }
    }
}
