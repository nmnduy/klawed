/*
 * tool_bash.c - Bash command execution tool
 */

#include "tool_bash.h"
#include "../klawed_internal.h"
#include "../tool_utils.h"
#include "../process_utils.h"
#include "../util/string_utils.h"
#include "../util/redact_utils.h"
#include "../message_queue.h"
#include "../logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <unistd.h>
#include <errno.h>
#include <strings.h>

// External reference to the thread-local TUI message queue
extern _Thread_local TUIMessageQueue *g_active_tool_queue;

cJSON* tool_bash(cJSON *params, ConversationState *state) {
    // Check for interrupt before starting
    if (state && state->interrupt_requested) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Operation interrupted by user");
        return error;
    }

    const cJSON *cmd_json = cJSON_GetObjectItem(params, "command");
    if (!cmd_json || !cJSON_IsString(cmd_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing 'command' parameter");
        return error;
    }

    const char *command = cmd_json->valuestring;

    // Create a copy of the command to trim trailing whitespace
    char *command_copy = strdup(command);
    if (!command_copy) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Memory allocation failed for command copy");
        return error;
    }

    // Trim trailing whitespace from the command copy
    trim_trailing_whitespace(command_copy);

    // Check for verbose tool logging
    int tool_verbose = 0;
    const char *tool_verbose_env = getenv("KLAWED_TOOL_VERBOSE");
    if (tool_verbose_env) {
        tool_verbose = atoi(tool_verbose_env);
        if (tool_verbose < 0) tool_verbose = 0;
        if (tool_verbose > 2) tool_verbose = 2;
    }

    // Verbose logging for Bash tool
    if (tool_verbose >= 1) {
        LOG_DEBUG("[TOOL VERBOSE] Bash tool executing command: %s", command_copy);
    }

    // Get timeout from parameter, environment, or use default (30 seconds)
    // Zero timeout is prohibited to prevent indefinite blocking
    int timeout_seconds = 30;  // Default timeout

    // First check if timeout parameter is provided
    const cJSON *timeout_json = cJSON_GetObjectItem(params, "timeout");
    if (timeout_json && cJSON_IsNumber(timeout_json)) {
        int timeout_val = timeout_json->valueint;
        // Reject zero and negative timeouts - use default instead
        if (timeout_val > 0) {
            timeout_seconds = timeout_val;
        }
        // If timeout_val <= 0, keep default (30 seconds)
    } else {
        // Fall back to environment variable
        const char *timeout_env = getenv("KLAWED_BASH_TIMEOUT");
        if (timeout_env) {
            int timeout_val = atoi(timeout_env);
            // Reject zero and negative timeouts - use default instead
            if (timeout_val > 0) {
                timeout_seconds = timeout_val;
            }
            // If timeout_val <= 0, keep default (30 seconds)
        }
    }

    // Warn only for very long timeouts (> 3 minutes)
    if (timeout_seconds > 180 && g_active_tool_queue) {
        char warning[128];
        snprintf(warning, sizeof(warning), "[Warning] Bash timeout set to %d seconds (over 3 minutes)", timeout_seconds);
        post_tui_message(g_active_tool_queue, TUI_MSG_ADD_LINE, warning);
    }

    // Check for timeout limits (some timeout implementations have max limits)
    // Common limit is 999999 seconds (~11.5 days)
    if (timeout_seconds > 999999) {
        LOG_WARN("Timeout value %d seconds exceeds typical 'timeout' command limit (999999)", timeout_seconds);
        if (g_active_tool_queue) {
            char warning[256];
            snprintf(warning, sizeof(warning),
                    "[Warning] Timeout value %d seconds may exceed 'timeout' command limits",
                    timeout_seconds);
            post_tui_message(g_active_tool_queue, TUI_MSG_ADD_LINE, warning);
        }
    }

    // Execute command and capture both stdout and stderr
    // Use shell wrapper with proper quoting and stderr redirection
    // full_command needs extra space for "timeout %ds sh -c '...' </dev/null 2>&1" wrapper
    // Worst case: "timeout " (8) + up to 10 digits + " sh -c '" (9) + "'" (1) + " </dev/null 2>&1" (17) + null (1) = up to 46 bytes
    char full_command[BUFFER_SIZE + 64];
    // Escape single quotes in the command for shell safety
    char escaped_command[BUFFER_SIZE];
    size_t j = 0;
    for (size_t i = 0; command_copy[i] && j < sizeof(escaped_command) - 1; i++) {
        if (command_copy[i] == '\'') {
            if (j < sizeof(escaped_command) - 2) {
                escaped_command[j++] = '\'';
                escaped_command[j++] = '\\';
                escaped_command[j++] = '\'';
                escaped_command[j++] = '\'';
            }
        } else {
            escaped_command[j++] = command_copy[i];
        }
    }
    escaped_command[j] = '\0';

    // Use shell wrapper to ensure consistent execution and stderr capture
    // Redirect stdin to /dev/null to prevent child processes from competing for terminal input
    snprintf(full_command, sizeof(full_command),
             "sh -c '%s' </dev/null 2>&1", escaped_command);

    // Temporarily redirect stderr to prevent any direct terminal output
    int saved_stderr = -1;
    FILE *stderr_redirect = NULL;

    // Only redirect stderr if we're in TUI mode (g_active_tool_queue is set)
    if (g_active_tool_queue) {
        saved_stderr = dup(STDERR_FILENO);
        stderr_redirect = freopen("/dev/null", "w", stderr);
        if (!stderr_redirect) {
            LOG_WARN("Failed to redirect stderr, continuing without redirection");
            if (saved_stderr != -1) {
                close(saved_stderr);
                saved_stderr = -1;
            }
        }
    }

    // Use the new process_utils function for robust command execution
    int timed_out = 0;
    char *output = NULL;
    size_t output_size = 0;
    volatile int *interrupt_flag = state ? &state->interrupt_requested : NULL;

    // Create a local interrupt flag if state is NULL
    volatile int local_interrupt_flag = 0;
    if (!interrupt_flag) {
        interrupt_flag = &local_interrupt_flag;
    }

    int exit_code = execute_command_with_timeout(
        full_command,
        timeout_seconds,
        &timed_out,
        &output,
        &output_size,
        interrupt_flag
    );

    // Check for truncation
    int truncated = 0;
    if (output_size >= BASH_OUTPUT_MAX_SIZE) {
        truncated = 1;
        // Truncate the output to the maximum size, ensuring we don't split UTF-8 characters
        if (output_size > BASH_OUTPUT_MAX_SIZE) {
            char *truncated_output = truncate_utf8(output, BASH_OUTPUT_MAX_SIZE);
            if (truncated_output) {
                free(output);
                output = truncated_output;
                output_size = strlen(output);
            }
        }
    }

    // Check if ANSI filtering is disabled
    const char *filter_env = getenv("KLAWED_BASH_FILTER_ANSI");
    int filter_ansi = 1;  // Default: filter ANSI sequences
    if (filter_env && (strcmp(filter_env, "0") == 0 || strcasecmp(filter_env, "false") == 0)) {
        filter_ansi = 0;
    }

    // Strip ANSI escape sequences to prevent terminal corruption (unless disabled)
    char *clean_output = NULL;
    if (filter_ansi && output) {
        clean_output = strip_ansi_escapes(output);
    }

    /* Redact secrets from command output (catches env/printenv/cat .env leaking keys) */
    const char *raw_output = clean_output ? clean_output : (output ? output : "");
    char *redacted_output = redact_sensitive_text(raw_output);

    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "exit_code", exit_code);
    cJSON_AddStringToObject(result, "output", redacted_output ? redacted_output : raw_output);
    free(redacted_output);

    free(clean_output);

    if (timed_out) {
        char timeout_msg[256];
        snprintf(timeout_msg, sizeof(timeout_msg),
                "Command timed out after %d seconds. Use KLAWED_BASH_TIMEOUT to adjust timeout.",
                timeout_seconds);
        cJSON_AddStringToObject(result, "timeout_error", timeout_msg);
    }

    if (truncated) {
        char truncate_msg[256];
        snprintf(truncate_msg, sizeof(truncate_msg),
                "Command output was truncated at %zu bytes (maximum: %d bytes).",
                output_size, BASH_OUTPUT_MAX_SIZE);
        cJSON_AddStringToObject(result, "truncation_warning", truncate_msg);
    }

    // Restore stderr if we redirected it
    if (saved_stderr != -1) {
        dup2(saved_stderr, STDERR_FILENO);
        close(saved_stderr);
        fflush(stderr);
    }

    free(output);
    free(command_copy);
    return result;
}
