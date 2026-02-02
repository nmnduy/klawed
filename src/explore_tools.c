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
#include <time.h>
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

    const char *tools_path = "tools/web_browse_agent/web_browse_agent";
    if (access(tools_path, X_OK) == 0) {
        if (strlcpy(resolved_path, tools_path, sizeof(resolved_path)) < sizeof(resolved_path)) {
            return resolved_path;
        }
    }

    // Also check for bin subdirectory (legacy path)
    tools_path = "tools/web_browse_agent/bin/web_browse_agent";
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
// Tool: web_search - Search the web using DuckDuckGo Lite
// ============================================================================

cJSON* tool_web_search(cJSON *params, void *state) {
    (void)state;

    if (!is_explore_mode_enabled()) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error",
            "web_search is only available in Explore mode. Set KLAWED_EXPLORE_MODE=1");
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

    cJSON *result = cJSON_CreateObject();

    // Use DuckDuckGo Lite HTML interface
    char *encoded_query = url_encode(query);
    if (!encoded_query) {
        cJSON_AddStringToObject(result, "error", "Failed to encode query");
        return result;
    }

    char url[2048];
    snprintf(url, sizeof(url), "https://lite.duckduckgo.com/lite/?q=%s", encoded_query);
    free(encoded_query);

    LOG_INFO("web_search: Searching DuckDuckGo Lite: %s", url);

    CURL *curl = curl_easy_init();
    if (!curl) {
        cJSON_AddStringToObject(result, "error", "Failed to initialize HTTP client");
        return result;
    }

    ResponseBuffer response = {0};
    response.data = malloc(1);
    if (!response.data) {
        curl_easy_cleanup(curl);
        cJSON_AddStringToObject(result, "error", "Out of memory");
        return result;
    }
    response.data[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "klawed/1.0");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        LOG_ERROR("web_search: DuckDuckGo request failed: %s", curl_easy_strerror(res));
        free(response.data);
        cJSON_AddStringToObject(result, "error", "Failed to fetch search results");
        return result;
    }

    // Parse HTML results - DuckDuckGo Lite returns simple HTML with results
    // Each result is in a <tr> with class "result-link" or similar
    // For now, return the raw HTML and let the LLM parse it
    cJSON_AddStringToObject(result, "search_engine", "DuckDuckGo Lite");
    cJSON_AddStringToObject(result, "query", query);
    cJSON_AddStringToObject(result, "html_results", response.data);
    cJSON_AddNumberToObject(result, "max_requested", max_results);

    free(response.data);
    return result;
}

// ============================================================================
// Tool: web_read - Navigate to URL and extract content using web_browse_agent
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

    cJSON *result = cJSON_CreateObject();

    const char *agent_path = get_web_agent_path();
    char session_id[64];
    snprintf(session_id, sizeof(session_id), "klawed-read-%d", (int)time(NULL));

    // Step 1: Open the URL
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "timeout %d %s --session %s open \"%s\" </dev/null 2>&1",
             WEB_AGENT_TIMEOUT, agent_path, session_id, url);

    LOG_INFO("web_read: Opening URL: %s", url);
    int status = system(cmd);
    if (status != 0) {
        cJSON_AddStringToObject(result, "error", "Failed to open URL");
        cJSON_AddStringToObject(result, "url", url);
        return result;
    }

    // Step 2: Get the HTML content
    snprintf(cmd, sizeof(cmd),
             "timeout %d %s --session %s html </dev/null 2>&1",
             WEB_AGENT_TIMEOUT, agent_path, session_id);

    LOG_INFO("web_read: Getting HTML content");
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        cJSON_AddStringToObject(result, "error", "Failed to get page content");
        cJSON_AddStringToObject(result, "url", url);
        return result;
    }

    char *output = malloc(MAX_WEB_OUTPUT);
    if (!output) {
        pclose(fp);
        cJSON_AddStringToObject(result, "error", "Out of memory");
        cJSON_AddStringToObject(result, "url", url);
        return result;
    }

    size_t total = 0;
    size_t n;
    while ((n = fread(output + total, 1, MAX_WEB_OUTPUT - total - 1, fp)) > 0) {
        total += n;
        if (total >= MAX_WEB_OUTPUT - 1) break;
    }
    output[total] = '\0';
    pclose(fp);

    // Step 3: End the session (best effort cleanup)
    snprintf(cmd, sizeof(cmd),
             "timeout 10 %s --session %s end-session </dev/null 2>&1",
             agent_path, session_id);
    int cleanup_status __attribute__((unused)) = system(cmd);

    cJSON_AddStringToObject(result, "url", url);
    cJSON_AddStringToObject(result, "content", output);
    free(output);

    return result;
}

// ============================================================================
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
