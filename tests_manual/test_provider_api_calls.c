/*
 * test_provider_api_calls.c - Manual provider API call tests
 *
 * Makes REAL API calls to LLM providers and validates tool handling.
 * These tests cost money and are NOT part of the normal test suite.
 *
 * Usage:
 *   make -C tests_manual
 *   ./tests_manual/test_provider_api_calls [options]
 *
 * Options:
 *   --list-providers     List all configured providers from ~/.klawed/config.json
 *   --provider <name>    Use specific provider (overrides config)
 *   --test-read          Test Read tool
 *   --test-glob          Test Glob tool
 *   --test-grep          Test Grep tool
 *   --test-bash          Test Bash tool
 *   --test-multi         Test multiple tools
 *   --test-all           Run all tests
 *   --verbose            Show detailed output
 *   --help               Show help
 *
 * Environment:
 *   KLAWED_LLM_PROVIDER  Override provider selection
 *
 * WARNING: These tests make ACTUAL API calls and may incur costs!
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <bsd/string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#include "../src/config.h"

// Test configuration
#define MAX_TESTS 10
#define MAX_RESPONSE_SIZE (1024 * 1024)  // 1MB
#define DEFAULT_TIMEOUT_MS 120000         // 2 minutes
#undef MAX_TOKENS
#define MAX_TOKENS 4096

// Bash command safelist - only read-only commands allowed
static const char *BASH_SAFE_COMMANDS[] = {
    "ls", "cat", "pwd", "echo", "git status", "git log", "git branch",
    "wc", "head", "tail", "find", "grep", "ps", "df", "du", "whoami",
    "uname", "date", "env", "printenv", "id", "groups", "hostname",
    "which", "whereis", "file", "stat", "readlink", "realpath",
    "curl -I", "curl -s", "curl --head",  // HEAD requests only
    NULL
};

// Test types
typedef enum {
    TEST_READ,
    TEST_GLOB,
    TEST_GREP,
    TEST_BASH,
    TEST_MULTI,
    TEST_COUNT
} TestType;

// Test definition
typedef struct {
    TestType type;
    const char *name;
    const char *query;
    const char *expected_tools;
} TestDefinition;

// Test result
typedef struct {
    const char *name;
    int passed;
    int failed;
    double duration_ms;
    char *tools_used;
    char *response;
    char *error;
} TestResult;

// Tool call from LLM (local definition to avoid conflict with klawed_internal.h)
typedef struct {
    char id[64];
    char name[32];
    cJSON *arguments;
} APITestToolCall;

// Global options
static int verbose = 0;
static int max_turns = 10;  // Maximum conversation turns to prevent runaway

// Test definitions
static const TestDefinition TESTS[] = {
    {TEST_READ, "read",
     "Read the README.md file and tell me what this project is about in one sentence.",
     "Read"},
    {TEST_GLOB, "glob",
     "List all C source files in the src/ directory using the Glob tool.",
     "Glob"},
    {TEST_GREP, "grep",
     "Search for 'TODO' comments in the codebase using the Grep tool.",
     "Grep"},
    {TEST_BASH, "bash",
     "Check the git status of this repository using the Bash tool.",
     "Bash"},
    {TEST_MULTI, "multi",
     "Find test files using Glob, then read the first one you find to tell me what it tests.",
     "Glob,Read"},
};

// ============================================================================
// Utility Functions
// ============================================================================

static void print_header(const char *title) {
    printf("\n%s\n", title);
    size_t len = strlen(title);
    for (size_t i = 0; i < len; i++) putchar('=');
    printf("\n");
}

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static int is_bash_command_safe(const char *command) {
    if (!command || !command[0]) return 0;

    // Check for dangerous patterns
    const char *dangerous[] = {
        ";", "&&", "||", "|", ">", "<", "`", "$", "rm ", "mv ", "cp ",
        "chmod ", "chown ", "sudo ", "su ", "dd ", "mkfs", "fdisk",
        "wget ", "curl -o", "curl --output", "curl -O",
        NULL
    };

    for (int i = 0; dangerous[i]; i++) {
        if (strstr(command, dangerous[i]) != NULL) {
            return 0;
        }
    }

    // Check against safelist
    for (int i = 0; BASH_SAFE_COMMANDS[i]; i++) {
        size_t len = strlen(BASH_SAFE_COMMANDS[i]);
        if (strncmp(command, BASH_SAFE_COMMANDS[i], len) == 0) {
            return 1;
        }
    }

    return 0;
}

// ============================================================================
// Tool Definitions (JSON schema for API)
// ============================================================================

static cJSON* create_tool_definitions(void) {
    cJSON *tools = cJSON_CreateArray();

    // Read tool
    cJSON *read_tool = cJSON_CreateObject();
    cJSON_AddStringToObject(read_tool, "type", "function");
    cJSON *read_func = cJSON_CreateObject();
    cJSON_AddStringToObject(read_func, "name", "Read");
    cJSON_AddStringToObject(read_func, "description", "Read a file from the filesystem");
    cJSON *read_params = cJSON_CreateObject();
    cJSON_AddStringToObject(read_params, "type", "object");
    cJSON *read_props = cJSON_CreateObject();
    cJSON *file_path = cJSON_CreateObject();
    cJSON_AddStringToObject(file_path, "type", "string");
    cJSON_AddStringToObject(file_path, "description", "Absolute path to the file");
    cJSON_AddItemToObject(read_props, "file_path", file_path);
    cJSON_AddItemToObject(read_params, "properties", read_props);
    cJSON *read_required = cJSON_CreateArray();
    cJSON_AddItemToArray(read_required, cJSON_CreateString("file_path"));
    cJSON_AddItemToObject(read_params, "required", read_required);
    cJSON_AddItemToObject(read_func, "parameters", read_params);
    cJSON_AddItemToObject(read_tool, "function", read_func);
    cJSON_AddItemToArray(tools, read_tool);

    // Glob tool
    cJSON *glob_tool = cJSON_CreateObject();
    cJSON_AddStringToObject(glob_tool, "type", "function");
    cJSON *glob_func = cJSON_CreateObject();
    cJSON_AddStringToObject(glob_func, "name", "Glob");
    cJSON_AddStringToObject(glob_func, "description", "Find files matching a pattern");
    cJSON *glob_params = cJSON_CreateObject();
    cJSON_AddStringToObject(glob_params, "type", "object");
    cJSON *glob_props = cJSON_CreateObject();
    cJSON *pattern = cJSON_CreateObject();
    cJSON_AddStringToObject(pattern, "type", "string");
    cJSON_AddStringToObject(pattern, "description", "Glob pattern to match");
    cJSON_AddItemToObject(glob_props, "pattern", pattern);
    cJSON_AddItemToObject(glob_params, "properties", glob_props);
    cJSON *glob_required = cJSON_CreateArray();
    cJSON_AddItemToArray(glob_required, cJSON_CreateString("pattern"));
    cJSON_AddItemToObject(glob_params, "required", glob_required);
    cJSON_AddItemToObject(glob_func, "parameters", glob_params);
    cJSON_AddItemToObject(glob_tool, "function", glob_func);
    cJSON_AddItemToArray(tools, glob_tool);

    // Grep tool
    cJSON *grep_tool = cJSON_CreateObject();
    cJSON_AddStringToObject(grep_tool, "type", "function");
    cJSON *grep_func = cJSON_CreateObject();
    cJSON_AddStringToObject(grep_func, "name", "Grep");
    cJSON_AddStringToObject(grep_func, "description", "Search for text patterns in files");
    cJSON *grep_params = cJSON_CreateObject();
    cJSON_AddStringToObject(grep_params, "type", "object");
    cJSON *grep_props = cJSON_CreateObject();
    cJSON *grep_pattern = cJSON_CreateObject();
    cJSON_AddStringToObject(grep_pattern, "type", "string");
    cJSON_AddStringToObject(grep_pattern, "description", "Pattern to search for");
    cJSON_AddItemToObject(grep_props, "pattern", grep_pattern);
    cJSON *path = cJSON_CreateObject();
    cJSON_AddStringToObject(path, "type", "string");
    cJSON_AddStringToObject(path, "description", "Path to search in (default: current directory)");
    cJSON_AddItemToObject(grep_props, "path", path);
    cJSON_AddItemToObject(grep_params, "properties", grep_props);
    cJSON *grep_required = cJSON_CreateArray();
    cJSON_AddItemToArray(grep_required, cJSON_CreateString("pattern"));
    cJSON_AddItemToObject(grep_params, "required", grep_required);
    cJSON_AddItemToObject(grep_func, "parameters", grep_params);
    cJSON_AddItemToObject(grep_tool, "function", grep_func);
    cJSON_AddItemToArray(tools, grep_tool);

    // Bash tool (restricted to read-only)
    cJSON *bash_tool = cJSON_CreateObject();
    cJSON_AddStringToObject(bash_tool, "type", "function");
    cJSON *bash_func = cJSON_CreateObject();
    cJSON_AddStringToObject(bash_func, "name", "Bash");
    cJSON_AddStringToObject(bash_func, "description",
        "Execute a bash command (read-only: ls, cat, pwd, git status, etc.)");
    cJSON *bash_params = cJSON_CreateObject();
    cJSON_AddStringToObject(bash_params, "type", "object");
    cJSON *bash_props = cJSON_CreateObject();
    cJSON *command = cJSON_CreateObject();
    cJSON_AddStringToObject(command, "type", "string");
    cJSON_AddStringToObject(command, "description", "Command to execute");
    cJSON_AddItemToObject(bash_props, "command", command);
    cJSON_AddItemToObject(bash_params, "properties", bash_props);
    cJSON *bash_required = cJSON_CreateArray();
    cJSON_AddItemToArray(bash_required, cJSON_CreateString("command"));
    cJSON_AddItemToObject(bash_params, "required", bash_required);
    cJSON_AddItemToObject(bash_func, "parameters", bash_params);
    cJSON_AddItemToObject(bash_tool, "function", bash_func);
    cJSON_AddItemToArray(tools, bash_tool);

    return tools;
}

// ============================================================================
// Tool Execution
// ============================================================================

static char* read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        return strdup("Error: File not found");
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size > 100000) {  // Limit to 100KB
        fclose(f);
        return strdup("Error: File too large (max 100KB)");
    }

    char *content = malloc(size + 1);
    if (!content) {
        fclose(f);
        return strdup("Error: Memory allocation failed");
    }

    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);
    return content;
}

static char* execute_glob(const char *pattern) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "find . -path '%s' -type f 2>/dev/null | head -20", pattern);

    // Replace ** with proper find pattern
    char *clean_pattern = strdup(pattern);
    char *p = clean_pattern;
    while (*p) {
        if (*p == '*' && *(p+1) == '*') {
            // Simplify ** to *
            memmove(p, p+1, strlen(p));
        }
        p++;
    }

    snprintf(cmd, sizeof(cmd), "find . -name '%s' -type f 2>/dev/null | head -20", clean_pattern);
    free(clean_pattern);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) return strdup("Error: Failed to execute glob");

    char buffer[4096];
    size_t total = 0;
    char *result = malloc(4096);
    result[0] = '\0';

    while (fgets(buffer, sizeof(buffer), pipe)) {
        size_t len = strlen(buffer);
        if (total + len + 1 > 4096) break;
        strcat(result, buffer);
        total += len;
    }

    pclose(pipe);
    return result;
}

static char* execute_grep(const char *pattern, const char *search_path) {
    char cmd[1024];
    const char *path = search_path && search_path[0] ? search_path : ".";
    snprintf(cmd, sizeof(cmd), "grep -r '%s' '%s' 2>/dev/null | head -20", pattern, path);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) return strdup("Error: Failed to execute grep");

    char buffer[4096];
    size_t total = 0;
    char *result = malloc(4096);
    result[0] = '\0';

    while (fgets(buffer, sizeof(buffer), pipe)) {
        size_t len = strlen(buffer);
        if (total + len + 1 > 4096) break;
        strcat(result, buffer);
        total += len;
    }

    pclose(pipe);
    if (total == 0) {
        free(result);
        return strdup("No matches found");
    }
    return result;
}

static char* execute_bash(const char *command) {
    if (!is_bash_command_safe(command)) {
        return strdup("Error: Command not in safelist. Only read-only commands allowed.");
    }

    FILE *pipe = popen(command, "r");
    if (!pipe) return strdup("Error: Failed to execute command");

    char buffer[4096];
    size_t total = 0;
    size_t capacity = 4096;
    char *result = malloc(capacity);
    result[0] = '\0';

    while (fgets(buffer, sizeof(buffer), pipe)) {
        size_t len = strlen(buffer);
        if (total + len + 1 > capacity) {
            capacity *= 2;
            result = realloc(result, capacity);
        }
        strcat(result, buffer);
        total += len;
    }

    pclose(pipe);

    if (total == 0) {
        free(result);
        return strdup("(no output)");
    }
    return result;
}

static char* api_test_execute_tool(const char *name, cJSON *arguments, char **tool_name_out) {
    *tool_name_out = strdup(name);

    if (strcmp(name, "Read") == 0) {
        cJSON *file_path = cJSON_GetObjectItem(arguments, "file_path");
        if (cJSON_IsString(file_path)) {
            return read_file(file_path->valuestring);
        }
        return strdup("Error: Missing file_path");
    }

    if (strcmp(name, "Glob") == 0) {
        cJSON *pattern = cJSON_GetObjectItem(arguments, "pattern");
        if (cJSON_IsString(pattern)) {
            return execute_glob(pattern->valuestring);
        }
        return strdup("Error: Missing pattern");
    }

    if (strcmp(name, "Grep") == 0) {
        cJSON *pattern = cJSON_GetObjectItem(arguments, "pattern");
        cJSON *path = cJSON_GetObjectItem(arguments, "path");
        if (cJSON_IsString(pattern)) {
            return execute_grep(pattern->valuestring,
                               cJSON_IsString(path) ? path->valuestring : NULL);
        }
        return strdup("Error: Missing pattern");
    }

    if (strcmp(name, "Bash") == 0) {
        cJSON *command = cJSON_GetObjectItem(arguments, "command");
        if (cJSON_IsString(command)) {
            return execute_bash(command->valuestring);
        }
        return strdup("Error: Missing command");
    }

    free(*tool_name_out);
    *tool_name_out = NULL;
    return strdup("Error: Unknown tool");
}

// ============================================================================
// HTTP Client
// ============================================================================

typedef struct {
    char *data;
    size_t size;
} ResponseBuffer;

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    ResponseBuffer *buf = (ResponseBuffer*)userp;

    char *new_data = realloc(buf->data, buf->size + total + 1);
    if (!new_data) return 0;

    buf->data = new_data;
    memcpy(buf->data + buf->size, contents, total);
    buf->size += total;
    buf->data[buf->size] = '\0';

    return total;
}

static char* make_api_request(const LLMProviderConfig *config, const char *request_body,
                              long *status_code, char **error_msg) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        *error_msg = strdup("Failed to initialize CURL");
        return NULL;
    }

    ResponseBuffer resp = {0};
    resp.data = malloc(1);
    resp.data[0] = '\0';

    struct curl_slist *headers = NULL;

    // Provider-specific headers
    if (config->provider_type == PROVIDER_ANTHROPIC) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
        if (config->api_key[0]) {
            char auth_header[512];
            snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", config->api_key);
            headers = curl_slist_append(headers, auth_header);
        }
    } else if (config->provider_type == PROVIDER_OPENAI ||
               config->provider_type == PROVIDER_DEEPSEEK ||
               config->provider_type == PROVIDER_MOONSHOT) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        if (config->api_key[0]) {
            char auth_header[512];
            snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", config->api_key);
            headers = curl_slist_append(headers, auth_header);
        }
    } else if (config->provider_type == PROVIDER_BEDROCK) {
        // Bedrock uses AWS SigV4 - would need aws-sdk or custom signing
        *error_msg = strdup("Bedrock not supported in this test (requires AWS signing)");
        curl_easy_cleanup(curl);
        free(resp.data);
        curl_slist_free_all(headers);
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, config->api_base);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, DEFAULT_TIMEOUT_MS);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 30000);

    // Suppress progress output unless verbose
    if (!verbose) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    }

    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status_code);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        *error_msg = strdup(curl_easy_strerror(res));
        free(resp.data);
        return NULL;
    }

    return resp.data;
}

// ============================================================================
// Request/Response Building
// ============================================================================

static cJSON* build_messages_array(const char *system_prompt, const char *user_query) {
    cJSON *messages = cJSON_CreateArray();

    if (system_prompt) {
        cJSON *sys = cJSON_CreateObject();
        cJSON_AddStringToObject(sys, "role", "system");
        cJSON_AddStringToObject(sys, "content", system_prompt);
        cJSON_AddItemToArray(messages, sys);
    }

    cJSON *user = cJSON_CreateObject();
    cJSON_AddStringToObject(user, "role", "user");
    cJSON_AddStringToObject(user, "content", user_query);
    cJSON_AddItemToArray(messages, user);

    return messages;
}

static char* build_request_body(const LLMProviderConfig *config, cJSON *messages,
                                cJSON *tools, int stream_unused) {
    (void)stream_unused;
    cJSON *req = cJSON_CreateObject();

    cJSON_AddStringToObject(req, "model", config->model);
    cJSON_AddItemToObject(req, "messages", cJSON_Duplicate(messages, 1));
    cJSON_AddNumberToObject(req, "max_tokens", MAX_TOKENS);

    if (tools) {
        cJSON_AddItemToObject(req, "tools", cJSON_Duplicate(tools, 1));
    }

    // Provider-specific parameters
    if (config->provider_type == PROVIDER_ANTHROPIC) {
        // Anthropic uses max_tokens, already set
    } else {
        // OpenAI-compatible uses max_completion_tokens
        cJSON *max_toks = cJSON_GetObjectItem(req, "max_tokens");
        if (max_toks) {
            cJSON_AddNumberToObject(req, "max_completion_tokens", max_toks->valueint);
        }
    }

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    return body;
}

static int parse_tool_calls(cJSON *response, APITestToolCall *calls, int max_calls) {
    cJSON *choices = cJSON_GetObjectItem(response, "choices");
    if (!choices || !cJSON_IsArray(choices)) return 0;

    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    if (!choice) return 0;

    cJSON *message = cJSON_GetObjectItem(choice, "message");
    if (!message) return 0;

    cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
    if (!tool_calls || !cJSON_IsArray(tool_calls)) return 0;

    int count = 0;
    cJSON *tc = NULL;
    cJSON_ArrayForEach(tc, tool_calls) {
        if (count >= max_calls) break;

        cJSON *id = cJSON_GetObjectItem(tc, "id");
        cJSON *func = cJSON_GetObjectItem(tc, "function");

        if (func) {
            cJSON *name = cJSON_GetObjectItem(func, "name");
            cJSON *args = cJSON_GetObjectItem(func, "arguments");

            if (id && cJSON_IsString(id)) {
                strlcpy(calls[count].id, id->valuestring, sizeof(calls[count].id));
            }
            if (name && cJSON_IsString(name)) {
                strlcpy(calls[count].name, name->valuestring, sizeof(calls[count].name));
            }
            if (args && cJSON_IsString(args)) {
                calls[count].arguments = cJSON_Parse(args->valuestring);
            } else if (args && cJSON_IsObject(args)) {
                calls[count].arguments = cJSON_Duplicate(args, 1);
            } else {
                calls[count].arguments = cJSON_CreateObject();
            }
            count++;
        }
    }

    return count;
}

static char* extract_response_text(cJSON *response) {
    cJSON *choices = cJSON_GetObjectItem(response, "choices");
    if (!choices || !cJSON_IsArray(choices)) return NULL;

    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    if (!choice) return NULL;

    cJSON *message = cJSON_GetObjectItem(choice, "message");
    if (!message) return NULL;

    cJSON *content = cJSON_GetObjectItem(message, "content");
    if (cJSON_IsString(content)) {
        return strdup(content->valuestring);
    }

    return NULL;
}

// ============================================================================
// Conversation Loop
// ============================================================================

static char* run_conversation(const LLMProviderConfig *config, const char *query,
                              char **tools_used_out, char **error_out) {
    const char *system_prompt =
        "You are a helpful assistant with access to file system tools. "
        "Use the tools provided to answer the user's question. "
        "When done, provide a clear, concise answer.";

    cJSON *messages = build_messages_array(system_prompt, query);
    cJSON *tools = create_tool_definitions();

    char *final_response = NULL;
    char tools_used[512] = "";
    int turn = 0;

    while (turn < max_turns) {
        turn++;

        // Build and send request
        char *body = build_request_body(config, messages, tools, 0);
        if (verbose) {
            printf("  [Request turn %d]\n", turn);
        }

        long status = 0;
        char *error = NULL;
        char *resp_str = make_api_request(config, body, &status, &error);
        free(body);

        if (!resp_str) {
            *error_out = error ? error : strdup("Unknown error");
            break;
        }

        if (status != 200) {
            *error_out = malloc(512);
            snprintf(*error_out, 512, "HTTP %ld: %s", status, resp_str);
            free(resp_str);
            break;
        }

        cJSON *response = cJSON_Parse(resp_str);
        free(resp_str);

        if (!response) {
            *error_out = strdup("Failed to parse response JSON");
            break;
        }

        // Check for tool calls
        APITestToolCall calls[10];
        int num_calls = parse_tool_calls(response, calls, 10);

        if (num_calls == 0) {
            // No tool calls - we have final answer
            final_response = extract_response_text(response);
            cJSON_Delete(response);
            break;
        }

        // Execute tool calls and build tool results
        cJSON *tool_results = cJSON_CreateArray();

        for (int i = 0; i < num_calls; i++) {
            // Track tools used
            if (tools_used[0]) strcat(tools_used, ",");
            strcat(tools_used, calls[i].name);

            if (verbose) {
                printf("  [Tool call] %s\n", calls[i].name);
            }

            char *tool_name = NULL;
            char *result = api_test_execute_tool(calls[i].name, calls[i].arguments, &tool_name);

            // Add assistant's tool call to messages
            cJSON *assistant_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(assistant_msg, "role", "assistant");
            cJSON *tc_array = cJSON_CreateArray();
            cJSON *tc = cJSON_CreateObject();
            cJSON_AddStringToObject(tc, "id", calls[i].id);
            cJSON_AddStringToObject(tc, "type", "function");
            cJSON *func = cJSON_CreateObject();
            cJSON_AddStringToObject(func, "name", calls[i].name);
            char *args_str = cJSON_PrintUnformatted(calls[i].arguments);
            cJSON_AddStringToObject(func, "arguments", args_str ? args_str : "{}");
            free(args_str);
            cJSON_AddItemToObject(tc, "function", func);
            cJSON_AddItemToArray(tc_array, tc);
            cJSON_AddItemToObject(assistant_msg, "tool_calls", tc_array);
            cJSON_AddItemToArray(messages, assistant_msg);

            // Add tool result
            cJSON *tool_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(tool_msg, "role", "tool");
            cJSON_AddStringToObject(tool_msg, "tool_call_id", calls[i].id);
            cJSON_AddStringToObject(tool_msg, "content", result);
            cJSON_AddItemToArray(messages, tool_msg);

            free(result);
            if (tool_name) free(tool_name);
            cJSON_Delete(calls[i].arguments);
        }

        cJSON_Delete(tool_results);
        cJSON_Delete(response);
    }

    if (turn >= max_turns && !final_response) {
        *error_out = strdup("Max conversation turns exceeded");
    }

    cJSON_Delete(messages);
    cJSON_Delete(tools);

    *tools_used_out = strdup(tools_used);
    return final_response;
}

// ============================================================================
// Provider Configuration Loading
// ============================================================================

static int load_user_config(KlawedConfig *config) {
    config_init_defaults(config);

    // Try to load from ~/.klawed/config.json
    const char *home = getenv("HOME");
    if (!home) return -1;

    char path[512];
    snprintf(path, sizeof(path), "%s/.klawed/config.json", home);

    // Temporarily set data dir to load from home
    char *original_data_dir = getenv("KLAWED_DATA_DIR");
    setenv("KLAWED_DATA_DIR", path, 1);

    // Actually, config_load expects .klawed/ in current dir or uses DATA_DIR
    // Let's manually parse the config file
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = malloc(size + 1);
    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);

    cJSON *json = cJSON_Parse(content);
    free(content);

    if (!json) return -1;

    // Parse providers
    cJSON *providers = cJSON_GetObjectItem(json, "providers");
    if (providers && cJSON_IsObject(providers)) {
        cJSON *provider = NULL;
        provider = providers->child;
        while (provider) {
            if (config->provider_count >= CONFIG_MAX_PROVIDERS) break;

            NamedProviderConfig *npc = &config->providers[config->provider_count];
            strlcpy(npc->key, provider->string, sizeof(npc->key));

            cJSON *type = cJSON_GetObjectItem(provider, "provider_type");
            cJSON *name = cJSON_GetObjectItem(provider, "provider_name");
            cJSON *model = cJSON_GetObjectItem(provider, "model");
            cJSON *api_base = cJSON_GetObjectItem(provider, "api_base");
            cJSON *api_key_env = cJSON_GetObjectItem(provider, "api_key_env");

            if (type && cJSON_IsString(type)) {
                npc->config.provider_type = config_provider_type_from_string(type->valuestring);
            }
            if (name && cJSON_IsString(name)) {
                strlcpy(npc->config.provider_name, name->valuestring, sizeof(npc->config.provider_name));
            }
            if (model && cJSON_IsString(model)) {
                strlcpy(npc->config.model, model->valuestring, sizeof(npc->config.model));
            }
            if (api_base && cJSON_IsString(api_base)) {
                strlcpy(npc->config.api_base, api_base->valuestring, sizeof(npc->config.api_base));
            }
            if (api_key_env && cJSON_IsString(api_key_env)) {
                strlcpy(npc->config.api_key_env, api_key_env->valuestring, sizeof(npc->config.api_key_env));
                // Load API key from environment
                const char *key = getenv(api_key_env->valuestring);
                if (key) {
                    strlcpy(npc->config.api_key, key, sizeof(npc->config.api_key));
                }
            }

            config->provider_count++;
            provider = provider->next;
        }
    }

    // Parse active provider
    cJSON *active = cJSON_GetObjectItem(json, "active_provider");
    if (active && cJSON_IsString(active)) {
        strlcpy(config->active_provider, active->valuestring, sizeof(config->active_provider));
    }

    cJSON_Delete(json);

    // Restore original data dir
    if (original_data_dir) {
        setenv("KLAWED_DATA_DIR", original_data_dir, 1);
    } else {
        unsetenv("KLAWED_DATA_DIR");
    }

    return 0;
}

static const NamedProviderConfig* select_provider(const KlawedConfig *config,
                                                   const char *cli_provider,
                                                   const char *env_provider) {
    // Priority 1: CLI argument
    if (cli_provider) {
        return config_find_provider(config, cli_provider);
    }

    // Priority 2: Environment variable
    if (env_provider) {
        return config_find_provider(config, env_provider);
    }

    // Priority 3: Active provider from config
    if (config->active_provider[0]) {
        return config_find_provider(config, config->active_provider);
    }

    // Priority 4: First configured provider
    if (config->provider_count > 0) {
        return &config->providers[0];
    }

    return NULL;
}

// ============================================================================
// Test Execution
// ============================================================================

static TestResult run_test(const LLMProviderConfig *config, const TestDefinition *test) {
    TestResult result = {0};
    result.name = test->name;

    double start = get_time_ms();

    char *error = NULL;
    char *response = run_conversation(config, test->query, &result.tools_used, &error);

    result.duration_ms = get_time_ms() - start;

    if (error) {
        result.failed = 1;
        result.error = error;
    } else if (!response) {
        result.failed = 1;
        result.error = strdup("No response received");
    } else {
        result.passed = 1;
        result.response = response;

        // Check if expected tools were used
        if (test->expected_tools && result.tools_used) {
            // Simple check - could be more sophisticated
            char expected_copy[256];
            strlcpy(expected_copy, test->expected_tools, sizeof(expected_copy));

            char *saveptr;
            char *tool = strtok_r(expected_copy, ",", &saveptr);
            while (tool) {
                // Trim whitespace
                while (isspace(*tool)) tool++;
                char *end = tool + strlen(tool) - 1;
                while (end > tool && isspace(*end)) *end-- = '\0';

                if (!result.tools_used || strstr(result.tools_used, tool) == NULL) {
                    result.passed = 0;
                    result.failed = 1;
                    result.error = malloc(256);
                    snprintf(result.error, 256, "Expected tool '%s' was not used", tool);
                    break;
                }
                tool = strtok_r(NULL, ",", &saveptr);
            }
        }
    }

    return result;
}

// ============================================================================
// Output Formatting
// ============================================================================

static void print_provider_info(const NamedProviderConfig *provider) {
    printf("Using provider: %s\n", provider->key);
    printf("  Type:    %s\n", config_provider_type_to_string(provider->config.provider_type));
    printf("  Model:   %s\n", provider->config.model);
    printf("  API Base: %s\n", provider->config.api_base);
    printf("  API Key: %s\n", provider->config.api_key[0] ? "(set)" : "(not set)");
}

static void print_result(const TestResult *result) {
    const char *status = result->passed ? "PASS" : "FAIL";
    printf("\n  [%s] %s (%.0f ms)\n", status, result->name, result->duration_ms);

    if (result->tools_used && result->tools_used[0]) {
        printf("       Tools: %s\n", result->tools_used);
    }

    if (result->failed && result->error) {
        printf("       Error: %s\n", result->error);
    }

    if (verbose && result->response) {
        printf("       Response: %.200s%s\n", result->response,
               strlen(result->response) > 200 ? "..." : "");
    }
}

static void print_summary(TestResult *results, int count) {
    print_header("TEST SUMMARY");

    int passed = 0, failed = 0;
    for (int i = 0; i < count; i++) {
        if (results[i].passed) passed++;
        else failed++;
    }

    printf("Total tests: %d\n", count);
    printf("Passed:      %d\n", passed);
    printf("Failed:      %d\n", failed);

    printf("\n");
    for (int i = 0; i < count; i++) {
        const char *status = results[i].passed ? "PASS" : "FAIL";
        printf("  %s | %-20s | %6.0f ms | %s\n",
               status, results[i].name, results[i].duration_ms,
               results[i].tools_used ? results[i].tools_used : "-");
    }
}

static void list_providers(const KlawedConfig *config) {
    print_header("Configured Providers");

    if (config->provider_count == 0) {
        printf("No providers configured in ~/.klawed/config.json\n");
        return;
    }

    for (int i = 0; i < config->provider_count; i++) {
        const NamedProviderConfig *npc = &config->providers[i];
        int is_active = (strcmp(npc->key, config->active_provider) == 0);

        printf("\n%s%s%s\n", npc->key, is_active ? " [ACTIVE]" : "", "");
        printf("  Type:    %s\n", config_provider_type_to_string(npc->config.provider_type));
        printf("  Name:    %s\n", npc->config.provider_name[0] ? npc->config.provider_name : "(not set)");
        printf("  Model:   %s\n", npc->config.model);
        printf("  API Base: %s\n", npc->config.api_base);

        if (npc->config.api_key_env[0]) {
            const char *key_status = getenv(npc->config.api_key_env) ? "set" : "NOT SET";
            printf("  API Key Env: %s (%s)\n", npc->config.api_key_env, key_status);
        }
    }
}

// ============================================================================
// Main
// ============================================================================

static void print_usage(const char *program) {
    printf("Usage: %s [options]\n\n", program);
    printf("Options:\n");
    printf("  --list-providers     List all configured providers\n");
    printf("  --provider <name>    Use specific provider (overrides config)\n");
    printf("  --test-read          Test Read tool\n");
    printf("  --test-glob          Test Glob tool\n");
    printf("  --test-grep          Test Grep tool\n");
    printf("  --test-bash          Test Bash tool\n");
    printf("  --test-multi         Test multiple tools\n");
    printf("  --test-all           Run all tests\n");
    printf("  --verbose            Show detailed output\n");
    printf("  --help               Show this help\n");
    printf("\nProvider selection priority:\n");
    printf("  1. --provider <name> argument\n");
    printf("  2. KLAWED_LLM_PROVIDER environment variable\n");
    printf("  3. active_provider from ~/.klawed/config.json\n");
    printf("  4. First provider in config\n");
    printf("\nWARNING: These tests make ACTUAL API calls and may incur costs!\n");
}

int main(int argc, char *argv[]) {
    // Parse arguments
    int do_list = 0;
    int do_test_all = 0;
    int do_tests[TEST_COUNT] = {0};
    const char *cli_provider = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list-providers") == 0) {
            do_list = 1;
        } else if (strcmp(argv[i], "--provider") == 0 && i + 1 < argc) {
            cli_provider = argv[++i];
        } else if (strcmp(argv[i], "--test-read") == 0) {
            do_tests[TEST_READ] = 1;
        } else if (strcmp(argv[i], "--test-glob") == 0) {
            do_tests[TEST_GLOB] = 1;
        } else if (strcmp(argv[i], "--test-grep") == 0) {
            do_tests[TEST_GREP] = 1;
        } else if (strcmp(argv[i], "--test-bash") == 0) {
            do_tests[TEST_BASH] = 1;
        } else if (strcmp(argv[i], "--test-multi") == 0) {
            do_tests[TEST_MULTI] = 1;
        } else if (strcmp(argv[i], "--test-all") == 0) {
            do_test_all = 1;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            printf("Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    // Default action
    if (!do_list && !do_test_all && !do_tests[TEST_READ] && !do_tests[TEST_GLOB] &&
        !do_tests[TEST_GREP] && !do_tests[TEST_BASH] && !do_tests[TEST_MULTI]) {
        do_list = 1;
    }

    // Load configuration
    KlawedConfig config;
    if (load_user_config(&config) != 0) {
        printf("Warning: Could not load ~/.klawed/config.json\n");
        printf("Make sure you have configured providers.\n\n");
    }

    if (do_list) {
        list_providers(&config);
    }

    // Run tests if requested
    int num_tests_to_run = do_test_all ? TEST_COUNT : 0;
    if (!do_test_all) {
        for (int i = 0; i < TEST_COUNT; i++) {
            if (do_tests[i]) num_tests_to_run++;
        }
    }

    if (num_tests_to_run > 0) {
        print_header("Provider API Call Tests");
        printf("Testing real API calls with tool validation\n");
        printf("WARNING: These tests make ACTUAL API calls and may incur costs!\n\n");

        // Select provider
        const char *env_provider = getenv("KLAWED_LLM_PROVIDER");
        const NamedProviderConfig *provider = select_provider(&config, cli_provider, env_provider);

        if (!provider) {
            printf("Error: No provider configured. Please set up ~/.klawed/config.json\n");
            return 1;
        }

        print_provider_info(provider);
        printf("\n");

        // Verify API key is set for non-local providers
        if (!provider->config.api_key[0] &&
            provider->config.provider_type != PROVIDER_BEDROCK) {
            // Check if it's a local instance
            if (strstr(provider->config.api_base, "localhost") == NULL &&
                strstr(provider->config.api_base, "127.0.0.1") == NULL &&
                strstr(provider->config.api_base, "192.168.") == NULL) {
                printf("Error: API key not set for provider '%s'\n", provider->key);
                printf("Set the environment variable specified in api_key_env\n");
                return 1;
            }
        }

        printf("Running %d test(s)...\n", num_tests_to_run);

        TestResult results[TEST_COUNT];
        int result_count = 0;

        for (int i = 0; i < TEST_COUNT; i++) {
            if (do_test_all || do_tests[i]) {
                const TestDefinition *test = &TESTS[i];
                printf("\n  Running test: %s", test->name);
                fflush(stdout);

                TestResult result = run_test(&provider->config, test);
                results[result_count++] = result;

                printf("\r");
                print_result(&result);
            }
        }

        print_summary(results, result_count);

        // Cleanup
        for (int i = 0; i < result_count; i++) {
            free(results[i].tools_used);
            free(results[i].response);
            free(results[i].error);
        }

        // Return exit code based on results
        for (int i = 0; i < result_count; i++) {
            if (results[i].failed) return 1;
        }
    }

    return 0;
}
