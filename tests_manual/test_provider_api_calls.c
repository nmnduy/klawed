/*
 * test_provider_api_calls.c - Manual provider API call tests
 *
 * Makes REAL API calls to LLM providers using the actual klawed provider code.
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

// Define ARENA_IMPLEMENTATION in exactly one file for the header-only arena library
#define ARENA_IMPLEMENTATION
#include "config.h"
#include "provider.h"
#include "arena.h"
#include "logger.h"
#include "api/api_response.h"

// Stub for TUI functions not needed in tests
#include "tui.h"
void tui_add_conversation_line(TUIState *tui, const char *prefix, const char *text, TUIColorPair color_pair) {
    (void)tui; (void)prefix; (void)text; (void)color_pair;
}
void tui_update_last_conversation_line(TUIState *tui, const char *text) { (void)tui; (void)text; }

// Test configuration
#define MAX_TESTS 10
#define DEFAULT_TIMEOUT_MS 120000
#undef MAX_TOKENS
#define MAX_TOKENS 4096

// Bash command safelist - only read-only commands allowed
static const char *BASH_SAFE_COMMANDS[] = {
    "ls", "cat", "pwd", "echo", "git status", "git log", "git branch",
    "wc", "head", "tail", "find", "grep", "ps", "df", "du", "whoami",
    "uname", "date", "env", "printenv", "id", "groups", "hostname",
    "which", "whereis", "file", "stat", "readlink", "realpath",
    "curl -I", "curl -s", "curl --head",
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

// Global options
static int verbose = 0;
static int max_turns = 10;

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

    for (int i = 0; BASH_SAFE_COMMANDS[i]; i++) {
        size_t len = strlen(BASH_SAFE_COMMANDS[i]);
        if (strncmp(command, BASH_SAFE_COMMANDS[i], len) == 0) {
            return 1;
        }
    }

    return 0;
}

// ============================================================================
// Tool Execution (Local implementations for testing)
// ============================================================================

static char* read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return strdup("Error: File not found");

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size > 100000) {
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
    snprintf(cmd, sizeof(cmd), "find . -name '%s' -type f 2>/dev/null | head -20", pattern);

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

// ============================================================================
// Minimal ConversationState for Testing
// ============================================================================

#include "klawed_internal.h"

static int init_test_conversation_state(ConversationState *state, const char *model) {
    memset(state, 0, sizeof(ConversationState));
    state->count = 0;
    state->max_tokens = MAX_TOKENS;
    state->model = strdup(model ? model : "gpt-4");
    state->working_dir = getcwd(NULL, 0);
    state->session_id = NULL;
    state->tui = NULL;
    state->provider = NULL;
    state->interrupt_requested = 0;

    if (pthread_mutex_init(&state->conv_mutex, NULL) != 0) {
        return -1;
    }
    state->conv_mutex_initialized = 1;

    return 0;
}

static void cleanup_test_conversation_state(ConversationState *state) {
    if (!state) return;

    for (int i = 0; i < state->count; i++) {
        for (int j = 0; j < state->messages[i].content_count; j++) {
            InternalContent *content = &state->messages[i].contents[j];
            free(content->text);
            free(content->tool_id);
            free(content->tool_name);
            if (content->tool_params) cJSON_Delete(content->tool_params);
            if (content->tool_output) cJSON_Delete(content->tool_output);
            free(content->image_path);
            free(content->mime_type);
            free(content->base64_data);
            free(content->reasoning_content);
        }
        free(state->messages[i].contents);
    }

    free(state->model);
    free(state->working_dir);
    free(state->api_key);
    free(state->api_url);
    free(state->session_id);

    if (state->conv_mutex_initialized) {
        pthread_mutex_destroy(&state->conv_mutex);
    }
}

// Note: add_user_message is defined in klawed_internal.h, we use it directly

static void add_assistant_message_with_tools(ConversationState *state, const char *content,
                                              cJSON *tool_calls_json) {
    if (state->count >= MAX_MESSAGES) return;

    int content_count = 1;
    if (tool_calls_json && cJSON_GetArraySize(tool_calls_json) > 0) {
        content_count += cJSON_GetArraySize(tool_calls_json);
    }

    InternalMessage *msg = &state->messages[state->count++];
    msg->role = MSG_ASSISTANT;
    msg->content_count = content_count;
    msg->contents = calloc(content_count, sizeof(InternalContent));

    int idx = 0;
    msg->contents[idx].type = INTERNAL_TEXT;
    msg->contents[idx].text = content ? strdup(content) : strdup("");
    idx++;

    if (tool_calls_json) {
        cJSON *tc = NULL;
        cJSON_ArrayForEach(tc, tool_calls_json) {
            if (idx >= content_count) break;
            msg->contents[idx].type = INTERNAL_TOOL_CALL;

            cJSON *id = cJSON_GetObjectItem(tc, "id");
            cJSON *func = cJSON_GetObjectItem(tc, "function");
            if (func) {
                cJSON *name = cJSON_GetObjectItem(func, "name");
                cJSON *args = cJSON_GetObjectItem(func, "arguments");

                if (id && cJSON_IsString(id)) {
                    msg->contents[idx].tool_id = strdup(id->valuestring);
                }
                if (name && cJSON_IsString(name)) {
                    msg->contents[idx].tool_name = strdup(name->valuestring);
                }
                if (args) {
                    msg->contents[idx].tool_params = cJSON_Duplicate(args, 1);
                }
            }
            idx++;
        }
    }
}

static void add_tool_result(ConversationState *state, const char *tool_call_id,
                            const char *result) {
    if (state->count >= MAX_MESSAGES) return;

    InternalMessage *msg = &state->messages[state->count++];
    msg->role = MSG_USER;
    msg->content_count = 1;
    msg->contents = calloc(1, sizeof(InternalContent));
    msg->contents[0].type = INTERNAL_TOOL_RESPONSE;
    msg->contents[0].tool_id = strdup(tool_call_id);
    msg->contents[0].tool_output = cJSON_CreateString(result ? result : "");
}

// ============================================================================
// Tool Call Execution
// ============================================================================

static char* execute_tool_call(const char *tool_call_json, char **tool_name_out) {
    cJSON *tc = cJSON_Parse(tool_call_json);
    if (!tc) return strdup("Error: Invalid tool call JSON");

    cJSON *func = cJSON_GetObjectItem(tc, "function");

    if (!func) {
        cJSON_Delete(tc);
        return strdup("Error: Missing function in tool call");
    }

    cJSON *name = cJSON_GetObjectItem(func, "name");
    cJSON *args = cJSON_GetObjectItem(func, "arguments");

    if (!name || !cJSON_IsString(name)) {
        cJSON_Delete(tc);
        return strdup("Error: Missing tool name");
    }

    *tool_name_out = strdup(name->valuestring);
    char *result = NULL;

    if (strcmp(name->valuestring, "Read") == 0) {
        cJSON *file_path = cJSON_GetObjectItem(args, "file_path");
        if (cJSON_IsString(file_path)) {
            result = read_file(file_path->valuestring);
        } else {
            result = strdup("Error: Missing file_path");
        }
    } else if (strcmp(name->valuestring, "Glob") == 0) {
        cJSON *pattern = cJSON_GetObjectItem(args, "pattern");
        if (cJSON_IsString(pattern)) {
            result = execute_glob(pattern->valuestring);
        } else {
            result = strdup("Error: Missing pattern");
        }
    } else if (strcmp(name->valuestring, "Grep") == 0) {
        cJSON *pattern = cJSON_GetObjectItem(args, "pattern");
        cJSON *path = cJSON_GetObjectItem(args, "path");
        if (cJSON_IsString(pattern)) {
            result = execute_grep(pattern->valuestring,
                                  cJSON_IsString(path) ? path->valuestring : NULL);
        } else {
            result = strdup("Error: Missing pattern");
        }
    } else if (strcmp(name->valuestring, "Bash") == 0) {
        cJSON *command = cJSON_GetObjectItem(args, "command");
        if (cJSON_IsString(command)) {
            result = execute_bash(command->valuestring);
        } else {
            result = strdup("Error: Missing command");
        }
    } else {
        result = strdup("Error: Unknown tool");
    }

    cJSON_Delete(tc);
    return result;
}

// ============================================================================
// Conversation Loop using Real Provider
// ============================================================================

static char* run_conversation(Provider *provider, const char *query,
                              char **tools_used_out, char **error_out) {
    const char *system_prompt =
        "You are a helpful assistant with access to file system tools. "
        "Use the tools provided to answer the user's question. "
        "When done, provide a clear, concise answer.";

    ConversationState state;
    if (init_test_conversation_state(&state, NULL) != 0) {
        *error_out = strdup("Failed to initialize conversation state");
        return NULL;
    }

    state.provider = provider;

    char tools_used[512] = "";
    char *final_response = NULL;
    int turn = 0;

    // Add system prompt as first user message (some APIs don't support system role)
    add_user_message(&state, system_prompt);
    add_user_message(&state, query);

    while (turn < max_turns) {
        turn++;

        if (verbose) {
            printf("  [Turn %d] Calling API...\n", turn);
        }

        ApiCallResult result = {0};
        provider->call_api(provider, &state, &result);

        if (result.error_message) {
            *error_out = result.error_message;
            result.error_message = NULL;
            cleanup_test_conversation_state(&state);
            return NULL;
        }

        if (!result.response) {
            *error_out = strdup("No response from API");
            cleanup_test_conversation_state(&state);
            return NULL;
        }

        // Extract text response
        if (result.response->message.text) {
            final_response = strdup(result.response->message.text);
        }

        // Check for tool calls
        if (result.response->tool_count == 0) {
            // No tool calls - we're done
            api_response_free(result.response);
            break;
        }

        // Execute tool calls
        cJSON *tool_calls_array = cJSON_CreateArray();

        for (int i = 0; i < result.response->tool_count; i++) {
            ToolCall *tc = &result.response->tools[i];

            // Track tool used
            if (tools_used[0]) strcat(tools_used, ",");
            strcat(tools_used, tc->name);

            if (verbose) {
                printf("  [Tool call] %s\n", tc->name);
            }

            // Create tool call JSON
            cJSON *tc_json = cJSON_CreateObject();
            cJSON_AddStringToObject(tc_json, "id", tc->id);
            cJSON *func = cJSON_CreateObject();
            cJSON_AddStringToObject(func, "name", tc->name);
            cJSON_AddItemToObject(func, "arguments", cJSON_Duplicate(tc->parameters, 1));
            cJSON_AddItemToObject(tc_json, "function", func);
            cJSON_AddItemToArray(tool_calls_array, tc_json);

            // Execute tool
            char *tc_json_str = cJSON_PrintUnformatted(tc_json);
            char *tool_name = NULL;
            char *tool_result = execute_tool_call(tc_json_str, &tool_name);
            free(tc_json_str);

            // Add to conversation
            add_tool_result(&state, tc->id, tool_result);

            free(tool_result);
            free(tool_name);
        }

        // Add assistant message with tool calls
        add_assistant_message_with_tools(&state, result.response->message.text, tool_calls_array);
        cJSON_Delete(tool_calls_array);

        api_response_free(result.response);
        final_response = NULL;
    }

    if (turn >= max_turns && !final_response) {
        *error_out = strdup("Max conversation turns exceeded");
    }

    cleanup_test_conversation_state(&state);

    *tools_used_out = strdup(tools_used);
    return final_response;
}

// ============================================================================
// Provider Configuration Loading
// ============================================================================

static int load_user_config(KlawedConfig *config) {
    config_init_defaults(config);

    const char *home = getenv("HOME");
    if (!home) return -1;

    char path[512];
    snprintf(path, sizeof(path), "%s/.klawed/config.json", home);

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

    cJSON *providers = cJSON_GetObjectItem(json, "providers");
    if (providers && cJSON_IsObject(providers)) {
        cJSON *provider = providers->child;
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
                const char *key = getenv(api_key_env->valuestring);
                if (key) {
                    strlcpy(npc->config.api_key, key, sizeof(npc->config.api_key));
                }
            }

            config->provider_count++;
            provider = provider->next;
        }
    }

    cJSON *active = cJSON_GetObjectItem(json, "active_provider");
    if (active && cJSON_IsString(active)) {
        strlcpy(config->active_provider, active->valuestring, sizeof(config->active_provider));
    }

    cJSON_Delete(json);
    return 0;
}

static const NamedProviderConfig* select_provider(const KlawedConfig *config,
                                                   const char *cli_provider,
                                                   const char *env_provider) {
    if (cli_provider) {
        return config_find_provider(config, cli_provider);
    }

    if (env_provider) {
        return config_find_provider(config, env_provider);
    }

    if (config->active_provider[0]) {
        return config_find_provider(config, config->active_provider);
    }

    if (config->provider_count > 0) {
        return &config->providers[0];
    }

    return NULL;
}

// ============================================================================
// Test Execution
// ============================================================================

static TestResult run_test(Provider *provider, const TestDefinition *test) {
    TestResult result = {0};
    result.name = test->name;

    double start = get_time_ms();

    char *error = NULL;
    char *response = run_conversation(provider, test->query, &result.tools_used, &error);

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

        if (test->expected_tools && result.tools_used) {
            char expected_copy[256];
            strlcpy(expected_copy, test->expected_tools, sizeof(expected_copy));

            char *saveptr;
            char *tool = strtok_r(expected_copy, ",", &saveptr);
            while (tool) {
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

        printf("\n%s%s\n", npc->key, is_active ? " [ACTIVE]" : "");
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

    if (!do_list && !do_test_all && !do_tests[TEST_READ] && !do_tests[TEST_GLOB] &&
        !do_tests[TEST_GREP] && !do_tests[TEST_BASH] && !do_tests[TEST_MULTI]) {
        do_list = 1;
    }

    KlawedConfig config;
    if (load_user_config(&config) != 0) {
        printf("Warning: Could not load ~/.klawed/config.json\n");
        printf("Make sure you have configured providers.\n\n");
    }

    if (do_list) {
        list_providers(&config);
    }

    int num_tests_to_run = do_test_all ? TEST_COUNT : 0;
    if (!do_test_all) {
        for (int i = 0; i < TEST_COUNT; i++) {
            if (do_tests[i]) num_tests_to_run++;
        }
    }

    if (num_tests_to_run > 0) {
        print_header("Provider API Call Tests");
        printf("Testing real API calls using actual klawed provider code\n");
        printf("WARNING: These tests make ACTUAL API calls and may incur costs!\n\n");

        const char *env_provider = getenv("KLAWED_LLM_PROVIDER");
        const NamedProviderConfig *named_provider = select_provider(&config, cli_provider, env_provider);

        if (!named_provider) {
            printf("Error: No provider configured. Please set up ~/.klawed/config.json\n");
            return 1;
        }

        print_provider_info(named_provider);
        printf("\n");

        // Initialize provider using the actual klawed provider code
        ProviderInitResult provider_result;
        provider_init_from_config(named_provider->key, &named_provider->config, &provider_result);

        if (provider_result.error_message) {
            printf("Error: Failed to initialize provider: %s\n", provider_result.error_message);
            free(provider_result.error_message);
            return 1;
        }

        if (!provider_result.provider) {
            printf("Error: Provider initialization returned NULL\n");
            free(provider_result.api_url);
            free(provider_result.model);
            return 1;
        }

        printf("Provider initialized: %s\n", provider_result.api_url ? provider_result.api_url : "(no URL)");
        printf("\nRunning %d test(s)...\n", num_tests_to_run);

        TestResult results[TEST_COUNT];
        int result_count = 0;

        for (int i = 0; i < TEST_COUNT; i++) {
            if (do_test_all || do_tests[i]) {
                const TestDefinition *test = &TESTS[i];
                printf("\n  Running test: %s", test->name);
                fflush(stdout);

                TestResult result = run_test(provider_result.provider, test);
                results[result_count++] = result;

                printf("\r");
                print_result(&result);
            }
        }

        print_summary(results, result_count);

        for (int i = 0; i < result_count; i++) {
            free(results[i].tools_used);
            free(results[i].response);
            free(results[i].error);
        }

        // Cleanup provider
        if (provider_result.provider->cleanup) {
            provider_result.provider->cleanup(provider_result.provider);
        }
        free(provider_result.api_url);
        free(provider_result.model);

        for (int i = 0; i < result_count; i++) {
            if (results[i].failed) return 1;
        }
    }

    return 0;
}
