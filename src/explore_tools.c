/*
 * explore_tools.c - Explore subagent tools for web research and documentation
 *
 * These tools are only available when KLAWED_EXPLORE_MODE=1
 *
 * Tools:
 * - web_search: Search the web using DuckDuckGo via web_browse_agent
 * - web_read: Navigate to a URL and extract content
 * - context7_search: Search for library documentation
 * - context7_docs: Fetch documentation for a specific library
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <bsd/string.h>

#include "explore_tools.h"
#include "logger.h"
#include "http_client.h"
#include "message_queue.h"
#include "util/string_utils.h"
#include "util/output_utils.h"

#define CONTEXT7_API_BASE "https://context7.com/api"
#define MAX_WEB_OUTPUT 100000  // 100KB max output from web_browse_agent
#define WEB_AGENT_TIMEOUT 120  // 2 minute timeout for web operations

// External reference to the thread-local TUI message queue for TUI protection
extern _Thread_local TUIMessageQueue *g_active_tool_queue;

// Get the path to web_browse_agent binary, preferring env override, then tools copy, then PATH
static const char* get_web_agent_path(void) {
    static char resolved_path[PATH_MAX];

    const char *env_path = getenv("KLAWED_WEB_BROWSE_AGENT_PATH");
    if (env_path && env_path[0] != '\0' && access(env_path, X_OK) == 0) {
        return env_path;
    }

    const char *tools_path = "tools/web_browse_agent/bin/web_browse_agent";
    if (access(tools_path, X_OK) == 0) {
        if (strlcpy(resolved_path, tools_path, sizeof(resolved_path)) < sizeof(resolved_path)) {
            return resolved_path;
        }
    }

    const char *path_env = getenv("PATH");
    if (!path_env || path_env[0] == '\0') {
        return env_path && env_path[0] != '\0' ? env_path : tools_path;
    }

    char *path_copy = strdup(path_env);
    if (!path_copy) {
        return env_path && env_path[0] != '\0' ? env_path : tools_path;
    }

    const char *found_path = NULL;
    char *saveptr = NULL;
    for (char *token = strtok_r(path_copy, ":", &saveptr); token; token = strtok_r(NULL, ":", &saveptr)) {
        if (strlcpy(resolved_path, token, sizeof(resolved_path)) >= sizeof(resolved_path)) {
            continue;
        }
        size_t len = strnlen(resolved_path, sizeof(resolved_path));
        if (len + 1 >= sizeof(resolved_path)) {
            continue;
        }
        resolved_path[len] = '/';
        resolved_path[len + 1] = '\0';
        if (strlcat(resolved_path, "web_browse_agent", sizeof(resolved_path)) >= sizeof(resolved_path)) {
            continue;
        }

        if (access(resolved_path, X_OK) == 0) {
            found_path = resolved_path;
            break;
        }
    }

    free(path_copy);

    if (found_path) {
        return found_path;
    }

    return env_path && env_path[0] != '\0' ? env_path : tools_path;
}

// Check if explore mode is enabled
int is_explore_mode_enabled(void) {
    const char *explore_mode = getenv("KLAWED_EXPLORE_MODE");
    return explore_mode && (strcmp(explore_mode, "1") == 0 ||
                           strcasecmp(explore_mode, "true") == 0 ||
                           strcasecmp(explore_mode, "yes") == 0);
}

// Check if web_browse_agent binary is available
int is_web_agent_available(void) {
    const char *agent_path = get_web_agent_path();
    return access(agent_path, X_OK) == 0;
}

static int is_web_agent_configured_only(void) {
    const char *agent_path = get_web_agent_path();
    return agent_path && agent_path[0] != '\0';
}

// Check if headless mode is enabled (default: true)
static int is_headless_mode(void) {
    // Prefer dedicated override for the browser agent; fallback to explore flag for compatibility
    const char *headless = getenv("KLAWED_WEB_BROWSE_AGENT_HEADLESS");
    if (!headless || headless[0] == '\0') {
        headless = getenv("KLAWED_EXPLORE_HEADLESS");
    }
    // Default to headless when unset
    if (!headless || headless[0] == '\0') {
        return 1;
    }
    return strcmp(headless, "1") == 0 ||
           strcasecmp(headless, "true") == 0 ||
           strcasecmp(headless, "yes") == 0;
}

static char* execute_web_agent(const char *prompt, int *exit_code) {
    if (!prompt) {
        return NULL;
    }

    const char *agent_path = get_web_agent_path();
    int headless = is_headless_mode();

    // Build command with proper escaping (extra space for redirections)
    size_t cmd_size = strlen(agent_path) + strlen(prompt) * 2 + 300;
    char *command = malloc(cmd_size);
    if (!command) {
        return NULL;
    }

    // Escape the prompt for shell
    size_t escaped_size = strlen(prompt) * 2 + 1;
    char *escaped_prompt = malloc(escaped_size);
    if (!escaped_prompt) {
        free(command);
        return NULL;
    }

    size_t j = 0;
    for (size_t i = 0; prompt[i] && j < escaped_size - 2; i++) {
        if (prompt[i] == '"' || prompt[i] == '\\' || prompt[i] == '$' || prompt[i] == '`') {
            escaped_prompt[j++] = '\\';
        }
        escaped_prompt[j++] = prompt[i];
    }
    escaped_prompt[j] = '\0';

    // Add </dev/null to prevent stdin interaction
    snprintf(command, cmd_size, "timeout %d %s %s --no-browser \"%s\" </dev/null 2>&1",
             WEB_AGENT_TIMEOUT,
             agent_path,
             headless ? "--headless" : "",
             escaped_prompt);

    free(escaped_prompt);

    LOG_INFO("Executing web_browse_agent: %s", command);

    // Temporarily redirect stderr to prevent direct terminal output in TUI mode
    int saved_stderr = -1;
    FILE *stderr_redirect = NULL;

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

    FILE *fp = popen(command, "r");
    free(command);

    if (!fp) {
        LOG_ERROR("Failed to execute web_browse_agent: %s", strerror(errno));
        // Restore stderr before returning
        if (saved_stderr != -1) {
            dup2(saved_stderr, STDERR_FILENO);
            close(saved_stderr);
            fflush(stderr);
        }
        return NULL;
    }

    // Read output
    char *output = malloc(MAX_WEB_OUTPUT);
    if (!output) {
        pclose(fp);
        // Restore stderr before returning
        if (saved_stderr != -1) {
            dup2(saved_stderr, STDERR_FILENO);
            close(saved_stderr);
            fflush(stderr);
        }
        return NULL;
    }

    size_t total = 0;
    size_t n;
    while ((n = fread(output + total, 1, MAX_WEB_OUTPUT - total - 1, fp)) > 0) {
        total += n;
        if (total >= MAX_WEB_OUTPUT - 1) {
            break;
        }
    }
    output[total] = '\0';

    int status = pclose(fp);
    if (exit_code) {
        *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    // Restore stderr after command execution
    if (saved_stderr != -1) {
        dup2(saved_stderr, STDERR_FILENO);
        close(saved_stderr);
        fflush(stderr);
    }

    // Strip ANSI escape sequences to prevent terminal corruption
    char *clean_output = strip_ansi_escapes(output);
    if (clean_output) {
        free(output);
        return clean_output;
    }

    return output;
}

// HTTP response buffer for Context7 API
typedef struct {
    char *data;
    size_t size;
} ResponseBuffer;

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    ResponseBuffer *buf = (ResponseBuffer *)userp;

    char *ptr = realloc(buf->data, buf->size + realsize + 1);
    if (!ptr) {
        return 0;
    }

    buf->data = ptr;
    memcpy(buf->data + buf->size, contents, realsize);
    buf->size += realsize;
    buf->data[buf->size] = '\0';

    return realsize;
}

// Make HTTP GET request to Context7 API
static char* context7_request(const char *endpoint, const char *query_params) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return NULL;
    }

    // Build URL
    size_t url_size = strlen(CONTEXT7_API_BASE) + strlen(endpoint) + strlen(query_params) + 2;
    char *url = malloc(url_size);
    if (!url) {
        curl_easy_cleanup(curl);
        return NULL;
    }
    snprintf(url, url_size, "%s%s?%s", CONTEXT7_API_BASE, endpoint, query_params);

    ResponseBuffer response = {0};
    response.data = malloc(1);
    if (!response.data) {
        free(url);
        curl_easy_cleanup(curl);
        return NULL;
    }
    response.data[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "klawed-explore/1.0");

    // Add API key header if available
    struct curl_slist *headers = NULL;
    const char *api_key = getenv("CONTEXT7_API_KEY");
    if (api_key && api_key[0] != '\0') {
        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header), "X-Context7-API-Key: %s", api_key);
        headers = curl_slist_append(headers, auth_header);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    CURLcode res = curl_easy_perform(curl);

    if (headers) {
        curl_slist_free_all(headers);
    }
    free(url);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        LOG_ERROR("Context7 API request failed: %s", curl_easy_strerror(res));
        free(response.data);
        return NULL;
    }

    return response.data;
}

// URL encode a string
static char* url_encode(const char *str) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return NULL;
    }

    char *encoded = curl_easy_escape(curl, str, 0);
    char *result = NULL;
    if (encoded) {
        result = strdup(encoded);
        curl_free(encoded);
    }

    curl_easy_cleanup(curl);
    return result;
}

// ============================================================================
// Tool: web_search - Search the web using DuckDuckGo
// ============================================================================

cJSON* tool_web_search(cJSON *params, void *state) {
    (void)state;  // Unused

    if (!is_explore_mode_enabled()) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error",
            "web_search is only available in Explore mode. Set KLAWED_EXPLORE_MODE=1");
        return error;
    }

    if (!is_web_agent_available()) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error",
            "web_browse_agent binary not found. Build it with: cd tools/web_browse_agent && make");
        return error;
    }

    cJSON *query_json = cJSON_GetObjectItem(params, "query");
    if (!query_json || !cJSON_IsString(query_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "query parameter is required");
        return error;
    }

    const char *query = query_json->valuestring;
    int max_results = 10;

    cJSON *max_json = cJSON_GetObjectItem(params, "max_results");
    if (max_json && cJSON_IsNumber(max_json)) {
        max_results = (int)max_json->valuedouble;
        if (max_results < 1) max_results = 1;
        if (max_results > 30) max_results = 30;
    }

    // Build prompt for web_browse_agent
    char prompt[2048];
    snprintf(prompt, sizeof(prompt),
        "Use web_search tool to search for: %s. Return up to %d results as JSON array "
        "with title, url, and snippet fields. Output ONLY the JSON, no other text.",
        query, max_results);

    int exit_code;
    char *output = execute_web_agent(prompt, &exit_code);

    cJSON *result = cJSON_CreateObject();

    if (!output) {
        cJSON_AddStringToObject(result, "error", "Failed to execute web search");
        return result;
    }

    if (exit_code != 0) {
        cJSON_AddStringToObject(result, "warning",
            "web_browse_agent exited with non-zero status");
    }

    // Try to extract JSON from output
    char *json_start = strchr(output, '[');
    char *json_end = strrchr(output, ']');

    if (json_start && json_end && json_end > json_start) {
        // Extract and parse JSON array
        size_t json_len = (size_t)(json_end - json_start) + 1;
        char *json_str = malloc(json_len + 1);
        if (json_str) {
            memcpy(json_str, json_start, json_len);
            json_str[json_len] = '\0';

            cJSON *results_array = cJSON_Parse(json_str);
            free(json_str);

            if (results_array && cJSON_IsArray(results_array)) {
                cJSON_AddItemToObject(result, "results", results_array);
                cJSON_AddNumberToObject(result, "count", cJSON_GetArraySize(results_array));
            } else {
                if (results_array) cJSON_Delete(results_array);
                cJSON_AddStringToObject(result, "raw_output", output);
            }
        }
    } else {
        // Return raw output if no JSON found
        cJSON_AddStringToObject(result, "raw_output", output);
    }

    free(output);
    return result;
}

// ============================================================================
// Tool: web_read - Navigate to URL and extract content
// ============================================================================

cJSON* tool_web_read(cJSON *params, void *state) {
    (void)state;

    if (!is_explore_mode_enabled()) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error",
            "web_read is only available in Explore mode. Set KLAWED_EXPLORE_MODE=1");
        return error;
    }

    if (!is_web_agent_available()) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error",
            "web_browse_agent binary not found. Build it with: cd tools/web_browse_agent && make");
        return error;
    }

    cJSON *url_json = cJSON_GetObjectItem(params, "url");
    if (!url_json || !cJSON_IsString(url_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "url parameter is required");
        return error;
    }

    const char *url = url_json->valuestring;
    int max_length = 50000;

    cJSON *max_json = cJSON_GetObjectItem(params, "max_length");
    if (max_json && cJSON_IsNumber(max_json)) {
        max_length = (int)max_json->valuedouble;
        if (max_length < 1000) max_length = 1000;
        if (max_length > 100000) max_length = 100000;
    }

    // Build prompt for web_browse_agent
    char prompt[4096];
    snprintf(prompt, sizeof(prompt),
        "Navigate to %s using browser_navigate, then use get_page_content to extract "
        "the main text content. Output the content with a citation header showing "
        "the page title and URL. Limit output to %d characters.",
        url, max_length);

    int exit_code;
    char *output = execute_web_agent(prompt, &exit_code);

    cJSON *result = cJSON_CreateObject();

    if (!output) {
        cJSON_AddStringToObject(result, "error", "Failed to read web page");
        return result;
    }

    cJSON_AddStringToObject(result, "url", url);
    cJSON_AddStringToObject(result, "content", output);

    if (exit_code != 0) {
        cJSON_AddStringToObject(result, "warning",
            "web_browse_agent exited with non-zero status");
    }

    free(output);
    return result;
}

// ============================================================================
// Tool: web_browse_agent - sessionful browser automation
// ============================================================================
//
// New REPL-style API (v1.0.0):
// - Uses persistent sessions with --session flag
// - Commands are sent one at a time to a running browser instance
// - Driver process auto-terminates when klawed exits (parent PID monitoring)
// - Use 'commands' subcommand to list available browser commands
//

// Execute web_browse_agent with session-based command
// Handles TUI protection by temporarily redirecting stderr when in interactive mode
static char* execute_web_agent_session(const char *session_id, const char *command,
                                       const char *const *args, int nargs,
                                       int *exit_code) {
    if (!session_id || !command) {
        return NULL;
    }

    const char *agent_path = get_web_agent_path();
    int headless = is_headless_mode();

    // Calculate command buffer size (extra space for </dev/null 2>&1 suffix)
    size_t cmd_size = strlen(agent_path) + strlen(session_id) + strlen(command) + 300;
    for (int i = 0; i < nargs; i++) {
        if (args[i]) {
            cmd_size += strlen(args[i]) * 2 + 4; // extra space for quoting
        }
    }

    char *cmd_buf = malloc(cmd_size);
    if (!cmd_buf) {
        return NULL;
    }

    // Build command: web_browse_agent --session <id> [--headless] <command> [args...]
    // Add </dev/null to prevent stdin interaction, 2>&1 to capture stderr
    int written = snprintf(cmd_buf, cmd_size, "timeout %d %s --session %s %s --json %s",
                           WEB_AGENT_TIMEOUT,
                           agent_path,
                           session_id,
                           headless ? "--headless" : "",
                           command);

    // Append arguments
    for (int i = 0; i < nargs && args[i]; i++) {
        size_t remaining = cmd_size - (size_t)written;
        // Simple shell quoting - wrap in single quotes, escape existing single quotes
        written += snprintf(cmd_buf + written, remaining, " '%s'", args[i]);
    }

    // Append stdin/stderr redirections
    size_t remaining = cmd_size - (size_t)written;
    snprintf(cmd_buf + written, remaining, " </dev/null 2>&1");

    LOG_INFO("Executing web_browse_agent: %s", cmd_buf);

    // Temporarily redirect stderr to prevent direct terminal output in TUI mode
    // This prevents corrupting the ncurses display if the subprocess writes to stderr
    int saved_stderr = -1;
    FILE *stderr_redirect = NULL;

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

    FILE *fp = popen(cmd_buf, "r");
    free(cmd_buf);

    if (!fp) {
        LOG_ERROR("Failed to execute web_browse_agent: %s", strerror(errno));
        // Restore stderr before returning
        if (saved_stderr != -1) {
            dup2(saved_stderr, STDERR_FILENO);
            close(saved_stderr);
            fflush(stderr);
        }
        return NULL;
    }

    char *output = malloc(MAX_WEB_OUTPUT);
    if (!output) {
        pclose(fp);
        // Restore stderr before returning
        if (saved_stderr != -1) {
            dup2(saved_stderr, STDERR_FILENO);
            close(saved_stderr);
            fflush(stderr);
        }
        return NULL;
    }

    size_t total = 0;
    size_t n;
    while ((n = fread(output + total, 1, MAX_WEB_OUTPUT - total - 1, fp)) > 0) {
        total += n;
        if (total >= MAX_WEB_OUTPUT - 1) {
            break;
        }
    }
    output[total] = '\0';

    int status = pclose(fp);
    if (exit_code) {
        *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    // Restore stderr after command execution
    if (saved_stderr != -1) {
        dup2(saved_stderr, STDERR_FILENO);
        close(saved_stderr);
        fflush(stderr);
    }

    // Strip ANSI escape sequences to prevent terminal corruption
    char *clean_output = strip_ansi_escapes(output);
    if (clean_output) {
        free(output);
        return clean_output;
    }

    return output;
}

cJSON* tool_web_browse_agent(cJSON *params, void *state) {
    (void)state;

    if (!is_web_agent_configured_only()) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error",
            "web_browse_agent binary not found. Build it with: cd tools/web_browse_agent && make");
        return error;
    }

    // Get session ID - default to "klawed-<pid>" to avoid collisions between instances
    char default_session[32];
    snprintf(default_session, sizeof(default_session), "klawed-%d", (int)getpid());
    const char *session_id = default_session;
    cJSON *session_json = cJSON_GetObjectItem(params, "session");
    if (session_json && cJSON_IsString(session_json) && session_json->valuestring[0] != '\0') {
        session_id = session_json->valuestring;
    }

    // Get command
    cJSON *command_json = cJSON_GetObjectItem(params, "command");
    if (!command_json || !cJSON_IsString(command_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error",
            "command parameter is required. Use 'commands' to list available commands.");
        return error;
    }
    const char *command = command_json->valuestring;

    // Get arguments array (optional)
    cJSON *args_json = cJSON_GetObjectItem(params, "args");
    const char *args[32] = {NULL};
    int nargs = 0;

    if (args_json && cJSON_IsArray(args_json)) {
        cJSON *arg;
        cJSON_ArrayForEach(arg, args_json) {
            if (cJSON_IsString(arg) && nargs < 31) {
                args[nargs++] = arg->valuestring;
            }
        }
    }

    // Emit command info to TUI
    char cmd_info[512];
    if (nargs > 0) {
        snprintf(cmd_info, sizeof(cmd_info), "[web_browse_agent] session=%s command=%s args[0]=%s",
                 session_id, command, args[0]);
    } else {
        snprintf(cmd_info, sizeof(cmd_info), "[web_browse_agent] session=%s command=%s",
                 session_id, command);
    }
    LOG_INFO("web_browse_agent: command=%s, g_active_tool_queue=%p", command, (void*)g_active_tool_queue);
    if (g_active_tool_queue) {
        tool_emit_line("", cmd_info);
    } else {
        LOG_WARN("web_browse_agent: g_active_tool_queue is NULL, TUI output will not be displayed");
    }

    // Execute command
    int exit_code = -1;
    char *output = execute_web_agent_session(session_id, command, args, nargs, &exit_code);

    cJSON *result = cJSON_CreateObject();
    if (!output) {
        LOG_ERROR("web_browse_agent: execute_web_agent_session returned NULL");
        tool_emit_line("", "[web_browse_agent] Error: Failed to execute");
        cJSON_AddStringToObject(result, "error", "Failed to execute web_browse_agent");
        return result;
    }

    LOG_DEBUG("web_browse_agent: received output length=%zu, exit_code=%d",
              strlen(output), exit_code);

    // Emit output to TUI (show first portion if output is large)
    if (output[0] != '\0') {
        LOG_DEBUG("web_browse_agent: emitting output to TUI (g_active_tool_queue=%p)",
                  (void*)g_active_tool_queue);
        
        // Add a visual separator before the output
        tool_emit_line("", "[web_browse_agent] Output:");
        
        // Show output line by line, up to a reasonable limit
        char *line_start = output;
        char *line_end;
        int line_count = 0;
        const int max_lines = 50;  // Limit output lines shown in TUI

        while ((line_end = strchr(line_start, '\n')) != NULL && line_count < max_lines) {
            *line_end = '\0';
            if (line_start[0] != '\0') {  // Skip empty lines
                LOG_DEBUG("web_browse_agent: emitting line %d: %.80s", line_count + 1, line_start);
                tool_emit_line(" ", line_start);
            }
            *line_end = '\n';  // Restore newline
            line_start = line_end + 1;
            line_count++;
        }

        // Show last line if no trailing newline
        if (*line_start != '\0' && line_count < max_lines) {
            LOG_DEBUG("web_browse_agent: emitting last line (no trailing newline): %.80s", line_start);
            tool_emit_line(" ", line_start);
            line_count++;
        }

        // Indicate if output was truncated
        if (line_count >= max_lines && *line_start != '\0') {
            LOG_DEBUG("web_browse_agent: output truncated at %d lines", max_lines);
            tool_emit_line(" ", "[... output truncated ...]");
        }

        LOG_DEBUG("web_browse_agent: emitted %d lines to TUI", line_count);
    } else {
        LOG_DEBUG("web_browse_agent: output is empty");
    }

    // Show exit code if non-zero
    if (exit_code != 0) {
        char exit_info[64];
        snprintf(exit_info, sizeof(exit_info), "[web_browse_agent] exit_code=%d", exit_code);
        tool_emit_line("", exit_info);
    }

    // Try to parse JSON output
    cJSON *json_output = cJSON_Parse(output);
    if (json_output) {
        cJSON_AddItemToObject(result, "result", json_output);
    } else {
        cJSON_AddStringToObject(result, "output", output);
    }

    cJSON_AddNumberToObject(result, "exit_code", exit_code);
    cJSON_AddStringToObject(result, "session", session_id);
    cJSON_AddStringToObject(result, "command", command);

    free(output);
    return result;
}

// ============================================================================
// Tool: context7_search - Search for library documentation
// ============================================================================

cJSON* tool_context7_search(cJSON *params, void *state) {
    (void)state;

    if (!is_explore_mode_enabled()) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error",
            "context7_search is only available in Explore mode. Set KLAWED_EXPLORE_MODE=1");
        return error;
    }

    cJSON *query_json = cJSON_GetObjectItem(params, "query");
    cJSON *library_json = cJSON_GetObjectItem(params, "library_name");

    if (!query_json || !cJSON_IsString(query_json) ||
        !library_json || !cJSON_IsString(library_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error",
            "Both query and library_name parameters are required");
        return error;
    }

    char *encoded_query = url_encode(query_json->valuestring);
    char *encoded_library = url_encode(library_json->valuestring);

    if (!encoded_query || !encoded_library) {
        free(encoded_query);
        free(encoded_library);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to encode parameters");
        return error;
    }

    char query_params[2048];
    snprintf(query_params, sizeof(query_params),
             "query=%s&libraryName=%s", encoded_query, encoded_library);

    free(encoded_query);
    free(encoded_library);

    char *response = context7_request("/v2/libs/search", query_params);

    if (!response) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to search Context7");
        return error;
    }

    cJSON *result = cJSON_Parse(response);
    free(response);

    if (!result) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to parse Context7 response");
        return error;
    }

    return result;
}

// ============================================================================
// Tool: context7_docs - Fetch documentation for a library
// ============================================================================

cJSON* tool_context7_docs(cJSON *params, void *state) {
    (void)state;

    if (!is_explore_mode_enabled()) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error",
            "context7_docs is only available in Explore mode. Set KLAWED_EXPLORE_MODE=1");
        return error;
    }

    cJSON *library_id_json = cJSON_GetObjectItem(params, "library_id");
    cJSON *query_json = cJSON_GetObjectItem(params, "query");

    if (!library_id_json || !cJSON_IsString(library_id_json) ||
        !query_json || !cJSON_IsString(query_json)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error",
            "Both library_id and query parameters are required");
        return error;
    }

    char *encoded_library = url_encode(library_id_json->valuestring);
    char *encoded_query = url_encode(query_json->valuestring);

    if (!encoded_library || !encoded_query) {
        free(encoded_library);
        free(encoded_query);
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to encode parameters");
        return error;
    }

    char query_params[2048];
    snprintf(query_params, sizeof(query_params),
             "libraryId=%s&query=%s", encoded_library, encoded_query);

    free(encoded_library);
    free(encoded_query);

    char *response = context7_request("/v2/context", query_params);

    if (!response) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Failed to fetch Context7 documentation");
        return error;
    }

    // The response is plain text, not JSON
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "library_id", library_id_json->valuestring);
    cJSON_AddStringToObject(result, "query", query_json->valuestring);
    cJSON_AddStringToObject(result, "documentation", response);
    cJSON_AddStringToObject(result, "source", "Context7 (https://context7.com)");

    free(response);
    return result;
}

// ============================================================================
// Tool definitions for Explore mode
// ============================================================================

const char* explore_tool_web_browse_agent_schema(void) {
    return "{"
        "\"type\": \"function\","
        "\"function\": {"
            "\"name\": \"web_browse_agent\","
            "\"description\": \"Control a persistent browser session for web automation. "
                "Sessions maintain state (tabs, cookies, history) across commands. "
                "The browser driver auto-terminates when klawed exits. "
                "Use command='commands' to list available browser commands.\","
            "\"parameters\": {"
                "\"type\": \"object\","
                "\"properties\": {"
                    "\"command\": {"
                        "\"type\": \"string\","
                        "\"description\": \"Browser command to execute. Available: open, list-tabs, switch-tab, close-tab, eval, click, type, wait-for, screenshot, html, set-viewport, cookies, session-info, describe-commands, end-session, ping, commands\""
                    "},"
                    "\"args\": {"
                        "\"type\": \"array\","
                        "\"items\": {\"type\": \"string\"},"
                        "\"description\": \"Arguments for the command (e.g., ['https://example.com'] for open, ['#button'] for click)\""
                    "},"
                    "\"session\": {"
                        "\"type\": \"string\","
                        "\"description\": \"Session ID (default: 'klawed'). Use different sessions for parallel browser instances\""
                    "}"
                "},"
                "\"required\": [\"command\"]"
            "}"
        "}"
    "}";
}

const char* explore_tool_web_search_schema(void) {
    return "{"
        "\"type\": \"function\","
        "\"function\": {"
            "\"name\": \"web_search\","
            "\"description\": \"Search the web using DuckDuckGo. Returns search results with titles, URLs, and snippets. Use this to find relevant web pages for research.\","
            "\"parameters\": {"
                "\"type\": \"object\","
                "\"properties\": {"
                    "\"query\": {"
                        "\"type\": \"string\","
                        "\"description\": \"The search query\""
                    "},"
                    "\"max_results\": {"
                        "\"type\": \"integer\","
                        "\"description\": \"Maximum number of results (default: 10, max: 30)\""
                    "}"
                "},"
                "\"required\": [\"query\"]"
            "}"
        "}"
    "}";
}

const char* explore_tool_web_read_schema(void) {
    return "{"
        "\"type\": \"function\","
        "\"function\": {"
            "\"name\": \"web_read\","
            "\"description\": \"Navigate to a URL and extract the main text content. Removes navigation, ads, and other non-content elements. Returns the article/main content with citation.\","
            "\"parameters\": {"
                "\"type\": \"object\","
                "\"properties\": {"
                    "\"url\": {"
                        "\"type\": \"string\","
                        "\"description\": \"The URL to read\""
                    "},"
                    "\"max_length\": {"
                        "\"type\": \"integer\","
                        "\"description\": \"Maximum content length in characters (default: 50000)\""
                    "}"
                "},"
                "\"required\": [\"url\"]"
            "}"
        "}"
    "}";
}

const char* explore_tool_context7_search_schema(void) {
    return "{"
        "\"type\": \"function\","
        "\"function\": {"
            "\"name\": \"context7_search\","
            "\"description\": \"Search for library/package documentation using Context7. Returns matching libraries with their IDs, descriptions, and snippet counts. Use this before context7_docs to find the correct library ID.\","
            "\"parameters\": {"
                "\"type\": \"object\","
                "\"properties\": {"
                    "\"query\": {"
                        "\"type\": \"string\","
                        "\"description\": \"What you're trying to accomplish (used for relevance ranking)\""
                    "},"
                    "\"library_name\": {"
                        "\"type\": \"string\","
                        "\"description\": \"The library/package name to search for\""
                    "}"
                "},"
                "\"required\": [\"query\", \"library_name\"]"
            "}"
        "}"
    "}";
}

const char* explore_tool_context7_docs_schema(void) {
    return "{"
        "\"type\": \"function\","
        "\"function\": {"
            "\"name\": \"context7_docs\","
            "\"description\": \"Fetch up-to-date documentation for a library using Context7. Use context7_search first to find the library ID unless you already know it (format: /org/project).\","
            "\"parameters\": {"
                "\"type\": \"object\","
                "\"properties\": {"
                    "\"library_id\": {"
                        "\"type\": \"string\","
                        "\"description\": \"Context7 library ID (e.g., '/vercel/next.js', '/facebook/react')\""
                    "},"
                    "\"query\": {"
                        "\"type\": \"string\","
                        "\"description\": \"What you want to learn about the library (e.g., 'routing', 'authentication')\""
                    "}"
                "},"
                "\"required\": [\"library_id\", \"query\"]"
            "}"
        "}"
    "}";
}
