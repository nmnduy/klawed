/*
 * tool_subagent.c - Subagent management tools
 */

#include "tool_subagent.h"
#include "../klawed_internal.h"
#include "../logger.h"
#include "../message_queue.h"
#include "../subagent_manager.h"
#include "../data_dir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <limits.h>
#include <ctype.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

// External reference to the thread-local TUI message queue
extern _Thread_local TUIMessageQueue *g_active_tool_queue;

// ============================================================================
// Subagent Tool
// ============================================================================

cJSON* tool_subagent(cJSON *params, ConversationState *state) {
    // Check for interrupt before starting
    if (state && state->interrupt_requested) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Operation interrupted by user");
        return error;
    }

    const cJSON *prompt_json = cJSON_GetObjectItem(params, "prompt");
    if (!prompt_json || !cJSON_IsString(prompt_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing 'prompt' parameter");
        return error;
    }

    const char *prompt = prompt_json->valuestring;
    if (strlen(prompt) == 0) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Prompt cannot be empty");
        return error;
    }

    // Get optional timeout parameter (default: 300 seconds = 5 minutes)
    // Zero timeout is prohibited to prevent indefinite blocking
    int timeout_seconds = 300;
    const cJSON *timeout_json = cJSON_GetObjectItem(params, "timeout");
    if (timeout_json && cJSON_IsNumber(timeout_json)) {
        int timeout_val = timeout_json->valueint;
        // Reject zero and negative timeouts - use default instead
        if (timeout_val > 0) {
            timeout_seconds = timeout_val;
        }
        // If timeout_val <= 0, keep default (300 seconds)
    }

    // Warn if timeout is > 15 minutes (900 seconds)
    if (timeout_seconds > 900) {
        LOG_WARN("Subagent timeout set to %d seconds (unusually long, > 15 minutes)", timeout_seconds);
        if (g_active_tool_queue) {
            char warning[128];
            snprintf(warning, sizeof(warning), "[Warning] Subagent timeout set to %d seconds (unusually long)", timeout_seconds);
            post_tui_message(g_active_tool_queue, TUI_MSG_ADD_LINE, warning);
        }
    }

    // Note: tail_lines parameter is not used in this function
    // It's only used in tool_check_subagent_progress
    // We parse it here for API consistency but don't store it
    const cJSON *tail_json = cJSON_GetObjectItem(params, "tail_lines");
    if (tail_json && cJSON_IsNumber(tail_json)) {
        int tail_lines = tail_json->valueint;
        if (tail_lines < 0) {
            // Default to 100 if negative (not used, just for consistency)
        }
    }

    // Get optional provider parameter
    const char *provider = NULL;
    const cJSON *provider_json = cJSON_GetObjectItem(params, "provider");
    if (provider_json && cJSON_IsString(provider_json)) {
        provider = provider_json->valuestring;
        // Validate that provider string is not empty
        if (provider && strlen(provider) == 0) {
            provider = NULL;
        }
    }

    // Get optional working_dir parameter
    const char *subagent_working_dir = NULL;
    const cJSON *working_dir_json = cJSON_GetObjectItem(params, "working_dir");
    if (working_dir_json && cJSON_IsString(working_dir_json)) {
        subagent_working_dir = working_dir_json->valuestring;
        // Validate that working_dir is not empty and is an absolute path
        if (subagent_working_dir && strlen(subagent_working_dir) == 0) {
            subagent_working_dir = NULL;
        } else if (subagent_working_dir && subagent_working_dir[0] != '/') {
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "working_dir must be an absolute path (starting with '/')");
            return error;
        }
    }

    // Create unique log file in data_dir/subagent/ directory
    if (!state) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Internal error: null state");
        return error;
    }

    // Build log directory path using data_dir
    char log_dir[PATH_MAX];

    // If working_dir is set and different from cwd, build absolute path
    // Otherwise use data_dir directly
    if (state->working_dir && state->working_dir[0] != '\0') {
        // Build path: working_dir/data_dir/subagent
        const char *base = data_dir_get_base();
        snprintf(log_dir, sizeof(log_dir), "%s/%s/subagent", state->working_dir, base);
    } else {
        // Use data_dir_build_path for current directory
        if (data_dir_build_path(log_dir, sizeof(log_dir), "subagent") != 0) {
            snprintf(log_dir, sizeof(log_dir), ".klawed/subagent");
        }
    }

    // Create directory if it doesn't exist
    if (mkdir(log_dir, 0755) != 0 && errno != EEXIST) {
        cJSON *error = cJSON_CreateObject();
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "Failed to create subagent log directory: %s", strerror(errno));
        cJSON_AddStringToObject(error, "error", err_msg);
        return error;
    }

    // Generate unique log filename with timestamp
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm_info);

    // Use dynamic allocation to avoid truncation warnings
    size_t log_file_size = strlen(log_dir) + 64; // Extra space for suffix
    char *log_file = malloc(log_file_size);
    if (!log_file) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Out of memory");
        return error;
    }
    snprintf(log_file, log_file_size, "%s/subagent_%s_%d.log", log_dir, timestamp, getpid());

    // Find the path to the current executable
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        // Fallback for macOS and other systems without /proc/self/exe
#ifdef __APPLE__
        uint32_t size = sizeof(exe_path);
        if (_NSGetExecutablePath(exe_path, &size) != 0) {
            free(log_file);
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "Failed to determine executable path");
            return error;
        }
        // len is not used in this branch
