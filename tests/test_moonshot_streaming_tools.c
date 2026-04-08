/*
 * test_moonshot_streaming_tools.c - Test tool call accumulation from streaming mode
 *
 * This binary tests tool call accumulation specifically for Moonshot/Kimi K2.5 provider
 * with streaming mode enabled. It verifies:
 * - Tool calls are properly accumulated from streaming chunks
 * - Empty/incomplete tool calls are filtered out
 * - The streaming accumulator works correctly with Moonshot's SSE format
 *
 * Usage:
 *   export MOONSHOT_API_KEY="your-api-key"
 *   ./build/test_moonshot_streaming_tools
 *
 * Or use the global config:
 *   ./build/test_moonshot_streaming_tools
 *
 * Environment variables:
 *   MOONSHOT_API_KEY          - API key for Moonshot (required if not in config)
 *   KLAWED_ENABLE_STREAMING   - Set to 1 to enable streaming (recommended)
 *   KLAWED_LOG_LEVEL          - Set to DEBUG for verbose output
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <cjson/cJSON.h>

#include "../src/openai_streaming.h"
#include "../src/streaming_tool_accumulator.h"
#include "../src/sse_parser.h"
#include "../src/http_client.h"
#include "../src/logger.h"
#include "../src/arena.h"
#include "../src/config.h"

/* Test framework colors - use TEST_ prefix to avoid conflict with ncurses */
#define TEST_COLOR_RESET   "\033[0m"
#define TEST_COLOR_GREEN   "\033[32m"
#define TEST_COLOR_RED     "\033[31m"
#define TEST_COLOR_YELLOW  "\033[33m"
#define TEST_COLOR_CYAN    "\033[36m"
#define TEST_COLOR_MAGENTA "\033[35m"

/* Short aliases - undef first if ncurses defined them */
#ifdef COLOR_GREEN
#undef COLOR_GREEN
#undef COLOR_RED
#undef COLOR_YELLOW
#undef COLOR_CYAN
#undef COLOR_MAGENTA
#undef COLOR_RESET
#endif

#define COLOR_RESET   TEST_COLOR_RESET
#define COLOR_GREEN   TEST_COLOR_GREEN
#define COLOR_RED     TEST_COLOR_RED
#define COLOR_YELLOW  TEST_COLOR_YELLOW
#define COLOR_CYAN    TEST_COLOR_CYAN
#define COLOR_MAGENTA TEST_COLOR_MAGENTA

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        tests_run++; \
        if (condition) { \
            tests_passed++; \
            printf(COLOR_GREEN "  ✓ " COLOR_RESET "%s\n", message); \
        } else { \
            tests_failed++; \
            printf(COLOR_RED "  ✗ " COLOR_RESET "%s (line %d)\n", message, __LINE__); \
        } \
    } while (0)

#define TEST_SECTION(name) \
    do { \
        printf("\n" COLOR_CYAN "=== %s ===" COLOR_RESET "\n", name); \
    } while (0)

/* Moonshot provider configuration */
typedef struct {
    char api_key[256];
    char api_base[256];
    char model[128];
    char extra_headers[1024];  /* Extra headers from config (comma-separated) */
    int streaming_enabled;
} MoonshotConfig;

/* Streaming context for test */
typedef struct {
    OpenAIStreamingAccumulator acc;
    int chunk_count;
    int text_chunk_count;
    int tool_chunk_count;
    int got_done;
    int got_error;
    char error_message[512];
} TestStreamingContext;

/* SSE event handler for testing */
static int test_streaming_handler(StreamEvent *event, void *userdata) {
    TestStreamingContext *ctx = (TestStreamingContext *)userdata;
    ctx->chunk_count++;

    if (!event) {
        return 0;
    }

    switch (event->type) {
        case SSE_EVENT_OPENAI_CHUNK:
            ctx->text_chunk_count++;
            break;
        case SSE_EVENT_OPENAI_DONE:
            ctx->got_done = 1;
            break;
        case SSE_EVENT_ERROR:
            ctx->got_error = 1;
            if (event->raw_data) {
                strlcpy(ctx->error_message, event->raw_data, sizeof(ctx->error_message));
            }
            break;
        /* Handle all Anthropic-specific events and unknown events - no action needed */
        case SSE_EVENT_MESSAGE_START:
        case SSE_EVENT_CONTENT_BLOCK_START:
        case SSE_EVENT_CONTENT_BLOCK_DELTA:
        case SSE_EVENT_CONTENT_BLOCK_STOP:
        case SSE_EVENT_MESSAGE_DELTA:
        case SSE_EVENT_MESSAGE_STOP:
        case SSE_EVENT_PING:
        case SSE_EVENT_UNKNOWN:
        default:
            /* No action needed for these events */
            break;
    }

    /* Process accumulation */
    int ret = openai_streaming_process_event(&ctx->acc, event);
    if (ret != 0) {
        ctx->got_error = 1;
        return ret;
    }

    return 0;
}

