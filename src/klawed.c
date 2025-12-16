/*
 * Klawed - Pure C Implementation
 * A lightweight coding agent that interacts with OpenAI-compatible APIs
 *
 * Compilation: make
 * Usage: ./klawed "your prompt here"
 *
 * Dependencies: libcurl, cJSON, pthread
 */

#ifndef __APPLE__
    #define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <bsd/stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <errno.h>
#include <glob.h>
#include <regex.h>
#include <fcntl.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <sqlite3.h>
#include <limits.h>
#include <libgen.h>
#include <dirent.h>
#include <ctype.h>
#include <signal.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

// Socket IPC support
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include "colorscheme.h"
#include "fallback_colors.h"
#include "patch_parser.h"
#include "tool_utils.h"
#ifndef TEST_BUILD
#include "openai_messages.h"
#endif

#ifdef TEST_BUILD
// Disable unused function warnings for test builds since not all functions are used by tests
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
// Test build: stub out persistence (logger is linked via LOGGER_OBJ)
// Stub persistence types and functions
typedef struct PersistenceDB { int dummy; } PersistenceDB;
static PersistenceDB* persistence_init(const char *path) { (void)path; return NULL; }
static void persistence_close(PersistenceDB *db) { (void)db; }
static void persistence_log_api_call(
    PersistenceDB *db,
    const char *session_id,
    const char *url,
    const char *request,
    const char *headers,
    const char *response,
    const char *model,
    const char *status,
    int code,
    const char *error_msg,
    long duration_ms,
    int tool_count
) { (void)db; (void)session_id; (void)url; (void)request; (void)headers; (void)response; (void)model; (void)status; (void)code; (void)error_msg; (void)duration_ms; (void)tool_count; }

static int persistence_get_session_token_usage(
    PersistenceDB *db,
    const char *session_id,
    int *prompt_tokens,
    int *completion_tokens,
    int *cached_tokens
) { (void)db; (void)session_id; (void)prompt_tokens; (void)completion_tokens; (void)cached_tokens; return -1; }

// Stub Bedrock types and functions
typedef struct {
    char *access_key_id;
    char *secret_access_key;
    char *session_token;
    char *region;
    char *profile;
} AWSCredentials;

typedef struct BedrockConfigStruct {
    int enabled;
    char *region;
    char *model_id;
    char *endpoint;
    AWSCredentials *creds;
} BedrockConfig;

static int bedrock_is_enabled(void) { return 0; }
static BedrockConfig* bedrock_config_init(const char *model_id) { (void)model_id; return NULL; }
static void bedrock_config_free(BedrockConfig *config) { (void)config; }
static char* bedrock_convert_request(const char *openai_request) { (void)openai_request; return NULL; }
static cJSON* bedrock_convert_response(const char *bedrock_response) { (void)bedrock_response; return NULL; }
static struct curl_slist* bedrock_sign_request(
    struct curl_slist *headers,
    const char *method,
    const char *url,
    const char *payload,
    const AWSCredentials *creds,
    const char *region,
    const char *service
) { (void)method; (void)url; (void)payload; (void)creds; (void)region; (void)service; return headers; }
static int bedrock_handle_auth_error(BedrockConfig *config, long http_status, const char *error_message, const char *response_body) {
    (void)config; (void)http_status; (void)error_message; (void)response_body;
    return 0;
}

static void ensure_tool_results(ConversationState *state) {
    (void)state;
    // Stub for test builds
}
#else
// Normal build: use actual implementations
#include "logger.h"
#include "persistence.h"
#endif

// Visual indicators for interactive mode
#include "indicators.h"

// Internal API for module access
#include "klawed_internal.h"

// Session management
#ifndef TEST_BUILD
#include "session.h"
#endif
#include "provider.h"  // For ApiCallResult and Provider definitions
#include "todo.h"

// Commands module
#include "commands.h"

// TUI module
#include "tui.h"
#include "message_queue.h"
#include "ai_worker.h"

// Subagent management
#ifdef TEST_BUILD
// Stub SubagentManager types and functions for test builds
typedef struct SubagentManager { int dummy; } SubagentManager;
static int subagent_manager_init(SubagentManager *manager) { (void)manager; return 0; }
static void subagent_manager_free(SubagentManager *manager) { (void)manager; }
static int subagent_manager_add(SubagentManager *manager, pid_t pid, const char *log_file, const char *prompt, int timeout_seconds) { (void)manager; (void)pid; (void)log_file; (void)prompt; (void)timeout_seconds; return -1; }
static int subagent_manager_terminate_all(SubagentManager *manager, int grace_period_ms) { (void)manager; (void)grace_period_ms; return 0; }
static int subagent_manager_get_running_count(SubagentManager *manager) { (void)manager; return 0; }
#else
#include "subagent_manager.h"
#endif

// AWS Bedrock support
#ifndef TEST_BUILD
#include "aws_bedrock.h"
#endif

// MCP (Model Context Protocol) support
#ifndef TEST_BUILD
#include "mcp.h"
#endif

// Base64 encoding/decoding for binary content
#include "base64.h"

#ifdef TEST_BUILD
#define main klawed_main
#endif

// Version
// ============================================================================
// Output Helpers
// ============================================================================



static void print_assistant(const char *text) {
    // Use accent color for role name, foreground for main text
    char role_color_code[32];
    char text_color_code[32];
    const char *role_color_start;
    const char *text_color_start;

    // Get accent color for role name
    if (get_colorscheme_color(COLORSCHEME_ASSISTANT, role_color_code, sizeof(role_color_code)) == 0) {
        role_color_start = role_color_code;
    } else {
        LOG_WARN("Using fallback ANSI color for ASSISTANT");
        role_color_start = ANSI_FALLBACK_ASSISTANT;
    }

    // Get foreground color for main text
    if (get_colorscheme_color(COLORSCHEME_FOREGROUND, text_color_code, sizeof(text_color_code)) == 0) {
        text_color_start = text_color_code;
    } else {
        LOG_WARN("Using fallback ANSI color for FOREGROUND");
        text_color_start = ANSI_FALLBACK_FOREGROUND;
    }

    printf("%s[Assistant]%s %s%s%s\n", role_color_start, ANSI_RESET, text_color_start, text, ANSI_RESET);
    fflush(stdout);
}

static void print_tool(const char *tool_name, const char *details) {
    // Use status color for tool indicator (reduce rainbow), foreground for details
    char status_color_code[32];
    char text_color_code[32];
    const char *tool_color_start;
    const char *text_color_start;

    // Use STATUS color for the [Tool: ...] tag
    if (get_colorscheme_color(COLORSCHEME_STATUS, status_color_code, sizeof(status_color_code)) == 0) {
        tool_color_start = status_color_code;
    } else {
        LOG_WARN("Using fallback ANSI color for STATUS (tool tag)");
        tool_color_start = ANSI_FALLBACK_STATUS;
    }

    // Get foreground color for details
    if (get_colorscheme_color(COLORSCHEME_FOREGROUND, text_color_code, sizeof(text_color_code)) == 0) {
        text_color_start = text_color_code;
    } else {
        LOG_WARN("Using fallback ANSI color for FOREGROUND");
        text_color_start = ANSI_FALLBACK_FOREGROUND;
    }

    printf("%s[Tool: %s]%s", tool_color_start, tool_name, ANSI_RESET);
    if (details && strlen(details) > 0) {
        printf(" %s%s%s", text_color_start, details, ANSI_RESET);
    }
    printf("\n");
    fflush(stdout);
}

static void print_error(const char *text);

static void ui_append_line(TUIState *tui,
                           TUIMessageQueue *queue,
                           const char *prefix,
                           const char *text,
                           TUIColorPair color) {
    const char *safe_text = text ? text : "";
    const char *safe_prefix = prefix ? prefix : "";

    if (queue) {
        size_t prefix_len = safe_prefix[0] ? strlen(safe_prefix) : 0;
        size_t text_len = strlen(safe_text);
        size_t extra_space = (prefix_len > 0 && text_len > 0) ? 1 : 0;
        size_t total = prefix_len + extra_space + text_len + 1;

        char *formatted = malloc(total);
        if (!formatted) {
            LOG_ERROR("Failed to allocate memory for TUI message");
            // Fall through to direct UI/console output
        } else {
            if (prefix_len > 0 && text_len > 0) {
                snprintf(formatted, total, "%s %s", safe_prefix, safe_text);
            } else if (prefix_len > 0) {
                snprintf(formatted, total, "%s", safe_prefix);
            } else {
                snprintf(formatted, total, "%s", safe_text);
            }

            if (post_tui_message(queue, TUI_MSG_ADD_LINE, formatted) == 0) {
                free(formatted);
                return;
            }

            LOG_WARN("Failed to enqueue TUI message, falling back to direct render");
            free(formatted);
        }
    }

    if (tui) {
        tui_add_conversation_line(tui, safe_prefix, safe_text, color);
        return;
    }

    if (strcmp(safe_prefix, "[Assistant]") == 0) {
        print_assistant(safe_text);
        return;
    }

    if (strncmp(safe_prefix, "[Tool", 5) == 0) {
        const char *colon = strchr(safe_prefix, ':');
        const char *close = strrchr(safe_prefix, ']');
        const char *name_start = NULL;
        size_t name_len = 0;
        if (colon) {
            name_start = colon + 1;
            if (*name_start == ' ') {
                name_start++;
            }
            if (close && close > name_start) {
                name_len = (size_t)(close - name_start);
            }
        }

        char tool_name[128];
        if (name_len == 0 || name_len >= sizeof(tool_name)) {
            snprintf(tool_name, sizeof(tool_name), "tool");
        } else {
            memcpy(tool_name, name_start, name_len);
            tool_name[name_len] = '\0';
        }
        print_tool(tool_name, safe_text);
        return;
    }

    if (strcmp(safe_prefix, "[Error]") == 0) {
        print_error(safe_text);
        return;
    }

    if (safe_prefix[0]) {
        printf("%s %s\n", safe_prefix, safe_text);
    } else {
        printf("%s\n", safe_text);
    }
    fflush(stdout);
    return;
}

static void ui_set_status(TUIState *tui,
                          TUIMessageQueue *queue,
                          const char *status_text) {
    const char *safe = status_text ? status_text : "";
    if (queue) {
        if (post_tui_message(queue, TUI_MSG_STATUS, safe) == 0) {
            return;
        }
        LOG_WARN("Failed to enqueue status update, falling back to direct render");
    }

    if (tui) {
        tui_update_status(tui, safe);
        return;
    }
    if (safe[0] != '\0') {
        // Use status color when not in TUI for consistency with tips
        char status_color_buf[32];
        const char *status_color = NULL;
        if (get_colorscheme_color(COLORSCHEME_STATUS, status_color_buf, sizeof(status_color_buf)) == 0) {
            status_color = status_color_buf;
        } else {
            LOG_WARN("Using fallback ANSI color for STATUS (ui_set_status)");
            status_color = ANSI_FALLBACK_STATUS;
        }
        printf("%s[Status]%s %s\n", status_color, ANSI_RESET, safe);
    }
}

static void ui_show_error(TUIState *tui,
                          TUIMessageQueue *queue,
                          const char *error_text) {
    const char *safe = error_text ? error_text : "";
    if (queue) {
        if (post_tui_message(queue, TUI_MSG_ERROR, safe) == 0) {
            return;
        }
        LOG_WARN("Failed to enqueue error message, falling back to direct render");
    }
    if (tui) {
        tui_add_conversation_line(tui, "[Error]", safe, COLOR_PAIR_ERROR);
        return;
    }
    print_error(safe);
}

// =====================================================================
// Tool Output Helpers
// =====================================================================

static _Thread_local TUIMessageQueue *g_active_tool_queue = NULL;

// Oneshot/subagent mode flag - when enabled, tool outputs are wrapped in HTML-style tags
// for easier parsing by parent processes or scripts
static _Thread_local int g_oneshot_mode = 0;

// =====================================================================
// Emergency Cleanup for Subagents
// =====================================================================

// Global reference to subagent manager for emergency cleanup on unexpected termination
static SubagentManager *g_subagent_manager_for_cleanup = NULL;
static pthread_mutex_t g_cleanup_mutex = PTHREAD_MUTEX_INITIALIZER;

// Emergency cleanup handler called on exit or signal
static void emergency_cleanup_subagents(void) {
    pthread_mutex_lock(&g_cleanup_mutex);

    if (g_subagent_manager_for_cleanup) {
        LOG_INFO("Emergency cleanup: Terminating all running subagents");
        subagent_manager_terminate_all(g_subagent_manager_for_cleanup, 1000);  // 1 second grace period
        g_subagent_manager_for_cleanup = NULL;
    }

    pthread_mutex_unlock(&g_cleanup_mutex);
}

// Signal handler for SIGINT and SIGTERM
static void signal_handler_cleanup(int sig) {
    (void)sig;  // Unused
    emergency_cleanup_subagents();
    // Re-raise signal to allow default handler to run
    signal(sig, SIG_DFL);
    raise(sig);
}

// Register/unregister subagent manager for emergency cleanup
static void register_subagent_manager_for_cleanup(SubagentManager *manager) {
    pthread_mutex_lock(&g_cleanup_mutex);
    g_subagent_manager_for_cleanup = manager;
    pthread_mutex_unlock(&g_cleanup_mutex);

    if (manager) {
        // Register atexit handler (only once)
        static int atexit_registered = 0;
        if (!atexit_registered) {
            atexit(emergency_cleanup_subagents);
            atexit_registered = 1;
        }

        // Register signal handlers
        signal(SIGINT, signal_handler_cleanup);
        signal(SIGTERM, signal_handler_cleanup);
    }
}

static void tool_emit_line(const char *prefix, const char *text) {
    const char *safe_prefix = prefix ? prefix : "";
    const char *safe_text = text ? text : "";

    if (g_active_tool_queue) {
        size_t prefix_len = safe_prefix[0] ? strlen(safe_prefix) : 0;
        size_t text_len = strlen(safe_text);
        size_t extra = (prefix_len > 0 && text_len > 0) ? 1 : 0;
        size_t total = prefix_len + extra + text_len + 1;

        char *formatted = malloc(total);
        if (!formatted) {
            LOG_ERROR("Failed to allocate tool output buffer");
            return;
        }

        if (prefix_len > 0 && text_len > 0) {
            snprintf(formatted, total, "%s %s", safe_prefix, safe_text);
        } else if (prefix_len > 0) {
            snprintf(formatted, total, "%s", safe_prefix);
        } else {
            snprintf(formatted, total, "%s", safe_text);
        }

        if (post_tui_message(g_active_tool_queue, TUI_MSG_ADD_LINE, formatted) != 0) {
            LOG_WARN("Failed to post tool output to TUI queue");
        }
        free(formatted);
        return;
    }

    // In oneshot/subagent mode, suppress individual line output
    // Tool output will be captured and wrapped in HTML-style tags by the caller
    if (g_oneshot_mode) {
        return;
    }

    if (safe_prefix[0] && safe_text[0]) {
        printf("%s %s\n", safe_prefix, safe_text);
    } else if (safe_prefix[0]) {
        printf("%s\n", safe_prefix);
    } else {
        printf("%s\n", safe_text);
    }
    fflush(stdout);
}

static void emit_diff_line(const char *line,
                           const char *add_color,
                           const char *remove_color) {
    if (!line) {
        return;
    }

    // Trim trailing newlines
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        len--;
    }

    if (len == 0) {
        return;
    }

    char *trimmed = strndup(line, len);
    if (!trimmed) {
        LOG_ERROR("Failed to allocate trimmed diff line");
        return;
    }

    // Print with indentation and color if applicable
    if (g_active_tool_queue) {
        // For TUI mode: just pass the line as-is
        // The TUI will detect diff prefixes and color appropriately
        tool_emit_line("", trimmed);
    } else if (!g_oneshot_mode) {
        // For non-TUI mode (direct stdout): use ANSI color codes
        // Skip output in oneshot/subagent mode as it will be captured in JSON
        const char *color = NULL;
        if (trimmed[0] == '+' && trimmed[1] != '+') {
            color = add_color;
        } else if (trimmed[0] == '-' && trimmed[1] != '-') {
            color = remove_color;
        }

        if (color) {
            printf("  %s%s%s\n", color, trimmed, ANSI_RESET);
        } else {
            printf("  %s\n", trimmed);
        }
    }

    free(trimmed);
}

// Helper function to get current timestamp in YYYY-MM-DD HH:MM:SS format
static void get_current_timestamp(char *buffer, size_t buffer_size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", tm_info);
}

// Helper function to extract tool details from arguments
static char* get_tool_details(const char *tool_name, cJSON *arguments) {
    if (!arguments || !cJSON_IsObject(arguments)) {
        return NULL;
    }

    static char details[256]; // static buffer for thread safety
    details[0] = '\0';

    if (strcmp(tool_name, "Bash") == 0) {
        cJSON *command = cJSON_GetObjectItem(arguments, "command");
        if (cJSON_IsString(command)) {
            summarize_bash_command(command->valuestring, details, sizeof(details));
        }
    } else if (strcmp(tool_name, "Subagent") == 0) {
        cJSON *prompt = cJSON_GetObjectItem(arguments, "prompt");
        if (cJSON_IsString(prompt)) {
            // Show first 50 chars of the prompt
            const char *prompt_str = prompt->valuestring;
            size_t len = strlen(prompt_str);
            if (len > 50) {
                snprintf(details, sizeof(details), "%.47s...", prompt_str);
            } else {
                strlcpy(details, prompt_str, sizeof(details));
            }
        }
    } else if (strcmp(tool_name, "Read") == 0) {
        cJSON *file_path = cJSON_GetObjectItem(arguments, "file_path");
        cJSON *start_line = cJSON_GetObjectItem(arguments, "start_line");
        cJSON *end_line = cJSON_GetObjectItem(arguments, "end_line");

        if (cJSON_IsString(file_path)) {
            const char *path = file_path->valuestring;
            // Extract just the filename from the path
            const char *filename = strrchr(path, '/');
            filename = filename ? filename + 1 : path;

            if (cJSON_IsNumber(start_line) && cJSON_IsNumber(end_line)) {
                snprintf(details, sizeof(details), "%s:%d-%d", filename,
                        start_line->valueint, end_line->valueint);
            } else if (cJSON_IsNumber(start_line)) {
                snprintf(details, sizeof(details), "%s:%d", filename, start_line->valueint);
            } else {
                strlcpy(details, filename, sizeof(details));
            }
        }
    } else if (strcmp(tool_name, "Write") == 0) {
        cJSON *file_path = cJSON_GetObjectItem(arguments, "file_path");
        if (cJSON_IsString(file_path)) {
            const char *path = file_path->valuestring;
            // Extract just the filename from the path
            const char *filename = strrchr(path, '/');
            filename = filename ? filename + 1 : path;
            strlcpy(details, filename, sizeof(details));
        }
    } else if (strcmp(tool_name, "Edit") == 0) {
        cJSON *file_path = cJSON_GetObjectItem(arguments, "file_path");
        cJSON *use_regex = cJSON_GetObjectItem(arguments, "use_regex");

        if (cJSON_IsString(file_path)) {
            const char *path = file_path->valuestring;
            // Extract just the filename from the path
            const char *filename = strrchr(path, '/');
            filename = filename ? filename + 1 : path;

            const char *op_type = cJSON_IsTrue(use_regex) ? "(regex)" : "(string)";
            snprintf(details, sizeof(details), "%s %s", filename, op_type);
        }
    } else if (strcmp(tool_name, "Glob") == 0) {
        cJSON *pattern = cJSON_GetObjectItem(arguments, "pattern");
        if (cJSON_IsString(pattern)) {
            strlcpy(details, pattern->valuestring, sizeof(details));
        }
    } else if (strcmp(tool_name, "Grep") == 0) {
        cJSON *pattern = cJSON_GetObjectItem(arguments, "pattern");
        cJSON *path = cJSON_GetObjectItem(arguments, "path");

        if (cJSON_IsString(pattern)) {
            if (cJSON_IsString(path) && strlen(path->valuestring) > 0 &&
                strcmp(path->valuestring, ".") != 0) {
                snprintf(details, sizeof(details), "\"%s\" in %s",
                        pattern->valuestring, path->valuestring);
            } else {
                snprintf(details, sizeof(details), "\"%s\"", pattern->valuestring);
            }
        }
    } else if (strcmp(tool_name, "TodoWrite") == 0) {
        cJSON *todos = cJSON_GetObjectItem(arguments, "todos");
        if (cJSON_IsArray(todos)) {
            int count = cJSON_GetArraySize(todos);
            snprintf(details, sizeof(details), "%d task%s", count, count == 1 ? "" : "s");
        }
    } else if (strcmp(tool_name, "Sleep") == 0) {
        cJSON *duration = cJSON_GetObjectItem(arguments, "duration");
        if (cJSON_IsNumber(duration)) {
            int seconds = duration->valueint;
            if (seconds == 1) {
                snprintf(details, sizeof(details), "for 1 second");
            } else {
                snprintf(details, sizeof(details), "for %d seconds", seconds);
            }
        }
    } else if (strcmp(tool_name, "UploadImage") == 0) {
        cJSON *file_path = cJSON_GetObjectItem(arguments, "file_path");
        if (cJSON_IsString(file_path)) {
            const char *path = file_path->valuestring;
            // Extract just the filename from the path
            const char *filename = strrchr(path, '/');
            filename = filename ? filename + 1 : path;
            strlcpy(details, filename, sizeof(details));
        }
    } else if (strcmp(tool_name, "CheckSubagentProgress") == 0) {
        cJSON *pid = cJSON_GetObjectItem(arguments, "pid");
        cJSON *log_file = cJSON_GetObjectItem(arguments, "log_file");
        if (cJSON_IsNumber(pid)) {
            snprintf(details, sizeof(details), "PID %d", pid->valueint);
        } else if (cJSON_IsString(log_file)) {
            const char *path = log_file->valuestring;
            // Extract just the filename from the path
            const char *filename = strrchr(path, '/');
            filename = filename ? filename + 1 : path;
            snprintf(details, sizeof(details), "log: %s", filename);
        } else {
            snprintf(details, sizeof(details), "checking subagent");
        }
    } else if (strcmp(tool_name, "InterruptSubagent") == 0) {
        cJSON *pid = cJSON_GetObjectItem(arguments, "pid");
        if (cJSON_IsNumber(pid)) {
            snprintf(details, sizeof(details), "PID %d", pid->valueint);
        } else {
            snprintf(details, sizeof(details), "interrupt subagent");
        }
    } else if (strcmp(tool_name, "ListMcpResources") == 0) {
        cJSON *server = cJSON_GetObjectItem(arguments, "server");
        if (cJSON_IsString(server)) {
            snprintf(details, sizeof(details), "server: %s", server->valuestring);
        } else {
            snprintf(details, sizeof(details), "all servers");
        }
    } else if (strcmp(tool_name, "ReadMcpResource") == 0) {
        cJSON *server = cJSON_GetObjectItem(arguments, "server");
        cJSON *uri = cJSON_GetObjectItem(arguments, "uri");
        if (cJSON_IsString(server) && cJSON_IsString(uri)) {
            // Show server and truncate URI if too long
            const char *uri_str = uri->valuestring;
            size_t uri_len = strlen(uri_str);
            if (uri_len > 30) {
                snprintf(details, sizeof(details), "%s: %.27s...", server->valuestring, uri_str);
            } else {
                snprintf(details, sizeof(details), "%s: %s", server->valuestring, uri_str);
            }
        } else if (cJSON_IsString(server)) {
            snprintf(details, sizeof(details), "server: %s", server->valuestring);
        } else if (cJSON_IsString(uri)) {
            const char *uri_str = uri->valuestring;
            size_t uri_len = strlen(uri_str);
            if (uri_len > 30) {
                snprintf(details, sizeof(details), "%.27s...", uri_str);
            } else {
                snprintf(details, sizeof(details), "%s", uri_str);
            }
        }
    } else if (strncmp(tool_name, "mcp_", 4) == 0) {
        // Handle MCP tools (format: mcp_<server>_<toolname>)
        // Extract the actual tool name after the server prefix for display
        const char *actual_tool = strchr(tool_name + 4, '_');
        if (actual_tool) {
            actual_tool++; // Skip the underscore

            // Try to extract the most relevant argument for display
            // Common patterns: url, text, path, element, values, etc.
            cJSON *url = cJSON_GetObjectItem(arguments, "url");
            cJSON *text = cJSON_GetObjectItem(arguments, "text");
            cJSON *path = cJSON_GetObjectItem(arguments, "path");
            cJSON *element = cJSON_GetObjectItem(arguments, "element");

            if (cJSON_IsString(url)) {
                // Tools with URL parameter (navigate, fetch, etc.)
                snprintf(details, sizeof(details), "%s: %s", actual_tool, url->valuestring);
            } else if (cJSON_IsString(text) && strlen(text->valuestring) > 0) {
                // Tools with text parameter (type, search, etc.)
                snprintf(details, sizeof(details), "%s: %.30s%s", actual_tool,
                        text->valuestring,
                        strlen(text->valuestring) > 30 ? "..." : "");
            } else if (cJSON_IsString(path)) {
                // Tools with path parameter (read, write, etc.)
                snprintf(details, sizeof(details), "%s: %s", actual_tool, path->valuestring);
            } else if (cJSON_IsString(element)) {
                // Tools with element parameter (click, hover, etc.)
                snprintf(details, sizeof(details), "%s: %s", actual_tool, element->valuestring);
            } else {
                // Generic display: just show the tool name
                snprintf(details, sizeof(details), "%s", actual_tool);
            }
            details[sizeof(details) - 1] = '\0';
        } else {
            // Fallback: show the full tool name without "mcp_" prefix
            snprintf(details, sizeof(details), "%s", tool_name + 4);
            details[sizeof(details) - 1] = '\0';
        }
    }

    return strlen(details) > 0 ? details : NULL;
}

static void print_error(const char *text) {
    // Log to file only (no stderr output)
    LOG_ERROR("%s", text);
}



// ============================================================================
// Data Structures
// ============================================================================

// Note: MessageRole, ContentType, ContentBlock, Message, and ConversationState
// are now defined in klawed_internal.h for sharing across modules


// ============================================================================
// ESC Key Interrupt Handling
// ============================================================================



// ============================================================================
// Utility Functions
// ============================================================================

// For testing, we need to export some functions
#ifdef TEST_BUILD
#define STATIC
// Forward declarations for TEST_BUILD
char* read_file(const char *path);
int write_file(const char *path, const char *content);
char* resolve_path(const char *path, const char *working_dir);
cJSON* tool_read(cJSON *params, ConversationState *state);
cJSON* tool_write(cJSON *params, ConversationState *state);
cJSON* tool_edit(cJSON *params, ConversationState *state);
cJSON* tool_todo_write(cJSON *params, ConversationState *state);
cJSON* tool_bash(cJSON *params, ConversationState *state);
cJSON* tool_subagent(cJSON *params, ConversationState *state);
static cJSON* tool_sleep(cJSON *params, ConversationState *state);
static cJSON* tool_upload_image(cJSON *params, ConversationState *state);
static cJSON* tool_check_subagent_progress(cJSON *params, ConversationState *state);
static cJSON* tool_interrupt_subagent(cJSON *params, ConversationState *state);
#else
#define STATIC static
// Forward declarations
char* read_file(const char *path);
int write_file(const char *path, const char *content);
char* resolve_path(const char *path, const char *working_dir);
#endif


char* read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize < 0) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);

    char *content = malloc((size_t)fsize + 1);
    if (content) {
        size_t bytes_read = fread(content, 1, (size_t)fsize, f);
        (void)bytes_read; // Suppress unused result warning
        content[fsize] = 0;
    }

    fclose(f);
    return content;
}

int write_file(const char *path, const char *content) {
    // Create parent directories if they don't exist
    char *path_copy = strdup(path);
    if (!path_copy) return -1;

    // Extract directory path
    char *dir_path = dirname(path_copy);

    // Create directory recursively (ignore errors if directory already exists)
    char mkdir_cmd[PATH_MAX];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p '%s' 2>/dev/null", dir_path);
    int mkdir_result = system(mkdir_cmd);
    (void)mkdir_result; // Suppress unused result warning

    free(path_copy);

    // Now try to open/create the file
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);

    return (written == len) ? 0 : -1;
}

char* resolve_path(const char *path, const char *working_dir) {
    // Join with working_dir if relative; attempt to canonicalize if possible.
    char joined[PATH_MAX];
    if (path[0] == '/') {
        snprintf(joined, sizeof(joined), "%s", path);
    } else {
        snprintf(joined, sizeof(joined), "%s/%s", working_dir, path);
    }

    // Try to canonicalize. This succeeds only if the path (or its parents) exist.
    char *clean = realpath(joined, NULL);
    if (clean) {
        return clean; // Caller takes ownership
    }

    // Fall back to the joined path even if parent dirs don't exist.
    // This enables tools like Write to create missing directories (mkdir -p in write_file).
    return strdup(joined);
}