#else
        // Try argv[0] as fallback
        const char *fallback = "./build/klawed";
        strlcpy(exe_path, fallback, sizeof(exe_path));
#endif
    } else {
        exe_path[len] = '\0';
    }

    // Build the command to execute the subagent
    // We'll use double quotes and escape the prompt properly
    // Allocate enough space for worst case (every char needs escaping)
    size_t escaped_size = strlen(prompt) * 2 + 1;
    char *escaped_prompt = malloc(escaped_size);
    if (!escaped_prompt) {
        free(log_file);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Out of memory");
        return error;
    }

    // Escape double quotes, backslashes, dollar signs, and backticks for shell
    size_t j = 0;
    for (size_t i = 0; prompt[i] && j < escaped_size - 2; i++) {
        if (prompt[i] == '"' || prompt[i] == '\\' || prompt[i] == '$' || prompt[i] == '`') {
            escaped_prompt[j++] = '\\';
        }
        escaped_prompt[j++] = prompt[i];
    }
    escaped_prompt[j] = '\0';

    LOG_INFO("Starting subagent: %s", log_file);

    // Build the command
    char command[BUFFER_SIZE * 2];
    snprintf(command, sizeof(command),
             "\"%s\" \"%s\" > \"%s\" 2>&1 </dev/null",
             exe_path, escaped_prompt, log_file);

    free(escaped_prompt);

    // Fork and execute
    pid_t pid = fork();
    if (pid < 0) {
        free(log_file);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to fork subagent process");
        return error;
    }

    if (pid == 0) {
        // Child process - create a new process group so we can kill all descendants
        // setsid() creates a new session AND a new process group with the child as leader
        // This allows us to use kill(-pid, sig) to terminate the entire subtree
        if (setsid() == -1) {
            // setsid() can fail if we're already a session leader, which shouldn't happen
            // Fall back to setpgid() which just creates a new process group
            if (setpgid(0, 0) != 0) {
                fprintf(stderr, "Warning: Failed to create new process group: %s\n", strerror(errno));
            }
        }

        // Set environment variable to indicate this is a subagent
        if (setenv("KLAWED_IS_SUBAGENT", "1", 1) != 0) {
            fprintf(stderr, "Warning: Failed to set KLAWED_IS_SUBAGENT environment variable: %s\n", strerror(errno));
        }

        // Set LLM provider if specified in parameters
        if (provider && provider[0] != '\0') {
            if (setenv("KLAWED_LLM_PROVIDER", provider, 1) != 0) {
                fprintf(stderr, "Warning: Failed to set KLAWED_LLM_PROVIDER environment variable: %s\n", strerror(errno));
            }
        }

        // Set custom environment variables from KLAWED_SUBAGENT_ENV_VARS
        // Format: "KEY1=VALUE1,KEY2=VALUE2,..."
        const char *env_vars_str = getenv("KLAWED_SUBAGENT_ENV_VARS");
        if (env_vars_str && env_vars_str[0] != '\0') {
            // Make a copy since we'll be modifying it
            char *env_vars_copy = strdup(env_vars_str);
            if (env_vars_copy) {
                char *saveptr = NULL;
                char *token = strtok_r(env_vars_copy, ",", &saveptr);
                while (token) {
                    // Skip leading whitespace
                    while (*token == ' ' || *token == '\t') token++;

                    // Find the '=' separator
                    char *equals = strchr(token, '=');
                    if (equals && equals != token) {
                        *equals = '\0';
                        const char *key = token;
                        const char *value = equals + 1;

                        if (setenv(key, value, 1) != 0) {
                            fprintf(stderr, "Warning: Failed to set environment variable %s: %s\n",
                                    key, strerror(errno));
                        }
                    }
                    token = strtok_r(NULL, ",", &saveptr);
                }
                free(env_vars_copy);
            }
        }

        // Change working directory if specified
        if (subagent_working_dir && subagent_working_dir[0] != '\0') {
            if (chdir(subagent_working_dir) != 0) {
                fprintf(stderr, "Failed to change to working directory '%s': %s\n",
                        subagent_working_dir, strerror(errno));
                exit(1);
            }
        }

        // Execute the command
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        // If we get here, exec failed
        fprintf(stderr, "Failed to execute subagent: %s\n", strerror(errno));
        exit(1);
    }

    // Parent process - return immediately with PID and log file info
    // The orchestrator can check progress by reading the log file

    // Register the subagent with the manager for real-time monitoring
    if (state->subagent_manager) {
        if (subagent_manager_add(state->subagent_manager, pid, log_file, prompt, timeout_seconds) == 0) {
            LOG_DEBUG("Registered subagent PID %d with manager", pid);
        } else {
            LOG_WARN("Failed to register subagent PID %d with manager", pid);
        }
    }

    // Build result with PID and log file info
    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "pid", pid);
    cJSON_AddStringToObject(result, "log_file", log_file);
    cJSON_AddNumberToObject(result, "timeout_seconds", timeout_seconds);
    if (subagent_working_dir && subagent_working_dir[0] != '\0') {
        cJSON_AddStringToObject(result, "working_dir", subagent_working_dir);
    }

    // Add message about how to check progress - use dynamic allocation to avoid truncation
    size_t msg_size = strlen(log_file) + 256; // Extra space for message template and PID
    char *msg = malloc(msg_size);
    if (msg) {
        snprintf(msg, msg_size,
                 "Subagent started with PID %d. Log file: %s\n"
                 "Use 'CheckSubagentProgress' tool to monitor progress or 'InterruptSubagent' to stop it.",
                 pid, log_file);
        cJSON_AddStringToObject(result, "message", msg);
        free(msg);
    } else {
        // Fallback if malloc fails
        cJSON_AddStringToObject(result, "message", "Subagent started. Check log file for progress.");
    }

    free(log_file);
    return result;
}

