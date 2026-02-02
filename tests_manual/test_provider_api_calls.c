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
 * This test uses the actual project code for:
 *   - Conversation state management (src/conversation/)
 *   - Message building (add_user_message, add_tool_results, etc.)
 *   - Tool execution (execute_tool from tool_executor.c)
 *   - API calls (call_api_with_retries)
 *
 * Options:
 *   --list-providers     List all configured providers
 *   --provider <name>    Use specific provider
 *   --prompt <text>      Run a single custom prompt
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
#include <bsd/string.h>
#include <unistd.h>
#include <time.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#define ARENA_IMPLEMENTATION
#include "config.h"
#include "provider.h"
#include "arena.h"
#include "logger.h"
#include "klawed_internal.h"
#include "api/api_response.h"
#include "tools/tool_executor.h"
#include "conversation/message_builder.h"
#include "conversation/message_parser.h"
#include "api/api_client.h"
#include "context/system_prompt.h"

// Stubs for functions not needed in tests
#include "tui.h"
void tui_add_conversation_line(TUIState *tui, const char *prefix, const char *text, TUIColorPair color_pair) {
    (void)tui; (void)prefix; (void)text; (void)color_pair;
}
void tui_update_last_conversation_line(TUIState *tui, const char *text) { (void)tui; (void)text; }

// Test scenario types
typedef enum {
    TEST_SINGLE,      // Single custom or default prompt
    TEST_READ,        // Test Read tool
    TEST_GLOB,        // Test Glob tool
    TEST_BASH,        // Test Bash tool
    TEST_GREP,        // Test Grep tool
    TEST_MULTI,       // Test multiple tools
    TEST_ALL          // Run all tests
} TestMode;

// Global options
static int verbose = 0;

// Tool execution tracking
#define MAX_TOOLS_TRACKED 32
static char *tools_used[MAX_TOOLS_TRACKED];
static int tools_count = 0;

static void record_tool_used(const char *tool_name) {
    if (tools_count >= MAX_TOOLS_TRACKED) return;
    // Check if already recorded
    for (int i = 0; i < tools_count; i++) {
        if (strcmp(tools_used[i], tool_name) == 0) return;
    }
    tools_used[tools_count++] = strdup(tool_name);
}

static void free_tools_recorded(void) {
    for (int i = 0; i < tools_count; i++) {
        free(tools_used[i]);
        tools_used[i] = NULL;
    }
    tools_count = 0;
}

// ============================================================================
// Utility Functions
// ============================================================================

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
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