// Add a directory to the additional working directories list
// Returns: 0 on success, -1 on error
int add_directory(ConversationState *state, const char *path) {
    if (!state || !path) {
        return -1;
    }

    if (conversation_state_lock(state) != 0) {
        return -1;
    }

    // Validate that directory exists
    int result = -1;
    struct stat st;
    char *resolved_path = NULL;

    // Resolve path (handle relative paths)
    if (path[0] == '/') {
        resolved_path = realpath(path, NULL);
    } else {
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", state->working_dir, path);
        resolved_path = realpath(full_path, NULL);
    }

    if (!resolved_path) {
        goto out;  // Path doesn't exist or can't be resolved
    }

    if (stat(resolved_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        goto out;  // Not a directory
    }

    // Check if directory is already in the list (avoid duplicates)
    if (strcmp(resolved_path, state->working_dir) == 0) {
        goto out;  // Already the main working directory
    }

    for (int i = 0; i < state->additional_dirs_count; i++) {
        if (strcmp(resolved_path, state->additional_dirs[i]) == 0) {
            goto out;  // Already in additional directories
        }
    }

    // Expand array if needed
    if (state->additional_dirs_count >= state->additional_dirs_capacity) {
        int new_capacity = state->additional_dirs_capacity == 0 ? 4 : state->additional_dirs_capacity * 2;
        char **new_array = reallocarray(state->additional_dirs, (size_t)new_capacity, sizeof(char*));
        if (!new_array) {
            goto out;  // Out of memory
        }
        state->additional_dirs = new_array;
        state->additional_dirs_capacity = new_capacity;
    }

    // Add directory to list
    state->additional_dirs[state->additional_dirs_count++] = resolved_path;
    resolved_path = NULL;
    result = 0;

out:
    conversation_state_unlock(state);
    free(resolved_path);
    return result;
}

static cJSON* tool_upload_image(cJSON *params, ConversationState *state) {
    const cJSON *path_json = cJSON_GetObjectItem(params, "file_path");

    if (!path_json || !cJSON_IsString(path_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing 'file_path' parameter");
        return error;
    }

    // Check for interrupt before starting
    if (state && state->interrupt_requested) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Operation interrupted by user");
        return error;
    }

    // Clean the path - remove newlines and trailing whitespace
    // Users might copy paths that include newlines
    char cleaned_path[PATH_MAX];
    const char *src_ptr = path_json->valuestring;
    char *dst_ptr = cleaned_path;
    size_t j = 0;

    while (*src_ptr && j < sizeof(cleaned_path) - 1) {
        if (*src_ptr != '\n' && *src_ptr != '\r') {
            *dst_ptr++ = *src_ptr;
            j++;
        }
        src_ptr++;
    }
    *dst_ptr = '\0';

    // Also trim trailing whitespace
    while (dst_ptr > cleaned_path && (*(dst_ptr-1) == ' ' || *(dst_ptr-1) == '\t')) {
        *(--dst_ptr) = '\0';
    }

    // Resolve the path relative to working directory
    char *resolved_path = resolve_path(cleaned_path, state->working_dir);
    if (!resolved_path) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to resolve path");
        return error;
    }

    // Check if file exists and is readable
    if (access(resolved_path, R_OK) != 0) {
        cJSON *error = cJSON_CreateObject();
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "Cannot read image file '%s': %s",
                 resolved_path, strerror(errno));
        cJSON_AddStringToObject(error, "error", err_msg);
        free(resolved_path);
        return error;
    }

    // Check if this is a macOS temporary screenshot file
    // macOS temporary screenshots are in paths like:
    // /var/folders/xx/xxxxxxxxxxxxxxxxxxxx/T/TemporaryItems/NSIRD_screencaptureui_xxxxxx/
    int created_temp_copy = 0;
    char *temp_copy_path = NULL;
    char *path_to_read = resolved_path; // This will point to either original or temp copy

    if (strstr(resolved_path, "/var/folders/") == resolved_path ||
        strstr(resolved_path, "/private/var/folders/") == resolved_path) {
        // This is a macOS temporary file - create a copy in a safe location

        // Create a temporary filename in /tmp
        // Use mkstemp to create a secure temp file
        char template[PATH_MAX];
        snprintf(template, sizeof(template), "/tmp/klawed-upload-XXXXXX");

        int fd = mkstemp(template);
        if (fd == -1) {
            LOG_WARN("Failed to create temp file for macOS screenshot copy: %s", strerror(errno));
            // Continue with original path - it might work
        } else {
            close(fd);
            temp_copy_path = strdup(template);

            // Copy the file
            FILE *src = fopen(resolved_path, "rb");
            FILE *dst = fopen(temp_copy_path, "wb");

            if (src && dst) {
                char buffer[8192];
                size_t bytes;
                while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
                    fwrite(buffer, 1, bytes, dst);
                }

                fclose(src);
                fclose(dst);

                LOG_DEBUG("Copied macOS temporary screenshot from '%s' to '%s'",
                         resolved_path, temp_copy_path);

                // Use the temp copy for reading
                path_to_read = temp_copy_path;
                created_temp_copy = 1;
            } else {
                if (src) fclose(src);
                if (dst) fclose(dst);
                unlink(template); // Clean up failed copy
                free(temp_copy_path);
                temp_copy_path = NULL;
                LOG_WARN("Failed to copy macOS temporary screenshot");
                // Continue with original path
            }
        }
    }

    // Read the image file as binary
    FILE *f = fopen(path_to_read, "rb");
    if (!f) {
        cJSON *error = cJSON_CreateObject();
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "Failed to open image file '%s': %s",
                 path_to_read, strerror(errno));
        cJSON_AddStringToObject(error, "error", err_msg);
        free(resolved_path);
        if (temp_copy_path) {
            free(temp_copy_path);
        }
        return error;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(f);
        free(resolved_path);
        if (temp_copy_path) {
            free(temp_copy_path);
        }
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Image file is empty or invalid");
        return error;
    }

    // Read file content
    unsigned char *image_data = malloc((size_t)file_size);
    if (!image_data) {
        fclose(f);
        free(resolved_path);
        if (temp_copy_path) {
            free(temp_copy_path);
        }
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Memory allocation failed");
        return error;
    }

    size_t bytes_read = fread(image_data, 1, (size_t)file_size, f);
    fclose(f);

    if (bytes_read != (size_t)file_size) {
        free(image_data);
        free(resolved_path);
        if (temp_copy_path) {
            free(temp_copy_path);
        }
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to read entire image file");
        return error;
    }

    // Determine MIME type from file extension first
    // Use the cleaned path for extension detection, not the temp copy
    const char *mime_type = "image/jpeg"; // default
    const char *ext = strrchr(cleaned_path, '.');
    if (!ext) {
        // Fall back to resolved path
        ext = strrchr(resolved_path, '.');
    }
    if (ext) {
        // Convert to lowercase for case-insensitive comparison
        char lower_ext[16];
        size_t ext_len = strlen(ext);
        if (ext_len < sizeof(lower_ext)) {
            for (size_t k = 0; k < ext_len; k++) {
                lower_ext[k] = (char)tolower((unsigned char)ext[k]);
            }
            lower_ext[ext_len] = '\0';

            if (strcmp(lower_ext, ".png") == 0) {
                mime_type = "image/png";
            } else if (strcmp(lower_ext, ".jpg") == 0 || strcmp(lower_ext, ".jpeg") == 0) {
                mime_type = "image/jpeg";
            } else if (strcmp(lower_ext, ".gif") == 0) {
                mime_type = "image/gif";
            } else if (strcmp(lower_ext, ".webp") == 0) {
                mime_type = "image/webp";
            } else if (strcmp(lower_ext, ".bmp") == 0) {
                mime_type = "image/bmp";
            } else if (strcmp(lower_ext, ".tiff") == 0 || strcmp(lower_ext, ".tif") == 0) {
                mime_type = "image/tiff";
            } else if (strcmp(lower_ext, ".svg") == 0) {
                mime_type = "image/svg+xml";
            }
        }
    }

    // Try to detect image type from magic numbers (file signatures)
    // This helps with temporary files that might have no extension or wrong extension
    if (file_size >= 8) {  // Need at least 8 bytes for most magic numbers
        unsigned char magic[8];
        memcpy(magic, image_data, 8);

        // PNG: \x89PNG\r\n\x1a\n
        if (magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G' &&
            magic[4] == 0x0D && magic[5] == 0x0A && magic[6] == 0x1A && magic[7] == 0x0A) {
            mime_type = "image/png";
        }
        // JPEG: \xff\xd8\xff
        else if (magic[0] == 0xFF && magic[1] == 0xD8 && magic[2] == 0xFF) {
            mime_type = "image/jpeg";
        }
        // GIF: "GIF87a" or "GIF89a"
        else if (magic[0] == 'G' && magic[1] == 'I' && magic[2] == 'F' && magic[3] == '8' &&
                (magic[4] == '7' || magic[4] == '9') && magic[5] == 'a') {
            mime_type = "image/gif";
        }
        // WebP: "RIFF" + "WEBP" (need to check more bytes)
        else if (file_size >= 12) {
            // Check first 4 bytes for "RIFF"
            if (magic[0] == 'R' && magic[1] == 'I' && magic[2] == 'F' && magic[3] == 'F') {
                // Check bytes 8-11 for "WEBP"
                if (image_data[8] == 'W' && image_data[9] == 'E' &&
                    image_data[10] == 'B' && image_data[11] == 'P') {
                    mime_type = "image/webp";
                }
            }
        }
        // BMP: "BM"
        else if (magic[0] == 'B' && magic[1] == 'M') {
            mime_type = "image/bmp";
        }
        // TIFF: "II" (little-endian) or "MM" (big-endian)
        else if ((magic[0] == 'I' && magic[1] == 'I') || (magic[0] == 'M' && magic[1] == 'M')) {
            mime_type = "image/tiff";
        }
    }

    // Base64 encode the image (must happen after magic number detection)
    size_t encoded_size = 0;
    char *base64_data = base64_encode(image_data, (size_t)file_size, &encoded_size);
    free(image_data);

    if (!base64_data) {
        free(resolved_path);
        if (temp_copy_path) {
            free(temp_copy_path);
        }
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to encode image as base64");
        return error;
    }

    // Create an image content block instead of a regular tool result
    // This will be handled specially in the message building logic
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "success");
    cJSON_AddStringToObject(result, "message", "Image uploaded successfully");
    // Store the original resolved path, not the temp copy path
    cJSON_AddStringToObject(result, "file_path", resolved_path);
    cJSON_AddStringToObject(result, "original_path", path_json->valuestring);
    cJSON_AddStringToObject(result, "mime_type", mime_type);
    cJSON_AddNumberToObject(result, "file_size_bytes", (double)file_size);
    cJSON_AddStringToObject(result, "base64_data", base64_data);
    cJSON_AddStringToObject(result, "content_type", "image"); // Special marker for image content

    free(base64_data);

    // Clean up temp file if we created one
    if (created_temp_copy && temp_copy_path) {
        LOG_DEBUG("Cleaning up temporary screenshot copy: %s", temp_copy_path);
        unlink(temp_copy_path);
    }

    // Free paths
    free(resolved_path);
    if (temp_copy_path) {
        free(temp_copy_path);
    }

    return result;
}

// ============================================================================
// ANSI Escape Sequence Filtering
// ============================================================================

/**
 * Strip ANSI escape sequences from a string
 * This prevents terminal corruption when displaying command output in ncurses
 */
static char* strip_ansi_escapes(const char *input) {
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

// ============================================================================
// Binary File Handling for MCP Tools
// ============================================================================

/**
 * Save binary data to a file
 * Returns: 0 on success, -1 on error
 */
static int save_binary_file(const char *filename, const void *data, size_t size) {
    if (!filename || !data || size == 0) {
        return -1;
    }

    // Create parent directories if they don't exist
    char *path_copy = strdup(filename);
    if (!path_copy) return -1;

    // Extract directory path
    char *dir_path = dirname(path_copy);

    // Create directory recursively (ignore errors if directory already exists)
    char mkdir_cmd[PATH_MAX];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p '%s' 2>/dev/null", dir_path);
    int mkdir_result = system(mkdir_cmd);
    (void)mkdir_result; // Suppress unused result warning

    free(path_copy);

    // Open file for writing
    FILE *f = fopen(filename, "wb");
    if (!f) {
        LOG_ERROR("Failed to open file for binary writing: %s", filename);
        return -1;
    }

    // Write binary data
    size_t written = fwrite(data, 1, size, f);
    fclose(f);

    if (written != size) {
        LOG_ERROR("Failed to write all binary data to file: %s (written: %zu, expected: %zu)",
                 filename, written, size);
        return -1;
    }

    LOG_DEBUG("Successfully saved binary data to '%s' (%zu bytes)", filename, size);
    return 0;
}

/**
 * Generate timestamped filename
 * Format: <prefix>_YYYYMMDD_HHMMSS.<extension>
 */
static void generate_timestamped_filename(char *buffer, size_t buffer_size,
                                         const char *prefix, const char *mime_type) {
    if (!buffer || buffer_size == 0) return;

    // Get current time
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    // Determine file extension from MIME type
    const char *extension = "bin";
    if (mime_type) {
        if (strcmp(mime_type, "image/png") == 0) {
            extension = "png";
        } else if (strcmp(mime_type, "image/jpeg") == 0 || strcmp(mime_type, "image/jpg") == 0) {
            extension = "jpg";
        } else if (strcmp(mime_type, "image/gif") == 0) {
            extension = "gif";
        } else if (strcmp(mime_type, "image/webp") == 0) {
            extension = "webp";
        } else if (strncmp(mime_type, "image/", 6) == 0) {
            // Generic image type
            extension = "img";
        }
    }

    // Generate filename
    snprintf(buffer, buffer_size, "%s_%04d%02d%02d_%02d%02d%02d.%s",
             prefix ? prefix : "file",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
             extension);
}

/**
 * Format file size in human-readable format
 * Returns: Static buffer with formatted size (not thread-safe)
 */
static const char* format_file_size(size_t size) {
    static char buffer[32];

    if (size < 1024) {
        snprintf(buffer, sizeof(buffer), "%zu B", size);
    } else if (size < 1024 * 1024) {
        snprintf(buffer, sizeof(buffer), "%.1f KB", (double)size / 1024);
    } else if (size < 1024 * 1024 * 1024) {
        snprintf(buffer, sizeof(buffer), "%.1f MB", (double)size / (1024 * 1024));
    } else {
        snprintf(buffer, sizeof(buffer), "%.1f GB", (double)size / (1024 * 1024 * 1024));
    }

    return buffer;
}

// ============================================================================
// Diff Functionality
// ============================================================================

// Show unified diff between original content and current file
// Returns 0 on success, -1 on error
static int show_diff(const char *file_path, const char *original_content) {
    // Create temporary file for original content
    char temp_path[PATH_MAX];
    snprintf(temp_path, sizeof(temp_path), "%s.klawed_diff.XXXXXX", file_path);

    int fd = mkstemp(temp_path);
    if (fd == -1) {
        LOG_ERROR("Failed to create temporary file for diff");
        return -1;
    }

    // Write original content to temp file
    size_t content_len = strlen(original_content);
    ssize_t written = write(fd, original_content, content_len);
    close(fd);

    if (written < 0 || (size_t)written != content_len) {
        LOG_ERROR("Failed to write original content to temp file");
        unlink(temp_path);
        return -1;
    }

    // Run diff command to show changes
    char diff_cmd[PATH_MAX * 2];
    snprintf(diff_cmd, sizeof(diff_cmd), "diff -u \"%s\" \"%s\"", temp_path, file_path);

    FILE *pipe = popen(diff_cmd, "r");
    if (!pipe) {
        LOG_ERROR("Failed to run diff command");
        unlink(temp_path);
        return -1;
    }

    // Get color codes for added and removed lines
    char add_color[32], remove_color[32];
    const char *add_color_str, *remove_color_str;

    // Try to get colors from colorscheme, fall back to ANSI colors
    if (get_colorscheme_color(COLORSCHEME_DIFF_ADD, add_color, sizeof(add_color)) == 0) {
        add_color_str = add_color;
    } else {
        LOG_WARN("Using fallback ANSI color for DIFF_ADD");
        add_color_str = ANSI_FALLBACK_DIFF_ADD;
    }

    if (get_colorscheme_color(COLORSCHEME_DIFF_REMOVE, remove_color, sizeof(remove_color)) == 0) {
        remove_color_str = remove_color;
    } else {
        LOG_WARN("Using fallback ANSI color for DIFF_REMOVE");
        remove_color_str = ANSI_FALLBACK_DIFF_REMOVE;
    }

    // Read and display diff output with simple indentation
    char line[1024];
    int has_diff = 0;

    while (fgets(line, sizeof(line), pipe)) {
        has_diff = 1;
        emit_diff_line(line, add_color_str, remove_color_str);
    }

    int result = pclose(pipe);
    unlink(temp_path);

    if (!has_diff) {
        tool_emit_line(" ", "(No changes - files are identical)");
    } else if (result == 0) {
        // diff exit code 0 means no differences found
        tool_emit_line(" ", "(No differences found)");
    }


    return 0;
}

// ============================================================================
// Tool Implementations
// ============================================================================

STATIC cJSON* tool_bash(cJSON *params, ConversationState *state) {
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
        printf("[TOOL VERBOSE] Bash tool executing command: %s\n", command);
    }

    // Get timeout from parameter, environment, or use default (30 seconds)
    int timeout_seconds = 30;  // Default timeout

    // First check if timeout parameter is provided
    const cJSON *timeout_json = cJSON_GetObjectItem(params, "timeout");
    if (timeout_json && cJSON_IsNumber(timeout_json)) {
        timeout_seconds = timeout_json->valueint;
        if (timeout_seconds < 0) {
            timeout_seconds = 0;  // Negative values treated as 0 (no timeout)
        }
    } else {
        // Fall back to environment variable
        const char *timeout_env = getenv("KLAWED_BASH_TIMEOUT");
        if (timeout_env) {
            int timeout_val = atoi(timeout_env);
            if (timeout_val >= 0) {
                timeout_seconds = timeout_val;
            }
        }
    }

    // Execute command and capture both stdout and stderr
    // Use shell wrapper with proper quoting and stderr redirection
    // full_command needs extra space for "sh -c '...' </dev/null 2>&1" wrapper (24 bytes + null)
    char full_command[BUFFER_SIZE + 32];
    // Escape single quotes in the command for shell safety
    char escaped_command[BUFFER_SIZE];
    size_t j = 0;
    for (size_t i = 0; command[i] && j < sizeof(escaped_command) - 1; i++) {
        if (command[i] == '\'') {
            if (j < sizeof(escaped_command) - 2) {
                escaped_command[j++] = '\'';
                escaped_command[j++] = '\\';
                escaped_command[j++] = '\'';
                escaped_command[j++] = '\'';
            }
        } else {
            escaped_command[j++] = command[i];
        }
    }
    escaped_command[j] = '\0';

    // Use shell wrapper to ensure consistent execution and stderr capture
    // Redirect stdin to /dev/null to prevent child processes from competing for terminal input
    snprintf(full_command, sizeof(full_command), "sh -c '%s' </dev/null 2>&1", escaped_command);

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

    FILE *pipe = popen(full_command, "r");
    if (!pipe) {
        // Restore stderr before returning error
        if (saved_stderr != -1) {
            dup2(saved_stderr, STDERR_FILENO);
            close(saved_stderr);
            fflush(stderr);
        }
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to execute command");
        return error;
    }

    char *output = NULL;
    size_t total_size = 0;
    char buffer[BUFFER_SIZE];

    // Get the file descriptor from the pipe
    int fd = fileno(pipe);

    int timed_out = 0;
    int truncated = 0;

    while (1) {
        // Check for interrupt during long-running command
        if (state && state->interrupt_requested) {
            free(output);
            pclose(pipe);
            // Restore stderr before returning error
            if (saved_stderr != -1) {
                dup2(saved_stderr, STDERR_FILENO);
                close(saved_stderr);
                fflush(stderr);
            }
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "Operation interrupted by user");
            return error;
        }

        // Add cancellation point to allow thread cancellation during long reads
        pthread_testcancel();

        // Use select() to check if there's data available with timeout
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        // Set up timeout using select() - pass NULL for timeout if timeout_seconds is 0
        struct timeval timeout;
        struct timeval *timeout_ptr = NULL;

        if (timeout_seconds > 0) {
            timeout.tv_sec = timeout_seconds;
            timeout.tv_usec = 0;
            timeout_ptr = &timeout;
        }

        int select_result = select(fd + 1, &readfds, NULL, NULL, timeout_ptr);

        if (select_result == -1) {
            // select() error
            LOG_ERROR("select() failed: %s", strerror(errno));
            free(output);
            pclose(pipe);
            // Restore stderr before returning error
            if (saved_stderr != -1) {
                dup2(saved_stderr, STDERR_FILENO);
                close(saved_stderr);
                fflush(stderr);
            }
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "Failed to monitor command execution");
            return error;
        } else if (select_result == 0) {
            // Timeout occurred
            timed_out = 1;
            LOG_WARN("Bash command timed out after %d seconds: %s", timeout_seconds, command);
            break;
        }

        // Data is available, try to read
        if (fgets(buffer, sizeof(buffer), pipe) == NULL) {
            // EOF or error
            if (ferror(pipe)) {
                LOG_ERROR("Error reading from pipe: %s", strerror(errno));
            }
            break;
        }

        size_t len = strlen(buffer);

        // Check if adding this buffer would exceed maximum output size
        if (total_size + len >= BASH_OUTPUT_MAX_SIZE) {
            // Calculate how much we can add before hitting the limit
            size_t remaining = BASH_OUTPUT_MAX_SIZE - total_size;
            if (remaining > 0) {
                // Add partial buffer content
                char *new_output = realloc(output, total_size + remaining + 1);
                if (!new_output) {
                    free(output);
                    pclose(pipe);
                    // Restore stderr before returning error
                    if (saved_stderr != -1) {
                        dup2(saved_stderr, STDERR_FILENO);
                        close(saved_stderr);
                        fflush(stderr);
                    }
                    cJSON *error = cJSON_CreateObject();
                    cJSON_AddStringToObject(error, "error", "Out of memory");
                    return error;
                }
                output = new_output;
                memcpy(output + total_size, buffer, remaining);
                total_size += remaining;
                output[total_size] = '\0';
            }

            // Set truncated flag and break out of the loop
            truncated = 1;
            break;
        }

        char *new_output = realloc(output, total_size + len + 1);
        if (!new_output) {
            free(output);
            pclose(pipe);
            // Restore stderr before returning error
            if (saved_stderr != -1) {
                dup2(saved_stderr, STDERR_FILENO);
                close(saved_stderr);
                fflush(stderr);
            }
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "Out of memory");
            return error;
        }
        output = new_output;
        memcpy(output + total_size, buffer, len);
        total_size += len;
        output[total_size] = '\0';

        // Reset timeout for next read
        timeout.tv_sec = timeout_seconds;
        timeout.tv_usec = 0;
    }

    int status;
    if (timed_out) {
        // Kill the process group to ensure all child processes are terminated
        pid_t pgid = getpgid(fileno(pipe));
        if (pgid > 0) {
            kill(-pgid, SIGTERM);
            // Give it a moment to terminate gracefully
            usleep(100000); // 100ms
            kill(-pgid, SIGKILL);
        }
        status = pclose(pipe);
    } else {
        status = pclose(pipe);
    }

    int exit_code;
    if (timed_out) {
        exit_code = -2;  // Special code for timeout
    } else {
        exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
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

    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "exit_code", exit_code);
    cJSON_AddStringToObject(result, "output", clean_output ? clean_output : (output ? output : ""));

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
                total_size, BASH_OUTPUT_MAX_SIZE);
        cJSON_AddStringToObject(result, "truncation_warning", truncate_msg);
    }

    // Restore stderr if we redirected it
    if (saved_stderr != -1) {
        dup2(saved_stderr, STDERR_FILENO);
        close(saved_stderr);
        fflush(stderr);
    }

    free(output);
    return result;
}