// ============================================================================
// CheckSubagentProgress Tool
// ============================================================================

cJSON* tool_check_subagent_progress(cJSON *params, ConversationState *state) {
    // Check for interrupt before starting
    if (state && state->interrupt_requested) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Operation interrupted by user");
        return error;
    }

    const cJSON *pid_json = cJSON_GetObjectItem(params, "pid");
    const cJSON *log_file_json = cJSON_GetObjectItem(params, "log_file");

    pid_t pid = 0;
    const char *log_file = NULL;

    // We need either PID or log file to check progress
    if (pid_json && cJSON_IsNumber(pid_json)) {
        pid = (pid_t)pid_json->valueint;
    }

    if (log_file_json && cJSON_IsString(log_file_json)) {
        log_file = log_file_json->valuestring;
    }

    if (pid == 0 && !log_file) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing 'pid' or 'log_file' parameter");
        return error;
    }

    // Get optional tail_lines parameter (default: 50 lines from end)
    int tail_lines = 50;
    const cJSON *tail_json = cJSON_GetObjectItem(params, "tail_lines");
    if (tail_json && cJSON_IsNumber(tail_json)) {
        tail_lines = tail_json->valueint;
        if (tail_lines < 0) {
            tail_lines = 50;  // Default to 50 if negative
        }
    }

    // Check if process is still running
    int is_running = 0;
    int exit_code = -1;

    if (pid > 0) {
        // Check process status using waitpid with WNOHANG (non-blocking)
        int status;
        pid_t result = waitpid(pid, &status, WNOHANG);

        if (result == 0) {
            // Process is still running
            is_running = 1;
        } else if (result == pid) {
            // Process has terminated
            is_running = 0;
            if (WIFEXITED(status)) {
                exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                exit_code = -WTERMSIG(status);  // Negative signal number
            }
        } else if (result == -1) {
            // Error or process doesn't exist
            if (errno == ECHILD) {
                // No child process with this PID
                is_running = 0;
                exit_code = -999;  // Special code for "no such process"
            }
        }
    }

    // Read log file if provided
    int total_lines = 0;
    char *tail_output = NULL;
    size_t tail_size = 0;

    if (log_file) {
        FILE *log_fp = fopen(log_file, "r");
        if (log_fp) {
            // Get configurable character limit per line
            int max_line_chars = SUBAGENT_LOG_LINE_MAX_CHARS;
            const char *env_max_chars = getenv("KLAWED_SUBAGENT_LOG_LINE_MAX_CHARS");
            if (env_max_chars) {
                int parsed_limit = atoi(env_max_chars);
                if (parsed_limit > 0) {
                    max_line_chars = parsed_limit;
                }
            }

            // Count total lines first
            char line[BUFFER_SIZE];
            while (fgets(line, sizeof(line), log_fp)) {
                total_lines++;
            }

            // Calculate which line to start from
            int start_line = (tail_lines > 0 && total_lines > tail_lines) ? (total_lines - tail_lines) : 0;

            // Read the tail content
            rewind(log_fp);
            // Check rewind error by checking if ftell returns -1
            // Save errno before ftell to avoid overwriting
            int saved_errno = errno;
            errno = 0;
            if (ftell(log_fp) == -1 && errno != 0) {
                fclose(log_fp);
                cJSON *error = cJSON_CreateObject();
                cJSON_AddStringToObject(error, "error", "Failed to rewind log file");
                return error;
            }
            errno = saved_errno;
            int current_line = 0;
            int lines_truncated = 0;

            while (fgets(line, sizeof(line), log_fp)) {
                if (current_line >= start_line) {
                    size_t line_len = strlen(line);

                    // Truncate line if it exceeds character limit
                    if ((int)line_len > max_line_chars) {
                        // Find a good place to truncate (preserve newline if present)
                        int truncate_pos = max_line_chars - 15;  // Leave space for "...[truncated]"
                        if (truncate_pos < 0) truncate_pos = 0;

                        // Try to avoid breaking in the middle of a word
                        while (truncate_pos > 0 && truncate_pos < (int)line_len &&
                               !isspace((unsigned char)line[truncate_pos]) &&
                               truncate_pos > max_line_chars - 50) {
                            truncate_pos--;
                        }

                        line[truncate_pos] = '\0';
                        strlcat(line, "...[truncated]", sizeof(line));

                        // Add back newline if original line had one
                        if (line_len > 0 && (line[line_len-1] == '\n' || line[line_len-1] == '\r')) {
                            strlcat(line, "\n", sizeof(line));
                        }

                        line_len = strlen(line);
                        lines_truncated++;
                    }

                    char *new_output = realloc(tail_output, tail_size + line_len + 1);
                    if (!new_output) {
                        free(tail_output);
                        fclose(log_fp);
                        cJSON *error = cJSON_CreateObject();
                        cJSON_AddStringToObject(error, "error", "Out of memory while reading log");
                        return error;
                    }
                    tail_output = new_output;
                    memcpy(tail_output + tail_size, line, line_len);
                    tail_size += line_len;
                    tail_output[tail_size] = '\0';
                }
                current_line++;
            }

            fclose(log_fp);

            // Add truncation warning if any lines were truncated
            if (lines_truncated > 0) {
                char truncation_msg[256];
                snprintf(truncation_msg, sizeof(truncation_msg),
                         "\n[Note: %d lines were truncated to %d characters each to preserve context]\n",
                         lines_truncated, max_line_chars);

                char *new_output = realloc(tail_output, tail_size + strlen(truncation_msg) + 1);
                if (new_output) {
                    tail_output = new_output;
                    memcpy(tail_output + tail_size, truncation_msg, strlen(truncation_msg));
                    tail_size += strlen(truncation_msg);
                    tail_output[tail_size] = '\0';
                }
            }
        }
    }

    // Build result
    cJSON *result = cJSON_CreateObject();

    if (pid > 0) {
        cJSON_AddNumberToObject(result, "pid", pid);
        cJSON_AddBoolToObject(result, "is_running", is_running);
        if (!is_running) {
            cJSON_AddNumberToObject(result, "exit_code", exit_code);
        }
    }

    if (log_file) {
        cJSON_AddStringToObject(result, "log_file", log_file);
        cJSON_AddNumberToObject(result, "total_lines", total_lines);
        cJSON_AddNumberToObject(result, "tail_lines_returned", tail_lines > total_lines ? total_lines : tail_lines);
        cJSON_AddStringToObject(result, "tail_output", tail_output ? tail_output : "");

        if (total_lines > tail_lines) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Log file contains %d lines. Only the last %d lines are shown. "
                     "Use Read tool to access the full log file, or Grep to search for specific content.",
                     total_lines, tail_lines);
            cJSON_AddStringToObject(result, "truncation_warning", msg);
        }
    }

    // Add summary message
    char summary[512];
    if (pid > 0) {
        if (is_running) {
            snprintf(summary, sizeof(summary),
                     "Subagent with PID %d is still running. Log file: %s",
                     pid, log_file ? log_file : "(unknown)");
        } else {
            snprintf(summary, sizeof(summary),
                     "Subagent with PID %d has completed with exit code %d. Log file: %s",
                     pid, exit_code, log_file ? log_file : "(unknown)");
        }
    } else {
        snprintf(summary, sizeof(summary),
                 "Log file: %s (PID unknown)",
                 log_file ? log_file : "(unknown)");
    }
    cJSON_AddStringToObject(result, "summary", summary);

    free(tail_output);
    return result;
}

