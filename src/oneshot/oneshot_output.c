/*
 * Oneshot Output Formatting
 *
 * Handles output formatting for oneshot mode execution.
 * Supports both human-readable and machine-readable (HTML+JSON) formats.
 */

#include "oneshot_output.h"
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
 * Print tool execution in human-readable format
 */
void oneshot_print_human_format(const char *tool_name,
                                 const char *tool_details,
                                 cJSON *tool_result) {
    // Use the existing UI function for consistency
    print_human_readable_tool_output(tool_name, tool_details, tool_result);
}