STATIC cJSON* tool_subagent(cJSON *params, ConversationState *state) {
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
    int timeout_seconds = 300;
    const cJSON *timeout_json = cJSON_GetObjectItem(params, "timeout");
    if (timeout_json && cJSON_IsNumber(timeout_json)) {
        timeout_seconds = timeout_json->valueint;
        if (timeout_seconds < 0) {
            timeout_seconds = 0;  // 0 = no timeout
        }
    }

    // Get optional tail_lines parameter (default: 100 lines from end)
    int tail_lines = 100;
    const cJSON *tail_json = cJSON_GetObjectItem(params, "tail_lines");
    if (tail_json && cJSON_IsNumber(tail_json)) {
        tail_lines = tail_json->valueint;
        if (tail_lines < 0) {
            tail_lines = 100;  // Default to 100 if negative
        }
    }

    // Create unique log file in .klawed/subagent/ directory
    char log_dir[PATH_MAX];
    snprintf(log_dir, sizeof(log_dir), "%s/.klawed/subagent", state->working_dir);

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
        len = (ssize_t)strlen(exe_path);
#else
        // Try argv[0] as fallback
        const char *fallback = "./build/klawed";
        strlcpy(exe_path, fallback, sizeof(exe_path));
        len = (ssize_t)strlen(exe_path);
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
        // Child process - set environment variable to indicate this is a subagent
        if (setenv("KLAWED_IS_SUBAGENT", "1", 1) != 0) {
            fprintf(stderr, "Warning: Failed to set KLAWED_IS_SUBAGENT environment variable: %s\n", strerror(errno));
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

STATIC cJSON* tool_check_subagent_progress(cJSON *params, ConversationState *state) {
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
            // Count total lines first
            char line[BUFFER_SIZE];
            while (fgets(line, sizeof(line), log_fp)) {
                total_lines++;
            }

            // Calculate which line to start from
            int start_line = (tail_lines > 0 && total_lines > tail_lines) ? (total_lines - tail_lines) : 0;

            // Read the tail content
            rewind(log_fp);
            int current_line = 0;

            while (fgets(line, sizeof(line), log_fp)) {
                if (current_line >= start_line) {
                    size_t line_len = strlen(line);
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

STATIC cJSON* tool_interrupt_subagent(cJSON *params, ConversationState *state) {
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

    // Try to kill the process
    int killed = 0;
    char kill_msg[256];

    // First try SIGTERM (graceful shutdown)
    if (kill(pid, SIGTERM) == 0) {
        snprintf(kill_msg, sizeof(kill_msg),
                 "Sent SIGTERM to subagent with PID %d. Waiting 2 seconds for graceful shutdown...",
                 pid);

        // Wait a bit for graceful shutdown
        sleep(2);

        // Check if it's still running
        int status;
        if (waitpid(pid, &status, WNOHANG) == 0) {
            // Still running, send SIGKILL
            if (kill(pid, SIGKILL) == 0) {
                snprintf(kill_msg + strlen(kill_msg), sizeof(kill_msg) - strlen(kill_msg),
                         " Process did not terminate gracefully. Sent SIGKILL.");
                killed = 1;
            } else {
                snprintf(kill_msg + strlen(kill_msg), sizeof(kill_msg) - strlen(kill_msg),
                         " Failed to send SIGKILL: %s", strerror(errno));
            }
        } else {
            // Process terminated
            snprintf(kill_msg + strlen(kill_msg), sizeof(kill_msg) - strlen(kill_msg),
                     " Process terminated gracefully.");
            killed = 1;
        }
    } else {
        // SIGTERM failed, maybe process doesn't exist or we don't have permission
        if (errno == ESRCH) {
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
    }

    // Build result
    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "pid", pid);
    cJSON_AddBoolToObject(result, "killed", killed);
    cJSON_AddStringToObject(result, "message", kill_msg);

    return result;
}

STATIC cJSON* tool_read(cJSON *params, ConversationState *state) {
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
        printf("[TOOL VERBOSE] Read tool reading file: %s\n", file_path);
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
            printf("[TOOL VERBOSE] Reading from line %d\n", start_line);
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
            printf("[TOOL VERBOSE] Reading to line %d\n", end_line);
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

    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "content", filtered_content);
    cJSON_AddNumberToObject(result, "total_lines", total_lines);

    if (start_line > 0 || end_line > 0) {
        cJSON_AddNumberToObject(result, "start_line", start_line > 0 ? start_line : 1);
        cJSON_AddNumberToObject(result, "end_line", end_line > 0 ? end_line : total_lines);
    }

    free(filtered_content);

    return result;
}

STATIC cJSON* tool_write(cJSON *params, ConversationState *state) {
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
        cJSON_AddStringToObject(error, "error", "Missing 'content' parameter");
        return error;
    }

    const char *file_path = path_json->valuestring;
    const char *content = content_json->valuestring;

    // Verbose logging for Write tool
    if (tool_verbose >= 1) {
        printf("[TOOL VERBOSE] Write tool writing to file: %s\n", file_path);
        if (tool_verbose >= 2) {
            size_t content_len = strlen(content);
            printf("[TOOL VERBOSE] Content length: %zu bytes\n", content_len);
            if (content_len > 0 && content_len <= 200) {
                printf("[TOOL VERBOSE] Content preview (first 200 chars):\n%.200s\n", content);
            } else if (content_len > 200) {
                printf("[TOOL VERBOSE] Content preview (first 200 chars):\n%.200s...\n", content);
            }
        }
    }

    // Check if content is in patch format
    if (is_patch_format(content)) {
        LOG_INFO("Detected patch format in Write tool, parsing and applying...");
        if (tool_verbose >= 1) {
            printf("[TOOL VERBOSE] Detected patch format, applying as patch\n");
        }

        // Parse the patch
        ParsedPatch *patch = parse_patch_format(content);
        if (!patch) {
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "Failed to parse patch format");
            return error;
        }

        // Apply the patch
        cJSON *result = apply_patch(patch, state);
        free_parsed_patch(patch);

        return result;
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

// Helper function for simple string multi-replace

// ============================================================================
// Parallel tool execution support
// ============================================================================

// Forward declaration
static cJSON* execute_tool(const char *tool_name, cJSON *input, ConversationState *state);

typedef void (*ToolCompletionCallback)(const ToolCompletion *completion, void *user_data);

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int total;
    int completed;
    int error_count;
    int cancelled;
    ToolCompletionCallback callback;
    void *callback_user_data;
} ToolExecutionTracker;

typedef struct {
    TUIState *tui;
    TUIMessageQueue *queue;
    Spinner *spinner;
    AIWorkerContext *worker_ctx;
} ToolCallbackContext;

typedef struct {
    char *tool_use_id;            // duplicated tool_call id
    const char *tool_name;        // name of the tool
    cJSON *input;                 // arguments for tool
    ConversationState *state;     // conversation state
    InternalContent *result_block;   // pointer to results array slot
    ToolExecutionTracker *tracker;  // shared tracker for completion signaling
    int notified;                  // guard against double notification
    TUIMessageQueue *queue;        // active TUI queue for tool output
} ToolThreadArg;

static int tool_tracker_init(ToolExecutionTracker *tracker,
                             int total,
                             ToolCompletionCallback callback,
                             void *user_data) {
    if (!tracker) {
        return -1;
    }

    if (pthread_mutex_init(&tracker->mutex, NULL) != 0) {
        return -1;
    }
    if (pthread_cond_init(&tracker->cond, NULL) != 0) {
        pthread_mutex_destroy(&tracker->mutex);
        return -1;
    }

    tracker->total = total;
    tracker->completed = 0;
    tracker->error_count = 0;
    tracker->cancelled = 0;
    tracker->callback = callback;
    tracker->callback_user_data = user_data;
    return 0;
}

static void tool_tracker_destroy(ToolExecutionTracker *tracker) {
    if (!tracker) {
        return;
    }
    pthread_cond_destroy(&tracker->cond);
    pthread_mutex_destroy(&tracker->mutex);
}

static void tool_tracker_notify_completion(ToolThreadArg *arg) {
    if (!arg || !arg->tracker) {
        return;
    }

    ToolExecutionTracker *tracker = arg->tracker;
    ToolCompletion completion = {0};
    ToolCompletionCallback cb = NULL;
    void *cb_data = NULL;

    pthread_mutex_lock(&tracker->mutex);
    if (!arg->notified) {
        arg->notified = 1;
        tracker->completed++;
        if (arg->result_block && arg->result_block->is_error) {
            tracker->error_count++;
        }

        completion.tool_name = arg->tool_name;
        completion.result = arg->result_block ? arg->result_block->tool_output : NULL;
        completion.is_error = arg->result_block ? arg->result_block->is_error : 1;
        completion.completed = tracker->completed;
        completion.total = tracker->total;
        cb = tracker->callback;
        cb_data = tracker->callback_user_data;

        pthread_cond_broadcast(&tracker->cond);
    }
    pthread_mutex_unlock(&tracker->mutex);

    if (cb) {
        cb(&completion, cb_data);
    }
}

static void tool_progress_callback(const ToolCompletion *completion, void *user_data) {
    if (!completion || !user_data) {
        return;
    }

    ToolCallbackContext *ctx = (ToolCallbackContext *)user_data;
    const char *tool_name = completion->tool_name ? completion->tool_name : "tool";
    const char *status_word = completion->is_error ? "failed" : "completed";

    char status[256];
    if (completion->total > 0) {
        snprintf(status, sizeof(status),
                 "Tool %s %s (%d/%d)",
                 tool_name,
                 status_word,
                 completion->completed,
                 completion->total);
    } else {
        snprintf(status, sizeof(status),
                 "Tool %s %s",
                 tool_name,
                 status_word);
    }

    if (ctx->spinner) {
        spinner_update(ctx->spinner, status);
    }

    if (ctx->worker_ctx) {
        ai_worker_handle_tool_completion(ctx->worker_ctx, completion);
    } else {
        ui_set_status(ctx->tui, ctx->queue, status);
    }
}

// Cleanup handler for tool thread cancellation
static void tool_thread_cleanup(void *arg) {
    ToolThreadArg *t = (ToolThreadArg *)arg;

    // Free input JSON if not already freed
    if (t->input) {
        cJSON_Delete(t->input);
        t->input = NULL;
    }

    // CRITICAL FIX: Check if we've already written results under mutex
    // to prevent data race with normal completion path
    pthread_mutex_t *mutex = t->tracker ? &t->tracker->mutex : NULL;
    if (mutex) {
        pthread_mutex_lock(mutex);
    }

    int should_write_result = !t->notified;

    if (mutex) {
        pthread_mutex_unlock(mutex);
    }

    // Only write cancellation result if not already completed
    if (should_write_result && t->result_block) {
        t->result_block->type = INTERNAL_TOOL_RESPONSE;
        t->result_block->tool_id = t->tool_use_id;
        t->result_block->tool_name = strdup(t->tool_name);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Tool execution cancelled by user");
        t->result_block->tool_output = error;
        t->result_block->is_error = 1;
    }

    tool_tracker_notify_completion(t);
}

static void *tool_thread_func(void *arg) {
    ToolThreadArg *t = (ToolThreadArg *)arg;

    // Enable thread cancellation
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    // Register cleanup handler
    pthread_cleanup_push(tool_thread_cleanup, arg);

    // Execute the tool
    TUIMessageQueue *previous_queue = g_active_tool_queue;
    g_active_tool_queue = t->queue;
    cJSON *res = execute_tool(t->tool_name, t->input, t->state);
    g_active_tool_queue = previous_queue;
    // Free input JSON
    cJSON_Delete(t->input);
    t->input = NULL;  // Mark as freed for cleanup handler
    // Populate result block
    // Check if this is an image upload result
    const cJSON *content_type = cJSON_GetObjectItem(res, "content_type");
    if (content_type && cJSON_IsString(content_type) && strcmp(content_type->valuestring, "image") == 0) {
        // This is an image upload - create image content instead of tool response
        t->result_block->type = INTERNAL_IMAGE;
        t->result_block->image_path = strdup(cJSON_GetObjectItem(res, "file_path")->valuestring);
        t->result_block->mime_type = strdup(cJSON_GetObjectItem(res, "mime_type")->valuestring);
        t->result_block->base64_data = strdup(cJSON_GetObjectItem(res, "base64_data")->valuestring);
        t->result_block->image_size = (size_t)cJSON_GetObjectItem(res, "file_size_bytes")->valueint;
        t->result_block->is_error = 0;
        // Free the result JSON since we've extracted the data
        cJSON_Delete(res);
    } else {
        // Regular tool response
        t->result_block->type = INTERNAL_TOOL_RESPONSE;
        t->result_block->tool_id = t->tool_use_id;
        t->result_block->tool_name = strdup(t->tool_name);  // Store tool name for error reporting
        t->result_block->tool_output = res;
        t->result_block->is_error = cJSON_HasObjectItem(res, "error");
    }

    tool_tracker_notify_completion(t);

    // Pop cleanup handler (execute=0 means don't run it on normal exit)
    pthread_cleanup_pop(0);

    return NULL;
}

// Helper function for simple string multi-replace
static char* str_replace_all(const char *content, const char *old_str, const char *new_str, int *replace_count) {
    *replace_count = 0;

    // Count occurrences
    const char *pos = content;
    while ((pos = strstr(pos, old_str)) != NULL) {
        (*replace_count)++;
        pos += strlen(old_str);
    }

    if (*replace_count == 0) {
        return NULL;
    }

    size_t old_len = strlen(old_str);
    size_t new_len = strlen(new_str);
    size_t content_len = strlen(content);
    size_t result_len = content_len + (size_t)(*replace_count) * (new_len - old_len);

    char *result = malloc(result_len + 1);
    if (!result) return NULL;

    char *dest = result;
    const char *src = content;

    while ((pos = strstr(src, old_str)) != NULL) {
        size_t len = (size_t)(pos - src);
        memcpy(dest, src, len);
        dest += len;
        memcpy(dest, new_str, new_len);
        dest += new_len;
        src = pos + old_len;
    }

    // Calculate remaining space and use strlcpy for safety
    size_t remaining_space = (size_t)((result + result_len + 1) - dest);
    strlcpy(dest, src, remaining_space);
    return result;
}

// Helper to expand backreferences in replacement string
// Supports \1 through \9 for capture groups, and \0 for full match
static char* expand_backreferences(const char *replacement, const char *src,
                                   const regmatch_t *matches, int num_matches) {
    if (!replacement || !src || !matches) return NULL;

    // Calculate required buffer size
    size_t buf_size = 256;
    char *result = malloc(buf_size);
    if (!result) return NULL;

    size_t result_len = 0;
    const char *p = replacement;

    while (*p) {
        if (*p == '\\' && *(p + 1) >= '0' && *(p + 1) <= '9') {
            // Backreference found
            int group_num = *(p + 1) - '0';
            p += 2;

            if (group_num < num_matches && matches[group_num].rm_so != -1) {
                size_t group_len = (size_t)(matches[group_num].rm_eo - matches[group_num].rm_so);

                // Ensure buffer has enough space
                if (result_len + group_len >= buf_size) {
                    buf_size = (result_len + group_len + 1) * 2;
                    char *new_result = realloc(result, buf_size);
                    if (!new_result) {
                        free(result);
                        return NULL;
                    }
                    result = new_result;
                }

                memcpy(result + result_len, src + matches[group_num].rm_so, group_len);
                result_len += group_len;
            }
        } else if (*p == '\\' && *(p + 1) == '\\') {
            // Escaped backslash
            if (result_len + 1 >= buf_size) {
                buf_size *= 2;
                char *new_result = realloc(result, buf_size);
                if (!new_result) {
                    free(result);
                    return NULL;
                }
                result = new_result;
            }
            result[result_len++] = '\\';
            p += 2;
        } else {
            // Regular character
            if (result_len + 1 >= buf_size) {
                buf_size *= 2;
                char *new_result = realloc(result, buf_size);
                if (!new_result) {
                    free(result);
                    return NULL;
                }
                result = new_result;
            }
            result[result_len++] = *p++;
        }
    }

    result[result_len] = '\0';
    return result;
}

// Helper function for regex replacement with capture group and flags support
// regex_flags: bitwise OR of REG_ICASE, REG_NEWLINE, etc.
static char* regex_replace_ex(const char *content, const char *pattern, const char *replacement,
                              int replace_all, int regex_flags, int *replace_count, char **error_msg) {
    *replace_count = 0;

    regex_t regex;
    int cflags = REG_EXTENDED | regex_flags;
    int ret = regcomp(&regex, pattern, cflags);
    if (ret != 0) {
        char err_buf[256];
        regerror(ret, &regex, err_buf, sizeof(err_buf));
        *error_msg = strdup(err_buf);
        return NULL;
    }

    // Support up to 10 capture groups (0 = full match, 1-9 = groups)
    #define MAX_MATCHES 10
    regmatch_t matches[MAX_MATCHES];
    const char *src = content;
    size_t result_capacity = strlen(content) * 2;
    char *result = malloc(result_capacity);
    if (!result) {
        regfree(&regex);
        *error_msg = strdup("Out of memory");
        return NULL;
    }

    char *dest = result;
    size_t dest_len = 0;

    while (regexec(&regex, src, MAX_MATCHES, matches, 0) == 0) {
        (*replace_count)++;

        // Copy text before match
        size_t prefix_len = (size_t)matches[0].rm_so;
        if (dest_len + prefix_len >= result_capacity) {
            result_capacity *= 2;
            char *new_result = realloc(result, result_capacity);
            if (!new_result) {
                free(result);
                regfree(&regex);
                *error_msg = strdup("Out of memory");
                return NULL;
            }
            result = new_result;
            dest = result + dest_len;
        }

        memcpy(dest, src, prefix_len);
        dest += prefix_len;
        dest_len += prefix_len;

        // Expand backreferences in replacement string
        char *expanded = expand_backreferences(replacement, src, matches, MAX_MATCHES);
        if (expanded) {
            size_t expanded_len = strlen(expanded);

            // Ensure buffer has enough space
            if (dest_len + expanded_len >= result_capacity) {
                result_capacity = (dest_len + expanded_len + 1) * 2;
                char *new_result = realloc(result, result_capacity);
                if (!new_result) {
                    free(expanded);
                    free(result);
                    regfree(&regex);
                    *error_msg = strdup("Out of memory");
                    return NULL;
                }
                result = new_result;
                dest = result + dest_len;
            }

            memcpy(dest, expanded, expanded_len);
            dest += expanded_len;
            dest_len += expanded_len;
            free(expanded);
        }

        src += matches[0].rm_eo;

        if (!replace_all) break;
    }

    // Copy remaining text
    size_t remaining = strlen(src);
    if (dest_len + remaining >= result_capacity) {
        result_capacity = dest_len + remaining + 1;
        char *new_result = realloc(result, result_capacity);
        if (!new_result) {
            free(result);
            regfree(&regex);
            *error_msg = strdup("Out of memory");
            return NULL;
        }
        result = new_result;
        dest = result + dest_len;
    }

    // Use strlcpy for safety
    size_t remaining_space = result_capacity - dest_len;
    strlcpy(dest, src, remaining_space);
    regfree(&regex);

    if (*replace_count == 0) {
        free(result);
        return NULL;
    }

    return result;
    #undef MAX_MATCHES
}

// Wrapper for backward compatibility (no flags)
__attribute__((unused))
static char* regex_replace(const char *content, const char *pattern, const char *replacement,
                          int replace_all, int *replace_count, char **error_msg) {
    return regex_replace_ex(content, pattern, replacement, replace_all, 0, replace_count, error_msg);
}

// === Helpers for Edit tool (file-scope) ===
static char* find_last_occurrence(const char *haystack, const char *needle) {
    if (!haystack || !needle || !*needle) return NULL;
    const char *last = NULL;
    const char *p = haystack;
    size_t nlen = strlen(needle);
    while ((p = strstr(p, needle)) != NULL) {
        last = p;
        p += nlen;
    }
    return (char*)last;
}

// Regex search supporting nth or last occurrence; if occurrence <= 0 => last
static int regex_find_pos(const char *text, const char *pattern, int occurrence, regmatch_t *out_match) {
    regex_t regex;
    int rc = regcomp(&regex, pattern, REG_EXTENDED);
    if (rc != 0) {
        return -1; // invalid regex
    }

    int found_index = -1;
    regmatch_t m;
    int index = 0;
    const char *cursor = text;
    int last_start = -1;
    regmatch_t last_m = (regmatch_t){0};
    while (*cursor) {
        if (regexec(&regex, cursor, 1, &m, 0) != 0) break;
        index++;
        int start = (int)(cursor - text) + (int)m.rm_so;
        if (occurrence > 0 && index == occurrence) {
            found_index = start;
            if (out_match) {
                out_match->rm_so = start;
                out_match->rm_eo = (int)(cursor - text) + (int)m.rm_eo;
            }
            regfree(&regex);
            return found_index;
        }
        // Save last
        last_start = start;
        last_m.rm_so = (int)(cursor - text) + (int)m.rm_so;
        last_m.rm_eo = (int)(cursor - text) + (int)m.rm_eo;
        // Advance cursor
        cursor += (m.rm_eo > 0) ? m.rm_eo : 1;
    }
    regfree(&regex);
    if (occurrence <= 0 && last_start >= 0) {
        if (out_match) {
            out_match->rm_so = last_m.rm_so;
            out_match->rm_eo = last_m.rm_eo;
        }
        return last_start;
    }
    return -2; // not found
}

STATIC cJSON* tool_edit(cJSON *params, ConversationState *state) {
    const cJSON *path_json = cJSON_GetObjectItem(params, "file_path");
    const cJSON *old_json = cJSON_GetObjectItem(params, "old_string");
    const cJSON *new_json = cJSON_GetObjectItem(params, "new_string");
    const cJSON *replace_all_json = cJSON_GetObjectItem(params, "replace_all");
    const cJSON *use_regex_json = cJSON_GetObjectItem(params, "use_regex");
    const cJSON *regex_flags_json = cJSON_GetObjectItem(params, "regex_flags"); // string: "i" for case-insensitive, "m" for multiline, "im" for both
    // Extended insert parameters (optional, backward compatible)
    const cJSON *insert_mode_json = cJSON_GetObjectItem(params, "insert_mode");
    const cJSON *anchor_json = cJSON_GetObjectItem(params, "anchor");
    const cJSON *anchor_is_regex_json = cJSON_GetObjectItem(params, "anchor_is_regex");
    const cJSON *insert_position_json = cJSON_GetObjectItem(params, "insert_position"); // "before" | "after"
    const cJSON *occurrence_json = cJSON_GetObjectItem(params, "occurrence"); // "first" | "last" | int
    const cJSON *fallback_to_eof_json = cJSON_GetObjectItem(params, "fallback_to_eof"); // bool

    if (!path_json || !new_json) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing required parameters");
        return error;
    }

    // Check if new_string content is in patch format
    const char *new_string_content = new_json->valuestring;
    if (is_patch_format(new_string_content)) {
        LOG_INFO("Detected patch format in Edit tool, parsing and applying...");

        // Parse the patch
        ParsedPatch *patch = parse_patch_format(new_string_content);
        if (!patch) {
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "Failed to parse patch format");
            return error;
        }

        // Apply the patch
        cJSON *result = apply_patch(patch, state);
        free_parsed_patch(patch);

        return result;
    }

    int replace_all = replace_all_json && cJSON_IsBool(replace_all_json) ?
                      cJSON_IsTrue(replace_all_json) : 0;
    int use_regex = use_regex_json && cJSON_IsBool(use_regex_json) ?
                    cJSON_IsTrue(use_regex_json) : 0;
    int insert_mode = insert_mode_json && cJSON_IsBool(insert_mode_json) ?
                      cJSON_IsTrue(insert_mode_json) : 0;
    int anchor_is_regex = anchor_is_regex_json && cJSON_IsBool(anchor_is_regex_json) ?
                          cJSON_IsTrue(anchor_is_regex_json) : 0;
    int fallback_to_eof = fallback_to_eof_json && cJSON_IsBool(fallback_to_eof_json) ?
                          cJSON_IsTrue(fallback_to_eof_json) : 0;

    // Parse regex flags
    int regex_flags = 0;
    if (regex_flags_json && cJSON_IsString(regex_flags_json)) {
        const char *flags_str = regex_flags_json->valuestring;
        for (const char *p = flags_str; *p; p++) {
            if (*p == 'i' || *p == 'I') {
                regex_flags |= REG_ICASE;
            } else if (*p == 'm' || *p == 'M') {
                regex_flags |= REG_NEWLINE;
            }
            // Additional flags can be added here (e.g., 's' for single-line mode if supported)
        }
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
    char *error_msg = NULL;

    // (helpers moved to file scope)

    if (insert_mode) {
        // Insertion mode using anchor
        const char *anchor = NULL;
        if (anchor_json && cJSON_IsString(anchor_json)) anchor = anchor_json->valuestring;
        // Default: if no anchor provided, fallback to old_string for convenience
        if (!anchor) anchor = old_str;

        if (!anchor) {
            free(content);
            free(original_content);
            free(resolved_path);
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "insert_mode requires 'anchor' or 'old_string'");
            return error;
        }

        int use_after = 0;
        if (insert_position_json && cJSON_IsString(insert_position_json)) {
            const char *pos_str = insert_position_json->valuestring;
            if (strcmp(pos_str, "after") == 0) use_after = 1;
        }

        // Determine which occurrence to use
        int which = 0; // 0 => last by default
        if (occurrence_json) {
            if (cJSON_IsString(occurrence_json)) {
                const char *o = occurrence_json->valuestring;
                if (strcmp(o, "first") == 0) which = 1;
                else if (strcmp(o, "last") == 0) which = 0;
            } else if (cJSON_IsNumber(occurrence_json)) {
                which = occurrence_json->valueint;
                if (which < 0) which = 0; // treat negative as last
            }
        }

        size_t content_len = strlen(content);
        size_t insert_at = (size_t)content_len; // default to EOF
        size_t anchor_len = 0;
        int found = 0;

        if (anchor_is_regex) {
            regmatch_t m = {0};
            int pos = regex_find_pos(content, anchor, which, &m);
            if (pos >= 0) {
                found = 1;
                insert_at = use_after ? (size_t)m.rm_eo : (size_t)m.rm_so;
            }
        } else {
            const char *loc = NULL;
            if (which <= 0) {
                loc = find_last_occurrence(content, anchor);
            } else {
                // find nth occurrence
                const char *p = content;
                int idx = 0;
                size_t nlen = strlen(anchor);
                while ((p = strstr(p, anchor)) != NULL) {
                    idx++;
                    if (idx == which) { loc = p; break; }
                    p += nlen;
                }
            }
            if (loc) {
                found = 1;
                anchor_len = strlen(anchor);
                insert_at = use_after ? (size_t)(loc - content) + anchor_len
                                      : (size_t)(loc - content);
            }
        }

        if (!found && !fallback_to_eof) {
            free(content);
            free(original_content);
            free(resolved_path);
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "Anchor not found in file");
            return error;
        }

        // Build new content: insert new_str at insert_at
        size_t new_len = strlen(new_str);
        char *buf = malloc(content_len + new_len + 1);
        if (!buf) {
            free(content);
            free(original_content);
            free(resolved_path);
            cJSON *error = cJSON_CreateObject();
            cJSON_AddStringToObject(error, "error", "Out of memory");
            return error;
        }
        memcpy(buf, content, insert_at);
        memcpy(buf + insert_at, new_str, new_len);
        memcpy(buf + insert_at + new_len, content + insert_at, content_len - insert_at + 1);
        new_content = buf;
        replace_count = 1;
    } else if (use_regex) {
        // Regex-based replacement with optional flags
        new_content = regex_replace_ex(content, old_str, new_str, replace_all, regex_flags, &replace_count, &error_msg);
    } else if (replace_all) {
        // Simple string multi-replace
        if (!old_str) {
            error_msg = strdup("replace_all requires 'old_string'");
            new_content = NULL;
        } else {
            new_content = str_replace_all(content, old_str, new_str, &replace_count);
        }
    } else {
        // Simple string single replace (original behavior)
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
    }

    if (!new_content) {
        free(content);
        free(original_content);
        free(resolved_path);
        cJSON *error = cJSON_CreateObject();
        if (error_msg) {
            cJSON_AddStringToObject(error, "error", error_msg);
            free(error_msg);
        } else if (replace_count == 0) {
            cJSON_AddStringToObject(error, "error",
                (insert_mode ? "Anchor not found in file" : (use_regex ? "Pattern not found in file" : "String not found in file")));
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

static cJSON* tool_glob(cJSON *params, ConversationState *state) {
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

// Helper function to check if a command exists
static int command_exists(const char *cmd) {
    char test_cmd[256];
    snprintf(test_cmd, sizeof(test_cmd), "command -v %s >/dev/null 2>&1", cmd);
    return system(test_cmd) == 0;
}

static cJSON* tool_grep(cJSON *params, ConversationState *state) {
    const cJSON *pattern_json = cJSON_GetObjectItem(params, "pattern");
    const cJSON *path_json = cJSON_GetObjectItem(params, "path");

    if (!pattern_json || !cJSON_IsString(pattern_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing 'pattern' parameter");
        return error;
    }

    const char *pattern = pattern_json->valuestring;
    const char *path = path_json && cJSON_IsString(path_json) ?
                       path_json->valuestring : ".";

    // Get max results from environment or use default
    int max_results = 100;  // Default limit
    const char *max_env = getenv("KLAWED_GREP_MAX_RESULTS");
    if (max_env) {
        int max_val = atoi(max_env);
        if (max_val > 0) {
            max_results = max_val;
        }
    }

    cJSON *result = cJSON_CreateObject();
    cJSON *matches = cJSON_CreateArray();
    int match_count = 0;
    int truncated = 0;

    // Determine which grep tool to use (prefer rg > ag > grep)
    const char *grep_tool = "grep";
    const char *exclusions = "";

    if (command_exists("rg")) {
        grep_tool = "rg";
        // rg uses -g '!pattern' for exclusions
        exclusions =
            "-g '!.git' "
            "-g '!.svn' "
            "-g '!.hg' "
            "-g '!node_modules' "
            "-g '!bower_components' "
            "-g '!vendor' "
            "-g '!build' "
            "-g '!dist' "
            "-g '!target' "
            "-g '!.cache' "
            "-g '!.venv' "
            "-g '!venv' "
            "-g '!__pycache__' "
            "-g '!*.min.js' "
            "-g '!*.min.css' "
            "-g '!*.pyc' "
            "-g '!*.o' "
            "-g '!*.a' "
            "-g '!*.so' "
            "-g '!*.dylib' "
            "-g '!*.exe' "
            "-g '!*.dll' "
            "-g '!*.class' "
            "-g '!*.jar' "
            "-g '!*.war' "
            "-g '!*.zip' "
            "-g '!*.tar' "
            "-g '!*.gz' "
            "-g '!*.log' "
            "-g '!.DS_Store' ";
    } else if (command_exists("ag")) {
        grep_tool = "ag";
        // ag uses --ignore=pattern options
        exclusions =
            "--ignore=.git "
            "--ignore=.svn "
            "--ignore=.hg "
            "--ignore=node_modules "
            "--ignore=bower_components "
            "--ignore=vendor "
            "--ignore=build "
            "--ignore=dist "
            "--ignore=target "
            "--ignore=.cache "
            "--ignore=.venv "
            "--ignore=venv "
            "--ignore=__pycache__ "
            "--ignore='*.min.js' "
            "--ignore='*.min.css' "
            "--ignore='*.pyc' "
            "--ignore='*.o' "
            "--ignore='*.a' "
            "--ignore='*.so' "
            "--ignore='*.dylib' "
            "--ignore='*.exe' "
            "--ignore='*.dll' "
            "--ignore='*.class' "
            "--ignore='*.jar' "
            "--ignore='*.war' "
            "--ignore='*.zip' "
            "--ignore='*.tar' "
            "--ignore='*.gz' "
            "--ignore='*.log' "
            "--ignore=.DS_Store ";
    } else {
        // Standard grep exclusions
        exclusions =
            "--exclude-dir=.git "
            "--exclude-dir=.svn "
            "--exclude-dir=.hg "
            "--exclude-dir=node_modules "
            "--exclude-dir=bower_components "
            "--exclude-dir=vendor "
            "--exclude-dir=build "
            "--exclude-dir=dist "
            "--exclude-dir=target "
            "--exclude-dir=.cache "
            "--exclude-dir=.venv "
            "--exclude-dir=venv "
            "--exclude-dir=__pycache__ "
            "--exclude='*.min.js' "
            "--exclude='*.min.css' "
            "--exclude='*.pyc' "
            "--exclude='*.o' "
            "--exclude='*.a' "
            "--exclude='*.so' "
            "--exclude='*.dylib' "
            "--exclude='*.exe' "
            "--exclude='*.dll' "
            "--exclude='*.class' "
            "--exclude='*.jar' "
            "--exclude='*.war' "
            "--exclude='*.zip' "
            "--exclude='*.tar' "
            "--exclude='*.gz' "
            "--exclude='*.log' "
            "--exclude='.DS_Store' ";
    }

    // Search in main working directory
    char command[BUFFER_SIZE * 2];
    if (strcmp(grep_tool, "rg") == 0) {
        // rg: recursive by default, shows line numbers by default when output is to terminal
        // but we need -n for consistency since we're piping
        snprintf(command, sizeof(command),
                 "cd %s && rg -n %s '%s' %s 2>/dev/null || true",
                 state->working_dir, exclusions, pattern, path);
    } else if (strcmp(grep_tool, "ag") == 0) {
        // ag: recursive by default, shows line numbers with -n
        snprintf(command, sizeof(command),
                 "cd %s && ag -n %s '%s' %s 2>/dev/null || true",
                 state->working_dir, exclusions, pattern, path);
    } else {
        // Standard grep
        snprintf(command, sizeof(command),
                 "cd %s && grep -r -n %s '%s' %s 2>/dev/null || true",
                 state->working_dir, exclusions, pattern, path);
    }

    FILE *pipe = popen(command, "r");
    if (!pipe) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to execute grep");
        return error;
    }

    char buffer[BUFFER_SIZE];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        // CRITICAL: Add cancellation point for long grep operations
        pthread_testcancel();

        if (match_count >= max_results) {
            truncated = 1;
            break;
        }
        buffer[strcspn(buffer, "\n")] = 0;  // Remove newline
        cJSON_AddItemToArray(matches, cJSON_CreateString(buffer));
        match_count++;
    }
    pclose(pipe);

    // Search in additional working directories (if not already truncated)
    for (int dir_idx = 0; dir_idx < state->additional_dirs_count && !truncated; dir_idx++) {
        if (strcmp(grep_tool, "rg") == 0) {
            snprintf(command, sizeof(command),
                     "cd %s && rg -n %s '%s' %s 2>/dev/null || true",
                     state->additional_dirs[dir_idx], exclusions, pattern, path);
        } else if (strcmp(grep_tool, "ag") == 0) {
            snprintf(command, sizeof(command),
                     "cd %s && ag -n %s '%s' %s 2>/dev/null || true",
                     state->additional_dirs[dir_idx], exclusions, pattern, path);
        } else {
            snprintf(command, sizeof(command),
                     "cd %s && grep -r -n %s '%s' %s 2>/dev/null || true",
                     state->additional_dirs[dir_idx], exclusions, pattern, path);
        }

        pipe = popen(command, "r");
        if (!pipe) continue;  // Skip this directory on error

        while (fgets(buffer, sizeof(buffer), pipe)) {
            // CRITICAL: Add cancellation point for long grep operations
            pthread_testcancel();

            if (match_count >= max_results) {
                truncated = 1;
                break;
            }
            buffer[strcspn(buffer, "\n")] = 0;  // Remove newline
            cJSON_AddItemToArray(matches, cJSON_CreateString(buffer));
            match_count++;
        }
        pclose(pipe);
    }

    cJSON_AddItemToObject(result, "matches", matches);

    // Add metadata about the search
    if (truncated) {
        char warning[256];
        snprintf(warning, sizeof(warning),
                "Results truncated at %d matches. Use KLAWED_GREP_MAX_RESULTS to adjust limit, or refine your search pattern.",
                max_results);
        cJSON_AddStringToObject(result, "warning", warning);
    }

    cJSON_AddNumberToObject(result, "match_count", match_count);

    return result;
}

STATIC cJSON* tool_todo_write(cJSON *params, ConversationState *state) {
    const cJSON *todos_json = cJSON_GetObjectItem(params, "todos");

    if (!todos_json || !cJSON_IsArray(todos_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing or invalid 'todos' parameter (must be array)");
        return error;
    }

    // Ensure todo_list is initialized
    if (!state->todo_list) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Todo list not initialized");
        return error;
    }

    // Clear existing todos
    todo_clear(state->todo_list);

    // Parse and add each todo
    int added = 0;
    int total = cJSON_GetArraySize(todos_json);

    for (int i = 0; i < total; i++) {
        cJSON *todo_item = cJSON_GetArrayItem(todos_json, i);
        if (!cJSON_IsObject(todo_item)) continue;

        const cJSON *content_json = cJSON_GetObjectItem(todo_item, "content");
        const cJSON *active_form_json = cJSON_GetObjectItem(todo_item, "activeForm");
        const cJSON *status_json = cJSON_GetObjectItem(todo_item, "status");

        if (!content_json || !cJSON_IsString(content_json) ||
            !active_form_json || !cJSON_IsString(active_form_json) ||
            !status_json || !cJSON_IsString(status_json)) {
            continue;  // Skip invalid todo items
        }

        const char *content = content_json->valuestring;
        const char *active_form = active_form_json->valuestring;
        const char *status_str = status_json->valuestring;

        // Parse status string to TodoStatus enum
        TodoStatus status;
        if (strcmp(status_str, "completed") == 0) {
            status = TODO_COMPLETED;
        } else if (strcmp(status_str, "in_progress") == 0) {
            status = TODO_IN_PROGRESS;
        } else if (strcmp(status_str, "pending") == 0) {
            status = TODO_PENDING;
        } else {
            continue;  // Invalid status, skip this item
        }

        // Add the todo item
        if (todo_add(state->todo_list, content, active_form, status) == 0) {
            added++;
        }
    }

    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "success");
    cJSON_AddNumberToObject(result, "added", added);
    cJSON_AddNumberToObject(result, "total", total);

    if (state->todo_list && state->todo_list->count > 0) {
        char *rendered = todo_render_to_string(state->todo_list);
        if (rendered) {
            cJSON_AddStringToObject(result, "rendered", rendered);
            free(rendered);
        }
    }

    return result;
}


// ============================================================================
// Sleep Tool Implementation
// ============================================================================

/**
 * tool_sleep - pauses execution for specified duration
 * params: { "duration": integer (seconds) }
 */
STATIC cJSON* tool_sleep(cJSON *params, ConversationState *state) {
    (void)state;
    cJSON *duration_json = cJSON_GetObjectItem(params, "duration");
    if (!duration_json || !cJSON_IsNumber(duration_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing or invalid 'duration' parameter (must be number of seconds)");
        return error;
    }
    int duration = duration_json->valueint;
    if (duration < 0) duration = 0;
    struct timespec req = { .tv_sec = duration, .tv_nsec = 0 };
    // Sleep for the duration (seconds)
    nanosleep(&req, NULL);
    // Return success result
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "success");
    cJSON_AddNumberToObject(result, "duration", duration);
    return result;
}

#ifndef TEST_BUILD
// MCP ListMcpResources tool handler
static cJSON* tool_list_mcp_resources(cJSON *params, ConversationState *state) {
    LOG_DEBUG("tool_list_mcp_resources: Starting resource listing");

    if (!state || !state->mcp_config) {
        LOG_ERROR("tool_list_mcp_resources: MCP not configured");
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "MCP not configured");
        return error;
    }

    // Extract optional server parameter
    const char *server_name = NULL;
    cJSON *server_json = cJSON_GetObjectItem(params, "server");
    if (server_json && cJSON_IsString(server_json)) {
        server_name = server_json->valuestring;
        LOG_DEBUG("tool_list_mcp_resources: Filtering by server '%s'", server_name);
    } else {
        LOG_DEBUG("tool_list_mcp_resources: No server filter specified, listing all servers");
    }

    // Call mcp_list_resources
    LOG_DEBUG("tool_list_mcp_resources: Calling mcp_list_resources");
    MCPResourceList *resource_list = mcp_list_resources(state->mcp_config, server_name);
    if (!resource_list) {
        LOG_ERROR("tool_list_mcp_resources: Failed to list resources");
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to list resources");
        return error;
    }

    // Build result JSON
    cJSON *result = cJSON_CreateObject();

    if (resource_list->is_error) {
        LOG_ERROR("tool_list_mcp_resources: Resource listing error: %s",
                 resource_list->error_message ? resource_list->error_message : "Unknown error");
        cJSON_AddStringToObject(result, "error",
            resource_list->error_message ? resource_list->error_message : "Unknown error");
        mcp_free_resource_list(resource_list);
        return result;
    }

    LOG_DEBUG("tool_list_mcp_resources: Found %d resources", resource_list->count);

    // Create resources array
    cJSON *resources = cJSON_CreateArray();
    for (int i = 0; i < resource_list->count; i++) {
        MCPResource *res = resource_list->resources[i];
        if (!res) continue;

        LOG_DEBUG("tool_list_mcp_resources: Resource %d: server='%s', uri='%s', name='%s'",
                 i, res->server ? res->server : "null", res->uri ? res->uri : "null",
                 res->name ? res->name : "null");

        cJSON *res_obj = cJSON_CreateObject();
        if (res->server) cJSON_AddStringToObject(res_obj, "server", res->server);
        if (res->uri) cJSON_AddStringToObject(res_obj, "uri", res->uri);
        if (res->name) cJSON_AddStringToObject(res_obj, "name", res->name);
        if (res->description) cJSON_AddStringToObject(res_obj, "description", res->description);
        if (res->mime_type) cJSON_AddStringToObject(res_obj, "mimeType", res->mime_type);

        cJSON_AddItemToArray(resources, res_obj);
    }

    cJSON_AddItemToObject(result, "resources", resources);
    cJSON_AddNumberToObject(result, "count", resource_list->count);

    mcp_free_resource_list(resource_list);
    LOG_DEBUG("tool_list_mcp_resources: Completed successfully");
    return result;
}

// MCP ReadMcpResource tool handler
static cJSON* tool_read_mcp_resource(cJSON *params, ConversationState *state) {
    LOG_DEBUG("tool_read_mcp_resource: Starting resource reading");

    if (!state || !state->mcp_config) {
        LOG_ERROR("tool_read_mcp_resource: MCP not configured");
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "MCP not configured");
        return error;
    }

    // Extract required parameters
    cJSON *server_json = cJSON_GetObjectItem(params, "server");
    cJSON *uri_json = cJSON_GetObjectItem(params, "uri");

    if (!server_json || !cJSON_IsString(server_json)) {
        LOG_ERROR("tool_read_mcp_resource: Missing or invalid 'server' parameter");
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing or invalid 'server' parameter");
        return error;
    }

    if (!uri_json || !cJSON_IsString(uri_json)) {
        LOG_ERROR("tool_read_mcp_resource: Missing or invalid 'uri' parameter");
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing or invalid 'uri' parameter");
        return error;
    }

    const char *server_name = server_json->valuestring;
    const char *uri = uri_json->valuestring;

    LOG_DEBUG("tool_read_mcp_resource: Reading resource from server '%s', uri='%s'", server_name, uri);

    // Call mcp_read_resource
    LOG_DEBUG("tool_read_mcp_resource: Calling mcp_read_resource");
    MCPResourceContent *content = mcp_read_resource(state->mcp_config, server_name, uri);
    if (!content) {
        LOG_ERROR("tool_read_mcp_resource: Failed to read resource");
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to read resource");
        return error;
    }

    // Build result JSON
    cJSON *result = cJSON_CreateObject();

    if (content->is_error) {
        LOG_ERROR("tool_read_mcp_resource: Resource reading error: %s",
                 content->error_message ? content->error_message : "Unknown error");
        cJSON_AddStringToObject(result, "error",
            content->error_message ? content->error_message : "Unknown error");
        mcp_free_resource_content(content);
        return result;
    }

    LOG_DEBUG("tool_read_mcp_resource: Resource read successfully, uri='%s', mime_type='%s', text_length=%zu",
             content->uri ? content->uri : "null",
             content->mime_type ? content->mime_type : "null",
             content->text ? strlen(content->text) : 0);

    if (content->uri) cJSON_AddStringToObject(result, "uri", content->uri);
    if (content->mime_type) cJSON_AddStringToObject(result, "mimeType", content->mime_type);
    if (content->text) cJSON_AddStringToObject(result, "text", content->text);

    // Note: Binary blob not yet supported

    mcp_free_resource_content(content);
    LOG_DEBUG("tool_read_mcp_resource: Completed successfully");
    return result;
}

// MCP CallMcpTool tool handler
static cJSON* tool_call_mcp_tool(cJSON *params, ConversationState *state) {
    LOG_DEBUG("tool_call_mcp_tool: Starting MCP tool call");

    if (!state || !state->mcp_config) {
        LOG_ERROR("tool_call_mcp_tool: MCP not configured");
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "MCP not configured");
        return error;
    }

    // Extract required parameters
    cJSON *server_json = cJSON_GetObjectItem(params, "server");
    cJSON *tool_json = cJSON_GetObjectItem(params, "tool");
    cJSON *args_json = cJSON_GetObjectItem(params, "arguments");

    if (!server_json || !cJSON_IsString(server_json)) {
        LOG_ERROR("tool_call_mcp_tool: Missing or invalid 'server' parameter");
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing or invalid 'server' parameter");
        return error;
    }

    if (!tool_json || !cJSON_IsString(tool_json)) {
        LOG_ERROR("tool_call_mcp_tool: Missing or invalid 'tool' parameter");
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Missing or invalid 'tool' parameter");
        return error;
    }

    const char *server_name = server_json->valuestring;
    const char *tool_name = tool_json->valuestring;

    LOG_DEBUG("tool_call_mcp_tool: Looking for server '%s' to call tool '%s'", server_name, tool_name);

    // Find server by name
    MCPServer *target = NULL;
    for (int i = 0; i < state->mcp_config->server_count; i++) {
        MCPServer *srv = state->mcp_config->servers[i];
        if (srv && srv->name && strcmp(srv->name, server_name) == 0) {
            target = srv;
            LOG_DEBUG("tool_call_mcp_tool: Found server '%s' at index %d", server_name, i);
            break;
        }
    }

    if (!target) {
        LOG_ERROR("tool_call_mcp_tool: MCP server '%s' not found", server_name);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "MCP server not found");
        return error;
    }

    if (!target->connected) {
        LOG_ERROR("tool_call_mcp_tool: MCP server '%s' not connected", server_name);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "MCP server not connected");
        return error;
    }

    // Ensure args is an object or null
    cJSON *args_object = NULL;
    if (args_json && cJSON_IsObject(args_json)) {
        args_object = args_json;
        char *args_str = cJSON_PrintUnformatted(args_json);
        LOG_DEBUG("tool_call_mcp_tool: Calling tool '%s' on server '%s' with args: %s",
                 tool_name, server_name, args_str ? args_str : "null");
        if (args_str) free(args_str);
    } else {
        LOG_DEBUG("tool_call_mcp_tool: Calling tool '%s' on server '%s' with no arguments",
                 tool_name, server_name);
    }

    LOG_DEBUG("tool_call_mcp_tool: Calling mcp_call_tool");
    MCPToolResult *call_result = mcp_call_tool(target, tool_name, args_object);
    cJSON *result = cJSON_CreateObject();
    if (!call_result) {
        LOG_ERROR("tool_call_mcp_tool: MCP tool call failed for tool '%s' on server '%s' (memory allocation error)",
                 tool_name, server_name);
        cJSON_AddStringToObject(result, "error", "MCP tool call failed: memory allocation error");
        return result;
    }

    if (call_result->is_error) {
        LOG_ERROR("tool_call_mcp_tool: MCP tool returned error: %s",
                 call_result->result ? call_result->result : "MCP tool error");
        // Include the detailed error message for the user
        cJSON_AddStringToObject(result, "error", call_result->result ? call_result->result : "MCP tool error");
    } else {
        LOG_DEBUG("tool_call_mcp_tool: MCP tool call succeeded, result length: %zu, blob size: %zu, mime_type: %s",
                 call_result->result ? strlen(call_result->result) : 0,
                 call_result->blob_size,
                 call_result->mime_type ? call_result->mime_type : "none");

        // Handle different content types
        if (call_result->blob && call_result->blob_size > 0) {
            // Binary content (e.g., images) - auto-save to file
            const char *mime_type = call_result->mime_type ? call_result->mime_type : "application/octet-stream";

            // Generate appropriate filename based on tool and MIME type
            char filename[256];
            if (strncmp(tool_name, "screenshot", 10) == 0 ||
                strncmp(tool_name, "take_screenshot", 15) == 0) {
                generate_timestamped_filename(filename, sizeof(filename), "screenshot", mime_type);
            } else if (strncmp(mime_type, "image/", 6) == 0) {
                generate_timestamped_filename(filename, sizeof(filename), "image", mime_type);
            } else {
                generate_timestamped_filename(filename, sizeof(filename), "file", mime_type);
            }

            // Save binary data to file
            int save_result = save_binary_file(filename, call_result->blob, call_result->blob_size);

            if (save_result == 0) {
                // Success - encode base64 for image content (if it's an image)
                int is_image = (strncmp(mime_type, "image/", 6) == 0);

                if (is_image) {
                    // For images, encode to base64 and mark as image content
                    // This allows the TUI to display it properly like UploadImage
                    size_t encoded_size = 0;
                    char *encoded_data = base64_encode(call_result->blob, call_result->blob_size, &encoded_size);
                    if (encoded_data) {
                        cJSON_AddStringToObject(result, "content_type", "image");
                        cJSON_AddStringToObject(result, "file_path", filename);
                        cJSON_AddStringToObject(result, "mime_type", mime_type);
                        cJSON_AddStringToObject(result, "base64_data", encoded_data);
                        cJSON_AddNumberToObject(result, "file_size_bytes", (double)call_result->blob_size);
                        free(encoded_data);
                        LOG_INFO("tool_call_mcp_tool: Saved image to '%s' (%zu bytes)", filename, call_result->blob_size);
                    } else {
                        // Encoding failed, fall back to file info only
                        LOG_WARN("tool_call_mcp_tool: Failed to encode image to base64, returning file info only");
                        cJSON_AddStringToObject(result, "status", "success");
                        cJSON_AddStringToObject(result, "message", "Image saved to file");
                        cJSON_AddStringToObject(result, "file_path", filename);
                        cJSON_AddStringToObject(result, "file_type", mime_type);
                        cJSON_AddNumberToObject(result, "file_size_bytes", (double)call_result->blob_size);
                        cJSON_AddStringToObject(result, "file_size_human", format_file_size(call_result->blob_size));
                    }
                } else {
                    // For non-image binary content, return file info only
                    cJSON_AddStringToObject(result, "status", "success");
                    cJSON_AddStringToObject(result, "message", "Binary content saved to file");
                    cJSON_AddStringToObject(result, "file_path", filename);
                    cJSON_AddStringToObject(result, "file_type", mime_type);
                    cJSON_AddNumberToObject(result, "file_size_bytes", (double)call_result->blob_size);
                    cJSON_AddStringToObject(result, "file_size_human", format_file_size(call_result->blob_size));
                    LOG_INFO("tool_call_mcp_tool: Saved binary content to '%s' (%zu bytes)", filename, call_result->blob_size);
                }
            } else {
                // Failed to save - fall back to base64 (but this shouldn't happen)
                LOG_WARN("tool_call_mcp_tool: Failed to save binary content to file, falling back to base64");
                cJSON_AddStringToObject(result, "content_type", "binary");
                cJSON_AddStringToObject(result, "mime_type", mime_type);

                size_t encoded_size = 0;
                char *encoded_data = base64_encode(call_result->blob, call_result->blob_size, &encoded_size);
                if (encoded_data) {
                    cJSON_AddStringToObject(result, "content", encoded_data);
                    free(encoded_data);
                } else {
                    cJSON_AddStringToObject(result, "content", "[binary data received - saving and encoding failed]");
                }
            }
        } else {
            // Text content
            cJSON_AddStringToObject(result, "content_type", "text");
            if (call_result->mime_type) {
                cJSON_AddStringToObject(result, "mime_type", call_result->mime_type);
            }
            cJSON_AddStringToObject(result, "content", call_result->result ? call_result->result : "");
        }
    }

    mcp_free_tool_result(call_result);
    LOG_DEBUG("tool_call_mcp_tool: Completed successfully");
    return result;
}
#endif

