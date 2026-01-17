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

#define CONTEXT7_API_BASE "https://context7.com/api"
#define MAX_WEB_OUTPUT 100000  // 100KB max output from web_browse_agent
#define WEB_AGENT_TIMEOUT 120  // 2 minute timeout for web operations

// Get the path to web_browse_agent binary, preferring env override, then vendor copy, then PATH
static const char* get_web_agent_path(void) {
    static char resolved_path[PATH_MAX];

    const char *env_path = getenv("KLAWED_WEB_BROWSE_AGENT_PATH");
    if (env_path && env_path[0] != '\0' && access(env_path, X_OK) == 0) {
        return env_path;
    }

    const char *vendor_path = "vendors/web_browse_agent/web_browse_agent";
    if (access(vendor_path, X_OK) == 0) {
        if (strlcpy(resolved_path, vendor_path, sizeof(resolved_path)) < sizeof(resolved_path)) {
            return resolved_path;
        }
    }

    const char *path_env = getenv("PATH");
    if (!path_env || path_env[0] == '\0') {
        return env_path && env_path[0] != '\0' ? env_path : vendor_path;
    }

    char *path_copy = strdup(path_env);
    if (!path_copy) {
        return env_path && env_path[0] != '\0' ? env_path : vendor_path;
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

    return env_path && env_path[0] != '\0' ? env_path : vendor_path;
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

    // Build command with proper escaping
    size_t cmd_size = strlen(agent_path) + strlen(prompt) * 2 + 256;
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

    snprintf(command, cmd_size, "timeout %d %s %s --no-browser \"%s\" 2>&1",
             WEB_AGENT_TIMEOUT,
             agent_path,
             headless ? "--headless" : "",
             escaped_prompt);

    free(escaped_prompt);

    LOG_INFO("Executing web_browse_agent: %s", command);

    FILE *fp = popen(command, "r");
    free(command);

    if (!fp) {
        LOG_ERROR("Failed to execute web_browse_agent: %s", strerror(errno));
        return NULL;
    }

    // Read output
    char *output = malloc(MAX_WEB_OUTPUT);
    if (!output) {
        pclose(fp);
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

    return output;
}

// Execute web_browse_agent with a raw command string (no prompt wrapping)
static char* execute_web_agent_raw(const char *args, int *exit_code) {
    if (!args) {
        return NULL;
    }

    const char *agent_path = get_web_agent_path();
    int headless = is_headless_mode();

    size_t cmd_size = strlen(agent_path) + strlen(args) + 256;
    char *command = malloc(cmd_size);
    if (!command) {
        return NULL;
    }

    snprintf(command, cmd_size, "timeout %d %s %s %s 2>&1",
             WEB_AGENT_TIMEOUT,
             agent_path,
             headless ? "--headless" : "",
             args);

    LOG_INFO("Executing web_browse_agent (raw): %s", command);

    FILE *fp = popen(command, "r");
    free(command);

    if (!fp) {
        LOG_ERROR("Failed to execute web_browse_agent: %s", strerror(errno));
        return NULL;
    }

    char *output = malloc(MAX_WEB_OUTPUT);
    if (!output) {
        pclose(fp);
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
            "web_browse_agent binary not found. Build it with: cd vendors/web_browse_agent && make");
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
            "web_browse_agent binary not found. Build it with: cd vendors/web_browse_agent && make");
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
// Tool: web_browse_agent - direct access to binary (no explore mode gate)
// ============================================================================

cJSON* tool_web_browse_agent(cJSON *params, void *state) {
    (void)state;

    if (!is_web_agent_configured_only()) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error",
            "web_browse_agent path not set. Set KLAWED_WEB_BROWSE_AGENT_PATH or place the binary at vendors/web_browse_agent/web_browse_agent");
        return error;
    }

    cJSON *args_json = cJSON_GetObjectItem(params, "args");
    cJSON *prompt_json = cJSON_GetObjectItem(params, "prompt");
    const char *prompt = NULL;
    const char *args = NULL;

    if (prompt_json && cJSON_IsString(prompt_json)) {
        prompt = prompt_json->valuestring;
    }
    if (args_json && cJSON_IsString(args_json)) {
        args = args_json->valuestring;
    }

    if (!prompt && !args) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error",
            "Provide either prompt (for wrapped mode) or args (raw arguments) for web_browse_agent");
        return error;
    }

    int exit_code = -1;
    char *output = NULL;
    if (prompt && args) {
        output = execute_web_agent_raw(args, &exit_code);
    } else if (prompt) {
        output = execute_web_agent(prompt, &exit_code);
    } else {
        output = execute_web_agent_raw(args, &exit_code);
    }

    cJSON *result = cJSON_CreateObject();
    if (!output) {
        cJSON_AddStringToObject(result, "error", "Failed to execute web_browse_agent");
        return result;
    }

    cJSON_AddNumberToObject(result, "exit_code", exit_code);
    cJSON_AddStringToObject(result, "output", output);

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
            "\"description\": \"Directly run the web_browse_agent binary. Provide either a full argument string (args) or a prompt to wrap. Headless mode follows KLAWED_EXPLORE_HEADLESS (default: on).\","
            "\"parameters\": {"
                "\"type\": \"object\","
                "\"properties\": {"
                    "\"args\": {\"type\": \"string\", \"description\": \"Raw arguments to pass to web_browse_agent (e.g., 'browser_navigate https://example.com')\"},"
                    "\"prompt\": {\"type\": \"string\", \"description\": \"Optional prompt; if provided, prompt mode is used instead of raw args\"}"
                "}"
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