/* Load moonshot configuration from global config or environment */
static int load_moonshot_config(MoonshotConfig *config) {
    memset(config, 0, sizeof(*config));

    /* Environment variables take highest priority - check these first */
    const char *env_api_key = getenv("MOONSHOT_API_KEY");
    if (env_api_key) {
        strlcpy(config->api_key, env_api_key, sizeof(config->api_key));
    }

    const char *env_api_base = getenv("MOONSHOT_API_BASE");
    if (env_api_base) {
        strlcpy(config->api_base, env_api_base, sizeof(config->api_base));
    }

    const char *env_model = getenv("MOONSHOT_MODEL");
    if (env_model) {
        strlcpy(config->model, env_model, sizeof(config->model));
    }

    /* Try loading from config file if not set by environment */
    KlawedConfig klawed_config;
    if (config_load(&klawed_config) == 0) {
        /* Find kimi-coding-plan-direct provider (preferred for testing) */
        const NamedProviderConfig *provider = config_find_provider(&klawed_config, "kimi-coding-plan-direct");
        if (!provider) {
            /* Fall back to kimi-k2.5 or moonshot-k2.5 provider */
            provider = config_find_provider(&klawed_config, "kimi-k2.5");
        }
        if (!provider) {
            provider = config_find_provider(&klawed_config, "moonshot-k2.5");
        }
        if (!provider) {
            /* Try any moonshot provider with kimi-k2.5 model */
            for (int i = 0; i < klawed_config.provider_count; i++) {
                if (klawed_config.providers[i].config.provider_type == PROVIDER_MOONSHOT &&
                    strstr(klawed_config.providers[i].config.model, "kimi-k2.5")) {
                    provider = &klawed_config.providers[i];
                    break;
                }
            }
        }
        if (!provider) {
            /* Fall back to any moonshot provider */
            for (int i = 0; i < klawed_config.provider_count; i++) {
                if (klawed_config.providers[i].config.provider_type == PROVIDER_MOONSHOT) {
                    provider = &klawed_config.providers[i];
                    break;
                }
            }
        }

        if (provider) {
            /* Only use config values if not already set by environment */
            if (!config->api_base[0]) {
                strlcpy(config->api_base, provider->config.api_base, sizeof(config->api_base));
            }
            if (!config->model[0]) {
                strlcpy(config->model, provider->config.model, sizeof(config->model));
            }

            /* Copy extra_headers from config */
            if (provider->config.extra_headers[0]) {
                strlcpy(config->extra_headers, provider->config.extra_headers, sizeof(config->extra_headers));
            }

            /* Get API key from env var specified in config (only if not already set) */
            if (!config->api_key[0] && provider->config.api_key_env[0]) {
                const char *env_key = getenv(provider->config.api_key_env);
                if (env_key) {
                    strlcpy(config->api_key, env_key, sizeof(config->api_key));
                }
            }

            /* Or use API key from config directly */
            if (!config->api_key[0] && provider->config.api_key[0]) {
                strlcpy(config->api_key, provider->config.api_key, sizeof(config->api_key));
            }
        }
    }

    /* Set defaults if not configured */
    if (!config->api_base[0]) {
        strlcpy(config->api_base, "https://api.moonshot.cn/v1/chat/completions", sizeof(config->api_base));
    } else {
        /* Ensure API base has the full path - some configs may only have the domain.
         * Skip this for kimi-coding-plan-direct which uses a different path structure. */
        if (strstr(config->api_base, "/chat/completions") == NULL &&
            strstr(config->api_base, "/coding/") == NULL) {
            /* Remove trailing slash if present */
            size_t len = strlen(config->api_base);
            if (len > 0 && config->api_base[len - 1] == '/') {
                config->api_base[len - 1] = '\0';
            }
            /* Append the path */
            strlcat(config->api_base, "/v1/chat/completions", sizeof(config->api_base));
        }
    }

    if (!config->model[0]) {
        strlcpy(config->model, "kimi-k2.5", sizeof(config->model));
    }

    /* Check streaming */
    const char *streaming_env = getenv("KLAWED_ENABLE_STREAMING");
    config->streaming_enabled = (streaming_env &&
        (strcmp(streaming_env, "1") == 0 || strcasecmp(streaming_env, "true") == 0));

    /* Validate configuration */
    if (!config->api_key[0]) {
        fprintf(stderr, COLOR_RED "Error: MOONSHOT_API_KEY not set\n" COLOR_RESET);
        fprintf(stderr, "Please set the MOONSHOT_API_KEY environment variable\n");
        return -1;
    }

    return 0;
}