// ============================================================================
// Tool Registry
// ============================================================================

typedef struct {
    const char *name;
    cJSON* (*handler)(cJSON *params, ConversationState *state);
} Tool;

static Tool tools[] = {
    {"Sleep", tool_sleep},
    {"Bash", tool_bash},
    {"Subagent", tool_subagent},
    {"CheckSubagentProgress", tool_check_subagent_progress},
    {"InterruptSubagent", tool_interrupt_subagent},
    {"Read", tool_read},
    {"Write", tool_write},
    {"Edit", tool_edit},
    {"Glob", tool_glob},
    {"Grep", tool_grep},
    {"TodoWrite", tool_todo_write},
    {"UploadImage", tool_upload_image},
#ifndef TEST_BUILD
    {"ListMcpResources", tool_list_mcp_resources},
    {"ReadMcpResource", tool_read_mcp_resource},
    {"CallMcpTool", tool_call_mcp_tool},
#endif
};

static const int num_tools = sizeof(tools) / sizeof(Tool);

// Validate that a tool name is in the provided tools list
// Returns 1 if valid, 0 if invalid (hallucinated)
static int is_tool_allowed(const char *tool_name, ConversationState *state) {
    if (!tool_name || !state) {
        return 0;
    }

    // Get the list of tools that were sent to the API
    cJSON *tool_defs = get_tool_definitions(state, 0);  // Don't need caching for validation
    if (!tool_defs) {
        LOG_ERROR("Failed to get tool definitions for validation");
        return 0;  // Fail closed - reject if we can't verify
    }

    int found = 0;
    cJSON *tool = NULL;
    cJSON_ArrayForEach(tool, tool_defs) {
        // Tools are in format: { "type": "function", "function": { "name": "ToolName", ... } }
        cJSON *func = cJSON_GetObjectItem(tool, "function");
        if (func) {
            cJSON *name = cJSON_GetObjectItem(func, "name");
            if (name && cJSON_IsString(name) && strcmp(name->valuestring, tool_name) == 0) {
                found = 1;
                break;
            }
        }
    }

    cJSON_Delete(tool_defs);

    if (!found) {
        LOG_WARN("Tool validation failed: '%s' was not in the provided tools list (possible model hallucination)", tool_name);
    }

    return found;
}

static cJSON* execute_tool(const char *tool_name, cJSON *input, ConversationState *state) {
    // Time the tool execution
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    cJSON *result = NULL;

    // Check for verbose tool logging
    int tool_verbose = 0;
    const char *tool_verbose_env = getenv("KLAWED_TOOL_VERBOSE");
    if (tool_verbose_env) {
        tool_verbose = atoi(tool_verbose_env);
        if (tool_verbose < 0) tool_verbose = 0;
        if (tool_verbose > 2) tool_verbose = 2;  // 0=off, 1=basic, 2=detailed
    }

    // Log tool execution attempt
    char *input_str = cJSON_PrintUnformatted(input);
    LOG_DEBUG("execute_tool: Attempting to execute tool '%s' with input: %s",
              tool_name, input_str ? input_str : "null");
    if (input_str) free(input_str);

    // Verbose logging: print to stdout if enabled
    if (tool_verbose >= 1) {
        printf("\n[TOOL VERBOSE] Executing tool: %s\n", tool_name);
        if (tool_verbose >= 2) {
            char *formatted_input = cJSON_Print(input);
            if (formatted_input) {
                printf("[TOOL VERBOSE] Input parameters:\n%s\n", formatted_input);
                free(formatted_input);
            }
        }
    }

    // Try built-in tools first
    for (int i = 0; i < num_tools; i++) {
        if (strcmp(tools[i].name, tool_name) == 0) {
            // Check if we're running as a subagent and exclude subagent-related tools to prevent recursion
            const char *is_subagent_env = getenv("KLAWED_IS_SUBAGENT");
            int is_subagent = is_subagent_env && (strcmp(is_subagent_env, "1") == 0 || 
                                                 strcasecmp(is_subagent_env, "true") == 0 ||
                                                 strcasecmp(is_subagent_env, "yes") == 0);
            
            if (is_subagent && (strcmp(tool_name, "Subagent") == 0 || 
                               strcmp(tool_name, "CheckSubagentProgress") == 0 || 
                               strcmp(tool_name, "InterruptSubagent") == 0)) {
                cJSON *error = cJSON_CreateObject();
                cJSON_AddStringToObject(error, "error", "Subagent-related tools are disabled when running as a subagent to prevent recursion");
                result = error;
                break;
            }
            
            LOG_DEBUG("execute_tool: Found built-in tool '%s' at index %d", tool_name, i);
            result = tools[i].handler(input, state);
            break;
        }
    }

#ifndef TEST_BUILD
    // If not found in built-in tools, try MCP tools
    if (!result && state && state->mcp_config && strncmp(tool_name, "mcp_", 4) == 0) {
        LOG_DEBUG("execute_tool: Tool '%s' matches MCP pattern, attempting MCP lookup", tool_name);
        MCPServer *server = mcp_find_tool_server(state->mcp_config, tool_name);
        if (server) {
            LOG_DEBUG("execute_tool: Found MCP server '%s' for tool '%s'", server->name, tool_name);
            // Extract the actual tool name (remove mcp_<server>_ prefix)
            const char *actual_tool_name = strchr(tool_name + 4, '_');
            if (actual_tool_name) {
                actual_tool_name++;  // Skip the underscore

                LOG_INFO("Calling MCP tool '%s' on server '%s' (original tool name: '%s')",
                         actual_tool_name, server->name, tool_name);

                MCPToolResult *mcp_result = mcp_call_tool(server, actual_tool_name, input);
                if (mcp_result) {
                    LOG_DEBUG("execute_tool: MCP tool call succeeded, is_error=%d", mcp_result->is_error);
                    result = cJSON_CreateObject();

                    if (mcp_result->is_error) {
                        LOG_WARN("execute_tool: MCP tool returned error: %s",
                                mcp_result->result ? mcp_result->result : "MCP tool error");
                        cJSON_AddStringToObject(result, "error", mcp_result->result ? mcp_result->result : "MCP tool error");
                    } else {
                        LOG_DEBUG("execute_tool: MCP tool returned success, result length: %zu, blob size: %zu, mime_type: %s",
                                 mcp_result->result ? strlen(mcp_result->result) : 0,
                                 mcp_result->blob_size,
                                 mcp_result->mime_type ? mcp_result->mime_type : "none");

                        // Handle different content types
                        if (mcp_result->blob && mcp_result->blob_size > 0) {
                            // Binary content (e.g., images) - auto-save to file
                            const char *mime_type = mcp_result->mime_type ? mcp_result->mime_type : "application/octet-stream";

                            // Generate appropriate filename based on tool and MIME type
                            char filename[256];
                            if (strncmp(actual_tool_name, "screenshot", 10) == 0 ||
                                strncmp(actual_tool_name, "take_screenshot", 15) == 0) {
                                generate_timestamped_filename(filename, sizeof(filename), "screenshot", mime_type);
                            } else if (strncmp(mime_type, "image/", 6) == 0) {
                                generate_timestamped_filename(filename, sizeof(filename), "image", mime_type);
                            } else {
                                generate_timestamped_filename(filename, sizeof(filename), "file", mime_type);
                            }

                            // Save binary data to file
                            int save_result = save_binary_file(filename, mcp_result->blob, mcp_result->blob_size);

                            if (save_result == 0) {
                                // Success - encode base64 for image content (if it's an image)
                                int is_image = (strncmp(mime_type, "image/", 6) == 0);

                                if (is_image) {
                                    // For images, encode to base64 and mark as image content
                                    // This allows the TUI to display it properly like UploadImage
                                    size_t encoded_size = 0;
                                    char *encoded_data = base64_encode(mcp_result->blob, mcp_result->blob_size, &encoded_size);
                                    if (encoded_data) {
                                        cJSON_AddStringToObject(result, "content_type", "image");
                                        cJSON_AddStringToObject(result, "file_path", filename);
                                        cJSON_AddStringToObject(result, "mime_type", mime_type);
                                        cJSON_AddStringToObject(result, "base64_data", encoded_data);
                                        cJSON_AddNumberToObject(result, "file_size_bytes", (double)mcp_result->blob_size);
                                        free(encoded_data);
                                        LOG_INFO("execute_tool: Saved image to '%s' (%zu bytes)", filename, mcp_result->blob_size);
                                    } else {
                                        // Encoding failed, fall back to file info only
                                        LOG_WARN("execute_tool: Failed to encode image to base64, returning file info only");
                                        cJSON_AddStringToObject(result, "status", "success");
                                        cJSON_AddStringToObject(result, "message", "Image saved to file");
                                        cJSON_AddStringToObject(result, "file_path", filename);
                                        cJSON_AddStringToObject(result, "file_type", mime_type);
                                        cJSON_AddNumberToObject(result, "file_size_bytes", (double)mcp_result->blob_size);
                                        cJSON_AddStringToObject(result, "file_size_human", format_file_size(mcp_result->blob_size));
                                    }
                                } else {
                                    // For non-image binary content, return file info only
                                    cJSON_AddStringToObject(result, "status", "success");
                                    cJSON_AddStringToObject(result, "message", "Binary content saved to file");
                                    cJSON_AddStringToObject(result, "file_path", filename);
                                    cJSON_AddStringToObject(result, "file_type", mime_type);
                                    cJSON_AddNumberToObject(result, "file_size_bytes", (double)mcp_result->blob_size);
                                    cJSON_AddStringToObject(result, "file_size_human", format_file_size(mcp_result->blob_size));
                                    LOG_INFO("execute_tool: Saved binary content to '%s' (%zu bytes)", filename, mcp_result->blob_size);
                                }
                            } else {
                                // Failed to save - fall back to base64 (but this shouldn't happen)
                                LOG_WARN("execute_tool: Failed to save binary content to file, falling back to base64");
                                cJSON_AddStringToObject(result, "content_type", "binary");
                                cJSON_AddStringToObject(result, "mime_type", mime_type);

                                size_t encoded_size = 0;
                                char *encoded_data = base64_encode(mcp_result->blob, mcp_result->blob_size, &encoded_size);
                                if (encoded_data) {
                                    cJSON_AddStringToObject(result, "content", encoded_data);
                                    free(encoded_data);
                                } else {
                                    cJSON_AddStringToObject(result, "content", "[binary data received - saving and encoding failed]");
                                }
                            }
                        } else {
                            // Text content
                            cJSON_AddStringToObject(result, "content_type", "text");
                            if (mcp_result->mime_type) {
                                cJSON_AddStringToObject(result, "mime_type", mcp_result->mime_type);
                            }
                            cJSON_AddStringToObject(result, "content", mcp_result->result ? mcp_result->result : "");
                        }
                    }

                    mcp_free_tool_result(mcp_result);
                } else {
                    LOG_ERROR("execute_tool: MCP tool call failed for tool '%s' on server '%s'",
                              actual_tool_name, server->name);
                    result = cJSON_CreateObject();
                    cJSON_AddStringToObject(result, "error", "MCP tool call failed");
                }
            } else {
                LOG_ERROR("execute_tool: Failed to extract actual tool name from '%s'", tool_name);
            }
        } else {
            LOG_WARN("execute_tool: No MCP server found for tool '%s'", tool_name);
        }
    } else if (!result && state && state->mcp_config) {
        LOG_DEBUG("execute_tool: Tool '%s' not found in built-in tools and doesn't match MCP pattern", tool_name);
    }
#endif

    if (!result) {
        LOG_WARN("execute_tool: No result generated for tool '%s'", tool_name);
        result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "error", "Unknown tool");
    }

    // Log execution time and result
    clock_gettime(CLOCK_MONOTONIC, &end);
    long duration_ms = (end.tv_sec - start.tv_sec) * 1000 +
                       (end.tv_nsec - start.tv_nsec) / 1000000;

    char *result_str = cJSON_PrintUnformatted(result);
    LOG_DEBUG("execute_tool: Tool '%s' executed in %ld ms, result: %s",
              tool_name, duration_ms, result_str ? result_str : "null");
    if (result_str) free(result_str);

    LOG_INFO("Tool '%s' executed in %ld ms", tool_name, duration_ms);

    // Verbose logging: print result summary if enabled
    if (tool_verbose >= 1) {
        printf("[TOOL VERBOSE] Tool '%s' completed in %ld ms\n", tool_name, duration_ms);
        if (tool_verbose >= 2) {
            char *formatted_result = cJSON_Print(result);
            if (formatted_result) {
                printf("[TOOL VERBOSE] Result:\n%s\n", formatted_result);
                free(formatted_result);
            }
        }
    }

    return result;
}

// ============================================================================
// Tool Definitions for API
// ============================================================================

// Forward declaration for cache_control helper


cJSON* get_tool_definitions(ConversationState *state, int enable_caching) {
    cJSON *tool_array = cJSON_CreateArray();
    int plan_mode = state ? state->plan_mode : 0;
    
    // Check if we're running as a subagent - if so, exclude the Subagent tool to prevent recursion
    const char *is_subagent_env = getenv("KLAWED_IS_SUBAGENT");
    int is_subagent = is_subagent_env && (strcmp(is_subagent_env, "1") == 0 || 
                                         strcasecmp(is_subagent_env, "true") == 0 ||
                                         strcasecmp(is_subagent_env, "yes") == 0);
    
    LOG_DEBUG("[TOOLS] get_tool_definitions: plan_mode=%d, is_subagent=%d", plan_mode, is_subagent);
    // Sleep tool
    cJSON *sleep_tool = cJSON_CreateObject();
    cJSON_AddStringToObject(sleep_tool, "type", "function");
    cJSON *sleep_func = cJSON_CreateObject();
    cJSON_AddStringToObject(sleep_func, "name", "Sleep");
    cJSON_AddStringToObject(sleep_func, "description", "Pauses execution for specified duration (seconds)");
    cJSON *sleep_params = cJSON_CreateObject();
    cJSON_AddStringToObject(sleep_params, "type", "object");
    cJSON *sleep_props = cJSON_CreateObject();
    cJSON *duration_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(duration_prop, "type", "integer");
    cJSON_AddStringToObject(duration_prop, "description", "Duration to sleep in seconds");
    cJSON_AddItemToObject(sleep_props, "duration", duration_prop);
    cJSON_AddItemToObject(sleep_params, "properties", sleep_props);
    cJSON *sleep_req = cJSON_CreateArray();
    cJSON_AddItemToArray(sleep_req, cJSON_CreateString("duration"));
    cJSON_AddItemToObject(sleep_params, "required", sleep_req);
    cJSON_AddItemToObject(sleep_func, "parameters", sleep_params);
    cJSON_AddItemToObject(sleep_tool, "function", sleep_func);
    if (enable_caching) {
        add_cache_control(sleep_tool);
    }
    cJSON_AddItemToArray(tool_array, sleep_tool);

    // Read tool
    cJSON *read = cJSON_CreateObject();
    cJSON_AddStringToObject(read, "type", "function");
    cJSON *read_func = cJSON_CreateObject();
    cJSON_AddStringToObject(read_func, "name", "Read");
    cJSON_AddStringToObject(read_func, "description",
        "Reads a file from the filesystem with optional line range support");
    cJSON *read_params = cJSON_CreateObject();
    cJSON_AddStringToObject(read_params, "type", "object");
    cJSON *read_props = cJSON_CreateObject();
    cJSON *read_path = cJSON_CreateObject();
    cJSON_AddStringToObject(read_path, "type", "string");
    cJSON_AddStringToObject(read_path, "description", "The absolute path to the file");
    cJSON_AddItemToObject(read_props, "file_path", read_path);
    cJSON *read_start = cJSON_CreateObject();
    cJSON_AddStringToObject(read_start, "type", "integer");
    cJSON_AddStringToObject(read_start, "description",
        "Optional: Starting line number (1-indexed, inclusive)");
    cJSON_AddItemToObject(read_props, "start_line", read_start);
    cJSON *read_end = cJSON_CreateObject();
    cJSON_AddStringToObject(read_end, "type", "integer");
    cJSON_AddStringToObject(read_end, "description",
        "Optional: Ending line number (1-indexed, inclusive)");
    cJSON_AddItemToObject(read_props, "end_line", read_end);
    cJSON_AddItemToObject(read_params, "properties", read_props);
    cJSON *read_req = cJSON_CreateArray();
    cJSON_AddItemToArray(read_req, cJSON_CreateString("file_path"));
    cJSON_AddItemToObject(read_params, "required", read_req);
    cJSON_AddItemToObject(read_func, "parameters", read_params);
    cJSON_AddItemToObject(read, "function", read_func);
    cJSON_AddItemToArray(tool_array, read);

    // Bash, Subagent, Write, and Edit tools - excluded in plan mode
    if (!plan_mode) {
        // Bash tool
        cJSON *bash = cJSON_CreateObject();
        cJSON_AddStringToObject(bash, "type", "function");
        cJSON *bash_func = cJSON_CreateObject();
        cJSON_AddStringToObject(bash_func, "name", "Bash");
        cJSON_AddStringToObject(bash_func, "description",
            "Executes bash commands. Note: stderr is automatically redirected to stdout "
            "to prevent terminal corruption, so both stdout and stderr output will be "
            "captured in the 'output' field. Commands have a configurable timeout "
            "(default: 30 seconds) to prevent hanging. Use the 'timeout' parameter to "
            "override the default or set to 0 for no timeout. If the output exceeds "
            "12,228 bytes, it will be truncated and a 'truncation_warning' field "
            "will be added to the result.");
        cJSON *bash_params = cJSON_CreateObject();
        cJSON_AddStringToObject(bash_params, "type", "object");
        cJSON *bash_props = cJSON_CreateObject();
        cJSON *bash_cmd = cJSON_CreateObject();
        cJSON_AddStringToObject(bash_cmd, "type", "string");
        cJSON_AddStringToObject(bash_cmd, "description", "The command to execute");
        cJSON_AddItemToObject(bash_props, "command", bash_cmd);
        cJSON *bash_timeout = cJSON_CreateObject();
        cJSON_AddStringToObject(bash_timeout, "type", "integer");
        cJSON_AddStringToObject(bash_timeout, "description",
            "Optional: Timeout in seconds. Default: 30 (from KLAWED_BASH_TIMEOUT env var). "
            "Set to 0 for no timeout. Commands that timeout will return exit code -2.");
        cJSON_AddItemToObject(bash_props, "timeout", bash_timeout);
        cJSON_AddItemToObject(bash_params, "properties", bash_props);
        cJSON *bash_req = cJSON_CreateArray();
        cJSON_AddItemToArray(bash_req, cJSON_CreateString("command"));
        cJSON_AddItemToObject(bash_params, "required", bash_req);
        cJSON_AddItemToObject(bash_func, "parameters", bash_params);
        cJSON_AddItemToObject(bash, "function", bash_func);
        cJSON_AddItemToArray(tool_array, bash);

        // Subagent tool - exclude if running as subagent to prevent recursion
        if (!is_subagent) {
            cJSON *subagent = cJSON_CreateObject();
        cJSON_AddStringToObject(subagent, "type", "function");
        cJSON *subagent_func = cJSON_CreateObject();
        cJSON_AddStringToObject(subagent_func, "name", "Subagent");
        cJSON_AddStringToObject(subagent_func, "description",
            "Spawns a new instance of klawed with the same configuration to work on a "
            "delegated task in a fresh context. The subagent runs independently and writes "
            "all output (stdout and stderr) to a log file. Returns the tail of the log "
            "(last 100 lines by default) which typically contains the task summary. "
            "For large outputs, use Read tool to access the full log file, or Grep to "
            "search for specific content. Use this when: (1) you need a fresh context "
            "without conversation history, (2) delegating a complex independent task, "
            "(3) avoiding context limit issues. Note: The subagent has full tool access "
            "including Write, Edit, and Bash.");
        cJSON *subagent_params = cJSON_CreateObject();
        cJSON_AddStringToObject(subagent_params, "type", "object");
        cJSON *subagent_props = cJSON_CreateObject();
        cJSON *subagent_prompt = cJSON_CreateObject();
        cJSON_AddStringToObject(subagent_prompt, "type", "string");
        cJSON_AddStringToObject(subagent_prompt, "description",
            "The task prompt for the subagent. Be specific and include all necessary context.");
        cJSON_AddItemToObject(subagent_props, "prompt", subagent_prompt);
        cJSON *subagent_timeout = cJSON_CreateObject();
        cJSON_AddStringToObject(subagent_timeout, "type", "integer");
        cJSON_AddStringToObject(subagent_timeout, "description",
            "Optional: Timeout in seconds. Default: 300 (5 minutes). Set to 0 for no timeout.");
        cJSON_AddItemToObject(subagent_props, "timeout", subagent_timeout);
        cJSON *subagent_tail = cJSON_CreateObject();
        cJSON_AddStringToObject(subagent_tail, "type", "integer");
        cJSON_AddStringToObject(subagent_tail, "description",
            "Optional: Number of lines to return from end of log. Default: 100. "
            "The summary is usually at the end.");
        cJSON_AddItemToObject(subagent_props, "tail_lines", subagent_tail);
        cJSON_AddItemToObject(subagent_params, "properties", subagent_props);
        cJSON *subagent_req = cJSON_CreateArray();
        cJSON_AddItemToArray(subagent_req, cJSON_CreateString("prompt"));
        cJSON_AddItemToObject(subagent_params, "required", subagent_req);
        cJSON_AddItemToObject(subagent_func, "parameters", subagent_params);
            cJSON_AddItemToObject(subagent, "function", subagent_func);
            cJSON_AddItemToArray(tool_array, subagent);
        }

        // Write tool
        cJSON *write = cJSON_CreateObject();
        cJSON_AddStringToObject(write, "type", "function");
        cJSON *write_func = cJSON_CreateObject();
        cJSON_AddStringToObject(write_func, "name", "Write");
        cJSON_AddStringToObject(write_func, "description", "Writes content to a file");
        cJSON *write_params = cJSON_CreateObject();
        cJSON_AddStringToObject(write_params, "type", "object");
        cJSON *write_props = cJSON_CreateObject();
        cJSON *write_path = cJSON_CreateObject();
        cJSON_AddStringToObject(write_path, "type", "string");
        cJSON_AddStringToObject(write_path, "description", "Path to the file to write");
        cJSON_AddItemToObject(write_props, "file_path", write_path);
        cJSON *write_content = cJSON_CreateObject();
        cJSON_AddStringToObject(write_content, "type", "string");
        cJSON_AddStringToObject(write_content, "description", "Content to write to the file");
        cJSON_AddItemToObject(write_props, "content", write_content);
        cJSON_AddItemToObject(write_params, "properties", write_props);
        cJSON *write_req = cJSON_CreateArray();
        cJSON_AddItemToArray(write_req, cJSON_CreateString("file_path"));
        cJSON_AddItemToArray(write_req, cJSON_CreateString("content"));
        cJSON_AddItemToObject(write_params, "required", write_req);
        cJSON_AddItemToObject(write_func, "parameters", write_params);
        cJSON_AddItemToObject(write, "function", write_func);
        cJSON_AddItemToArray(tool_array, write);

        // Edit tool
        cJSON *edit = cJSON_CreateObject();
        cJSON_AddStringToObject(edit, "type", "function");
        cJSON *edit_func = cJSON_CreateObject();
        cJSON_AddStringToObject(edit_func, "name", "Edit");
        cJSON_AddStringToObject(edit_func, "description",
            "Performs string replacements in files with three operation modes: "
            "(1) Simple text replacement - literal string matching, "
            "(2) Regex replacement - POSIX extended regex with capture groups (\\\\0-\\\\9) "
            "and backreferences, supports flags 'i' (case-insensitive) and 'm' (multiline), "
            "(3) Patch format - unified diff format for context-aware edits with verification. "
            "Automatically detects mode: patch format if old_string starts with '---', "
            "regex if use_regex=true, otherwise simple text. Use simple mode for exact matches, "
            "regex for patterns (e.g., date reformatting with '([0-9]{2})/([0-9]{2})/([0-9]{4})' "
            "to '\\\\3-\\\\1-\\\\2'), and patch format for precise edits with context verification. "
            "See docs/edit-tool.md for comprehensive examples and migration guide.");
        cJSON *edit_params = cJSON_CreateObject();
        cJSON_AddStringToObject(edit_params, "type", "object");
        cJSON *edit_props = cJSON_CreateObject();
        cJSON *edit_path = cJSON_CreateObject();
        cJSON_AddStringToObject(edit_path, "type", "string");
        cJSON_AddStringToObject(edit_path, "description", "Path to the file to edit");
        cJSON_AddItemToObject(edit_props, "file_path", edit_path);
        cJSON *old_str = cJSON_CreateObject();
        cJSON_AddStringToObject(old_str, "type", "string");
        cJSON_AddStringToObject(old_str, "description",
            "String, regex pattern, or patch format to search for. "
            "For simple mode: exact text to find. "
            "For regex mode: POSIX extended regex pattern (use_regex must be true). "
            "Supports capture groups () and backreferences \\\\1-\\\\9 in new_string. "
            "For patch mode: unified diff format starting with '---' (new_string should be empty). "
            "Examples: 'TODO' (simple), '([0-9]+)-([0-9]+)' (regex), '--- file.txt\\n+++ file.txt\\n@@ -1,3 +1,3 @@...' (patch)");
        cJSON_AddItemToObject(edit_props, "old_string", old_str);
        cJSON *new_str = cJSON_CreateObject();
        cJSON_AddStringToObject(new_str, "type", "string");
        cJSON_AddStringToObject(new_str, "description",
            "Replacement string. For simple/regex modes: the text to replace with. "
            "For regex mode: supports backreferences \\\\0 (full match), \\\\1-\\\\9 (capture groups), "
            "and \\\\\\\\ (literal backslash). Example: '\\\\2-\\\\1' swaps two captured groups. "
            "For patch mode: leave empty (patch contains both old and new content).");
        cJSON_AddItemToObject(edit_props, "new_string", new_str);
        cJSON *replace_all = cJSON_CreateObject();
        cJSON_AddStringToObject(replace_all, "type", "boolean");
        cJSON_AddStringToObject(replace_all, "description",
            "If true, replace all occurrences; if false, replace only first occurrence. "
            "Default: false. Only applies to simple and regex modes. Ignored in patch mode.");
        cJSON_AddItemToObject(edit_props, "replace_all", replace_all);
        cJSON *use_regex = cJSON_CreateObject();
        cJSON_AddStringToObject(use_regex, "type", "boolean");
        cJSON_AddStringToObject(use_regex, "description",
            "If true, treat old_string as POSIX extended regex pattern with capture group support. "
            "Default: false. Enables backreferences in new_string. Use with regex_flags for "
            "case-insensitive or multiline matching. Automatically false for patch mode.");
        cJSON_AddItemToObject(edit_props, "use_regex", use_regex);
        cJSON *regex_flags = cJSON_CreateObject();
        cJSON_AddStringToObject(regex_flags, "type", "string");
        cJSON_AddStringToObject(regex_flags, "description",
            "Optional regex flags (only when use_regex=true). Supported flags: "
            "'i' or 'I' = case-insensitive matching (REG_ICASE), "
            "'m' or 'M' = multiline mode where ^ and $ match line boundaries (REG_NEWLINE). "
            "Combine flags as needed: 'im' for both. Default: empty (case-sensitive, single-line). "
            "Examples: 'i' to match 'TODO', 'todo', 'ToDo'; 'm' to match '^Line' at start of each line.");
        cJSON_AddItemToObject(edit_props, "regex_flags", regex_flags);
        cJSON_AddItemToObject(edit_params, "properties", edit_props);
        cJSON *edit_req = cJSON_CreateArray();
        cJSON_AddItemToArray(edit_req, cJSON_CreateString("file_path"));
        cJSON_AddItemToArray(edit_req, cJSON_CreateString("old_string"));
        cJSON_AddItemToArray(edit_req, cJSON_CreateString("new_string"));
        cJSON_AddItemToObject(edit_params, "required", edit_req);
        cJSON_AddItemToObject(edit_func, "parameters", edit_params);
        cJSON_AddItemToObject(edit, "function", edit_func);
        cJSON_AddItemToArray(tool_array, edit);
    }

    // Glob tool
    cJSON *glob_tool = cJSON_CreateObject();
    cJSON_AddStringToObject(glob_tool, "type", "function");
    cJSON *glob_func = cJSON_CreateObject();
    cJSON_AddStringToObject(glob_func, "name", "Glob");
    cJSON_AddStringToObject(glob_func, "description", "Finds files matching a pattern");
    cJSON *glob_params = cJSON_CreateObject();
    cJSON_AddStringToObject(glob_params, "type", "object");
    cJSON *glob_props = cJSON_CreateObject();
    cJSON *glob_pattern = cJSON_CreateObject();
    cJSON_AddStringToObject(glob_pattern, "type", "string");
    cJSON_AddStringToObject(glob_pattern, "description", "Glob pattern to match files against");
    cJSON_AddItemToObject(glob_props, "pattern", glob_pattern);
    cJSON_AddItemToObject(glob_params, "properties", glob_props);
    cJSON *glob_req = cJSON_CreateArray();
    cJSON_AddItemToArray(glob_req, cJSON_CreateString("pattern"));
    cJSON_AddItemToObject(glob_params, "required", glob_req);
    cJSON_AddItemToObject(glob_func, "parameters", glob_params);
    cJSON_AddItemToObject(glob_tool, "function", glob_func);
    cJSON_AddItemToArray(tool_array, glob_tool);

    // Grep tool
    cJSON *grep_tool = cJSON_CreateObject();
    cJSON_AddStringToObject(grep_tool, "type", "function");
    cJSON *grep_func = cJSON_CreateObject();
    cJSON_AddStringToObject(grep_func, "name", "Grep");
    cJSON_AddStringToObject(grep_func, "description",
        "Searches for patterns in files. Results limited to 100 matches by default "
        "(configurable via KLAWED_GREP_MAX_RESULTS). Automatically excludes common "
        "build directories, dependencies, and binary files (.git, node_modules, build/, "
        "*.min.js, etc). Returns 'match_count' and 'warning' if truncated.");
    cJSON *grep_params = cJSON_CreateObject();
    cJSON_AddStringToObject(grep_params, "type", "object");
    cJSON *grep_props = cJSON_CreateObject();
    cJSON *grep_pattern = cJSON_CreateObject();
    cJSON_AddStringToObject(grep_pattern, "type", "string");
    cJSON_AddStringToObject(grep_pattern, "description", "Pattern to search for");
    cJSON_AddItemToObject(grep_props, "pattern", grep_pattern);
    cJSON *grep_path = cJSON_CreateObject();
    cJSON_AddStringToObject(grep_path, "type", "string");
    cJSON_AddStringToObject(grep_path, "description", "Path to search in (default: .)");
    cJSON_AddItemToObject(grep_props, "path", grep_path);
    cJSON_AddItemToObject(grep_params, "properties", grep_props);
    cJSON *grep_req = cJSON_CreateArray();
    cJSON_AddItemToArray(grep_req, cJSON_CreateString("pattern"));
    cJSON_AddItemToObject(grep_params, "required", grep_req);
    cJSON_AddItemToObject(grep_func, "parameters", grep_params);
    cJSON_AddItemToObject(grep_tool, "function", grep_func);
    cJSON_AddItemToArray(tool_array, grep_tool);

    // UploadImage tool
    cJSON *upload_image_tool = cJSON_CreateObject();
    cJSON_AddStringToObject(upload_image_tool, "type", "function");
    cJSON *upload_image_func = cJSON_CreateObject();
    cJSON_AddStringToObject(upload_image_func, "name", "UploadImage");
    cJSON_AddStringToObject(upload_image_func, "description",
        "Uploads an image file to be included in the conversation context using OpenAI-compatible format. Supports common image formats (PNG, JPEG, GIF, WebP).");
    cJSON *upload_image_params = cJSON_CreateObject();
    cJSON_AddStringToObject(upload_image_params, "type", "object");
    cJSON *upload_image_props = cJSON_CreateObject();
    cJSON *image_path_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(image_path_prop, "type", "string");
    cJSON_AddStringToObject(image_path_prop, "description", "Path to the image file to upload");
    cJSON_AddItemToObject(upload_image_props, "file_path", image_path_prop);
    cJSON_AddItemToObject(upload_image_params, "properties", upload_image_props);
    cJSON *upload_image_req = cJSON_CreateArray();
    cJSON_AddItemToArray(upload_image_req, cJSON_CreateString("file_path"));
    cJSON_AddItemToObject(upload_image_params, "required", upload_image_req);
    cJSON_AddItemToObject(upload_image_func, "parameters", upload_image_params);
    cJSON_AddItemToObject(upload_image_tool, "function", upload_image_func);
    cJSON_AddItemToArray(tool_array, upload_image_tool);

    // TodoWrite tool
    cJSON *todo_tool = cJSON_CreateObject();
    cJSON_AddStringToObject(todo_tool, "type", "function");
    cJSON *todo_func = cJSON_CreateObject();
    cJSON_AddStringToObject(todo_func, "name", "TodoWrite");
    cJSON_AddStringToObject(todo_func, "description",
        "Creates and updates a task list to track progress on multi-step tasks");
    cJSON *todo_params = cJSON_CreateObject();
    cJSON_AddStringToObject(todo_params, "type", "object");
    cJSON *todo_props = cJSON_CreateObject();

    // Define the todos array parameter
    cJSON *todos_array = cJSON_CreateObject();
    cJSON_AddStringToObject(todos_array, "type", "array");
    cJSON_AddStringToObject(todos_array, "description",
        "Array of todo items to display. Replaces the entire todo list.");

    // Define the items schema for the array
    cJSON *todos_items = cJSON_CreateObject();
    cJSON_AddStringToObject(todos_items, "type", "object");
    cJSON *item_props = cJSON_CreateObject();

    cJSON *content_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(content_prop, "type", "string");
    cJSON_AddStringToObject(content_prop, "description",
        "Task description in imperative form (e.g., 'Run tests')");
    cJSON_AddItemToObject(item_props, "content", content_prop);

    cJSON *active_form_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(active_form_prop, "type", "string");
    cJSON_AddStringToObject(active_form_prop, "description",
        "Task description in present continuous form (e.g., 'Running tests')");
    cJSON_AddItemToObject(item_props, "activeForm", active_form_prop);

    cJSON *status_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(status_prop, "type", "string");
    cJSON *status_enum = cJSON_CreateArray();
    cJSON_AddItemToArray(status_enum, cJSON_CreateString("pending"));
    cJSON_AddItemToArray(status_enum, cJSON_CreateString("in_progress"));
    cJSON_AddItemToArray(status_enum, cJSON_CreateString("completed"));
    cJSON_AddItemToObject(status_prop, "enum", status_enum);
    cJSON_AddStringToObject(status_prop, "description",
        "Current status of the task");
    cJSON_AddItemToObject(item_props, "status", status_prop);

    cJSON_AddItemToObject(todos_items, "properties", item_props);
    cJSON *item_required = cJSON_CreateArray();
    cJSON_AddItemToArray(item_required, cJSON_CreateString("content"));
    cJSON_AddItemToArray(item_required, cJSON_CreateString("activeForm"));
    cJSON_AddItemToArray(item_required, cJSON_CreateString("status"));
    cJSON_AddItemToObject(todos_items, "required", item_required);

    cJSON_AddItemToObject(todos_array, "items", todos_items);
    cJSON_AddItemToObject(todo_props, "todos", todos_array);

    cJSON_AddItemToObject(todo_params, "properties", todo_props);
    cJSON *todo_req = cJSON_CreateArray();
    cJSON_AddItemToArray(todo_req, cJSON_CreateString("todos"));
    cJSON_AddItemToObject(todo_params, "required", todo_req);
    cJSON_AddItemToObject(todo_func, "parameters", todo_params);
    cJSON_AddItemToObject(todo_tool, "function", todo_func);

    // Add cache_control to the last tool (TodoWrite) if caching is enabled
    // This is the second cache breakpoint (tool definitions)
    if (enable_caching) {
        add_cache_control(todo_tool);
    }

    cJSON_AddItemToArray(tool_array, todo_tool);

#ifndef TEST_BUILD
    // Add MCP tools if MCP is enabled and configured
    if (state && state->mcp_config && mcp_is_enabled()) {
        LOG_DEBUG("get_tool_definitions: Adding MCP tools to tool definitions");

        // 1) Dynamic MCP tools discovered from servers
        cJSON *mcp_tools = mcp_get_all_tools(state->mcp_config);
        if (mcp_tools && cJSON_IsArray(mcp_tools)) {
            int mcp_tool_count = cJSON_GetArraySize(mcp_tools);
            LOG_DEBUG("get_tool_definitions: Found %d dynamic MCP tools", mcp_tool_count);
            cJSON *t = NULL;
            int idx = 0;
            cJSON_ArrayForEach(t, mcp_tools) {
                // Each t is already a full Claude tool definition object
                cJSON *name_obj = cJSON_GetObjectItem(t, "name");
                const char *tool_name = name_obj && cJSON_IsString(name_obj) ? name_obj->valuestring : "unknown";
                LOG_DEBUG("get_tool_definitions: Adding dynamic MCP tool %d: '%s'", idx, tool_name);
                cJSON_AddItemToArray(tool_array, cJSON_Duplicate(t, 1));
                idx++;
            }
            cJSON_Delete(mcp_tools);
        } else {
            LOG_DEBUG("get_tool_definitions: No dynamic MCP tools found");
        }

        // 2) Built-in helper tools for MCP resources and generic invocation
        LOG_DEBUG("get_tool_definitions: Adding built-in MCP resource tools");

        // ListMcpResources tool
        cJSON *list_res_tool = cJSON_CreateObject();
        cJSON_AddStringToObject(list_res_tool, "type", "function");
        cJSON *list_res_func = cJSON_CreateObject();
        cJSON_AddStringToObject(list_res_func, "name", "ListMcpResources");
        cJSON_AddStringToObject(list_res_func, "description",
            "Lists available resources from configured MCP servers. "
            "Each resource object includes a 'server' field indicating which server it's from.");
        cJSON *list_res_params = cJSON_CreateObject();
        cJSON_AddStringToObject(list_res_params, "type", "object");
        cJSON *list_res_props = cJSON_CreateObject();
        cJSON *server_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(server_prop, "type", "string");
        cJSON_AddStringToObject(server_prop, "description",
            "Optional server name to filter resources by. If not provided, resources from all servers will be returned.");
        cJSON_AddItemToObject(list_res_props, "server", server_prop);
        cJSON_AddItemToObject(list_res_params, "properties", list_res_props);
        cJSON_AddItemToObject(list_res_func, "parameters", list_res_params);
        cJSON_AddItemToObject(list_res_tool, "function", list_res_func);
        cJSON_AddItemToArray(tool_array, list_res_tool);

        // ReadMcpResource tool
        cJSON *read_res_tool = cJSON_CreateObject();
        cJSON_AddStringToObject(read_res_tool, "type", "function");
        cJSON *read_res_func = cJSON_CreateObject();
        cJSON_AddStringToObject(read_res_func, "name", "ReadMcpResource");
        cJSON_AddStringToObject(read_res_func, "description",
            "Reads a specific resource from an MCP server, identified by server name and resource URI.");
        cJSON *read_res_params = cJSON_CreateObject();
        cJSON_AddStringToObject(read_res_params, "type", "object");
        cJSON *read_res_props = cJSON_CreateObject();
        cJSON *read_server_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(read_server_prop, "type", "string");
        cJSON_AddStringToObject(read_server_prop, "description", "The name of the MCP server to read from");
        cJSON_AddItemToObject(read_res_props, "server", read_server_prop);
        cJSON *uri_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(uri_prop, "type", "string");
        cJSON_AddStringToObject(uri_prop, "description", "The URI of the resource to read");
        cJSON_AddItemToObject(read_res_props, "uri", uri_prop);
        cJSON_AddItemToObject(read_res_params, "properties", read_res_props);
        cJSON *read_res_req = cJSON_CreateArray();
        cJSON_AddItemToArray(read_res_req, cJSON_CreateString("server"));
        cJSON_AddItemToArray(read_res_req, cJSON_CreateString("uri"));
        cJSON_AddItemToObject(read_res_params, "required", read_res_req);
        cJSON_AddItemToObject(read_res_func, "parameters", read_res_params);
        cJSON_AddItemToObject(read_res_tool, "function", read_res_func);
        cJSON_AddItemToArray(tool_array, read_res_tool);

        // CallMcpTool tool (generic MCP tool invoker)
        cJSON *call_tool = cJSON_CreateObject();
        cJSON_AddStringToObject(call_tool, "type", "function");
        cJSON *call_func = cJSON_CreateObject();
        cJSON_AddStringToObject(call_func, "name", "CallMcpTool");
        cJSON_AddStringToObject(call_func, "description",
            "Calls a specific MCP tool by server and tool name with JSON arguments.");
        cJSON *call_params = cJSON_CreateObject();
        cJSON_AddStringToObject(call_params, "type", "object");
        cJSON *call_props = cJSON_CreateObject();
        cJSON *call_server_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(call_server_prop, "type", "string");
        cJSON_AddStringToObject(call_server_prop, "description", "The MCP server name (as in config)");
        cJSON_AddItemToObject(call_props, "server", call_server_prop);
        cJSON *call_tool_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(call_tool_prop, "type", "string");
        cJSON_AddStringToObject(call_tool_prop, "description", "The tool name exposed by the server");
        cJSON_AddItemToObject(call_props, "tool", call_tool_prop);
        cJSON *call_args_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(call_args_prop, "type", "object");
        cJSON_AddStringToObject(call_args_prop, "description", "Arguments object per the tool's JSON schema");
        cJSON_AddItemToObject(call_props, "arguments", call_args_prop);
        cJSON_AddItemToObject(call_params, "properties", call_props);
        cJSON *call_req = cJSON_CreateArray();
        cJSON_AddItemToArray(call_req, cJSON_CreateString("server"));
        cJSON_AddItemToArray(call_req, cJSON_CreateString("tool"));
        cJSON_AddItemToObject(call_params, "required", call_req);
        cJSON_AddItemToObject(call_func, "parameters", call_params);
        cJSON_AddItemToObject(call_tool, "function", call_func);
        cJSON_AddItemToArray(tool_array, call_tool);

        LOG_INFO("Added MCP resource tools (ListMcpResources, ReadMcpResource)");
    }
#else
    (void)state;  // Suppress unused parameter warning in test builds
#endif

    return tool_array;
}