static void list_providers(const KlawedConfig *config) {
    printf("\nConfigured Providers\n");
    printf("====================\n");

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
// Conversation Execution using Actual Project Code
// ============================================================================

static char* run_conversation(Provider *provider __attribute__((unused)),
                              ConversationState *state,
                              const char *query, char **error_out) {
    double start = get_time_ms();

    // Add user message using actual project function
    add_user_message(state, query);

    if (verbose) {
        printf("  [Calling API...]\n");
    }

    // Call API with retries using actual project function
    ApiResponse *response = call_api_with_retries(state);

    if (!response) {
        *error_out = strdup("Failed to get response from API");
        return NULL;
    }

    if (response->error_message) {
        *error_out = strdup(response->error_message);
        api_response_free(response);
        return NULL;
    }

    // Add assistant message to conversation using actual project function
    cJSON *choices = cJSON_GetObjectItem(response->raw_response, "choices");
    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *choice = cJSON_GetArrayItem(choices, 0);
        cJSON *message = cJSON_GetObjectItem(choice, "message");
        if (message) {
            add_assistant_message_openai(state, message);
        }
    }

    // Check for tool calls
    int tool_count = response->tool_count;
    ToolCall *tool_calls = response->tools;

    if (tool_count == 0) {
        // No tool calls - return the text response
        char *result = response->message.text ? strdup(response->message.text) : strdup("(no response)");
        api_response_free(response);

        if (verbose) {
            printf("  [Completed in %.0f ms]\n", get_time_ms() - start);
        }
        return result;
    }

    // Process tool calls using actual project tool execution
    if (verbose) {
        printf("  [Processing %d tool call(s)]\n", tool_count);
    }

    InternalContent *results = calloc((size_t)tool_count, sizeof(InternalContent));
    if (!results) {
        *error_out = strdup("Failed to allocate tool result buffer");
        api_response_free(response);
        return NULL;
    }

    for (int i = 0; i < tool_count; i++) {
        ToolCall *tool = &tool_calls[i];

        if (verbose) {
            printf("    Executing: %s\n", tool->name ? tool->name : "(unknown)");
            if (tool->parameters) {
                char *params = cJSON_Print(tool->parameters);
                printf("    Parameters: %s\n", params);
                free(params);
            }
        }
        
        // Record tool usage
        if (tool->name) {
            record_tool_used(tool->name);
        }

        // Prepare tool input
        cJSON *input = tool->parameters
            ? cJSON_Duplicate(tool->parameters, 1)
            : cJSON_CreateObject();

        // Execute tool using actual project function
        cJSON *tool_result = execute_tool(tool->name, input, state);

        if (verbose) {
            char *result_str = cJSON_Print(tool_result);
            printf("    Tool result: %s\n", result_str ? result_str : "(null)");
            free(result_str);
        }

        // Convert to InternalContent
        results[i].type = INTERNAL_TOOL_RESPONSE;
        results[i].tool_id = tool->id ? strdup(tool->id) : strdup("unknown");
        results[i].tool_name = tool->name ? strdup(tool->name) : strdup("tool");
        results[i].tool_output = tool_result;
        results[i].is_error = tool_result ? cJSON_HasObjectItem(tool_result, "error") : 1;

        cJSON_Delete(input);
    }

    // Add tool results to conversation using actual project function
    if (add_tool_results(state, results, tool_count) != 0) {
        *error_out = strdup("Failed to add tool results to conversation");
        // Results were freed by add_tool_results
        api_response_free(response);
        return NULL;
    }

    api_response_free(response);

    // Call API again with tool results
    if (verbose) {
        printf("  [Calling API with tool results...]\n");
    }

    response = call_api_with_retries(state);

    if (!response) {
        *error_out = strdup("Failed to get response after tool execution");
        return NULL;
    }

    if (response->error_message) {
        *error_out = strdup(response->error_message);
        api_response_free(response);
        return NULL;
    }

    // Add assistant message to conversation
    choices = cJSON_GetObjectItem(response->raw_response, "choices");
    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *choice = cJSON_GetArrayItem(choices, 0);
        cJSON *message = cJSON_GetObjectItem(choice, "message");
        if (message) {
            add_assistant_message_openai(state, message);
        }
    }

    char *result = response->message.text ? strdup(response->message.text) : strdup("(no response)");
    api_response_free(response);

    if (verbose) {
        printf("  [Completed in %.0f ms]\n", get_time_ms() - start);
    }

    return result;
}

// ============================================================================
// Main
// ============================================================================