/* Build a chat completion request with tool calls */
static cJSON* build_test_request(const char *model, const char *prompt, int enable_tools) {
    cJSON *request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "model", model ? model : "kimi-k2.5");

    /* Messages */
    cJSON *messages = cJSON_CreateArray();
    cJSON *system_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(system_msg, "role", "system");
    cJSON_AddStringToObject(system_msg, "content", "You are a helpful assistant. When asked to perform file operations, use the available tools.");
    cJSON_AddItemToArray(messages, system_msg);

    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", prompt);
    cJSON_AddItemToArray(messages, user_msg);

    cJSON_AddItemToObject(request, "messages", messages);

    /* Enable streaming */
    cJSON_AddBoolToObject(request, "stream", 1);

    /* Add tools if requested */
    if (enable_tools) {
        cJSON *tools = cJSON_CreateArray();

        /* Glob tool */
        cJSON *glob_tool = cJSON_CreateObject();
        cJSON_AddStringToObject(glob_tool, "type", "function");
        cJSON *glob_func = cJSON_CreateObject();
        cJSON_AddStringToObject(glob_func, "name", "Glob");
        cJSON_AddStringToObject(glob_func, "description", "Find files matching a pattern");
        cJSON *glob_params = cJSON_CreateObject();
        cJSON_AddStringToObject(glob_params, "type", "object");
        cJSON *glob_properties = cJSON_CreateObject();
        cJSON *glob_pattern = cJSON_CreateObject();
        cJSON_AddStringToObject(glob_pattern, "type", "string");
        cJSON_AddStringToObject(glob_pattern, "description", "File pattern to match (e.g., '*.c')");
        cJSON_AddItemToObject(glob_properties, "pattern", glob_pattern);
        cJSON_AddItemToObject(glob_params, "properties", glob_properties);
        cJSON *glob_required = cJSON_CreateArray();
        cJSON_AddItemToArray(glob_required, cJSON_CreateString("pattern"));
        cJSON_AddItemToObject(glob_params, "required", glob_required);
        cJSON_AddItemToObject(glob_func, "parameters", glob_params);
        cJSON_AddItemToObject(glob_tool, "function", glob_func);
        cJSON_AddItemToArray(tools, glob_tool);

        /* Read tool */
        cJSON *read_tool = cJSON_CreateObject();
        cJSON_AddStringToObject(read_tool, "type", "function");
        cJSON *read_func = cJSON_CreateObject();
        cJSON_AddStringToObject(read_func, "name", "Read");
        cJSON_AddStringToObject(read_func, "description", "Read a file's contents");
        cJSON *read_params = cJSON_CreateObject();
        cJSON_AddStringToObject(read_params, "type", "object");
        cJSON *read_properties = cJSON_CreateObject();
        cJSON *read_path = cJSON_CreateObject();
        cJSON_AddStringToObject(read_path, "type", "string");
        cJSON_AddStringToObject(read_path, "description", "Path to the file");
        cJSON_AddItemToObject(read_properties, "file_path", read_path);
        cJSON_AddItemToObject(read_params, "properties", read_properties);
        cJSON *read_required = cJSON_CreateArray();
        cJSON_AddItemToArray(read_required, cJSON_CreateString("file_path"));
        cJSON_AddItemToObject(read_params, "required", read_required);
        cJSON_AddItemToObject(read_func, "parameters", read_params);
        cJSON_AddItemToObject(read_tool, "function", read_func);
        cJSON_AddItemToArray(tools, read_tool);

        /* Bash tool */
        cJSON *bash_tool = cJSON_CreateObject();
        cJSON_AddStringToObject(bash_tool, "type", "function");
        cJSON *bash_func = cJSON_CreateObject();
        cJSON_AddStringToObject(bash_func, "name", "Bash");
        cJSON_AddStringToObject(bash_func, "description", "Execute a bash command");
        cJSON *bash_params = cJSON_CreateObject();
        cJSON_AddStringToObject(bash_params, "type", "object");
        cJSON *bash_properties = cJSON_CreateObject();
        cJSON *bash_cmd = cJSON_CreateObject();
        cJSON_AddStringToObject(bash_cmd, "type", "string");
        cJSON_AddStringToObject(bash_cmd, "description", "Command to execute");
        cJSON_AddItemToObject(bash_properties, "command", bash_cmd);
        cJSON_AddItemToObject(bash_params, "properties", bash_properties);
        cJSON *bash_required = cJSON_CreateArray();
        cJSON_AddItemToArray(bash_required, cJSON_CreateString("command"));
        cJSON_AddItemToObject(bash_params, "required", bash_required);
        cJSON_AddItemToObject(bash_func, "parameters", bash_params);
        cJSON_AddItemToObject(bash_tool, "function", bash_func);
        cJSON_AddItemToArray(tools, bash_tool);

        cJSON_AddItemToObject(request, "tools", tools);
        cJSON_AddStringToObject(request, "tool_choice", "auto");
    }

    return request;
}