// ============================================================================
// API Client
// ============================================================================

// Helper: Check if prompt caching is enabled
static int is_prompt_caching_enabled(void) {
    const char *disable_cache = getenv("DISABLE_PROMPT_CACHING");
    if (disable_cache && (strcmp(disable_cache, "1") == 0 ||
                          strcmp(disable_cache, "true") == 0 ||
                          strcmp(disable_cache, "TRUE") == 0)) {
        return 0;
    }
    return 1;
}

// Helper: Add cache_control to a JSON object (for content blocks)
void add_cache_control(cJSON *obj) {
    cJSON *cache_ctrl = cJSON_CreateObject();
    cJSON_AddStringToObject(cache_ctrl, "type", "ephemeral");
    cJSON_AddItemToObject(obj, "cache_control", cache_ctrl);
}

/**
 * Build request JSON from conversation state (in OpenAI format)
 * This is called by providers to get the request body
 * Returns: Newly allocated JSON string (caller must free), or NULL on error
 */
char* build_request_json_from_state(ConversationState *state) {
    if (!state) {
        LOG_ERROR("ConversationState is NULL");
        return NULL;
    }

    if (conversation_state_lock(state) != 0) {
        return NULL;
    }

    // Ensure all tool calls have matching results before building request
#ifndef TEST_BUILD
    ensure_tool_results(state);
#endif

    char *json_str = NULL;

    // Check if prompt caching is enabled
    int enable_caching = is_prompt_caching_enabled();
    LOG_DEBUG("Building request (caching: %s, messages: %d)",
              enable_caching ? "enabled" : "disabled", state->count);

    // Build request body
    cJSON *request = cJSON_CreateObject();
    if (!request) {
        LOG_ERROR("Failed to allocate request object");
        goto unlock;
    }

    cJSON_AddStringToObject(request, "model", state->model);
    cJSON_AddNumberToObject(request, "max_completion_tokens", MAX_TOKENS);

    // Add messages in OpenAI format
    cJSON *messages_array = cJSON_CreateArray();
    if (!messages_array) {
        LOG_ERROR("Failed to allocate messages array");
        goto unlock;
    }
    for (int i = 0; i < state->count; i++) {
        cJSON *msg = cJSON_CreateObject();
        if (!msg) {
            LOG_ERROR("Failed to allocate message object");
            goto unlock;
        }

        // Determine role
        const char *role;
        if (state->messages[i].role == MSG_SYSTEM) {
            role = "system";
        } else if (state->messages[i].role == MSG_USER) {
            role = "user";
        } else {
            role = "assistant";
        }
        cJSON_AddStringToObject(msg, "role", role);

        // Determine if this is one of the last 3 messages (for cache breakpoint)
        // We want to cache the last few messages to speed up subsequent turns
        int is_recent_message = (i >= state->count - 3) && enable_caching;

        // Build content based on message type
        if (state->messages[i].role == MSG_SYSTEM) {
            // System messages: use content array with cache_control if enabled
            if (state->messages[i].content_count > 0 &&
                state->messages[i].contents[0].type == INTERNAL_TEXT) {

                // For system messages, use content array to support cache_control
                cJSON *content_array = cJSON_CreateArray();
                cJSON *text_block = cJSON_CreateObject();
                cJSON_AddStringToObject(text_block, "type", "text");
                cJSON_AddStringToObject(text_block, "text", state->messages[i].contents[0].text);

                // Add cache_control to system message if caching is enabled
                // This is the first cache breakpoint (system prompt)
                if (enable_caching) {
                    add_cache_control(text_block);
                }

                cJSON_AddItemToArray(content_array, text_block);
                cJSON_AddItemToObject(msg, "content", content_array);
            }
        } else if (state->messages[i].role == MSG_USER) {
            // User messages: check if it's tool results or plain text
            int has_tool_results = 0;
            for (int j = 0; j < state->messages[i].content_count; j++) {
                if (state->messages[i].contents[j].type == INTERNAL_TOOL_RESPONSE) {
                    has_tool_results = 1;
                    break;
                }
            }

            if (has_tool_results) {
                // For tool results, we need to add them as "tool" role messages
                for (int j = 0; j < state->messages[i].content_count; j++) {
                    InternalContent *cb = &state->messages[i].contents[j];
                    if (cb->type == INTERNAL_TOOL_RESPONSE) {
                        cJSON *tool_msg = cJSON_CreateObject();
                        cJSON_AddStringToObject(tool_msg, "role", "tool");
                        cJSON_AddStringToObject(tool_msg, "tool_call_id", cb->tool_id);
                        // Convert result to string
                        char *result_str = cJSON_PrintUnformatted(cb->tool_output);
                        cJSON_AddStringToObject(tool_msg, "content", result_str);
                        free(result_str);
                        cJSON_AddItemToArray(messages_array, tool_msg);
                    }
                }
                // Free the msg object we created but won't use
                cJSON_Delete(msg);
                continue; // Skip adding the user message itself
            } else {
                // Regular user message - handle text and image content
                if (state->messages[i].content_count > 0) {
                    // Use content array for recent messages to support cache_control and mixed content
                    if (is_recent_message) {
                        cJSON *content_array = cJSON_CreateArray();

                        for (int j = 0; j < state->messages[i].content_count; j++) {
                            InternalContent *cb = &state->messages[i].contents[j];

                            if (cb->type == INTERNAL_TEXT) {
                                // Text content
                                cJSON *text_block = cJSON_CreateObject();
                                cJSON_AddStringToObject(text_block, "type", "text");
                                cJSON_AddStringToObject(text_block, "text", cb->text);

                                // Add cache_control to the last user message
                                if (i == state->count - 1) {
                                    add_cache_control(text_block);
                                }

                                cJSON_AddItemToArray(content_array, text_block);
                            } else if (cb->type == INTERNAL_IMAGE) {
                                // Image content - OpenAI format
                                cJSON *image_block = cJSON_CreateObject();
                                cJSON_AddStringToObject(image_block, "type", "image_url");
                                cJSON *image_url = cJSON_CreateObject();

                                // Calculate required buffer size for data URL
                                size_t data_url_size = strlen("data:") + strlen(cb->mime_type) +
                                                     strlen(";base64,") + strlen(cb->base64_data) + 1;
                                char *data_url = malloc(data_url_size);
                                if (data_url) {
                                    snprintf(data_url, data_url_size, "data:%s;base64,%s",
                                             cb->mime_type, cb->base64_data);
                                    cJSON_AddStringToObject(image_url, "url", data_url);
                                    free(data_url);
                                }
                                cJSON_AddItemToObject(image_block, "image_url", image_url);
                                cJSON_AddItemToArray(content_array, image_block);
                            }
                        }

                        cJSON_AddItemToObject(msg, "content", content_array);
                    } else {
                        // For older messages, use simple string content (images not supported in simple format)
                        if (state->messages[i].contents[0].type == INTERNAL_TEXT) {
                            cJSON_AddStringToObject(msg, "content", state->messages[i].contents[0].text);
                        }
                    }
                }
            }
        } else {
            // Assistant messages
            cJSON *tool_calls = NULL;
            char *text_content = NULL;

            for (int j = 0; j < state->messages[i].content_count; j++) {
                InternalContent *cb = &state->messages[i].contents[j];

                if (cb->type == INTERNAL_TEXT) {
                    text_content = cb->text;
                } else if (cb->type == INTERNAL_TOOL_CALL) {
                    if (!tool_calls) {
                        tool_calls = cJSON_CreateArray();
                    }
                    cJSON *tool_call = cJSON_CreateObject();
                    cJSON_AddStringToObject(tool_call, "id", cb->tool_id);
                    cJSON_AddStringToObject(tool_call, "type", "function");
                    cJSON *function = cJSON_CreateObject();
                    cJSON_AddStringToObject(function, "name", cb->tool_name);
                    char *args_str = cJSON_PrintUnformatted(cb->tool_params);
                    cJSON_AddStringToObject(function, "arguments", args_str);
                    free(args_str);
                    cJSON_AddItemToObject(tool_call, "function", function);
                    cJSON_AddItemToArray(tool_calls, tool_call);
                }
            }

            // Add content (may be null if only tool calls)
            if (text_content) {
                cJSON_AddStringToObject(msg, "content", text_content);
            } else {
                cJSON_AddNullToObject(msg, "content");
            }

            if (tool_calls) {
                cJSON_AddItemToObject(msg, "tool_calls", tool_calls);
            }
        }

        cJSON_AddItemToArray(messages_array, msg);
    }

    cJSON_AddItemToObject(request, "messages", messages_array);

    // Add tools with cache_control support (including MCP tools if available)
    // In plan mode, exclude Bash, Subagent, Write, and Edit tools
    cJSON *tool_defs = get_tool_definitions(state, enable_caching);
    cJSON_AddItemToObject(request, "tools", tool_defs);

    conversation_state_unlock(state);
    state = NULL;

    json_str = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);

    LOG_DEBUG("Request built successfully (size: %zu bytes)", json_str ? strlen(json_str) : 0);
    return json_str;

unlock:
    conversation_state_unlock(state);
    if (request) {
        cJSON_Delete(request);
    }
    return NULL;
}

// ============================================================================
// API Response Management
// ============================================================================

/**
 * Free an ApiResponse structure and all its owned resources
 */
void api_response_free(ApiResponse *response) {
    if (!response) return;

    // Free assistant message text
    free(response->message.text);

    // Free tool calls
    if (response->tools) {
        for (int i = 0; i < response->tool_count; i++) {
            free(response->tools[i].id);
            free(response->tools[i].name);
            if (response->tools[i].parameters) {
                cJSON_Delete(response->tools[i].parameters);
            }
        }
        free(response->tools);
    }

    // Free raw response
    if (response->raw_response) {
        cJSON_Delete(response->raw_response);
    }

    // Free error message
    free(response->error_message);

    free(response);
}

// ============================================================================
// API Call Logic
// ============================================================================



/**
 * Call API with retry logic (generic wrapper around provider->call_api)
 * Handles exponential backoff for retryable errors
 * Returns: ApiResponse or NULL on error
 */
static ApiResponse* call_api_with_retries(ConversationState *state) {
    if (!state) {
        LOG_ERROR("Invalid conversation state");
        return NULL;
    }

    // Log plan mode before API call
    LOG_DEBUG("[API] call_api_with_retries: plan_mode=%d", state->plan_mode);

    // Lazy-initialize provider to avoid blocking initial TUI render
    if (!state->provider) {
        LOG_INFO("Initializing API provider in background context...");
        ProviderInitResult provider_result;
        provider_init(state->model, state->api_key, &provider_result);
        if (!provider_result.provider) {
            const char *msg = provider_result.error_message ? provider_result.error_message : "unknown error";
            LOG_ERROR("Provider initialization failed: %s", msg);
            print_error("Failed to initialize API provider. Check configuration.");
            free(provider_result.error_message);
            free(provider_result.api_url);
            return NULL;
        }

        // Transfer ownership to state and update API URL
        if (state->api_url) {
            free(state->api_url);
        }
        state->api_url = provider_result.api_url;
        state->provider = provider_result.provider;
        free(provider_result.error_message);

        LOG_INFO("Provider initialized: %s, API URL: %s",
                 state->provider->name, state->api_url ? state->api_url : "(null)");
    }

    int attempt_num = 1;
    int backoff_ms = INITIAL_BACKOFF_MS;
    char *last_error = NULL;
    long last_http_status = 0;

    struct timespec call_start, call_end, retry_start;
    clock_gettime(CLOCK_MONOTONIC, &call_start);
    retry_start = call_start;

    LOG_DEBUG("Starting API call (provider: %s, model: %s)",
              state->provider->name, state->model);

    while (1) {
        // Check for interrupt request
        if (state->interrupt_requested) {
            LOG_INFO("API call interrupted by user request");
            print_error("Operation interrupted by user");
            free(last_error);
            return NULL;
        }

        // Check if we've exceeded max retry duration
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - retry_start.tv_sec) * 1000 +
                         (now.tv_nsec - retry_start.tv_nsec) / 1000000;

        if (attempt_num > 1 && elapsed_ms >= state->max_retry_duration_ms) {
            LOG_ERROR("Maximum retry duration (%d ms) exceeded after %d attempts",
                     state->max_retry_duration_ms, attempt_num - 1);

            // Include the last error details for user context
            char error_msg[1024];
            if (last_error && last_http_status > 0) {
                snprintf(error_msg, sizeof(error_msg),
                        "Maximum retry duration exceeded. Last error: %s (HTTP %ld)",
                        last_error, last_http_status);
            } else {
                snprintf(error_msg, sizeof(error_msg),
                        "Maximum retry duration exceeded");
            }
            print_error(error_msg);
            free(last_error);
            return NULL;
        }

        // Call provider's single-attempt API call
        LOG_DEBUG("API call attempt %d (elapsed: %ld ms)", attempt_num, elapsed_ms);
        ApiCallResult result = state->provider->call_api(state->provider, state);

        // Success case
        if (result.response) {
            clock_gettime(CLOCK_MONOTONIC, &call_end);
            long total_ms = (call_end.tv_sec - call_start.tv_sec) * 1000 +
                           (call_end.tv_nsec - call_start.tv_nsec) / 1000000;

            LOG_INFO("API call succeeded (duration: %ld ms, provider duration: %ld ms, attempts: %d, auth_refreshed: %s, plan_mode: %s)",
                     total_ms, result.duration_ms, attempt_num,
                     result.auth_refreshed ? "yes" : "no",
                     state->plan_mode ? "yes" : "no");



            // Log success to persistence
            if (state->persistence_db && result.raw_response) {
                // Tool count is already available in the ApiResponse
                int tool_count = result.response->tool_count;

                persistence_log_api_call(
                    state->persistence_db,
                    state->session_id,
                    state->api_url,
                    result.request_json ? result.request_json : "(request not available)",
                    result.headers_json,
                    result.raw_response,
                    state->model,
                    "success",
                    (int)result.http_status,
                    NULL,
                    result.duration_ms,
                    tool_count
                );
            }

            // Cleanup and return
            free(result.raw_response);
            free(result.request_json);
            free(result.error_message);
            return result.response;
        }

        // Error case - check if retryable
        LOG_WARN("API call failed (attempt %d): %s (HTTP %ld, retryable: %s)",
                 attempt_num,
                 result.error_message ? result.error_message : "(unknown)",
                 result.http_status,
                 result.is_retryable ? "yes" : "no");

        // Log error to persistence
        if (state->persistence_db) {
            persistence_log_api_call(
                state->persistence_db,
                state->session_id,
                state->api_url,
                result.request_json ? result.request_json : "(request not available)",
                result.headers_json,
                result.raw_response,
                state->model,
                "error",
                (int)result.http_status,
                result.error_message,
                result.duration_ms,
                0
            );
        }

        // Save last error details for potential timeout message
        if (last_error) {
            free(last_error);
        }
        last_error = result.error_message ? strdup(result.error_message) : NULL;
        last_http_status = result.http_status;

        // Check if we should retry
        if (!result.is_retryable) {
            // Non-retryable error
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg),
                    "API call failed: %s (HTTP %ld)",
                    result.error_message ? result.error_message : "unknown error",
                    result.http_status);
            print_error(error_msg);

            // Create an error response instead of returning NULL
            ApiResponse *error_response = calloc(1, sizeof(ApiResponse));
            if (error_response) {
                error_response->error_message = strdup(result.error_message ? result.error_message : "unknown error");
            }

            free(last_error);
            free(result.raw_response);
            free(result.request_json);
            free(result.error_message);
            return error_response;
        }

        // Calculate backoff with jitter (0-25% reduction)
        uint32_t jitter = arc4random_uniform((uint32_t)(backoff_ms / 4));
        int delay_ms = backoff_ms - (int)jitter;

        // Check if this delay would exceed max retry duration
        clock_gettime(CLOCK_MONOTONIC, &now);
        elapsed_ms = (now.tv_sec - retry_start.tv_sec) * 1000 +
                    (now.tv_nsec - retry_start.tv_nsec) / 1000000;
        long remaining_ms = state->max_retry_duration_ms - elapsed_ms;

        if (delay_ms > remaining_ms) {
            delay_ms = (int)remaining_ms;
            if (delay_ms <= 0) {
                LOG_ERROR("Maximum retry duration (%d ms) exceeded", state->max_retry_duration_ms);

                // Include the error details for user context
                char error_msg[1024];
                if (result.error_message && result.http_status > 0) {
                    snprintf(error_msg, sizeof(error_msg),
                            "Maximum retry duration exceeded. Last error: %s (HTTP %ld)",
                            result.error_message, result.http_status);
                } else {
                    snprintf(error_msg, sizeof(error_msg),
                            "Maximum retry duration exceeded");
                }
                print_error(error_msg);

                free(last_error);
                free(result.raw_response);
                free(result.request_json);
                free(result.error_message);
                return NULL;
            }
        }

        // Display retry message to user
        char retry_msg[512];
        const char *error_type = (result.http_status == 429) ? "Rate limit" :
                                (result.http_status == 408) ? "Request timeout" :
                                (result.http_status >= 500) ? "Server error" : "Error";
        snprintf(retry_msg, sizeof(retry_msg),
                "%s - retrying in %d ms... (attempt %d)",
                error_type, delay_ms, attempt_num + 1);
        print_error(retry_msg);

        LOG_INFO("Retrying after %d ms (elapsed: %ld ms, remaining: %ld ms)",
                delay_ms, elapsed_ms, remaining_ms);

        // Sleep and retry
        usleep((useconds_t)(delay_ms * 1000));
        backoff_ms = (int)(backoff_ms * BACKOFF_MULTIPLIER);
        if (backoff_ms > MAX_BACKOFF_MS) {
            backoff_ms = MAX_BACKOFF_MS;
        }

        free(result.raw_response);
        free(result.request_json);
        free(result.error_message);
        attempt_num++;
    }
}

/**
 * Main API call entry point
 */
static ApiResponse* call_api(ConversationState *state) {
    return call_api_with_retries(state);
}


// ============================================================================
// Context Building - Environment and Git Information
// ============================================================================

// Get current date in YYYY-MM-DD format
static char* get_current_date(void) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    char *date = malloc(11); // "YYYY-MM-DD\0"
    if (!date) return NULL;

    strftime(date, 11, "%Y-%m-%d", tm_info);
    return date;
}

// Check if current directory is a git repository
static int is_git_repo(const char *working_dir) {
    char git_path[PATH_MAX];
    snprintf(git_path, sizeof(git_path), "%s/.git", working_dir);

    struct stat st;
    return (stat(git_path, &st) == 0);
}

// Execute git command and return output
static char* exec_git_command(const char *command) {
    FILE *fp = popen(command, "r");
    if (!fp) return NULL;

    char *output = NULL;
    size_t output_size = 0;
    char buffer[1024];

    while (fgets(buffer, sizeof(buffer), fp)) {
        size_t len = strlen(buffer);
        char *new_output = realloc(output, output_size + len + 1);
        if (!new_output) {
            free(output);
            pclose(fp);
            return NULL;
        }
        output = new_output;
        memcpy(output + output_size, buffer, len);
        output_size += len;
        output[output_size] = '\0';
    }

    pclose(fp);

    // Trim trailing newline
    if (output && output_size > 0 && output[output_size-1] == '\n') {
        output[output_size-1] = '\0';
    }

    return output;
}

// Get git status information
static char* get_git_status(const char *working_dir) {
    if (!is_git_repo(working_dir)) {
        return NULL;
    }

    // Get current branch
    char *branch = exec_git_command("git rev-parse --abbrev-ref HEAD 2>/dev/null");
    if (!branch) branch = strdup("unknown");

    // Get git status (clean or modified)
    char *status_output = exec_git_command("git status --porcelain 2>/dev/null");
    const char *status = (status_output && strlen(status_output) > 0) ? "modified" : "clean";

    // Get recent commits (last 5)
    char *commits = exec_git_command("git log --oneline -5 2>/dev/null");
    if (!commits) commits = strdup("(no commits)");

    // Build the gitStatus block
    size_t total_size = 1024 + strlen(branch) + strlen(status) + strlen(commits);
    char *git_status = malloc(total_size);
    if (!git_status) {
        free(branch);
        free(status_output);
        free(commits);
        return NULL;
    }

    snprintf(git_status, total_size,
        "gitStatus: This is the git status at the start of the conversation. "
        "Note that this status is a snapshot in time, and will not update during the conversation.\n"
        "Current branch: %s\n\n"
        "Main branch (you will usually use this for PRs): \n\n"
        "Status:\n(%s)\n\n"
        "Recent commits:\n%s",
        branch, status, commits);

    free(branch);
    free(status_output);
    free(commits);

    return git_status;
}

// Get OS/Platform information
static char* get_os_version(void) {
    char *os_version = exec_git_command("uname -sr 2>/dev/null");
    if (!os_version) {
        os_version = strdup("Unknown");
    }
    return os_version;
}

static const char* get_platform(void) {
#ifdef __APPLE__
    return "darwin";
#elif defined(__linux__)
    return "linux";
#elif defined(_WIN32) || defined(_WIN64)
    return "win32";
#elif defined(__FreeBSD__)
    return "freebsd";
#elif defined(__OpenBSD__)
    return "openbsd";
#else
    return "unknown";
#endif
}

// Read KLAWED.md from working directory if it exists
static char* read_klawed_md(const char *working_dir) {
    char klawed_md_path[PATH_MAX];
    snprintf(klawed_md_path, sizeof(klawed_md_path), "%s/KLAWED.md", working_dir);

    // Check if file exists
    struct stat st;
    if (stat(klawed_md_path, &st) != 0) {
        return NULL; // File doesn't exist
    }

    // Read the file
    FILE *f = fopen(klawed_md_path, "r");
    if (!f) {
        return NULL;
    }

    // Allocate buffer based on file size
    size_t file_size = (size_t)st.st_size;
    char *content = malloc(file_size + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }

    size_t read_size = fread(content, 1, file_size, f);
    fclose(f);

    if (read_size != file_size) {
        free(content);
        return NULL;
    }

    content[file_size] = '\0';
    return content;
}