static void print_usage(const char *program) {
    printf("Usage: %s [options]\n\n", program);
    printf("Options:\n");
    printf("  --list-providers     List all configured providers\n");
    printf("  --provider <name>    Use specific provider (overrides config)\n");
    printf("  --prompt <text>      Run a single custom prompt\n");
    printf("  --test-read          Test Read tool (read README.md)\n");
    printf("  --test-glob          Test Glob tool (find C files)\n");
    printf("  --test-bash          Test Bash tool (check git status)\n");
    printf("  --test-grep          Test Grep tool (search for TODO)\n");
    printf("  --test-multi         Test multiple tools together\n");
    printf("  --test-all           Run all test scenarios\n");
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
    // Initialize logger and curl
    log_init();
    curl_global_init(CURL_GLOBAL_DEFAULT);

    int do_list = 0;
    const char *cli_provider = NULL;
    const char *custom_prompt = NULL;
    TestMode test_mode = TEST_SINGLE;
    int tests_to_run[6] = {0};  // Track which tests to run for --test-all
    int test_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list-providers") == 0) {
            do_list = 1;
        } else if (strcmp(argv[i], "--provider") == 0 && i + 1 < argc) {
            cli_provider = argv[++i];
        } else if (strcmp(argv[i], "--prompt") == 0 && i + 1 < argc) {
            custom_prompt = argv[++i];
        } else if (strcmp(argv[i], "--test-read") == 0) {
            test_mode = TEST_READ;
        } else if (strcmp(argv[i], "--test-glob") == 0) {
            test_mode = TEST_GLOB;
        } else if (strcmp(argv[i], "--test-bash") == 0) {
            test_mode = TEST_BASH;
        } else if (strcmp(argv[i], "--test-grep") == 0) {
            test_mode = TEST_GREP;
        } else if (strcmp(argv[i], "--test-multi") == 0) {
            test_mode = TEST_MULTI;
        } else if (strcmp(argv[i], "--test-all") == 0) {
            test_mode = TEST_ALL;
            // Mark all individual tests to run (skip TEST_SINGLE and TEST_ALL)
            tests_to_run[test_count++] = TEST_READ;
            tests_to_run[test_count++] = TEST_GLOB;
            tests_to_run[test_count++] = TEST_BASH;
            tests_to_run[test_count++] = TEST_GREP;
            tests_to_run[test_count++] = TEST_MULTI;
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

    // Load configuration
    KlawedConfig config;
    if (load_user_config(&config) != 0) {
        printf("Error: Could not load ~/.klawed/config.json\n");
        return 1;
    }

    if (do_list) {
        list_providers(&config);
        return 0;
    }

    // Select provider
    const char *env_provider = getenv("KLAWED_LLM_PROVIDER");
    const NamedProviderConfig *named_provider = select_provider(&config, cli_provider, env_provider);

    if (!named_provider) {
        printf("Error: No provider configured. Please set up ~/.klawed/config.json\n");
        return 1;
    }

    printf("Provider API Call Test\n");
    printf("======================\n");
    printf("Using provider: %s\n", named_provider->key);
    printf("  Type:   %s\n", config_provider_type_to_string(named_provider->config.provider_type));
    printf("  Model:  %s\n", named_provider->config.model);
    printf("  API:    %s\n", named_provider->config.api_base);
    printf("\n");

    // Initialize provider
    ProviderInitResult provider_result;
    provider_init_from_config(named_provider->key, &named_provider->config, &provider_result);

    if (provider_result.error_message) {
        printf("Error: Failed to initialize provider: %s\n", provider_result.error_message);
        free(provider_result.error_message);
        return 1;
    }

    if (!provider_result.provider) {
        printf("Error: Provider initialization returned NULL\n");
        return 1;
    }

    // Initialize conversation state
    ConversationState state = {0};
    if (conversation_state_init(&state) != 0) {
        printf("Error: Failed to initialize conversation state\n");
        provider_result.provider->cleanup(provider_result.provider);
        return 1;
    }

    state.provider = provider_result.provider;
    state.api_key = strdup(named_provider->config.api_key);
    state.api_url = strdup(named_provider->config.api_base);
    state.model = strdup(named_provider->config.model);
    state.max_tokens = 4096;
    state.max_retry_duration_ms = 60000;  // 60 seconds

    // Change to project root (parent of tests_manual) for tests
    if (chdir("..") != 0) {
        printf("Warning: Failed to change to parent directory\n");
    }
    
    // Set working directory
    char cwd_buf[4096];
    if (getcwd(cwd_buf, sizeof(cwd_buf))) {
        state.working_dir = strdup(cwd_buf);
        if (verbose) {
            printf("  Working directory: %s\n", cwd_buf);
        }
    }

    // Build and add system prompt using actual project function
    char *system_prompt = build_system_prompt(&state);
    if (system_prompt) {
        add_system_message(&state, system_prompt);
        free(system_prompt);
    }

    // Test prompts for different scenarios
    const char *test_prompts[] = {
        [TEST_SINGLE] =
            "Please detect and summarize the OS environment and computer specs of this system. "
            "Use the Bash tool to run commands like 'uname -a', 'cat /etc/os-release' (if it exists), "
            "'lscpu' or 'cat /proc/cpuinfo', and 'free -h' or 'df -h' to gather system information. "
            "Also use Glob to find any relevant config files in /etc/. "
            "Provide a brief summary of: 1) OS name and version, 2) CPU info, 3) Memory info, 4) Disk usage. "
            "Keep your response concise.",
        [TEST_READ] =
            "Read the file 'README.md' (relative path) and tell me what this project is about in one sentence. "
            "If the file doesn't exist, use Glob to see what .md files are available first.",
        [TEST_GLOB] =
            "Use the Glob tool with pattern '*.c' to find all C source files in the current directory. "
            "Tell me how many C files you found and list the first 5 filenames.",
        [TEST_BASH] =
            "Use the Bash tool to check the current git status (git status --short) and tell me if there are any uncommitted changes.",
        [TEST_GREP] =
            "Use the Grep tool to search for 'TODO' in all .c and .h files. "
            "Tell me how many TODO items you found and show me 2-3 examples.",
        [TEST_MULTI] =
            "I need to understand this codebase. Please: 1) Use Glob to find all header files (*.h), "
            "2) Read the first header file you find to see what it contains, "
            "3) Use Bash to count the total lines of C code (find . -name '*.c' | xargs wc -l). "
            "Give me a brief summary."
    };

    const char *test_names[] = {
        [TEST_SINGLE] = "system-info",
        [TEST_READ] = "read",
        [TEST_GLOB] = "glob",
        [TEST_BASH] = "bash",
        [TEST_GREP] = "grep",
        [TEST_MULTI] = "multi-tool"
    };

    // Determine which tests to run
    int tests_to_run_single[1] = {test_mode};
    int *tests_list = (test_mode == TEST_ALL) ? tests_to_run : tests_to_run_single;
    int num_tests = (test_mode == TEST_ALL) ? test_count : 1;

    if (custom_prompt) {
        // Custom prompt overrides test mode
        num_tests = 1;
        tests_list[0] = TEST_SINGLE;
        test_prompts[TEST_SINGLE] = custom_prompt;
    }

    int total_passed = 0;
    int total_failed = 0;

    for (int t = 0; t < num_tests; t++) {
        int current_test = tests_list[t];
        const char *current_prompt = test_prompts[current_test];
        const char *test_name = test_names[current_test];

        // Reset conversation state for each test (except keep provider setup)
        conversation_free(&state);

        printf("\n%s\n", (num_tests > 1) ? "========================================" : "");
        printf("Test %d/%d: %s\n", t + 1, num_tests, test_name);
        if (num_tests > 1) printf("========================================\n");
        printf("Prompt: \"%s\"\n\n", current_prompt);

        // Reset tools tracking
        free_tools_recorded();

        char *error = NULL;
        double test_start = get_time_ms();
        char *response = run_conversation(provider_result.provider, &state, current_prompt, &error);
        double test_duration = get_time_ms() - test_start;

        printf("\nResult:\n");
        if (error) {
            printf("  ERROR: %s\n", error);
            free(error);
            total_failed++;
        } else if (response) {
            printf("  Response: %s\n", response);
            free(response);
            total_passed++;
        } else {
            printf("  (no response)\n");
            total_failed++;
        }

        // Print tools used summary
        if (tools_count > 0) {
            printf("\n  Tools used (%d): ", tools_count);
            for (int i = 0; i < tools_count; i++) {
                printf("%s%s", tools_used[i], (i < tools_count - 1) ? ", " : "");
            }
            printf(" | Time: %.0f ms\n", test_duration);
        } else {
            printf("\n  Tools used: none (direct response) | Time: %.0f ms\n", test_duration);
        }
    }

    // Print summary if running multiple tests
    if (num_tests > 1) {
        printf("\n========================================\n");
        printf("TEST SUMMARY\n");
        printf("========================================\n");
        printf("Total:  %d\n", num_tests);
        printf("Passed: %d\n", total_passed);
        printf("Failed: %d\n", total_failed);
        printf("========================================\n");
    }

    // Cleanup
    conversation_free(&state);
    conversation_state_destroy(&state);

    if (provider_result.provider->cleanup) {
        provider_result.provider->cleanup(provider_result.provider);
    }
    free(provider_result.api_url);
    free(provider_result.model);
    free(state.api_key);
    free(state.api_url);
    free(state.model);
    free(state.working_dir);

    curl_global_cleanup();
    log_shutdown();
    free_tools_recorded();

    return (total_failed > 0) ? 1 : 0;
}