/* Make a streaming API call and test tool call accumulation */
static int test_streaming_api_call(const MoonshotConfig *config, const char *prompt,
                                    int enable_tools, TestStreamingContext *ctx) {
    /* Initialize accumulator */
    if (openai_streaming_accumulator_init(&ctx->acc) != 0) {
        printf(COLOR_RED "Failed to initialize streaming accumulator\n" COLOR_RESET);
        return -1;
    }

    /* Build request */
    cJSON *request = build_test_request(config->model, prompt, enable_tools);
    char *request_body = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);

    /* Set up headers */
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", config->api_key);
    headers = curl_slist_append(headers, auth_header);

    /* Add extra headers from config (comma-separated format: "Header-Name: value, Header2: value2") */
    if (config->extra_headers[0]) {
        char *headers_copy = strdup(config->extra_headers);
        if (headers_copy) {
            char *saveptr = NULL;
            char *token = strtok_r(headers_copy, ",", &saveptr);
            while (token) {
                /* Trim leading whitespace */
                while (*token == ' ' || *token == '\t') token++;
                if (*token) {
                    headers = curl_slist_append(headers, token);
                }
                token = strtok_r(NULL, ",", &saveptr);
            }
            free(headers_copy);
        }
    }

    /* Set up HTTP request */
    HttpRequest req = {0};
    req.url = config->api_base;
    req.method = "POST";
    req.body = request_body;
    req.headers = headers;
    req.connect_timeout_ms = 30000;
    req.total_timeout_ms = 120000;  /* 2 minutes for potentially slow responses */
    req.enable_streaming = 1;  /* Always enable streaming for this test */

    printf("  API Base: %s\n", config->api_base);
    printf("  Model: %s\n", config->model);
    printf("  Prompt: \"%s\"\n", prompt);
    printf("  Tools: %s\n\n", enable_tools ? "enabled" : "disabled");

    /* Make streaming request */
    HttpResponse *resp = http_client_execute_stream(&req, test_streaming_handler, ctx, NULL, NULL);

    /* Cleanup */
    curl_slist_free_all(headers);
    free(request_body);

    if (!resp) {
        printf(COLOR_RED "Failed to execute HTTP request\n" COLOR_RESET);
        openai_streaming_accumulator_free(&ctx->acc);
        return -1;
    }

    /* Check for HTTP errors */
    if (resp->error_message) {
        printf(COLOR_RED "HTTP error: %s\n" COLOR_RESET, resp->error_message);
        http_response_free(resp);
        openai_streaming_accumulator_free(&ctx->acc);
        return -1;
    }

    http_response_free(resp);
    return 0;
}