// Build complete system prompt with environment context
char* build_system_prompt(ConversationState *state) {
    const char *working_dir = state->working_dir;
    char *date = get_current_date();
    const char *platform = get_platform();
    char *os_version = get_os_version();
    int is_git = is_git_repo(working_dir);
    char *git_status = is_git ? get_git_status(working_dir) : NULL;
    char *klawed_md = read_klawed_md(working_dir);

    // Calculate required buffer size
    size_t prompt_size = 2048; // Base size for the prompt template
    if (git_status) prompt_size += strlen(git_status);
    if (klawed_md) prompt_size += strlen(klawed_md) + 512; // Extra space for formatting

    // Add space for additional directories
    for (int i = 0; i < state->additional_dirs_count; i++) {
        prompt_size += strlen(state->additional_dirs[i]) + 4; // path + ", " separator
    }

    char *prompt = malloc(prompt_size);
    if (!prompt) {
        free(date);
        free(os_version);
        free(git_status);
        free(klawed_md);
        return NULL;
    }

    // Build the system prompt with additional directories
    // Log plan mode when building system prompt
    LOG_DEBUG("[SYSTEM] build_system_prompt: plan_mode=%d", state->plan_mode);

    int offset = snprintf(prompt, prompt_size,
        "Here is useful information about the environment you are running in:\n"
        "<env>\n"
        "Planning mode: %s\n"
        "Working directory: %s\n"
        "Additional working directories: ",
        state->plan_mode ? "ENABLED - You can ONLY use read-only tools (Read, Glob, Grep, Sleep, UploadImage, TodoWrite). The Bash, Subagent, Write, and Edit tools are NOT available in planning mode." : "disabled",
        working_dir);

    // Add additional directories
    if (state->additional_dirs_count > 0) {
        for (int i = 0; i < state->additional_dirs_count; i++) {
            if (i > 0) {
                offset += snprintf(prompt + offset, prompt_size - (size_t)offset, ", ");
            }
            offset += snprintf(prompt + offset, prompt_size - (size_t)offset, "%s", state->additional_dirs[i]);
        }
    }
    offset += snprintf(prompt + offset, prompt_size - (size_t)offset, "\n");

    offset += snprintf(prompt + offset, prompt_size - (size_t)offset,
        "Is directory a git repo: %s\n"
        "Platform: %s\n"
        "OS Version: %s\n"
        "Today's date: %s\n"
        "</env>\n",
        is_git ? "Yes" : "No",
        platform,
        os_version,
        date);

    // Add git status if available
    if (git_status && offset < (int)prompt_size) {
        offset += snprintf(prompt + offset, prompt_size - (size_t)offset, "\n%s\n", git_status);
    }

    // Add KLAWED.md content if available
    if (klawed_md && offset < (int)prompt_size) {
        offset += snprintf(prompt + offset, prompt_size - (size_t)offset,
            "\n<system-reminder>\n"
            "As you answer the user's questions, you can use the following context:\n"
            "# klawedMd\n"
            "Codebase and user instructions are shown below. Be sure to adhere to these instructions. "
            "IMPORTANT: These instructions OVERRIDE any default behavior and you MUST follow them exactly as written.\n\n"
            "Contents of %s/KLAWED.md (project instructions, checked into the codebase):\n\n"
            "%s\n\n"
            "      IMPORTANT: this context may or may not be relevant to your tasks. "
            "You should not respond to this context unless it is highly relevant to your task.\n"
            "</system-reminder>\n",
            working_dir, klawed_md);
    }

    free(date);
    free(os_version);
    free(git_status);
    free(klawed_md);

    (void)offset; // Suppress unused variable warning after final snprintf

    return prompt;
}

// ============================================================================
// Message Management
// ============================================================================

int conversation_state_init(ConversationState *state) {
    if (!state) {
        return -1;
    }

    if (state->conv_mutex_initialized) {
        return 0;
    }

    if (pthread_mutex_init(&state->conv_mutex, NULL) != 0) {
        LOG_ERROR("Failed to initialize conversation mutex");
        return -1;
    }

    state->conv_mutex_initialized = 1;
    state->interrupt_requested = 0;  // Initialize interrupt flag

    // Initialize subagent manager
    state->subagent_manager = malloc(sizeof(SubagentManager));
    if (state->subagent_manager) {
        if (subagent_manager_init(state->subagent_manager) != 0) {
            LOG_ERROR("Failed to initialize subagent manager");
            free(state->subagent_manager);
            state->subagent_manager = NULL;
            // Continue anyway - not a critical failure
        } else {
            LOG_DEBUG("Subagent manager initialized successfully");
            // Register for emergency cleanup on unexpected termination
            register_subagent_manager_for_cleanup(state->subagent_manager);
        }
    } else {
        LOG_ERROR("Failed to allocate memory for subagent manager");
        // Continue anyway - not a critical failure
    }

    return 0;
}

void conversation_state_destroy(ConversationState *state) {
    if (!state || !state->conv_mutex_initialized) {
        return;
    }

    // Clean up subagent manager
    if (state->subagent_manager) {
        // Unregister from emergency cleanup
        register_subagent_manager_for_cleanup(NULL);

        subagent_manager_free(state->subagent_manager);
        free(state->subagent_manager);
        state->subagent_manager = NULL;
    }

    pthread_mutex_destroy(&state->conv_mutex);
    state->conv_mutex_initialized = 0;
}

int conversation_state_lock(ConversationState *state) {
    if (!state) {
        return -1;
    }

    if (!state->conv_mutex_initialized) {
        if (conversation_state_init(state) != 0) {
            return -1;
        }
    }

    if (pthread_mutex_lock(&state->conv_mutex) != 0) {
        LOG_ERROR("Failed to lock conversation mutex");
        return -1;
    }
    return 0;
}

void conversation_state_unlock(ConversationState *state) {
    if (!state || !state->conv_mutex_initialized) {
        return;
    }
    pthread_mutex_unlock(&state->conv_mutex);
}

static void add_system_message(ConversationState *state, const char *text) {
    if (conversation_state_lock(state) != 0) {
        return;
    }

    if (state->count >= MAX_MESSAGES) {
        LOG_ERROR("Maximum message count reached");
        conversation_state_unlock(state);
        return;
    }

    InternalMessage *msg = &state->messages[state->count++];
    msg->role = MSG_SYSTEM;
    msg->contents = calloc(1, sizeof(InternalContent));
    if (!msg->contents) {
        LOG_ERROR("Failed to allocate memory for message content");
        state->count--;
        return;
    }
    msg->content_count = 1;
    // calloc already zeros memory, but explicitly set for analyzer
    msg->contents[0].type = INTERNAL_TEXT;
    msg->contents[0].text = NULL;
    msg->contents[0].tool_id = NULL;
    msg->contents[0].tool_name = NULL;
    msg->contents[0].tool_params = NULL;
    msg->contents[0].tool_output = NULL;

    msg->contents[0].text = strdup(text);
    if (!msg->contents[0].text) {
        LOG_ERROR("Failed to duplicate message text");
        free(msg->contents);
        msg->contents = NULL;
        state->count--;
        conversation_state_unlock(state);
        return;
    }

    conversation_state_unlock(state);
}

void add_user_message(ConversationState *state, const char *text) {
    if (conversation_state_lock(state) != 0) {
        return;
    }

    if (state->count >= MAX_MESSAGES) {
        LOG_ERROR("Maximum message count reached");
        conversation_state_unlock(state);
        return;
    }

    InternalMessage *msg = &state->messages[state->count++];
    msg->role = MSG_USER;
    msg->contents = calloc(1, sizeof(InternalContent));
    if (!msg->contents) {
        LOG_ERROR("Failed to allocate memory for message content");
        state->count--; // Rollback count increment
        return;
    }
    msg->content_count = 1;
    // calloc already zeros memory, but explicitly set for analyzer
    msg->contents[0].type = INTERNAL_TEXT;
    msg->contents[0].text = NULL;
    msg->contents[0].tool_id = NULL;
    msg->contents[0].tool_name = NULL;
    msg->contents[0].tool_params = NULL;
    msg->contents[0].tool_output = NULL;

    msg->contents[0].text = strdup(text);
    if (!msg->contents[0].text) {
        LOG_ERROR("Failed to duplicate message text");
        free(msg->contents);
        msg->contents = NULL;
        state->count--; // Rollback count increment
        conversation_state_unlock(state);
        return;
    }

    conversation_state_unlock(state);
}

// Parse OpenAI message format and add to conversation
static void add_assistant_message_openai(ConversationState *state, cJSON *message) {
    if (conversation_state_lock(state) != 0) {
        return;
    }

    if (state->count >= MAX_MESSAGES) {
        LOG_ERROR("Maximum message count reached");
        conversation_state_unlock(state);
        return;
    }

    InternalMessage *msg = &state->messages[state->count++];
    msg->role = MSG_ASSISTANT;

    // Count content blocks (text + tool calls)
    int content_count = 0;
    cJSON *content = cJSON_GetObjectItem(message, "content");
    cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");

    if (content && cJSON_IsString(content) && content->valuestring) {
        content_count++;
    }

    // Count VALID tool calls (those with 'function' field)
    int tool_calls_count = 0;
    if (tool_calls && cJSON_IsArray(tool_calls)) {
        int array_size = cJSON_GetArraySize(tool_calls);
        for (int i = 0; i < array_size; i++) {
            cJSON *tool_call = cJSON_GetArrayItem(tool_calls, i);
            cJSON *function = cJSON_GetObjectItem(tool_call, "function");
            cJSON *id = cJSON_GetObjectItem(tool_call, "id");
            if (function && id && cJSON_IsString(id)) {
                tool_calls_count++;
            } else {
                LOG_WARN("Skipping malformed tool_call at index %d (missing 'function' or 'id' field)", i);
            }
        }
        content_count += tool_calls_count;
    }

    // Ensure we have at least some content
    if (content_count == 0) {
        LOG_WARN("Assistant message has no content");
        state->count--; // Rollback count increment
        conversation_state_unlock(state);
        return;
    }

    msg->contents = calloc((size_t)content_count, sizeof(InternalContent));
    if (!msg->contents) {
        LOG_ERROR("Failed to allocate memory for message content");
        state->count--; // Rollback count increment
        conversation_state_unlock(state);
        return;
    }
    msg->content_count = content_count;

    int idx = 0;

    // Add text content if present
    if (content && cJSON_IsString(content) && content->valuestring) {
        msg->contents[idx].type = INTERNAL_TEXT;
        msg->contents[idx].text = strdup(content->valuestring);
        if (!msg->contents[idx].text) {
            LOG_ERROR("Failed to duplicate message text");
            free(msg->contents);
            msg->contents = NULL;
            state->count--;
            conversation_state_unlock(state);
            return;
        }
        idx++;
    }

    // Add tool calls if present
    if (tool_calls && cJSON_IsArray(tool_calls)) {
        int array_size = cJSON_GetArraySize(tool_calls);
        for (int i = 0; i < array_size; i++) {
            cJSON *tool_call = cJSON_GetArrayItem(tool_calls, i);
            cJSON *id = cJSON_GetObjectItem(tool_call, "id");
            cJSON *function = cJSON_GetObjectItem(tool_call, "function");

            // Skip malformed tool calls (already logged warning during counting)
            if (!function || !id || !cJSON_IsString(id)) {
                continue;
            }

            cJSON *name = cJSON_GetObjectItem(function, "name");
            cJSON *arguments = cJSON_GetObjectItem(function, "arguments");

            msg->contents[idx].type = INTERNAL_TOOL_CALL;
            msg->contents[idx].tool_id = strdup(id->valuestring);
            if (!msg->contents[idx].tool_id) {
                LOG_ERROR("Failed to duplicate tool use ID");
                // Cleanup previously allocated content
                for (int j = 0; j < idx; j++) {
                    free(msg->contents[j].text);
                    free(msg->contents[j].tool_id);
                    free(msg->contents[j].tool_name);
                }
                free(msg->contents);
                msg->contents = NULL;
                state->count--;
                conversation_state_unlock(state);
                return;
            }
            msg->contents[idx].tool_name = strdup(name->valuestring);
            if (!msg->contents[idx].tool_name) {
                LOG_ERROR("Failed to duplicate tool name");
                free(msg->contents[idx].tool_id);
                // Cleanup previously allocated content
                for (int j = 0; j < idx; j++) {
                    free(msg->contents[j].text);
                    free(msg->contents[j].tool_id);
                    free(msg->contents[j].tool_name);
                }
                free(msg->contents);
                msg->contents = NULL;
                state->count--;
                conversation_state_unlock(state);
                return;
            }

            // Parse arguments string as JSON
            if (arguments && cJSON_IsString(arguments)) {
                msg->contents[idx].tool_params = cJSON_Parse(arguments->valuestring);
                if (!msg->contents[idx].tool_params) {
                    LOG_WARN("Failed to parse tool arguments, using empty object");
                    msg->contents[idx].tool_params = cJSON_CreateObject();
                }
            } else {
                msg->contents[idx].tool_params = cJSON_CreateObject();
            }
            idx++;
        }
    }

    conversation_state_unlock(state);
}

// Helper: Free an array of InternalContent and its internal allocations
static void free_internal_contents(InternalContent *results, int count) {
    if (!results) return;
    for (int i = 0; i < count; i++) {
        InternalContent *cb = &results[i];
        free(cb->text);
        free(cb->tool_id);
        free(cb->tool_name);
        if (cb->tool_params) cJSON_Delete(cb->tool_params);
        if (cb->tool_output) cJSON_Delete(cb->tool_output);

        // Handle INTERNAL_IMAGE type
        if (cb->type == INTERNAL_IMAGE) {
            free(cb->image_path);
            free(cb->mime_type);
            free(cb->base64_data);
        }
    }
    free(results);
}

// Helper: Check if TodoWrite was executed in the results array
static int check_todo_write_executed(InternalContent *results, int count) {
    if (!results) return 0;
    for (int i = 0; i < count; i++) {
        if (results[i].tool_name && strcmp(results[i].tool_name, "TodoWrite") == 0) {
            return 1;
        }
    }
    return 0;
}

// Returns 0 on success, -1 on failure
static int add_tool_results(ConversationState *state, InternalContent *results, int count) {
    LOG_DEBUG("add_tool_results: Adding %d tool results to conversation", count);

    if (conversation_state_lock(state) != 0) {
        LOG_ERROR("add_tool_results: Failed to acquire conversation lock");
        free_internal_contents(results, count);
        return -1;
    }

    if (state->count >= MAX_MESSAGES) {
        LOG_ERROR("add_tool_results: Cannot add results - maximum message count (%d) reached", MAX_MESSAGES);
        // Free results since they won't be added to state
        free_internal_contents(results, count);
        conversation_state_unlock(state);
        return -1;
    }

    // Log each tool result being added
    for (int i = 0; i < count; i++) {
        InternalContent *result = &results[i];
        LOG_DEBUG("add_tool_results: result[%d]: tool_id=%s, tool_name=%s, is_error=%d",
                  i, result->tool_id ? result->tool_id : "NULL",
                  result->tool_name ? result->tool_name : "NULL",
                  result->is_error);
    }

    InternalMessage *msg = &state->messages[state->count++];
    msg->role = MSG_USER;
    msg->contents = results;
    msg->content_count = count;

    LOG_INFO("add_tool_results: Successfully added %d tool results as msg[%d]", count, state->count - 1);

    conversation_state_unlock(state);
    return 0;
}

// ============================================================================
// Interactive Mode - Simple Terminal I/O
// ============================================================================

void clear_conversation(ConversationState *state) {
    if (conversation_state_lock(state) != 0) {
        return;
    }

    // Keep the system message (first message)
    int system_msg_count = 0;

    if (state->count > 0 && state->messages[0].role == MSG_SYSTEM) {
        // System message remains intact
        system_msg_count = 1;
    }

    // Free all other message content
    for (int i = system_msg_count; i < state->count; i++) {
        for (int j = 0; j < state->messages[i].content_count; j++) {
            InternalContent *cb = &state->messages[i].contents[j];
            free(cb->text);
            free(cb->tool_id);
            free(cb->tool_name);
            if (cb->tool_params) cJSON_Delete(cb->tool_params);
            if (cb->tool_output) cJSON_Delete(cb->tool_output);
        }
        free(state->messages[i].contents);
    }

    // Reset message count (keeping system message)
    state->count = system_msg_count;

    // Clear todo list
    if (state->todo_list) {
        todo_free(state->todo_list);
        todo_init(state->todo_list);
        LOG_DEBUG("Todo list cleared and reinitialized");
    }

    conversation_state_unlock(state);
}

// Free all messages and their contents (including system message). Use at program shutdown.
void conversation_free(ConversationState *state) {
    if (conversation_state_lock(state) != 0) {
        return;
    }

    // Free all messages
    for (int i = 0; i < state->count; i++) {
        for (int j = 0; j < state->messages[i].content_count; j++) {
            InternalContent *cb = &state->messages[i].contents[j];
            free(cb->text);
            free(cb->tool_id);
            free(cb->tool_name);
            if (cb->tool_params) cJSON_Delete(cb->tool_params);
            if (cb->tool_output) cJSON_Delete(cb->tool_output);
        }
        free(state->messages[i].contents);
    }
    state->count = 0;

    // Note: todo_list is freed separately in main cleanup
    // Do not call todo_free() here to avoid double-free

    conversation_state_unlock(state);
}

static void process_response(ConversationState *state,
                             ApiResponse *response,
                             TUIState *tui,
                             TUIMessageQueue *queue,
                             AIWorkerContext *worker_ctx) {
    // Time the entire response processing
    struct timespec proc_start, proc_end;
    clock_gettime(CLOCK_MONOTONIC, &proc_start);

    // Check for error response first - these are already displayed by call_api_with_retries
    // and don't have raw_response populated, so we exit early to avoid NULL pointer access
    if (response->error_message) {
        LOG_DEBUG("process_response: Error response encountered, exiting early: %s", response->error_message);
        return;
    }

    // Display assistant's text content if present
    if (response->message.text && response->message.text[0] != '\0') {
        // Skip whitespace-only content
        const char *p = response->message.text;
        while (*p && isspace((unsigned char)*p)) p++;

        if (*p != '\0') {  // Has non-whitespace content
            ui_append_line(tui, queue, "[Assistant]", p, COLOR_PAIR_ASSISTANT);
        }
    }

    // Add to conversation history (using raw response for now)
    // Extract message from raw_response for backward compatibility
    // Defense in depth: check for NULL raw_response
    if (!response->raw_response) {
        LOG_WARN("process_response: raw_response is NULL, cannot add to conversation history");
        return;
    }

    cJSON *choices = cJSON_GetObjectItem(response->raw_response, "choices");
    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *choice = cJSON_GetArrayItem(choices, 0);
        cJSON *message = cJSON_GetObjectItem(choice, "message");
        if (message) {
            add_assistant_message_openai(state, message);
        }
    }

    // Process tool calls from vendor-agnostic structure
    int tool_count = response->tool_count;
    ToolCall *tool_calls_array = response->tools;

    if (tool_count > 0) {

        LOG_INFO("Processing %d tool call(s)", tool_count);

        // Log details of each tool call
        for (int i = 0; i < tool_count; i++) {
            ToolCall *tool = &tool_calls_array[i];
            LOG_DEBUG("Tool call[%d]: id=%s, name=%s, has_params=%d",
                      i, tool->id ? tool->id : "NULL",
                      tool->name ? tool->name : "NULL",
                      tool->parameters != NULL);
        }

        struct timespec tool_start, tool_end;
        clock_gettime(CLOCK_MONOTONIC, &tool_start);

        InternalContent *results = calloc((size_t)tool_count, sizeof(InternalContent));
        if (!results) {
            ui_show_error(tui, queue, "Failed to allocate tool result buffer");
            return;
        }

        int valid_tool_calls = 0;
        for (int i = 0; i < tool_count; i++) {
            ToolCall *tool = &tool_calls_array[i];
            if (tool->name && tool->id) {
                valid_tool_calls++;
            }
        }

        pthread_t *threads = NULL;
        ToolThreadArg *args = NULL;
        if (valid_tool_calls > 0) {
            threads = calloc((size_t)valid_tool_calls, sizeof(pthread_t));
            args = calloc((size_t)valid_tool_calls, sizeof(ToolThreadArg));
            if (!threads || !args) {
                ui_show_error(tui, queue, "Failed to allocate tool thread structures");
                free(threads);
                free(args);
                free_internal_contents(results, tool_count);
                return;
            }
        }

        ToolCallbackContext callback_ctx = {
            .tui = tui,
            .queue = queue,
            .spinner = NULL,
            .worker_ctx = worker_ctx
        };

        Spinner *tool_spinner = NULL;
        if (!tui && !queue) {
            char spinner_msg[128];
            snprintf(spinner_msg, sizeof(spinner_msg), "Running %d tool%s...",
                     valid_tool_calls, valid_tool_calls == 1 ? "" : "s");
            tool_spinner = spinner_start(spinner_msg, SPINNER_YELLOW);
        } else {
            char status_msg[128];
            snprintf(status_msg, sizeof(status_msg), "Running %d tool%s...",
                     valid_tool_calls, valid_tool_calls == 1 ? "" : "s");
            ui_set_status(tui, queue, status_msg);
        }
        callback_ctx.spinner = tool_spinner;

        ToolExecutionTracker tracker;
        int tracker_initialized = 0;
        if (valid_tool_calls > 0) {
            if (tool_tracker_init(&tracker, valid_tool_calls, tool_progress_callback, &callback_ctx) != 0) {
                ui_show_error(tui, queue, "Failed to initialize tool tracker");
                if (tool_spinner) {
                    spinner_stop(tool_spinner, "Tool execution failed to start", 0);
                }
                free(threads);
                free(args);
                free_internal_contents(results, tool_count);
                return;
            }
            tracker_initialized = 1;
        }

        int started_threads = 0;
        int interrupted = 0;  // Track if user requested interruption during scheduling/waiting

        for (int i = 0; i < tool_count; i++) {
            // Check for interrupt before starting each tool
            if (state->interrupt_requested) {
                LOG_INFO("Tool execution interrupted by user request (before starting remaining tools)");
                ui_show_error(tui, queue, "Tool execution interrupted by user");
                interrupted = 1;

                // For any tools not yet started, emit a cancelled tool_result so the
                // conversation remains consistent (every tool_call gets a tool_result)
                for (int k = i; k < tool_count; k++) {
                    ToolCall *tcancel = &tool_calls_array[k];
                    InternalContent *slot = &results[k];
                    slot->type = INTERNAL_TOOL_RESPONSE;
                    slot->tool_id = tcancel->id ? strdup(tcancel->id) : strdup("unknown");
                    slot->tool_name = tcancel->name ? strdup(tcancel->name) : strdup("tool");
                    cJSON *err = cJSON_CreateObject();
                    cJSON_AddStringToObject(err, "error", "Tool execution cancelled before start");
                    slot->tool_output = err;
                    slot->is_error = 1;
                }
                break;  // Stop launching new tools
            }

            ToolCall *tool = &tool_calls_array[i];
            InternalContent *result_slot = &results[i];
            result_slot->type = INTERNAL_TOOL_RESPONSE;

            if (!tool->name || !tool->id) {
                LOG_ERROR("Tool call missing name or id (provider validation failed)");
                result_slot->tool_id = tool->id ? strdup(tool->id) : strdup("unknown");
                result_slot->tool_name = tool->name ? strdup(tool->name) : strdup("tool");
                cJSON *error = cJSON_CreateObject();
                cJSON_AddStringToObject(error, "error", "Tool call missing name or id");
                result_slot->tool_output = error;
                result_slot->is_error = 1;
                continue;
            }

            // Validate that the tool is in the allowed tools list (prevent hallucination)
            if (!is_tool_allowed(tool->name, state)) {
                LOG_ERROR("Tool validation failed: '%s' was not provided in tools list (model hallucination)", tool->name);
                result_slot->tool_id = strdup(tool->id);
                result_slot->tool_name = strdup(tool->name);
                cJSON *error = cJSON_CreateObject();
                char error_msg[512];

                // Check if this is a plan mode restriction
                int is_plan_mode_restriction = state->plan_mode &&
                    (strcmp(tool->name, "Bash") == 0 ||
                     strcmp(tool->name, "Subagent") == 0 ||
                     strcmp(tool->name, "Write") == 0 ||
                     strcmp(tool->name, "Edit") == 0);

                if (is_plan_mode_restriction) {
                    snprintf(error_msg, sizeof(error_msg),
                             "ERROR: Tool '%s' is not available in planning mode. "
                             "You are currently in PLANNING MODE which only allows read-only tools: "
                             "Read, Glob, Grep, Sleep, UploadImage, TodoWrite, ListMcpResources, ReadMcpResource. "
                             "Please reformulate your request using only these available tools.",
                             tool->name);
                } else {
                    snprintf(error_msg, sizeof(error_msg),
                             "ERROR: Tool '%s' does not exist or was not provided to you. "
                             "Please check the list of available tools and try again with a valid tool name.",
                             tool->name);
                }

                cJSON_AddStringToObject(error, "error", error_msg);
                result_slot->tool_output = error;
                result_slot->is_error = 1;

                // Display error to user
                char prefix_with_tool[128];
                snprintf(prefix_with_tool, sizeof(prefix_with_tool), "[%s]", tool->name);
                char display_error[512];
                if (is_plan_mode_restriction) {
                    snprintf(display_error, sizeof(display_error),
                             "Error: Tool '%s' not available in planning mode (read-only mode)",
                             tool->name);
                } else {
                    snprintf(display_error, sizeof(display_error),
                             "Error: Tool '%s' not available (not in provided tools list)",
                             tool->name);
                }
                ui_append_line(tui, queue, prefix_with_tool, display_error, COLOR_PAIR_ERROR);
                continue;
            }

            cJSON *input = tool->parameters
                ? cJSON_Duplicate(tool->parameters, /*recurse*/1)
                : cJSON_CreateObject();

            char *tool_details = get_tool_details(tool->name, input);
            char prefix_with_tool[128];
            snprintf(prefix_with_tool, sizeof(prefix_with_tool), "[%s]", tool->name);
            ui_append_line(tui, queue, prefix_with_tool, tool_details, COLOR_PAIR_TOOL);

            if (!tracker_initialized) {
                cJSON *error = cJSON_CreateObject();
                cJSON_AddStringToObject(error, "error", "Internal error initializing tool tracker");
                result_slot->tool_id = strdup(tool->id);
                result_slot->tool_name = strdup(tool->name);
                result_slot->tool_output = error;
                result_slot->is_error = 1;
                cJSON_Delete(input);
                continue;
            }

            ToolThreadArg *current = &args[started_threads];
            current->tool_use_id = strdup(tool->id);
            current->tool_name = tool->name;
            current->input = input;
            current->state = state;
            current->result_block = result_slot;
            current->tracker = &tracker;
            current->notified = 0;
            current->queue = queue;

            int rc = pthread_create(&threads[started_threads], NULL, tool_thread_func, current);
            if (rc != 0) {
                LOG_ERROR("Failed to create tool thread for %s (rc=%d)", tool->name, rc);

                // CRITICAL FIX: Cancel already-started threads on failure
                // This prevents zombie threads if we fail mid-creation
                for (int cancel_idx = 0; cancel_idx < started_threads; cancel_idx++) {
                    pthread_cancel(threads[cancel_idx]);
                }
                // Threads will be joined later in the cleanup path

                cJSON_Delete(input);
                current->input = NULL;

                result_slot->tool_id = current->tool_use_id;
                result_slot->tool_name = strdup(tool->name);
                cJSON *error = cJSON_CreateObject();
                cJSON_AddStringToObject(error, "error", "Failed to start tool thread");
                result_slot->tool_output = error;
                result_slot->is_error = 1;
                tool_tracker_notify_completion(current);
                current->tool_use_id = NULL;
                continue;
            }

            started_threads++;
        }

        if (tracker_initialized && started_threads > 0) {
            while (1) {
                // Check for interrupt request
                if (state->interrupt_requested) {
                    LOG_INFO("Tool execution interrupted by user request");
                    interrupted = 1;

                    // CRITICAL FIX: Actually cancel the threads, not just the tracker
                    // Setting tracker.cancelled alone doesn't stop running threads
                    pthread_mutex_lock(&tracker.mutex);
                    tracker.cancelled = 1;
                    pthread_cond_broadcast(&tracker.cond);
                    pthread_mutex_unlock(&tracker.mutex);

                    // Cancel all running threads - this triggers cleanup handlers
                    for (int t = 0; t < started_threads; t++) {
                        pthread_cancel(threads[t]);
                    }
                    // Reset interrupt flag immediately after cancelling threads
                    state->interrupt_requested = 0;
                    break;
                }

                pthread_mutex_lock(&tracker.mutex);
                if (tracker.cancelled || tracker.completed >= tracker.total) {
                    pthread_mutex_unlock(&tracker.mutex);
                    break;
                }

                struct timespec deadline;
                clock_gettime(CLOCK_REALTIME, &deadline);
                deadline.tv_nsec += 100000000L;
                if (deadline.tv_nsec >= 1000000000L) {
                    deadline.tv_sec += 1;
                    deadline.tv_nsec -= 1000000000L;
                }

                // Wait for condition variable with timeout
                (void)pthread_cond_timedwait(&tracker.cond, &tracker.mutex, &deadline);
                if (tracker.cancelled || tracker.completed >= tracker.total) {
                    pthread_mutex_unlock(&tracker.mutex);
                    break;
                }
                pthread_mutex_unlock(&tracker.mutex);

                // Interactive interrupt handling (Ctrl+C) is done by the TUI event loop
                // Non-TUI mode doesn't have interactive interrupt support here
            }
        }

        for (int t = 0; t < started_threads; t++) {
            pthread_join(threads[t], NULL);
        }

        clock_gettime(CLOCK_MONOTONIC, &tool_end);
        long tool_exec_ms = (tool_end.tv_sec - tool_start.tv_sec) * 1000 +
                            (tool_end.tv_nsec - tool_start.tv_nsec) / 1000000;
        LOG_INFO("All %d tool(s) processed in %ld ms", started_threads, tool_exec_ms);

        if (tracker_initialized) {
            tool_tracker_destroy(&tracker);
        }

        // Log summary of all tool results
        LOG_DEBUG("Tool execution summary: %d results collected", tool_count);
        for (int i = 0; i < tool_count; i++) {
            LOG_DEBUG("Result[%d]: tool_id=%s, tool_name=%s, is_error=%d",
                      i, results[i].tool_id ? results[i].tool_id : "NULL",
                      results[i].tool_name ? results[i].tool_name : "NULL",
                      results[i].is_error);
        }

        int has_error = 0;
        for (int i = 0; i < tool_count; i++) {
            if (results[i].is_error) {
                has_error = 1;

                cJSON *error_obj = results[i].tool_output
                    ? cJSON_GetObjectItem(results[i].tool_output, "error")
                    : NULL;
                const char *error_msg = (error_obj && cJSON_IsString(error_obj))
                    ? error_obj->valuestring
                    : "Unknown error";
                const char *tool_name = results[i].tool_name ? results[i].tool_name : "tool";

                char error_display[512];
                snprintf(error_display, sizeof(error_display), "%s failed: %s", tool_name, error_msg);
                ui_show_error(tui, queue, error_display);
            }
        }

        // If interrupted at any point, ensure UI reflects it but continue to add
        // tool_result messages so the conversation stays consistent.
        if (interrupted) {
            if (!tui && !queue) {
                if (tool_spinner) {
                    spinner_stop(tool_spinner, "Interrupted by user (Ctrl+C) - tools terminated", 0);
                }
            } else {
                ui_set_status(tui, queue, "Interrupted by user (Ctrl+C) - tools terminated");
            }
            // Reset interrupt flag after tool execution is interrupted
            state->interrupt_requested = 0;
        }

        if (!tui && !queue) {
            if (tool_spinner) {
                if (has_error) {
                    spinner_stop(tool_spinner, "Tool execution completed with errors", 0);
                } else {
                    spinner_stop(tool_spinner, "Tool execution completed successfully", 1);
                }
            }
        } else {
            if (has_error) {
                ui_set_status(tui, queue, "Tool execution completed with errors");
            } else {
                ui_set_status(tui, queue, "");
            }
        }

        free(threads);
        free(args);

        // Extract TodoWrite information BEFORE transferring ownership to add_tool_results
        int todo_write_executed = check_todo_write_executed(results, tool_count);

        // Record tool results even in the interrupt path so that every tool_call
        // has a corresponding tool_result. This prevents 400s due to missing results.
        if (add_tool_results(state, results, tool_count) != 0) {
            LOG_ERROR("Failed to add tool results to conversation state");
            // Results were already freed by add_tool_results
            results = NULL;
        }

        if (todo_write_executed && state->todo_list && state->todo_list->count > 0) {
            // For TUI without queue, use colored rendering
            if (tui && !queue) {
                tui_render_todo_list(tui, state->todo_list);
            } else {
                // For queue or non-TUI, use plain text rendering
                char *todo_text = queue ? todo_render_to_string_plain(state->todo_list)
                                        : todo_render_to_string(state->todo_list);
                if (todo_text) {
                    ui_append_line(tui, queue, "[Assistant]", todo_text, COLOR_PAIR_ASSISTANT);
                    free(todo_text);
                }
            }
        }

        // Render active subagents after tool execution (if any are running)
        if (tui && state->subagent_manager) {
            int running_count = subagent_manager_get_running_count(state->subagent_manager);
            if (running_count > 0) {
                tui_render_active_subagents(tui);
            }
        }

        ApiResponse *next_response = NULL;
        if (!interrupted) {
            Spinner *followup_spinner = NULL;
            if (!tui && !queue) {
                // Use the same color as other status messages to reduce color variance
                followup_spinner = spinner_start("Processing tool results...", SPINNER_YELLOW);
            } else {
                ui_set_status(tui, queue, "Processing tool results...");
            }
            next_response = call_api(state);
            if (!tui && !queue) {
                spinner_stop(followup_spinner, NULL, 1);
            } else {
                ui_set_status(tui, queue, "");
            }
        }

        if (next_response) {
            process_response(state, next_response, tui, queue, worker_ctx);
            api_response_free(next_response);
        } else if (state->interrupt_requested) {
            // User interrupted the tool results processing
            LOG_INFO("Tool results processing interrupted by user");
            state->interrupt_requested = 0;  // Clear for next operation
            return;  // Exit gracefully without error
        } else if (!interrupted) {
            const char *error_msg = "API call failed after executing tools. Check logs for details.";
            ui_show_error(tui, queue, error_msg);
            LOG_ERROR("API call returned NULL after tool execution");
        }

        clock_gettime(CLOCK_MONOTONIC, &proc_end);
        long proc_ms = (proc_end.tv_sec - proc_start.tv_sec) * 1000 +
                       (proc_end.tv_nsec - proc_start.tv_nsec) / 1000000;
        LOG_INFO("Response processing completed in %ld ms (tools: %ld ms, recursion included)",
                 proc_ms, tool_exec_ms);
        return;
    }

    // No tools - just log completion time
    clock_gettime(CLOCK_MONOTONIC, &proc_end);
    long proc_ms = (proc_end.tv_sec - proc_start.tv_sec) * 1000 +
                   (proc_end.tv_nsec - proc_start.tv_nsec) / 1000000;
    LOG_INFO("Response processing completed in %ld ms (no tools)", proc_ms);
}

static void ai_worker_handle_instruction(AIWorkerContext *ctx, const AIInstruction *instruction) {
    if (!ctx || !instruction) {
        return;
    }

    ui_set_status(NULL, ctx->tui_queue, "Waiting for API response...");

    ApiResponse *response = call_api(ctx->state);

    ui_set_status(NULL, ctx->tui_queue, "");

    if (!response) {
        ui_show_error(NULL, ctx->tui_queue, "Failed to get response from API");
        return;
    }

    // Check if response contains an error message
    if (response->error_message) {
        ui_show_error(NULL, ctx->tui_queue, response->error_message);
        api_response_free(response);
        return;
    }

    cJSON *error = cJSON_GetObjectItem(response->raw_response, "error");
    if (error) {
        cJSON *error_message = cJSON_GetObjectItem(error, "message");
        const char *error_msg = error_message ? error_message->valuestring : "Unknown error";
        ui_show_error(NULL, ctx->tui_queue, error_msg);
        api_response_free(response);
        return;
    }



    process_response(ctx->state, response, NULL, ctx->tui_queue, ctx);
    api_response_free(response);
}