// ============================================================================
// InterruptSubagent Tool
// ============================================================================

cJSON* tool_interrupt_subagent(cJSON *params, ConversationState *state) {
    // Check for interrupt before starting
    if (state && state->interrupt_requested) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Operation interrupted by user");
        return error;
    }

    const cJSON *pid_json = cJSON_GetObjectItem(params, "pid");
    if (!pid_json || !cJSON_IsNumber(pid_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing 'pid' parameter");
        return error;
    }

    pid_t pid = (pid_t)pid_json->valueint;
    if (pid <= 0) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Invalid PID");
        return error;
    }

    // Try to kill the process group (subagent + all its children)
    int killed = 0;
    char kill_msg[512];

    // First try SIGTERM to the entire process group (graceful shutdown)
    // Using -pid sends signal to all processes in the group led by pid
    if (kill(-pid, SIGTERM) == 0) {
        snprintf(kill_msg, sizeof(kill_msg),
                 "Sent SIGTERM to subagent process group %d. Waiting 2 seconds for graceful shutdown...",
                 pid);

        // Wait a bit for graceful shutdown
        sleep(2);

        // Check if it's still running
        int status;
        if (waitpid(pid, &status, WNOHANG) == 0) {
            // Still running, send SIGKILL to entire process group
            if (kill(-pid, SIGKILL) == 0) {
                snprintf(kill_msg + strlen(kill_msg), sizeof(kill_msg) - strlen(kill_msg),
                         " Process group did not terminate gracefully. Sent SIGKILL to all.");
                killed = 1;
            } else if (errno == ESRCH) {
                // Process group gone, try direct kill
                if (kill(pid, SIGKILL) == 0) {
                    snprintf(kill_msg + strlen(kill_msg), sizeof(kill_msg) - strlen(kill_msg),
                             " Sent SIGKILL to PID %d.", pid);
                    killed = 1;
                } else {
                    snprintf(kill_msg + strlen(kill_msg), sizeof(kill_msg) - strlen(kill_msg),
                             " Failed to send SIGKILL: %s", strerror(errno));
                }
            } else {
                snprintf(kill_msg + strlen(kill_msg), sizeof(kill_msg) - strlen(kill_msg),
                         " Failed to send SIGKILL: %s", strerror(errno));
            }
        } else {
            // Process terminated
            snprintf(kill_msg + strlen(kill_msg), sizeof(kill_msg) - strlen(kill_msg),
                     " Process group terminated gracefully.");
            killed = 1;
        }
    } else if (errno == ESRCH) {
        // Process group doesn't exist - try direct process kill
        if (kill(pid, SIGTERM) == 0) {
            snprintf(kill_msg, sizeof(kill_msg),
                     "Sent SIGTERM to subagent PID %d (no process group). Waiting 2 seconds...",
                     pid);

            sleep(2);

            int status;
            if (waitpid(pid, &status, WNOHANG) == 0) {
                if (kill(pid, SIGKILL) == 0) {
                    snprintf(kill_msg + strlen(kill_msg), sizeof(kill_msg) - strlen(kill_msg),
                             " Sent SIGKILL.");
                    killed = 1;
                } else {
                    snprintf(kill_msg + strlen(kill_msg), sizeof(kill_msg) - strlen(kill_msg),
                             " Failed to send SIGKILL: %s", strerror(errno));
                }
            } else {
                snprintf(kill_msg + strlen(kill_msg), sizeof(kill_msg) - strlen(kill_msg),
                         " Process terminated gracefully.");
                killed = 1;
            }
        } else if (errno == ESRCH) {
            snprintf(kill_msg, sizeof(kill_msg),
                     "No subagent process found with PID %d (may have already terminated).",
                     pid);
        } else if (errno == EPERM) {
            snprintf(kill_msg, sizeof(kill_msg),
                     "Permission denied: cannot kill subagent with PID %d.",
                     pid);
        } else {
            snprintf(kill_msg, sizeof(kill_msg),
                     "Failed to send SIGTERM to PID %d: %s",
                     pid, strerror(errno));
        }
    } else if (errno == EPERM) {
        snprintf(kill_msg, sizeof(kill_msg),
                 "Permission denied: cannot kill subagent process group %d.",
                 pid);
    } else {
        snprintf(kill_msg, sizeof(kill_msg),
                 "Failed to send SIGTERM to process group %d: %s",
                 pid, strerror(errno));
    }

    // Build result
    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "pid", pid);
    cJSON_AddBoolToObject(result, "killed", killed);
    cJSON_AddStringToObject(result, "message", kill_msg);

    return result;
}