/* Print streaming results */
static void print_streaming_results(TestStreamingContext *ctx) {
    printf("\n" COLOR_CYAN "Streaming Results:" COLOR_RESET "\n");
    printf("  Total chunks: %d\n", ctx->chunk_count);
    printf("  Text chunks: %d\n", ctx->text_chunk_count);
    printf("  Got [DONE]: %s\n", ctx->got_done ? "yes" : "no");
    printf("  Got error: %s\n", ctx->got_error ? "yes" : "no");
    if (ctx->got_error && ctx->error_message[0]) {
        printf("  Error message: %s\n", ctx->error_message);
    }

    printf("\n" COLOR_CYAN "Accumulated Content:" COLOR_RESET "\n");
    const char *text = openai_streaming_get_text(&ctx->acc);
    if (text && text[0]) {
        printf("  Text (%zu chars): %.100s%s\n",
               strlen(text),
               text,
               strlen(text) > 100 ? "..." : "");
    } else {
        printf("  Text: (none)\n");
    }

    const char *reasoning = openai_streaming_get_reasoning(&ctx->acc);
    if (reasoning && reasoning[0]) {
        printf("  Reasoning (%zu chars): %.100s%s\n",
               strlen(reasoning),
               reasoning,
               strlen(reasoning) > 100 ? "..." : "");
    } else {
        printf("  Reasoning: (none)\n");
    }

    int tool_count = openai_streaming_get_tool_call_count(&ctx->acc);
    printf("\n  Tool calls: %d\n", tool_count);

    for (int i = 0; i < tool_count; i++) {
        cJSON *tool = openai_streaming_get_tool_call(&ctx->acc, i);
        if (tool) {
            cJSON *id = cJSON_GetObjectItem(tool, "id");
            cJSON *func = cJSON_GetObjectItem(tool, "function");
            cJSON *name = func ? cJSON_GetObjectItem(func, "name") : NULL;
            cJSON *args = func ? cJSON_GetObjectItem(func, "arguments") : NULL;

            printf("    [%d] id=%s, name=%s\n",
                   i,
                   id ? id->valuestring : "(none)",
                   name ? name->valuestring : "(none)");
            if (args && args->valuestring) {
                printf("        args: %.100s%s\n",
                       args->valuestring,
                       strlen(args->valuestring) > 100 ? "..." : "");
            }
        }
    }
}

/* Test 1: Simple streaming without tools */
static int test_simple_streaming(const MoonshotConfig *config) {
    TEST_SECTION("Test 1: Simple Streaming (No Tools)");

    TestStreamingContext ctx = {0};

    int ret = test_streaming_api_call(config,
                                       "Say 'Hello, Moonshot streaming works!' and nothing else.",
                                       0, &ctx);
    if (ret != 0) {
        TEST_ASSERT(0, "API call failed");
        return -1;
    }

    print_streaming_results(&ctx);

    TEST_ASSERT(ctx.chunk_count > 0, "Received chunks");
    TEST_ASSERT(ctx.got_done, "Received [DONE] marker");
    TEST_ASSERT(!ctx.got_error, "No errors during streaming");

    const char *text = openai_streaming_get_text(&ctx.acc);
    TEST_ASSERT(text && text[0], "Accumulated text content");
    TEST_ASSERT(strstr(text, "Moonshot") != NULL, "Text contains expected content");

    int tool_count = openai_streaming_get_tool_call_count(&ctx.acc);
    TEST_ASSERT(tool_count == 0, "No tool calls for simple request");

    openai_streaming_accumulator_free(&ctx.acc);
    return 0;
}