// ============================================================================
// Advanced Input Handler (readline-like)
// ============================================================================













// Socket IPC context
typedef struct {
    int server_fd;           // Listening socket file descriptor
    int client_fd;           // Connected client file descriptor (-1 if none)
    char *socket_path;       // Path to Unix domain socket
    int enabled;             // Whether socket IPC is enabled
} SocketIPC;

typedef struct {
    ConversationState *state;
    TUIState *tui;
    AIWorkerContext *worker;
    AIInstructionQueue *instruction_queue;
    TUIMessageQueue *tui_queue;
    int instruction_queue_capacity;
    SocketIPC socket_ipc;    // Socket IPC support
} InteractiveContext;

// Interrupt callback invoked by the TUI event loop when the user presses Ctrl+C in INSERT mode
static int interrupt_callback(void *user_data) {
    InteractiveContext *ctx = (InteractiveContext *)user_data;
    if (!ctx || !ctx->state) {
        return 0;
    }

    ConversationState *state = ctx->state;
    TUIMessageQueue *queue = ctx->tui_queue;
    AIInstructionQueue *instr_queue = ctx->instruction_queue;

    // Check if there's work in progress
    int queue_depth = instr_queue ? ai_queue_depth(instr_queue) : 0;
    int work_in_progress = (queue_depth > 0);

    // Debug log the queue depth
    LOG_DEBUG("interrupt_callback: queue_depth=%d, work_in_progress=%d", queue_depth, work_in_progress);

    // Ctrl+C always sets the interrupt flag to cancel any ongoing operations
    // It never exits the application - use Ctrl+D or :q/:quit command to exit
    // Always set the interrupt flag regardless of queue state
    state->interrupt_requested = 1;

    if (work_in_progress) {
        // There's work in the queue - inform user we're canceling
        LOG_INFO("User requested interrupt (Ctrl+C pressed) - canceling ongoing operations");
        ui_set_status(NULL, queue, "Interrupt requested - canceling operations...");
    } else {
        // No work in queue, but interrupt flag is set for any ongoing operations
        LOG_INFO("User pressed Ctrl+C - interrupt flag set for any ongoing operations");
        ui_set_status(NULL, queue, "Interruptted");
    }

    return 0;  // Always continue running (never exit on Ctrl+C)
}

// Submit callback invoked by the TUI event loop when the user presses Enter
static int submit_input_callback(const char *input, void *user_data) {
    InteractiveContext *ctx = (InteractiveContext *)user_data;
    if (!ctx || !ctx->state || !ctx->tui || !input) {
        return 0;
    }

    if (input[0] == '\0') {
        return 0;
    }

    TUIState *tui = ctx->tui;
    ConversationState *state = ctx->state;
    AIWorkerContext *worker = ctx->worker;
    TUIMessageQueue *queue = ctx->tui_queue;

    // Reset interrupt flag when new input is submitted
    state->interrupt_requested = 0;

    char *input_copy = strdup(input);
    if (!input_copy) {
        ui_show_error(tui, queue, "Memory allocation failed");
        return 0;
    }

    if (input_copy[0] == '/') {
        ui_append_line(tui, queue, "[User]", input_copy, COLOR_PAIR_USER);

        // Remember message count before command execution
        int msg_count_before = state->count;

        // Use the command system from commands.c
        const Command *executed_cmd = NULL;

        // Extract command name to check if it needs terminal interaction
        const char *cmd_name = input_copy + 1; // Skip '/'
        const char *space = strchr(cmd_name, ' ');
        size_t cmd_len = space ? (size_t)(space - cmd_name) : strlen(cmd_name);

        // Look up command to check if it needs terminal interaction
        char cmd_name_buf[64];
        if (cmd_len >= sizeof(cmd_name_buf) - 1) {
            cmd_len = sizeof(cmd_name_buf) - 1;
        }
        memcpy(cmd_name_buf, cmd_name, cmd_len);
        cmd_name_buf[cmd_len] = '\0';

        const Command *cmd = commands_lookup(cmd_name_buf);
        int needs_terminal = (cmd && cmd->needs_terminal);

        int cmd_result;

        if (needs_terminal) {
            // For commands that need terminal interaction (like /voice),
            // suspend the TUI to restore normal terminal mode
            if (tui_suspend(tui) != 0) {
                ui_show_error(tui, queue, "Failed to suspend TUI for command");
                free(input_copy);
                return 0;
            }

            // Execute command with normal terminal
            cmd_result = commands_execute(state, input_copy, &executed_cmd);

            // Resume TUI
            if (tui_resume(tui) != 0) {
                ui_show_error(tui, queue, "Failed to resume TUI after command");
                // Continue anyway
            }
        } else {
            // For regular commands, redirect stdout/stderr to /dev/null
            // to prevent TUI corruption
            // Commands use print_error() and printf() which interfere with ncurses
            int saved_stdout = dup(STDOUT_FILENO);
            int saved_stderr = dup(STDERR_FILENO);
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull != -1) {
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }

            cmd_result = commands_execute(state, input_copy, &executed_cmd);

            // Restore stdout/stderr
            if (saved_stdout != -1) {
                dup2(saved_stdout, STDOUT_FILENO);
                close(saved_stdout);
            }
            if (saved_stderr != -1) {
                dup2(saved_stderr, STDERR_FILENO);
                close(saved_stderr);
            }
        }

        // Check if it's an exit command
        if (cmd_result == -2) {
            free(input_copy);
            return 1;  // Exit the program
        }

        // Check if command failed (unknown command or error)
        if (cmd_result == -1) {
            // Extract command name for error message
            const char *err_cmd_line = input_copy + 1;
            const char *err_space = strchr(err_cmd_line, ' ');
            size_t err_cmd_len = err_space ? (size_t)(err_space - err_cmd_line) : strlen(err_cmd_line);

            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg),
                     "Unknown command: /%.*s (type /help for available commands)",
                     (int)err_cmd_len, err_cmd_line);
            ui_show_error(tui, queue, error_msg);
            free(input_copy);
            return 0;
        }

        // For /clear, also clear the TUI
        if (strncmp(input_copy, "/clear", 6) == 0) {
            tui_clear_conversation(tui);
        }

        // For /add-dir, rebuild system prompt
        if (strncmp(input_copy, "/add-dir ", 9) == 0 && cmd_result == 0) {
            char *new_system_prompt = build_system_prompt(state);
            if (new_system_prompt) {
                if (state->count > 0 && state->messages[0].role == MSG_SYSTEM) {
                    free(state->messages[0].contents[0].text);
                    state->messages[0].contents[0].text = strdup(new_system_prompt);
                    if (!state->messages[0].contents[0].text) {
                        ui_show_error(tui, queue, "Memory allocation failed");
                    }
                }
                free(new_system_prompt);
            } else {
                ui_show_error(tui, queue, "Failed to rebuild system prompt");
            }
        }

        // Check if command added new messages (e.g., /voice adds transcription)
        if (cmd_result == 0 && state->count > msg_count_before) {
            // Display any new user messages that were added
            for (int i = msg_count_before; i < state->count; i++) {
                if (state->messages[i].role == MSG_USER) {
                    // Get the text from the first text content
                    for (int j = 0; j < state->messages[i].content_count; j++) {
                        // Compare against InternalContentType to avoid enum mismatch
                        if (state->messages[i].contents[j].type == INTERNAL_TEXT) {
                            ui_append_line(tui, queue, "[Transcription]",
                                         state->messages[i].contents[j].text,
                                         COLOR_PAIR_USER);
                            break;
                        }
                    }
                }
            }
        }

        free(input_copy);
        return 0;
    }

    ui_append_line(tui, queue, "[User]", input_copy, COLOR_PAIR_USER);
    add_user_message(state, input_copy);

    if (worker) {
        if (ai_worker_submit(worker, input_copy) != 0) {
            ui_show_error(tui, queue, "Failed to queue instruction for processing");
        } else {
            if (ctx->instruction_queue) {
                int depth = ai_queue_depth(ctx->instruction_queue);
                if (depth > 0) {
                    char status[128];
                    if (ctx->instruction_queue_capacity > 0) {
                        snprintf(status, sizeof(status),
                                 "Instruction queued (%d/%d pending)",
                                 depth,
                                 ctx->instruction_queue_capacity);
                    } else {
                        snprintf(status, sizeof(status),
                                 "Instruction queued (%d pending)", depth);
                    }
                    ui_set_status(tui, queue, status);
                } else {
                    ui_set_status(tui, queue, "Instruction submitted (processing...)");
                }
            } else {
                ui_set_status(tui, queue, "Instruction queued for processing...");
            }
        }
    } else {
        ui_set_status(tui, queue, "Waiting for API response...");
        ApiResponse *response = call_api(state);
        ui_set_status(tui, queue, "");

        if (!response) {
            ui_show_error(tui, queue, "Failed to get response from API");
            free(input_copy);
            return 0;
        }

        // Check if response contains an error message
        if (response->error_message) {
            ui_show_error(tui, queue, response->error_message);
            api_response_free(response);
            free(input_copy);
            return 0;
        }

        cJSON *error = cJSON_GetObjectItem(response->raw_response, "error");
        if (error) {
            cJSON *error_message = cJSON_GetObjectItem(error, "message");
            const char *error_msg = error_message ? error_message->valuestring : "Unknown error";
            ui_show_error(tui, queue, error_msg);
            api_response_free(response);
            free(input_copy);
            return 0;
        }

        process_response(state, response, tui, queue, NULL);
        api_response_free(response);
    }

    free(input_copy);
    return 0;
}

// Execute a single command and exit
// Forward declaration for recursion
static int process_single_command_response(ConversationState *state, ApiResponse *response);

/**
 * Print token usage statistics for the current session
 * This is called at the end of single command mode (including subagent)
 */
static void print_token_usage(ConversationState *state) {
    if (!state || !state->persistence_db) {
        return;
    }

    int prompt_tokens = 0;
    int completion_tokens = 0;
    int cached_tokens = 0;

    // Get token usage for this session
    int result = persistence_get_session_token_usage(
        state->persistence_db,
        state->session_id,
        &prompt_tokens,
        &completion_tokens,
        &cached_tokens
    );

    if (result == 0) {
        // Calculate total tokens (excluding cached tokens since they're free)
        int total_tokens = prompt_tokens + completion_tokens;

        // Print token usage summary
        fprintf(stderr, "\n=== Token Usage Summary ===\n");
        fprintf(stderr, "Session: %s\n", state->session_id ? state->session_id : "unknown");
        fprintf(stderr, "Prompt tokens: %d\n", prompt_tokens);
        fprintf(stderr, "Completion tokens: %d\n", completion_tokens);
        if (cached_tokens > 0) {
            fprintf(stderr, "Cached tokens (free): %d\n", cached_tokens);
            fprintf(stderr, "Total billed tokens: %d (excluding %d cached)\n",
                    total_tokens, cached_tokens);
        } else {
            fprintf(stderr, "Total tokens: %d\n", total_tokens);
        }
        fprintf(stderr, "===========================\n");
    } else {
        fprintf(stderr, "\nNote: Token usage statistics unavailable\n");
    }
}

static int single_command_mode(ConversationState *state, const char *prompt) {
    LOG_INFO("Executing single command: %s", prompt);

    // Enable oneshot/subagent mode for structured tool output
    g_oneshot_mode = 1;

    // Add user message to conversation
    add_user_message(state, prompt);

    // Call API synchronously
    ApiResponse *response = call_api(state);
    if (!response) {
        LOG_ERROR("Failed to get response from API");
        fprintf(stderr, "Error: Failed to get response from API\n");
        return 1;
    }

    // Check if response contains an error message
    if (response->error_message) {
        LOG_ERROR("API error: %s", response->error_message);
        fprintf(stderr, "Error: %s\n", response->error_message);
        api_response_free(response);
        return 1;
    }

    cJSON *error = cJSON_GetObjectItem(response->raw_response, "error");
    if (error) {
        cJSON *error_message = cJSON_GetObjectItem(error, "message");
        const char *error_msg = error_message ? error_message->valuestring : "Unknown error";
        LOG_ERROR("API error: %s", error_msg);
        fprintf(stderr, "Error: %s\n", error_msg);
        api_response_free(response);
        return 1;
    }

    // Process response recursively (handles tool calls and follow-up responses)
    int result = process_single_command_response(state, response);
    api_response_free(response);

    // Print token usage summary at the end of single command mode
    // This includes subagent executions
    print_token_usage(state);

    return result;
}

/**
 * Process a single API response in single command mode
 * Recursively handles tool calls and follow-up responses
 * Returns: 0 on success, 1 on error
 */
static int process_single_command_response(ConversationState *state, ApiResponse *response) {
    // Print assistant's text content if present
    if (response->message.text && response->message.text[0] != '\0') {
        // Skip whitespace-only content
        const char *p = response->message.text;
        while (*p && isspace((unsigned char)*p)) p++;

        if (*p != '\0') {  // Has non-whitespace content
            printf("%s\n", p);
        }
    }

    // Add to conversation history
    cJSON *choices = cJSON_GetObjectItem(response->raw_response, "choices");
    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *choice = cJSON_GetArrayItem(choices, 0);
        cJSON *message = cJSON_GetObjectItem(choice, "message");
        if (message) {
            add_assistant_message_openai(state, message);
        }
    }

    // Process tool calls
    int tool_count = response->tool_count;
    ToolCall *tool_calls_array = response->tools;

    if (tool_count > 0) {
        LOG_INFO("Processing %d tool call(s) in single-command mode", tool_count);

        // Log details of each tool call
        for (int i = 0; i < tool_count; i++) {
            ToolCall *tool = &tool_calls_array[i];
            LOG_DEBUG("Tool call[%d]: id=%s, name=%s, has_params=%d",
                      i, tool->id ? tool->id : "NULL",
                      tool->name ? tool->name : "NULL",
                      tool->parameters != NULL);
        }

        InternalContent *results = calloc((size_t)tool_count, sizeof(InternalContent));
        if (!results) {
            LOG_ERROR("Failed to allocate tool result buffer");
            return 1;
        }

        int valid_tool_calls = 0;
        for (int i = 0; i < tool_count; i++) {
            ToolCall *tool = &tool_calls_array[i];
            if (tool->name && tool->id) {
                valid_tool_calls++;
            }
        }

        if (valid_tool_calls > 0) {
            // Execute tools (oneshot/subagent mode)
            for (int i = 0; i < tool_count; i++) {
                ToolCall *tool = &tool_calls_array[i];
                if (!tool->name || !tool->id) {
                    continue;
                }

                LOG_DEBUG("Executing tool: %s", tool->name);

                // Convert ToolCall to execute_tool parameters
                cJSON *input = tool->parameters
                    ? cJSON_Duplicate(tool->parameters, /*recurse*/1)
                    : cJSON_CreateObject();

                // Print tool name header with details
                char *tool_details = get_tool_details(tool->name, input);

                // Add timestamp to tool details
                char details_with_timestamp[384]; // 256 for details + 128 for timestamp
                if (tool_details && strlen(tool_details) > 0) {
                    char timestamp[32]; // Increased for YYYY-MM-DD HH:MM:SS format
                    get_current_timestamp(timestamp, sizeof(timestamp));
                    snprintf(details_with_timestamp, sizeof(details_with_timestamp),
                             "%s (%s)", tool_details, timestamp);
                } else {
                    char timestamp[32]; // Increased for YYYY-MM-DD HH:MM:SS format
                    get_current_timestamp(timestamp, sizeof(timestamp));
                    snprintf(details_with_timestamp, sizeof(details_with_timestamp),
                             "(%s)", timestamp);
                }

                // Print opening HTML-style tag with tool name and optional details
                printf("<tool name=\"%s\"", tool->name);
                if (strlen(details_with_timestamp) > 0) {
                    // Escape quotes and ampersands in tool details for XML attribute
                    // Worst case: every character could be & or " requiring 6 chars each
                    size_t max_escaped_len = strlen(details_with_timestamp) * 6 + 1;
                    char *escaped_details = malloc(max_escaped_len);
                    if (escaped_details) {
                        size_t j = 0;
                        for (size_t k = 0; details_with_timestamp[k]; k++) {
                            if (details_with_timestamp[k] == '"') {
                                escaped_details[j++] = '&';
                                escaped_details[j++] = 'q';
                                escaped_details[j++] = 'u';
                                escaped_details[j++] = 'o';
                                escaped_details[j++] = 't';
                                escaped_details[j++] = ';';
                            } else if (details_with_timestamp[k] == '&') {
                                escaped_details[j++] = '&';
                                escaped_details[j++] = 'a';
                                escaped_details[j++] = 'm';
                                escaped_details[j++] = 'p';
                                escaped_details[j++] = ';';
                            } else if (details_with_timestamp[k] == '<') {
                                escaped_details[j++] = '&';
                                escaped_details[j++] = 'l';
                                escaped_details[j++] = 't';
                                escaped_details[j++] = ';';
                            } else if (details_with_timestamp[k] == '>') {
                                escaped_details[j++] = '&';
                                escaped_details[j++] = 'g';
                                escaped_details[j++] = 't';
                                escaped_details[j++] = ';';
                            } else {
                                escaped_details[j++] = details_with_timestamp[k];
                            }
                        }
                        escaped_details[j] = '\0';
                        printf(" details=\"%s\"", escaped_details);
                        free(escaped_details);
                    }
                }
                printf(">\n");
                fflush(stdout);

                // Execute tool synchronously
                cJSON *tool_result = execute_tool(tool->name, input, state);

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

                // Convert result to InternalContent
                results[i].type = INTERNAL_TOOL_RESPONSE;
                results[i].tool_id = strdup(tool->id);
                results[i].tool_name = strdup(tool->name);
                results[i].tool_output = tool_result;
                results[i].is_error = tool_result ? cJSON_HasObjectItem(tool_result, "error") : 1;

                // Tool result is logged via LOG_DEBUG in execute_tool

                cJSON_Delete(input);
            }

            // Log summary of all tool results before adding to conversation
            LOG_DEBUG("Single-command mode: Collected %d tool results", tool_count);
            for (int i = 0; i < tool_count; i++) {
                LOG_DEBUG("Result[%d]: tool_id=%s, tool_name=%s, is_error=%d",
                          i, results[i].tool_id ? results[i].tool_id : "NULL",
                          results[i].tool_name ? results[i].tool_name : "NULL",
                          results[i].is_error);
            }

            // Add tool results to conversation
            // Note: add_tool_results takes ownership of the results array and its contents
            if (add_tool_results(state, results, tool_count) != 0) {
                LOG_ERROR("Failed to add tool results to conversation - cannot proceed");
                // Results were already freed by add_tool_results, don't free again
                return 1;
            }

            // Call API again with tool results and process recursively
            ApiResponse *next_response = call_api(state);
            if (next_response) {
                // Recursively process the next response (may contain more tool calls)
                int result = process_single_command_response(state, next_response);
                api_response_free(next_response);
                return result;
            } else {
                LOG_ERROR("Failed to get response after tool execution");
                fprintf(stderr, "Error: Failed to get response after tool execution\n");
                return 1;
            }

            // Do NOT free results here - add_tool_results() took ownership
        } else {
            // No valid tool calls, free the allocated results array
            free(results);
        }
    }

    // No tool calls - conversation is complete
    return 0;
}

// ============================================================================
// Socket IPC Functions
// ============================================================================

// Create and bind Unix domain socket
static int create_unix_socket(const char *socket_path) {
    LOG_DEBUG("create_unix_socket: Starting socket creation for path: %s", socket_path);
    
    // Remove existing socket file if it exists
    LOG_DEBUG("create_unix_socket: Removing existing socket file (if any)");
    unlink(socket_path);

    // Create socket
    LOG_DEBUG("create_unix_socket: Creating AF_UNIX socket with SOCK_STREAM");
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        LOG_ERROR("Failed to create socket: %s", strerror(errno));
        return -1;
    }
    LOG_DEBUG("create_unix_socket: Socket created successfully, fd: %d", server_fd);

    // Set socket to non-blocking
    LOG_DEBUG("create_unix_socket: Setting socket to non-blocking mode");
    int flags = fcntl(server_fd, F_GETFL, 0);
    if (flags < 0) {
        LOG_ERROR("Failed to get socket flags: %s", strerror(errno));
        close(server_fd);
        return -1;
    }
    if (fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        LOG_ERROR("Failed to set socket non-blocking: %s", strerror(errno));
        close(server_fd);
        return -1;
    }
    LOG_DEBUG("create_unix_socket: Socket set to non-blocking mode");

    // Bind socket
    LOG_DEBUG("create_unix_socket: Binding socket to path: %s", socket_path);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strlcpy(addr.sun_path, socket_path, sizeof(addr.sun_path));

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("Failed to bind socket: %s", strerror(errno));
        close(server_fd);
        return -1;
    }
    LOG_DEBUG("create_unix_socket: Socket bound successfully");

    // Listen for connections
    LOG_DEBUG("create_unix_socket: Listening for connections (backlog: 1)");
    if (listen(server_fd, 1) < 0) {
        LOG_ERROR("Failed to listen on socket: %s", strerror(errno));
        close(server_fd);
        unlink(socket_path);
        return -1;
    }

    LOG_INFO("Socket created and listening on: %s", socket_path);
    LOG_DEBUG("create_unix_socket: Socket setup complete, returning fd: %d", server_fd);
    return server_fd;
}

// Accept incoming connection
static int accept_socket_connection(int server_fd) {
    LOG_DEBUG("accept_socket_connection: Attempting to accept connection on server fd: %d", server_fd);
    
    struct sockaddr_un addr;
    socklen_t addr_len = sizeof(addr);

    int client_fd = accept(server_fd, (struct sockaddr*)&addr, &addr_len);
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_ERROR("Failed to accept connection: %s", strerror(errno));
        } else {
            LOG_DEBUG("accept_socket_connection: No pending connections (EAGAIN/EWOULDBLOCK)");
        }
        return -1;
    }

    LOG_DEBUG("accept_socket_connection: Connection accepted, client fd: %d", client_fd);

    // Set client socket to non-blocking
    LOG_DEBUG("accept_socket_connection: Setting client socket to non-blocking mode");
    int flags = fcntl(client_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
        LOG_DEBUG("accept_socket_connection: Client socket set to non-blocking");
    } else {
        LOG_WARN("accept_socket_connection: Failed to get client socket flags, continuing anyway");
    }

    LOG_INFO("Accepted socket connection (client fd: %d)", client_fd);
    LOG_DEBUG("accept_socket_connection: Returning client fd: %d", client_fd);
    return client_fd;
}

// Read input from socket
static int read_socket_input(int client_fd, char *buffer, size_t buffer_size) {
    LOG_DEBUG("read_socket_input: Attempting to read from client fd: %d, buffer size: %zu", 
              client_fd, buffer_size);
    
    ssize_t bytes_read = read(client_fd, buffer, buffer_size - 1);
    if (bytes_read < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_ERROR("Failed to read from socket: %s", strerror(errno));
            LOG_DEBUG("read_socket_input: Read error, returning -1");
            return -1;
        }
        LOG_DEBUG("read_socket_input: No data available (EAGAIN/EWOULDBLOCK), returning 0");
        return 0; // No data available
    } else if (bytes_read == 0) {
        LOG_INFO("Socket client disconnected (fd: %d)", client_fd);
        LOG_DEBUG("read_socket_input: Client disconnected, returning -1");
        return -1; // Client disconnected
    }

    buffer[bytes_read] = '\0';
    LOG_DEBUG("read_socket_input: Read %zd bytes from socket fd: %d", bytes_read, client_fd);
    LOG_DEBUG("read_socket_input: Data: \"%.*s\"", (int)bytes_read > 50 ? 50 : (int)bytes_read, buffer);
    return (int)bytes_read;
}

// Write output to socket
static int write_socket_output(int client_fd, const char *data, size_t data_len) {
    LOG_DEBUG("write_socket_output: Attempting to write %zu bytes to client fd: %d", data_len, client_fd);
    
    if (client_fd < 0) {
        LOG_DEBUG("write_socket_output: Invalid client fd, returning -1");
        return -1;
    }
    
    ssize_t bytes_written = write(client_fd, data, data_len);
    if (bytes_written < 0) {
        LOG_ERROR("Failed to write to socket: %s", strerror(errno));
        LOG_DEBUG("write_socket_output: Write error, returning -1");
        return -1;
    }
    
    LOG_DEBUG("write_socket_output: Wrote %zd bytes to socket fd: %d", bytes_written, client_fd);
    return (int)bytes_written;
}

// Check if socket has data available
static int socket_has_data(int fd) {
    LOG_DEBUG("socket_has_data: Checking if fd %d has data", fd);
    struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
    int result = poll(&pfd, 1, 0); // Non-blocking poll
    int has_data = result > 0 && (pfd.revents & POLLIN);
    LOG_DEBUG("socket_has_data: poll result: %d, revents: 0x%x, has_data: %d", 
              result, pfd.revents, has_data);
    return has_data;
}

// External input callback for socket IPC
static int socket_external_input_callback(void *user_data, char *buffer, int buffer_size) {
    LOG_DEBUG("socket_external_input_callback: Called with buffer_size: %d", buffer_size);
    
    InteractiveContext *ctx = (InteractiveContext *)user_data;
    if (!ctx) {
        LOG_DEBUG("socket_external_input_callback: ctx is NULL, returning 0");
        return 0;
    }
    
    if (!ctx->socket_ipc.enabled) {
        LOG_DEBUG("socket_external_input_callback: Socket IPC not enabled, returning 0");
        return 0;
    }

    SocketIPC *socket_ipc = &ctx->socket_ipc;
    LOG_DEBUG("socket_external_input_callback: Socket state - server_fd: %d, client_fd: %d, path: %s",
              socket_ipc->server_fd, socket_ipc->client_fd,
              socket_ipc->socket_path ? socket_ipc->socket_path : "(null)");

    // Accept new connection if none
    if (socket_ipc->client_fd < 0) {
        LOG_DEBUG("socket_external_input_callback: No client connected, attempting to accept new connection");
        socket_ipc->client_fd = accept_socket_connection(socket_ipc->server_fd);
        if (socket_ipc->client_fd >= 0) {
            LOG_DEBUG("socket_external_input_callback: New client connected, fd: %d", socket_ipc->client_fd);
        } else {
            LOG_DEBUG("socket_external_input_callback: No pending connections to accept");
        }
    }

    // Read from socket if connected
    if (socket_ipc->client_fd >= 0) {
        LOG_DEBUG("socket_external_input_callback: Checking if client fd %d has data", socket_ipc->client_fd);
        if (socket_has_data(socket_ipc->client_fd)) {
            LOG_DEBUG("socket_external_input_callback: Data available on client fd %d, reading...", socket_ipc->client_fd);
            int bytes = read_socket_input(socket_ipc->client_fd, buffer, (size_t)buffer_size - 1);
            if (bytes > 0) {
                LOG_DEBUG("socket_external_input_callback: Read %d bytes from socket, returning", bytes);
                return bytes;
            } else if (bytes < 0) {
                // Client disconnected
                LOG_DEBUG("socket_external_input_callback: Client disconnected, closing fd %d", socket_ipc->client_fd);
                close(socket_ipc->client_fd);
                socket_ipc->client_fd = -1;
                LOG_DEBUG("socket_external_input_callback: Client fd closed, returning 0");
                return 0;
            } else {
                LOG_DEBUG("socket_external_input_callback: No data read (bytes = 0)");
            }
        } else {
            LOG_DEBUG("socket_external_input_callback: No data available on client fd %d", socket_ipc->client_fd);
        }
    } else {
        LOG_DEBUG("socket_external_input_callback: No client connected");
    }

    LOG_DEBUG("socket_external_input_callback: No input available, returning 0");
    return 0;
}

// Cleanup socket resources
static void cleanup_socket(SocketIPC *socket_ipc) {
    LOG_DEBUG("cleanup_socket: Starting socket cleanup");
    if (!socket_ipc) {
        LOG_DEBUG("cleanup_socket: socket_ipc is NULL, returning");
        return;
    }

    LOG_DEBUG("cleanup_socket: Socket IPC state - enabled: %d, server_fd: %d, client_fd: %d, path: %s",
              socket_ipc->enabled, socket_ipc->server_fd, socket_ipc->client_fd,
              socket_ipc->socket_path ? socket_ipc->socket_path : "(null)");

    if (socket_ipc->client_fd >= 0) {
        LOG_DEBUG("cleanup_socket: Closing client fd: %d", socket_ipc->client_fd);
        close(socket_ipc->client_fd);
        socket_ipc->client_fd = -1;
        LOG_DEBUG("cleanup_socket: Client fd closed");
    }

    if (socket_ipc->server_fd >= 0) {
        LOG_DEBUG("cleanup_socket: Closing server fd: %d", socket_ipc->server_fd);
        close(socket_ipc->server_fd);
        socket_ipc->server_fd = -1;
        LOG_DEBUG("cleanup_socket: Server fd closed");
    }

    if (socket_ipc->socket_path) {
        LOG_DEBUG("cleanup_socket: Removing socket file: %s", socket_ipc->socket_path);
        unlink(socket_ipc->socket_path);
        LOG_DEBUG("cleanup_socket: Freeing socket path memory");
        free(socket_ipc->socket_path);
        socket_ipc->socket_path = NULL;
        LOG_DEBUG("cleanup_socket: Socket path cleaned up");
    }

    socket_ipc->enabled = 0;
    LOG_DEBUG("cleanup_socket: Socket cleanup complete");
}

// Process API response for socket mode (handles tool calls recursively)
static int process_response_for_socket_mode(ConversationState *state, ApiResponse *response, int client_fd) {
    if (!response) {
        return 1;
    }
    
    // Handle API call errors (network errors, etc.)
    if (response->error_message) {
        // Create a simple error JSON
        cJSON *error_json = cJSON_CreateObject();
        if (error_json) {
            cJSON_AddStringToObject(error_json, "error", response->error_message);
            char *json_str = cJSON_PrintUnformatted(error_json);
            if (json_str) {
                write_socket_output(client_fd, json_str, strlen(json_str));
                write_socket_output(client_fd, "\n", 1);
                free(json_str);
            }
            cJSON_Delete(error_json);
        } else {
            // Fallback to plain text error
            char error_buf[512];
            snprintf(error_buf, sizeof(error_buf), "{\"error\": \"%s\"}\n", response->error_message);
            write_socket_output(client_fd, error_buf, strlen(error_buf));
        }
        return 1;
    }
    
    // Check if we have a raw response
    if (!response->raw_response) {
        char error_buf[] = "{\"error\": \"No response data available\"}\n";
        write_socket_output(client_fd, error_buf, strlen(error_buf));
        return 1;
    }
    
    // Convert the entire raw response to JSON string
    char *json_str = cJSON_PrintUnformatted(response->raw_response);
    if (!json_str) {
        LOG_ERROR("Failed to serialize JSON response for socket mode");
        char error_buf[] = "{\"error\": \"Failed to serialize JSON response\"}\n";
        write_socket_output(client_fd, error_buf, strlen(error_buf));
        return 1;
    }
    
    // Send the JSON response through socket
    write_socket_output(client_fd, json_str, strlen(json_str));
    
    // Add newline after JSON for readability
    write_socket_output(client_fd, "\n", 1);
    
    // Free the JSON string
    free(json_str);
    
    // Add to conversation history (still needed for multi-turn conversations)
    cJSON *choices = cJSON_GetObjectItem(response->raw_response, "choices");
    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *choice = cJSON_GetArrayItem(choices, 0);
        cJSON *message = cJSON_GetObjectItem(choice, "message");
        if (message) {
            add_assistant_message_openai(state, message);
        }
    }
    
    return 0;
}

// Socket-only mode: runs as a daemon listening on socket, no TUI
static void socket_only_mode(ConversationState *state, const char *socket_path) {
    LOG_INFO("Starting socket-only mode on path: %s", socket_path);
    
    // Create socket
    SocketIPC socket_ipc = {0};
    socket_ipc.server_fd = create_unix_socket(socket_path);
    if (socket_ipc.server_fd < 0) {
        LOG_ERROR("Failed to create socket for socket-only mode");
        return;
    }
    
    socket_ipc.client_fd = -1;
    socket_ipc.socket_path = strdup(socket_path);
    socket_ipc.enabled = 1;
    
    LOG_INFO("Socket-only mode ready, listening on: %s", socket_path);
    
    // Main event loop for socket-only mode
    int running = 1;
    while (running) {
        // Accept new connection if none
        if (socket_ipc.client_fd < 0) {
            socket_ipc.client_fd = accept_socket_connection(socket_ipc.server_fd);
            if (socket_ipc.client_fd >= 0) {
                LOG_INFO("Client connected in socket-only mode");
            }
        }
        
        // Read from socket if connected
        if (socket_ipc.client_fd >= 0) {
            char buffer[4096];
            if (socket_has_data(socket_ipc.client_fd)) {
                int bytes = read_socket_input(socket_ipc.client_fd, buffer, sizeof(buffer));
                if (bytes > 0) {
                    LOG_INFO("Received %d bytes from socket", bytes);
                    
                    // Process the input
                    buffer[bytes] = '\0';
                    
                    // Add user message to conversation (but don't display it)
                    add_user_message(state, buffer);
                    
                    // Call API synchronously
                    ApiResponse *response = call_api(state);
                    if (response) {
                        // Process response and send output through socket
                        process_response_for_socket_mode(state, response, socket_ipc.client_fd);
                        api_response_free(response);
                    } else {
                        const char *error_msg = "Error: Failed to get response from API\n";
                        write_socket_output(socket_ipc.client_fd, error_msg, strlen(error_msg));
                    }
                    
                } else if (bytes < 0) {
                    // Client disconnected
                    LOG_INFO("Client disconnected in socket-only mode");
                    close(socket_ipc.client_fd);
                    socket_ipc.client_fd = -1;
                }
            }
        }
        
        // Small sleep to prevent busy waiting
        usleep(10000); // 10ms
    }
    
    // Cleanup
    cleanup_socket(&socket_ipc);
    LOG_INFO("Socket-only mode exiting");
}