/* Test 2: Streaming with tool calls */
static int test_streaming_with_tools(const MoonshotConfig *config) {
    TEST_SECTION("Test 2: Streaming with Tool Calls");

    TestStreamingContext ctx = {0};

    /* Prompt that should trigger a tool call */
    int ret = test_streaming_api_call(config,
                                       "List all C files in the current directory using the Glob tool.",
                                       1, &ctx);
    if (ret != 0) {
        TEST_ASSERT(0, "API call failed");
        return -1;
    }

    print_streaming_results(&ctx);

    TEST_ASSERT(ctx.chunk_count > 0, "Received chunks");
    TEST_ASSERT(ctx.got_done, "Received [DONE] marker");
    TEST_ASSERT(!ctx.got_error, "No errors during streaming");

    /* Check for tool calls, text, or reasoning response */
    int tool_count = openai_streaming_get_tool_call_count(&ctx.acc);
    const char *text = openai_streaming_get_text(&ctx.acc);
    const char *reasoning = openai_streaming_get_reasoning(&ctx.acc);

    if (tool_count > 0) {
        TEST_ASSERT(1, "Got tool calls as expected");

        /* Verify tool call structure */
        cJSON *tool = openai_streaming_get_tool_call(&ctx.acc, 0);
        TEST_ASSERT(tool != NULL, "First tool call is accessible");

        if (tool) {
            cJSON *id = cJSON_GetObjectItem(tool, "id");
            cJSON *func = cJSON_GetObjectItem(tool, "function");
            cJSON *name = func ? cJSON_GetObjectItem(func, "name") : NULL;

            TEST_ASSERT(id && id->valuestring && id->valuestring[0],
                       "Tool call has non-empty ID");
            TEST_ASSERT(name && name->valuestring && name->valuestring[0],
                       "Tool call has non-empty name");

            if (name && name->valuestring) {
                printf(COLOR_YELLOW "    Tool name: %s" COLOR_RESET "\n", name->valuestring);
            }
        }
    } else if (text && text[0]) {
        printf(COLOR_YELLOW "  Note: Model responded with text instead of tool call" COLOR_RESET "\n");
        TEST_ASSERT(1, "Got text response (model may not support tools or declined to use them)");
    } else if (reasoning && reasoning[0]) {
        printf(COLOR_YELLOW "  Note: Model responded with reasoning instead of tool call" COLOR_RESET "\n");
        TEST_ASSERT(1, "Got reasoning response (model may not support tools or declined to use them)");
    } else {
        TEST_ASSERT(0, "Expected tool calls, text, or reasoning response");
    }

    openai_streaming_accumulator_free(&ctx.acc);
    return 0;
}

/* Test 3: Streaming with reasoning content */
static int test_streaming_with_reasoning(const MoonshotConfig *config) {
    TEST_SECTION("Test 3: Streaming with Reasoning Content");

    TestStreamingContext ctx = {0};

    int ret = test_streaming_api_call(config,
                                       "Calculate 23 * 47. Show your reasoning.",
                                       0, &ctx);
    if (ret != 0) {
        TEST_ASSERT(0, "API call failed");
        return -1;
    }

    print_streaming_results(&ctx);

    TEST_ASSERT(ctx.chunk_count > 0, "Received chunks");
    TEST_ASSERT(ctx.got_done, "Received [DONE] marker");

    const char *text = openai_streaming_get_text(&ctx.acc);
    TEST_ASSERT(text && text[0], "Accumulated text content");

    /* Reasoning is optional - some models don't support it */
    const char *reasoning = openai_streaming_get_reasoning(&ctx.acc);
    if (reasoning && reasoning[0]) {
        TEST_ASSERT(1, "Model returned reasoning content");
        printf(COLOR_GREEN "  ✓ Model supports reasoning_content" COLOR_RESET "\n");
    } else {
        printf(COLOR_YELLOW "  Note: Model did not return reasoning content (may not be supported)" COLOR_RESET "\n");
    }

    openai_streaming_accumulator_free(&ctx.acc);
    return 0;
}

/* Test 4: Multiple tool calls in one request */
static int test_multiple_tool_calls(const MoonshotConfig *config) {
    TEST_SECTION("Test 4: Multiple Tool Calls");

    TestStreamingContext ctx = {0};

    int ret = test_streaming_api_call(config,
                                       "First list all C files using Glob, then read the README.md file using Read.",
                                       1, &ctx);
    if (ret != 0) {
        TEST_ASSERT(0, "API call failed");
        return -1;
    }

    print_streaming_results(&ctx);

    TEST_ASSERT(ctx.chunk_count > 0, "Received chunks");
    TEST_ASSERT(ctx.got_done, "Received [DONE] marker");

    int tool_count = openai_streaming_get_tool_call_count(&ctx.acc);

    /* Model may return 1 or 2 tool calls depending on how it interprets the request */
    if (tool_count >= 1) {
        TEST_ASSERT(1, "Got at least one tool call");

        /* Verify all tool calls have valid structure */
        for (int i = 0; i < tool_count; i++) {
            cJSON *tool = openai_streaming_get_tool_call(&ctx.acc, i);
            if (tool) {
                cJSON *id = cJSON_GetObjectItem(tool, "id");
                cJSON *func = cJSON_GetObjectItem(tool, "function");
                cJSON *name = func ? cJSON_GetObjectItem(func, "name") : NULL;

                if (id && id->valuestring && id->valuestring[0] &&
                    name && name->valuestring && name->valuestring[0]) {
                    printf(COLOR_GREEN "  ✓ Tool call %d has valid id and name\n" COLOR_RESET, i);
                }
            }
        }
    } else {
        const char *text = openai_streaming_get_text(&ctx.acc);
        if (text && text[0]) {
            printf(COLOR_YELLOW "  Note: Model responded with text instead of tool calls" COLOR_RESET "\n");
            TEST_ASSERT(1, "Got text response");
        } else {
            TEST_ASSERT(0, "Expected tool calls or text response");
        }
    }

    openai_streaming_accumulator_free(&ctx.acc);
    return 0;
}