// Advanced input handler with readline-like keybindings, driven by non-blocking event loop
static void interactive_mode(ConversationState *state, int socket_ipc_enabled, const char *socket_path) {
    const char *prompt = ">>>";

    // Initialize TUI
    TUIState tui = {0};
    if (tui_init(&tui, state) != 0) {
        LOG_ERROR("Failed to initialize TUI");
        return;
    }

    // Set up database connection for token usage queries
    tui.persistence_db = state->persistence_db;
    tui.session_id = state->session_id;

    // Link TUI to state for streaming support
    state->tui = &tui;

    // Initialize command system
    commands_init();

    // Enable TUI mode for commands
    commands_set_tui_mode(1);

    // Build initial status line
    char status_msg[256];
    snprintf(status_msg, sizeof(status_msg), "Commands: /help for list | Ctrl+D to exit");
    tui_update_status(&tui, status_msg);

    // Display startup banner with mascot in the TUI
    tui_show_startup_banner(&tui, VERSION, state->model, state->working_dir);

    const size_t TUI_QUEUE_CAPACITY = 256;
    const size_t AI_QUEUE_CAPACITY = 16;
    TUIMessageQueue tui_queue;
    AIInstructionQueue instruction_queue;
    AIWorkerContext worker_ctx = {0};
    int tui_queue_initialized = 0;
    int instruction_queue_initialized = 0;
    int worker_started = 0;
    int async_enabled = 1;

    if (tui_msg_queue_init(&tui_queue, TUI_QUEUE_CAPACITY) != 0) {
        ui_show_error(&tui, NULL, "Failed to initialize TUI message queue; running in synchronous mode.");
        async_enabled = 0;
    } else {
        tui_queue_initialized = 1;
    }

    if (async_enabled) {
        if (ai_queue_init(&instruction_queue, AI_QUEUE_CAPACITY) != 0) {
            ui_show_error(&tui, NULL, "Failed to initialize instruction queue; running in synchronous mode.");
            async_enabled = 0;
        } else {
            instruction_queue_initialized = 1;
        }
    }

    if (async_enabled) {
        if (ai_worker_start(&worker_ctx, state, &instruction_queue, &tui_queue, ai_worker_handle_instruction) != 0) {
            ui_show_error(&tui, NULL, "Failed to start AI worker thread; running in synchronous mode.");
            async_enabled = 0;
        } else {
            worker_started = 1;
        }
    }

    if (!async_enabled) {
        if (worker_started) {
            ai_worker_stop(&worker_ctx);
            worker_started = 0;
        }
        if (instruction_queue_initialized) {
            ai_queue_free(&instruction_queue);
            instruction_queue_initialized = 0;
        }
        if (tui_queue_initialized) {
            tui_msg_queue_shutdown(&tui_queue);
            tui_msg_queue_free(&tui_queue);
            tui_queue_initialized = 0;
        }
    }

    // Initialize socket IPC if enabled
    SocketIPC socket_ipc = {0};
    if (socket_ipc_enabled && socket_path) {
        LOG_DEBUG("interactive_mode: Socket IPC enabled, creating socket at path: %s", socket_path);
        socket_ipc.server_fd = create_unix_socket(socket_path);
        if (socket_ipc.server_fd < 0) {
            LOG_ERROR("Failed to create socket, continuing without socket IPC");
            socket_ipc.enabled = 0;
            LOG_DEBUG("interactive_mode: Socket creation failed, socket IPC disabled");
        } else {
            socket_ipc.client_fd = -1;
            socket_ipc.socket_path = strdup(socket_path);
            socket_ipc.enabled = 1;
            LOG_DEBUG("interactive_mode: Socket IPC initialized successfully - server_fd: %d, path: %s",
                      socket_ipc.server_fd, socket_ipc.socket_path);
        }
    } else {
        LOG_DEBUG("interactive_mode: Socket IPC not enabled (socket_ipc_enabled: %d, socket_path: %s)",
                  socket_ipc_enabled, socket_path ? socket_path : "(null)");
    }

    InteractiveContext ctx = {
        .state = state,
        .tui = &tui,
        .worker = worker_started ? &worker_ctx : NULL,
        .instruction_queue = instruction_queue_initialized ? &instruction_queue : NULL,
        .tui_queue = tui_queue_initialized ? &tui_queue : NULL,
        .instruction_queue_capacity = instruction_queue_initialized ? (int)AI_QUEUE_CAPACITY : 0,
        .socket_ipc = socket_ipc,
    };

    void *event_loop_queue = tui_queue_initialized ? (void *)&tui_queue : NULL;
    tui_event_loop(&tui, prompt, submit_input_callback, interrupt_callback, NULL,
                   socket_ipc.enabled ? socket_external_input_callback : NULL,
                   &ctx, event_loop_queue);

    if (worker_started) {
        ai_worker_stop(&worker_ctx);
    }
    if (tui_queue_initialized) {
        tui_drain_message_queue(&tui, prompt, &tui_queue);
    }
    if (instruction_queue_initialized) {
        ai_queue_free(&instruction_queue);
    }
    if (tui_queue_initialized) {
        tui_msg_queue_shutdown(&tui_queue);
        tui_msg_queue_free(&tui_queue);
    }

    // Cleanup socket IPC
    cleanup_socket(&socket_ipc);

    // Disable TUI mode for commands before cleanup
    commands_set_tui_mode(0);

    // Cleanup TUI
    tui_cleanup(&tui);
    printf("Goodbye!\n");
}

// ============================================================================
// Session ID Generation
// ============================================================================

// Generate a unique session ID using timestamp and random data
// Helper function to get integer value from environment variable with default
static int get_env_int_retry(const char *name, int default_value) {
    const char *value = getenv(name);
    if (!value || value[0] == '\0') {
        return default_value;
    }

    char *endptr;
    long result = strtol(value, &endptr, 10);
    if (*endptr != '\0' || result < 0 || result > INT_MAX) {
        LOG_WARN("Invalid value for %s: '%s', using default %d", name, value, default_value);
        return default_value;
    }

    return (int)result;
}

#ifndef TEST_BUILD
// Dump conversation from database by session ID
static int dump_conversation_from_db(const char *session_id) {
    PersistenceDB *db = persistence_init(NULL);
    if (!db) {
        fprintf(stderr, "Error: Failed to open persistence database\n");
        return 1;
    }

    // Query for all API calls in this session
    const char *query =
        "SELECT timestamp, request_json, response_json, model, status, error_message "
        "FROM api_calls "
        "WHERE session_id = ? "
        "ORDER BY created_at ASC";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db->db, query, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to prepare query: %s\n", sqlite3_errmsg(db->db));
        persistence_close(db);
        return 1;
    }

    // If no session_id provided, get the most recent one
    if (!session_id) {
        const char *latest_query =
            "SELECT session_id FROM api_calls "
            "WHERE session_id IS NOT NULL "
            "ORDER BY created_at DESC LIMIT 1";

        sqlite3_stmt *latest_stmt = NULL;
        rc = sqlite3_prepare_v2(db->db, latest_query, -1, &latest_stmt, NULL);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Error: Failed to get latest session: %s\n", sqlite3_errmsg(db->db));
            sqlite3_finalize(stmt);
            persistence_close(db);
            return 1;
        }

        rc = sqlite3_step(latest_stmt);
        if (rc == SQLITE_ROW) {
            const unsigned char *sid = sqlite3_column_text(latest_stmt, 0);
            if (sid) {
                session_id = strdup((const char *)sid);
            }
        }
        sqlite3_finalize(latest_stmt);

        if (!session_id) {
            fprintf(stderr, "Error: No sessions found in database\n");
            sqlite3_finalize(stmt);
            persistence_close(db);
            return 1;
        }
    }

    sqlite3_bind_text(stmt, 1, session_id, -1, SQLITE_TRANSIENT);

    fprintf(stdout, "\n");
    fprintf(stdout, "=================================================================\n");
    fprintf(stdout, "                    CONVERSATION DUMP\n");
    fprintf(stdout, "=================================================================\n");
    fprintf(stdout, "Session ID: %s\n", session_id);
    fprintf(stdout, "=================================================================\n\n");

    int call_num = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        call_num++;

        const char *timestamp = (const char *)sqlite3_column_text(stmt, 0);
        const char *request_json = (const char *)sqlite3_column_text(stmt, 1);
        const char *response_json = (const char *)sqlite3_column_text(stmt, 2);
        const char *model = (const char *)sqlite3_column_text(stmt, 3);
        const char *status = (const char *)sqlite3_column_text(stmt, 4);
        const char *error_msg = (const char *)sqlite3_column_text(stmt, 5);

        fprintf(stdout, "-----------------------------------------------------------------\n");
        fprintf(stdout, "API Call #%d - %s\n", call_num, timestamp ? timestamp : "unknown");
        fprintf(stdout, "Model: %s\n", model ? model : "unknown");
        fprintf(stdout, "Status: %s\n", status ? status : "unknown");
        fprintf(stdout, "-----------------------------------------------------------------\n\n");

        // Parse and display request
        if (request_json) {
            cJSON *request = cJSON_Parse(request_json);
            if (request) {
                cJSON *messages = cJSON_GetObjectItem(request, "messages");
                if (messages && cJSON_IsArray(messages)) {
                    fprintf(stdout, "REQUEST MESSAGES:\n");
                    int msg_count = cJSON_GetArraySize(messages);
                    for (int i = 0; i < msg_count; i++) {
                        cJSON *msg = cJSON_GetArrayItem(messages, i);
                        cJSON *role = cJSON_GetObjectItem(msg, "role");
                        cJSON *content = cJSON_GetObjectItem(msg, "content");

                        if (role && cJSON_IsString(role)) {
                            fprintf(stdout, "\n  [%s]\n", role->valuestring);
                        }

                        if (content) {
                            if (cJSON_IsString(content)) {
                                fprintf(stdout, "  %s\n", content->valuestring);
                            } else if (cJSON_IsArray(content)) {
                                int content_count = cJSON_GetArraySize(content);
                                for (int j = 0; j < content_count; j++) {
                                    cJSON *block = cJSON_GetArrayItem(content, j);
                                    cJSON *type = cJSON_GetObjectItem(block, "type");

                                    if (type && cJSON_IsString(type)) {
                                        if (strcmp(type->valuestring, "text") == 0) {
                                            cJSON *text = cJSON_GetObjectItem(block, "text");
                                            if (text && cJSON_IsString(text)) {
                                                fprintf(stdout, "  %s\n", text->valuestring);
                                            }
                                        } else if (strcmp(type->valuestring, "tool_use") == 0) {
                                            cJSON *name = cJSON_GetObjectItem(block, "name");
                                            cJSON *id = cJSON_GetObjectItem(block, "id");
                                            fprintf(stdout, "  [TOOL_USE: %s", name && cJSON_IsString(name) ? name->valuestring : "unknown");
                                            if (id && cJSON_IsString(id)) {
                                                fprintf(stdout, " (id: %s)", id->valuestring);
                                            }
                                            fprintf(stdout, "]\n");
                                        } else if (strcmp(type->valuestring, "tool_result") == 0) {
                                            cJSON *tool_use_id = cJSON_GetObjectItem(block, "tool_use_id");
                                            fprintf(stdout, "  [TOOL_RESULT");
                                            if (tool_use_id && cJSON_IsString(tool_use_id)) {
                                                fprintf(stdout, " for %s", tool_use_id->valuestring);
                                            }
                                            fprintf(stdout, "]\n");
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                cJSON_Delete(request);
            }
        }

        // Parse and display response
        fprintf(stdout, "\nRESPONSE:\n");
        if (strcmp(status, "error") == 0 && error_msg) {
            fprintf(stdout, "  [ERROR] %s\n", error_msg);
        } else if (response_json) {
            cJSON *response = cJSON_Parse(response_json);
            if (response) {
                cJSON *content = cJSON_GetObjectItem(response, "content");
                if (content && cJSON_IsArray(content)) {
                    int content_count = cJSON_GetArraySize(content);
                    for (int i = 0; i < content_count; i++) {
                        cJSON *block = cJSON_GetArrayItem(content, i);
                        cJSON *type = cJSON_GetObjectItem(block, "type");

                        if (type && cJSON_IsString(type)) {
                            if (strcmp(type->valuestring, "text") == 0) {
                                cJSON *text = cJSON_GetObjectItem(block, "text");
                                if (text && cJSON_IsString(text)) {
                                    fprintf(stdout, "\n  %s\n", text->valuestring);
                                }
                            } else if (strcmp(type->valuestring, "tool_use") == 0) {
                                cJSON *name = cJSON_GetObjectItem(block, "name");
                                cJSON *id = cJSON_GetObjectItem(block, "id");
                                fprintf(stdout, "\n  [TOOL_USE: %s", name && cJSON_IsString(name) ? name->valuestring : "unknown");
                                if (id && cJSON_IsString(id)) {
                                    fprintf(stdout, " (id: %s)", id->valuestring);
                                }
                                fprintf(stdout, "]\n");
                            }
                        }
                    }
                }
                cJSON_Delete(response);
            }
        }

        fprintf(stdout, "\n");
    }

    if (call_num == 0) {
        fprintf(stdout, "No API calls found for this session.\n");
    }

    fprintf(stdout, "=================================================================\n");
    fprintf(stdout, "                    END OF CONVERSATION\n");
    fprintf(stdout, "=================================================================\n\n");

    sqlite3_finalize(stmt);
    persistence_close(db);
    return 0;
}
#endif // TEST_BUILD

// Format: sess_<timestamp>_<random>
// Returns: Newly allocated string (caller must free)
static char* generate_session_id(void) {
    char *session_id = malloc(64);
    if (!session_id) {
        return NULL;
    }

    // Get current time for timestamp
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    // Generate session ID: sess_<unix_timestamp>_<random_hex>
    // Use arc4random() for cryptographically secure random numbers
    unsigned int random_part = arc4random();
    snprintf(session_id, 64, "sess_%ld_%08x", ts.tv_sec, random_part);

    return session_id;
}





// ============================================================================
// Main Entry Point
#ifndef TEST_BUILD
// ============================================================================

int main(int argc, char *argv[]) {
    // Handle version flag first (no API key needed)
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("Klawed version %s\n", KLAWED_VERSION_FULL);
        return 0;
    }

    // Handle help flag first (no API key needed)
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        printf("Klawed - Pure C Implementation (OpenAI Compatible)\n");
        printf("Version: %s\n\n", KLAWED_VERSION_FULL);
        printf("Usage:\n");
        printf("  %s                               Start interactive mode\n", argv[0]);
        printf("  %s \"PROMPT\"                       Execute single command and exit\n", argv[0]);
        printf("  %s -d, --dump-conversation [ID]  Dump conversation to console and exit\n", argv[0]);
        printf("                                      (defaults to most recent session if no ID given)\n");
        printf("  %s -r, --resume [ID]             Resume a previous conversation session\n", argv[0]);
        printf("                                      (defaults to most recent session if no ID given)\n");
        printf("  %s -l, --list-sessions [N]       List available sessions (N = max to show)\n", argv[0]);
        printf("  %s -s, --socket PATH             Enable IPC socket at PATH for remote input\n", argv[0]);
        printf("  %s -h, --help                     Show this help message\n", argv[0]);
        printf("  %s --version                      Show version information\n\n", argv[0]);
        printf("Environment Variables:\n");
        printf("  API Configuration:\n");
        printf("    OPENAI_API_KEY       Required: Your OpenAI API key (not needed for Bedrock)\n");
        printf("    OPENAI_API_BASE      Optional: API base URL (default: %s)\n", API_BASE_URL);
        printf("    OPENAI_MODEL         Optional: Model name (default: %s)\n", DEFAULT_MODEL);
        printf("    ANTHROPIC_MODEL      Alternative: Model name (fallback if OPENAI_MODEL not set)\n");
        /* printf("    DISABLE_PROMPT_CACHING  Optional: Set to 1 to disable prompt caching\n\n"); */
        printf("  AWS Bedrock Configuration:\n");
        printf("    KLAWED_USE_BEDROCK  Set to 1 to use AWS Bedrock instead of OpenAI\n");
        printf("    ANTHROPIC_MODEL         Required for Bedrock: Claude model ID\n");
        printf("                            Examples: anthropic.claude-3-sonnet-20240229-v1:0\n");
        printf("                                      us.anthropic.claude-sonnet-4-5-20250929-v1:0\n");
        printf("    AWS credentials        Required: Configure via AWS CLI or environment\n\n");
        printf("  Logging and Persistence:\n");
        printf("    KLAWED_LOG_PATH    Optional: Full path to log file\n");
        printf("    KLAWED_LOG_DIR     Optional: Directory for logs (uses klawed.log filename)\n");
        printf("    KLAWED_LOG_LEVEL     Optional: Log level (DEBUG, INFO, WARN, ERROR)\n");
        printf("    KLAWED_DB_PATH     Optional: Path to SQLite database for API history\n");
        printf("                         Default: ~/.local/share/klawed/api_calls.db\n");
        printf("    KLAWED_MAX_RETRY_DURATION_MS  Optional: Maximum retry duration in milliseconds\n");
        printf("                                     Default: 600000 (10 minutes)\n\n");
        printf("  UI Customization:\n");
        printf("    KLAWED_THEME       Optional: Path to Kitty theme file\n\n");

        printf("Interactive Tips:\n");
        printf("  Esc/Ctrl+[ to enter Normal mode (vim-style), 'i' to insert\n");
        printf("  Scroll with j/k (line), Ctrl+D/U (half page), gg/G (top/bottom)\n");
        printf("  Or use PageUp/PageDown or Arrow keys to scroll\n");
        printf("  Type /help for commands (e.g., /clear, /exit, /add-dir, /voice)\n");
        printf("  Press Ctrl+C to cancel a running API/tool action\n\n");
        return 0;
    }

    // Check for dump conversation flag
#ifndef TEST_BUILD
    if ((argc == 2 || argc == 3) && (strcmp(argv[1], "-d") == 0 || strcmp(argv[1], "--dump-conversation") == 0)) {
        // Dump conversation mode: query database and display
        const char *session_id = (argc == 3) ? argv[2] : NULL;  // NULL = most recent session
        return dump_conversation_from_db(session_id);
    }
#endif

    // Check for resume session flag
#ifndef TEST_BUILD
    int resume_session = 0;
    const char *resume_session_id = NULL;
    if ((argc == 2 || argc == 3) && (strcmp(argv[1], "-r") == 0 || strcmp(argv[1], "--resume") == 0)) {
        resume_session = 1;
        resume_session_id = (argc == 3) ? argv[2] : NULL;  // NULL = most recent session
        LOG_INFO("Resume session mode enabled, session_id: %s", resume_session_id ? resume_session_id : "most recent");
    }
#endif

    // Check for list sessions flag
#ifndef TEST_BUILD
    int list_sessions = 0;
    int session_limit = 10;  // Default limit
    if ((argc == 2 || argc == 3) && (strcmp(argv[1], "-l") == 0 || strcmp(argv[1], "--list-sessions") == 0)) {
        list_sessions = 1;
        if (argc == 3) {
            session_limit = atoi(argv[2]);
            if (session_limit <= 0) {
                session_limit = 0;  // 0 means no limit
            }
        }
        LOG_INFO("List sessions mode enabled, limit: %d", session_limit);
    }
#endif

    // Check for socket IPC flag
    int socket_ipc_enabled = 0;
    char *socket_path = NULL;
    if ((argc == 2 || argc == 3) && (strcmp(argv[1], "-s") == 0 || strcmp(argv[1], "--socket") == 0)) {
        LOG_DEBUG("main: Socket IPC flag detected, argc: %d", argc);
        if (argc != 3) {
            fprintf(stderr, "Error: Socket path required with --socket option\n");
            fprintf(stderr, "Usage: %s --socket /path/to/socket\n", argv[0]);
            return 1;
        }
        socket_ipc_enabled = 1;
        socket_path = argv[2];
        LOG_INFO("Socket IPC enabled, path: %s", socket_path);
        LOG_DEBUG("main: Socket IPC configuration - enabled: %d, path: %s", 
                  socket_ipc_enabled, socket_path);
    } else {
        LOG_DEBUG("main: Socket IPC flag not detected or not in correct position");
    }

    // Handle list sessions mode
#ifndef TEST_BUILD
    if (list_sessions) {
        // Initialize persistence database
        PersistenceDB *db = persistence_init(NULL);
        if (!db) {
            fprintf(stderr, "Error: Failed to open persistence database\n");
            return 1;
        }

        int result = session_list_sessions(db, session_limit);
        persistence_close(db);
        return result == 0 ? 0 : 1;
    }
#endif

    // Check for single command mode: ./klawed "prompt"
    int is_single_command_mode = 0;
    char *single_command = NULL;

    if (argc == 2 && !resume_session && !list_sessions && !socket_ipc_enabled) {
        // Single argument provided - treat as prompt for single command mode
        // (but not if it's a resume flag without session ID)
        is_single_command_mode = 1;
        single_command = argv[1];
        LOG_INFO("Single command mode enabled with prompt: %s", single_command);
    } else if (argc > 2 && !resume_session && !list_sessions && !socket_ipc_enabled) {
        LOG_ERROR("Unexpected arguments provided");
        printf("Try '%s --help' for usage information.\n", argv[0]);
        return 1;
    } else if (argc > 3 && socket_ipc_enabled) {
        LOG_WARN("Extra arguments provided with -s flag, ignoring them (socket-only mode)");
    }

#ifndef TEST_BUILD
    // Check if Bedrock mode is enabled
    int use_bedrock = bedrock_is_enabled();
#else
    int use_bedrock = 0;
#endif

    const char *api_key = NULL;
    const char *api_base = NULL;
    const char *model = NULL;

    if (use_bedrock) {
        // Bedrock mode: API key not required, credentials loaded separately
        // Get model from ANTHROPIC_MODEL environment variable
        model = getenv("ANTHROPIC_MODEL");
        if (!model) {
            LOG_ERROR("ANTHROPIC_MODEL environment variable required when using AWS Bedrock");
            fprintf(stderr, "Error: ANTHROPIC_MODEL environment variable not set\n");
            fprintf(stderr, "Example: export ANTHROPIC_MODEL=us.anthropic.claude-sonnet-4-5-20250929-v1:0\n");
            return 1;
        }
        // API key and base URL will be handled by Bedrock module
        api_key = "bedrock";  // Placeholder
        api_base = "bedrock"; // Will be overridden by Bedrock endpoint
        LOG_INFO("Bedrock mode enabled, using model: %s", model);
    } else {
        // Standard mode: check for API key
        api_key = getenv("OPENAI_API_KEY");
        if (!api_key) {
            LOG_ERROR("OPENAI_API_KEY environment variable not set");
            fprintf(stderr, "Error: OPENAI_API_KEY environment variable not set\n");
            fprintf(stderr, "\nTo use AWS Bedrock instead, set:\n");
            fprintf(stderr, "  export KLAWED_USE_BEDROCK=true\n");
            fprintf(stderr, "  export ANTHROPIC_MODEL=us.anthropic.claude-sonnet-4-5-20250929-v1:0\n");
            fprintf(stderr, "  export AWS_REGION=us-west-2\n");
            fprintf(stderr, "  export AWS_PROFILE=your-profile\n");
            return 1;
        }

        // Get optional API base and model from environment
        api_base = getenv("OPENAI_API_BASE");
        if (!api_base) {
            api_base = API_BASE_URL;
        }

        model = getenv("OPENAI_MODEL");
        if (!model) {
            model = getenv("ANTHROPIC_MODEL");  // Try ANTHROPIC_MODEL as fallback
            if (!model) {
                model = DEFAULT_MODEL;
            }
        }
    }

    // Initialize CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // arc4random is self-seeding, no initialization needed

    // Initialize logging system
    if (log_init() != 0) {
        LOG_ERROR("Warning: Failed to initialize logging system");
    }

    // Configure log rotation: 10MB max size, keep 5 backups
    log_set_rotation(10, 5);

    // Set log level from environment or default to INFO
    const char *log_level_env = getenv("KLAWED_LOG_LEVEL");
    if (log_level_env) {
        if (strcmp(log_level_env, "DEBUG") == 0) {
            log_set_level(LOG_LEVEL_DEBUG);
        } else if (strcmp(log_level_env, "WARN") == 0) {
            log_set_level(LOG_LEVEL_WARN);
        } else if (strcmp(log_level_env, "ERROR") == 0) {
            log_set_level(LOG_LEVEL_ERROR);
        }
    }

    LOG_INFO("Application started");
    LOG_INFO("API URL: %s", api_base);
    LOG_INFO("Model: %s", model);

    // Initialize colorscheme EARLY (before any colored output/spinners)
    const char *theme = getenv("KLAWED_THEME");
    if (theme && strlen(theme) > 0) {
        // Theme can be:
        // 1. Built-in theme name (e.g., "dracula", "gruvbox-dark")
        // 2. Built-in with .conf extension (e.g., "dracula.conf")
        // 3. Absolute/relative path to external .conf file
        if (init_colorscheme(theme) != 0) {
            LOG_WARN("Failed to load colorscheme '%s', will use ANSI fallback colors", theme);
        } else {
            LOG_DEBUG("Colorscheme loaded successfully: %s", theme);
        }
    } else {
        // Try to load default built-in theme
        if (init_colorscheme("tender") != 0) {
            LOG_DEBUG("No default colorscheme found, using ANSI fallback colors");
        } else {
            LOG_DEBUG("Default colorscheme loaded: tender");
        }
    }

    // Initialize persistence layer
    PersistenceDB *persistence_db = persistence_init(NULL);  // NULL = use default path
    if (persistence_db) {
        LOG_INFO("Persistence layer initialized");
    } else {
        LOG_WARN("Failed to initialize persistence layer - API calls will not be logged");
    }

#ifndef TEST_BUILD
    // Initialize MCP (Model Context Protocol) subsystem
    if (mcp_init() == 0) {
        LOG_INFO("MCP subsystem initialized");
    } else {
        LOG_WARN("Failed to initialize MCP subsystem");
    }
#endif

    // Generate unique session ID for this conversation
    char *session_id = generate_session_id();
    if (!session_id) {
        LOG_WARN("Failed to generate session ID");
    }
    LOG_INFO("Session ID: %s", session_id ? session_id : "none");

    // Set session ID for logging
    if (session_id) {
        log_set_session_id(session_id);
    }

    // Initialize conversation state
    ConversationState state = {0};
    if (conversation_state_init(&state) != 0) {
        LOG_ERROR("Failed to initialize conversation state synchronization");
        fprintf(stderr, "Error: Unable to initialize conversation state\n");
        free(session_id);
        if (persistence_db) {
            persistence_close(persistence_db);
        }
        curl_global_cleanup();
        log_shutdown();
        return 1;
    }
    state.api_key = strdup(api_key);
    state.api_url = strdup(api_base);
    state.model = strdup(model);

    // Get current working directory - use PATH_MAX to satisfy static analyzer
    char cwd_buf[PATH_MAX];
    char *cwd = getcwd(cwd_buf, sizeof(cwd_buf));
    state.working_dir = cwd ? strdup(cwd) : NULL;

    state.session_id = session_id;
    state.persistence_db = persistence_db;
    state.max_retry_duration_ms = get_env_int_retry("KLAWED_MAX_RETRY_DURATION_MS", MAX_RETRY_DURATION_MS);

    // Initialize todo list
    state.todo_list = malloc(sizeof(TodoList));
    if (state.todo_list) {
        todo_init(state.todo_list);
        LOG_DEBUG("Todo list initialized");
    } else {
        LOG_ERROR("Failed to allocate memory for todo list");
    }

#ifndef TEST_BUILD
    // Resume session if requested
    if (resume_session && persistence_db) {
        LOG_INFO("Attempting to resume session: %s", resume_session_id ? resume_session_id : "most recent");

        // Load session from database
        if (session_load_from_db(persistence_db, resume_session_id, &state) == 0) {
            LOG_INFO("Successfully resumed session: %s", state.session_id);

            // Update session ID for logging
            if (state.session_id) {
                log_set_session_id(state.session_id);
            }

            // Update the local session_id variable to match the loaded session
            if (session_id && state.session_id && strcmp(session_id, state.session_id) != 0) {
                free(session_id);
                session_id = strdup(state.session_id);
            }
        } else {
            LOG_ERROR("Failed to resume session");
            if (resume_session_id) {
                fprintf(stderr, "Error: Failed to resume session '%s'. Session may not exist.\n", resume_session_id);
            } else {
                fprintf(stderr, "Error: Failed to resume most recent session. No sessions found in database.\n");
            }

            // Clean up and exit
            conversation_free(&state);
            free(session_id);
            if (persistence_db) {
                persistence_close(persistence_db);
            }
            curl_global_cleanup();
            log_shutdown();
            return 1;
        }
    }
#endif

#ifndef TEST_BUILD
    // Defer provider initialization to first API call to avoid blocking TUI startup.
    // state.api_url currently points at base URL from env; it will be replaced on init.
    state.provider = NULL;

    // Load MCP configuration if enabled
    if (mcp_is_enabled()) {
        LOG_DEBUG("MCP: MCP is enabled, loading configuration");
        const char *mcp_config_path = getenv("KLAWED_MCP_CONFIG");
        LOG_DEBUG("MCP: Using config path: %s", mcp_config_path ? mcp_config_path : "(default)");
        state.mcp_config = mcp_load_config(mcp_config_path);

        if (state.mcp_config) {
            LOG_INFO("MCP: Loaded %d server(s) from config", state.mcp_config->server_count);

            // Connect to all configured servers
            for (int i = 0; i < state.mcp_config->server_count; i++) {
                MCPServer *server = state.mcp_config->servers[i];
                LOG_DEBUG("MCP: Attempting to connect to server '%s'", server->name);
                if (mcp_connect_server(server) == 0) {
                    LOG_DEBUG("MCP: Connected to server '%s', discovering tools", server->name);
                    // Discover tools from connected server
                    int tool_count = mcp_discover_tools(server);
                    if (tool_count > 0) {
                        LOG_INFO("MCP: Server '%s' provides %d tool(s)", server->name, tool_count);
                        // Log individual tool names
                        for (int j = 0; j < tool_count; j++) {
                            if (server->tools[j]) {
                                LOG_DEBUG("MCP: Server '%s' tool %d: '%s'", server->name, j, server->tools[j]);
                            }
                        }
                    } else if (tool_count == 0) {
                        LOG_DEBUG("MCP: Server '%s' provides no tools", server->name);
                    } else {
                        LOG_WARN("MCP: Failed to discover tools from server '%s'", server->name);
                    }
                } else {
                    LOG_WARN("MCP: Failed to connect to server '%s'", server->name);
                }
            }

            // Log status
            char *status = mcp_get_status(state.mcp_config);
            if (status) {
                LOG_INFO("MCP Status: %s", status);
                free(status);
            }
        } else {
            LOG_DEBUG("MCP: No servers configured or failed to load config");
        }
    } else {
        state.mcp_config = NULL;
        LOG_DEBUG("MCP: Disabled (set KLAWED_MCP_ENABLED=1 to enable)");
    }
#else
    state.provider = NULL;
    state.mcp_config = NULL;
#endif

    // Check for allocation failures
    if (!state.api_key || !state.api_url || !state.model || !state.todo_list) {
        LOG_ERROR("Failed to allocate memory for conversation state");
        fprintf(stderr, "Error: Memory allocation failed\n");
        free(state.api_key);
        free(state.api_url);
        free(state.model);
        free(state.working_dir);
        if (state.todo_list) {
            free(state.todo_list);
        }
        conversation_state_destroy(&state);
        curl_global_cleanup();
        return 1;
    }

    if (!state.working_dir) {
        LOG_ERROR("Failed to get current working directory");
        free(state.api_key);
        free(state.api_url);
        free(state.model);
        conversation_state_destroy(&state);
        curl_global_cleanup();
        return 1;
    }

    LOG_INFO("API URL initialized: %s", state.api_url);

    // Build and add system prompt with environment context
    char *system_prompt = build_system_prompt(&state);
    if (system_prompt) {
        add_system_message(&state, system_prompt);

        // Debug: print system prompt if DEBUG_PROMPT environment variable is set
        if (getenv("DEBUG_PROMPT")) {
            printf("\n=== SYSTEM PROMPT (DEBUG) ===\n%s\n=== END SYSTEM PROMPT ===\n\n", system_prompt);
        }

        free(system_prompt);
        LOG_DEBUG("System prompt added with environment context");
    } else {
        LOG_WARN("Failed to build system prompt");
    }

    // Run in appropriate mode
    int exit_code = 0;
    if (socket_ipc_enabled) {
        // Socket-only mode: no TUI, only communicate via socket
        socket_only_mode(&state, socket_path);
    } else if (is_single_command_mode) {
        exit_code = single_command_mode(&state, single_command);
    } else {
        interactive_mode(&state, socket_ipc_enabled, socket_path);
    }

    // Cleanup conversation messages
    conversation_free(&state);

    // Free additional directories
    for (int i = 0; i < state.additional_dirs_count; i++) {
        free(state.additional_dirs[i]);
    }
    free(state.additional_dirs);

    // Cleanup todo list
    if (state.todo_list) {
        todo_free(state.todo_list);
        free(state.todo_list);
        state.todo_list = NULL;
        LOG_DEBUG("Todo list cleaned up");
    }

#ifndef TEST_BUILD
    // Cleanup provider
    if (state.provider) {
        state.provider->cleanup(state.provider);
        LOG_DEBUG("Provider cleaned up");
    }

    // Cleanup MCP configuration
    if (state.mcp_config) {
        mcp_free_config(state.mcp_config);
        state.mcp_config = NULL;
        LOG_DEBUG("MCP configuration cleaned up");
    }
#endif

    // Print session ID to console on exit
    if (state.session_id) {
        fprintf(stderr, "\nSession ID: %s\n", state.session_id);
    }

    free(state.api_key);
    free(state.api_url);
    free(state.model);
    free(state.working_dir);
    free(state.session_id);
    conversation_state_destroy(&state);

    // Close persistence layer
    if (state.persistence_db) {
        persistence_close(state.persistence_db);
        LOG_INFO("Persistence layer closed");
    }

#ifndef TEST_BUILD
    // Clean up MCP subsystem
    mcp_cleanup();
    LOG_INFO("MCP subsystem cleaned up");
#endif

    curl_global_cleanup();

    LOG_INFO("Application terminated");
    log_shutdown();

    return exit_code;
}

#endif // TEST_BUILD

#ifdef TEST_BUILD
#pragma GCC diagnostic pop
#endif