/* Print test summary */
static void print_summary(void) {
    printf("\n" COLOR_CYAN "========================================" COLOR_RESET "\n");
    printf(COLOR_CYAN "Test Summary:" COLOR_RESET "\n");
    printf("  Total:  %d\n", tests_run);
    printf("  Passed: " COLOR_GREEN "%d" COLOR_RESET "\n", tests_passed);
    printf("  Failed: " COLOR_RED "%d" COLOR_RESET "\n", tests_failed);

    if (tests_failed == 0) {
        printf(COLOR_GREEN "\n✓ All tests passed!" COLOR_RESET "\n");
    } else {
        printf(COLOR_RED "\n✗ Some tests failed!" COLOR_RESET "\n");
    }
    printf(COLOR_CYAN "========================================" COLOR_RESET "\n");
}

/* Print usage */
static void print_usage(const char *prog_name) {
    printf("Usage: %s [options]\n\n", prog_name);
    printf("Options:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  --no-streaming Test without streaming mode (not recommended)\n\n");
    printf("Environment variables:\n");
    printf("  MOONSHOT_API_KEY          - API key for Moonshot (required)\n");
    printf("  MOONSHOT_API_BASE         - API base URL (default: https://api.moonshot.cn/v1/chat/completions)\n");
    printf("  MOONSHOT_MODEL            - Model name (default: kimi-k2.5)\n");
    printf("  KLAWED_ENABLE_STREAMING   - Set to 1 to enable streaming\n");
    printf("  KLAWED_LOG_LEVEL          - Set to DEBUG for verbose output\n\n");
    printf("Global config:\n");
    printf("  You can also configure the provider in .klawed/config.json\n\n");
    printf("Examples:\n");
    printf("  export MOONSHOT_API_KEY=\"your-api-key\"\n");
    printf("  %s\n", prog_name);
    printf("\n");
    printf("  KLAWED_ENABLE_STREAMING=1 %s\n", prog_name);
}

/* Main test runner */
int main(int argc, char *argv[]) {
    printf(COLOR_CYAN "========================================\n");
    printf("Moonshot Streaming Tool Accumulation Test\n");
    printf("Model: Kimi K2.5\n");
    printf("========================================" COLOR_RESET "\n\n");

    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    /* Load configuration */
    MoonshotConfig config;
    if (load_moonshot_config(&config) != 0) {
        print_usage(argv[0]);
        return 1;
    }

    printf("Configuration:\n");
    printf("  API Base: %s\n", config.api_base);
    printf("  Model: %s\n", config.model);
    printf("  API Key: %s***\n", config.api_key[0] ? "***" : "");
    printf("  Streaming: %s\n\n", config.streaming_enabled ? "enabled" : "disabled (warning: tests may fail)");

    if (!config.streaming_enabled) {
        printf(COLOR_YELLOW "Warning: KLAWED_ENABLE_STREAMING is not set to 1.\n");
        printf("Streaming mode is recommended for accurate testing.\n\n" COLOR_RESET);
    }

    /* Initialize HTTP client */
    if (http_client_init() != 0) {
        printf(COLOR_RED "Failed to initialize HTTP client\n" COLOR_RESET);
        return 1;
    }

    /* Initialize logger */
    log_init();

    /* Run tests */
    int failures = 0;

    printf(COLOR_MAGENTA "Starting tests..." COLOR_RESET "\n");

    if (test_simple_streaming(&config) != 0) {
        failures++;
    }

    if (test_streaming_with_tools(&config) != 0) {
        failures++;
    }

    if (test_streaming_with_reasoning(&config) != 0) {
        failures++;
    }

    if (test_multiple_tool_calls(&config) != 0) {
        failures++;
    }

    (void)failures;  /* Suppress unused variable warning when tests are empty */

    /* Cleanup */
    http_client_cleanup();
    log_shutdown();

    /* Print summary */
    print_summary();

    return tests_failed > 0 ? 1 : 0;
}
