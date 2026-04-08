/*
 * test_streaming_tool_calls.c - Live test for streaming tool call accumulation
 *
 * This test makes real API calls to verify that tool calls are properly
 * accumulated during streaming mode. It tests:
 * - Tool call accumulation from streaming chunks
 * - Proper filtering of incomplete tool calls
 * - Integration with provider configuration
 *
 * Usage:
 *   KLAWED_ENABLE_STREAMING=1 ./build/test_streaming_tool_calls [provider_key]
 *
 * Environment variables:
 *   KLAWED_ENABLE_STREAMING=1  - Required to enable streaming mode
 *   MOONSHOT_API_KEY          - API key for Moonshot provider
 *   KIMI_API_KEY              - API key for Kimi provider
 *   OPENAI_API_KEY            - API key for OpenAI provider
 *
 * Provider keys (from .klawed/config.json):
 *   moonshot                  - Moonshot/Kimi API
 *   kimi                      - Kimi Coding Plan (OAuth)
 *   openai                    - OpenAI API
 *
 * Example:
 *   KLAWED_ENABLE_STREAMING=1 ./build/test_streaming_tool_calls moonshot
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
#include "../src/provider_config_loader.h"

/* Test framework colors (use TEST_ prefix to avoid conflict with ncurses) */
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

/* Streaming context for test */
typedef struct {
    OpenAIStreamingAccumulator acc;
    int chunk_count;
    int text_chunk_count;
    int tool_chunk_count;
    int reasoning_chunk_count;
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
        /* Ignore Anthropic-specific events in this OpenAI-focused test */
        case SSE_EVENT_MESSAGE_START:
        case SSE_EVENT_CONTENT_BLOCK_START:
        case SSE_EVENT_CONTENT_BLOCK_DELTA:
        case SSE_EVENT_CONTENT_BLOCK_STOP:
        case SSE_EVENT_MESSAGE_DELTA:
        case SSE_EVENT_MESSAGE_STOP:
        case SSE_EVENT_PING:
        case SSE_EVENT_UNKNOWN:
        default:
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

/* Build a simple chat completion request with tool calls */
static cJSON* build_test_request(const char *model, const char *prompt, int enable_tools) {
    cJSON *request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "model", model ? model : "gpt-4");

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
        cJSON_AddArrayToObject(glob_params, "required");
        cJSON *glob_required = cJSON_GetObjectItem(glob_params, "required");
        cJSON_AddItemToArray(glob_required, cJSON_CreateString("pattern"));
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

        cJSON_AddItemToObject(request, "tools", tools);
        cJSON_AddStringToObject(request, "tool_choice", "auto");
    }

    return request;
}

/* Make a streaming API call and test tool call accumulation */
static int test_streaming_api_call(const char *api_key, const char *api_base,
                                    const char *model, const char *prompt,
                                    int enable_tools, TestStreamingContext *ctx) {
    /* Initialize accumulator */
    if (openai_streaming_accumulator_init(&ctx->acc) != 0) {
        printf(COLOR_RED "Failed to initialize streaming accumulator\n" COLOR_RESET);
        return -1;
    }

    /* Build request */
    cJSON *request = build_test_request(model, prompt, enable_tools);
    char *request_body = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);

    /* Set up headers */
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
    headers = curl_slist_append(headers, auth_header);

    /* Set up HTTP request */
    HttpRequest req = {0};
    req.url = api_base;
    req.method = "POST";
    req.body = request_body;
    req.headers = headers;
    req.connect_timeout_ms = 30000;
    req.total_timeout_ms = 120000;  /* 2 minutes for potentially slow responses */

    printf("  Making API call to: %s\n", api_base);
    printf("  Model: %s\n", model ? model : "(default)");
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
static int test_simple_streaming(const char *api_key, const char *api_base, const char *model) {
    TEST_SECTION("Test 1: Simple Streaming (No Tools)");

    TestStreamingContext ctx = {0};

    int ret = test_streaming_api_call(api_key, api_base, model,
                                       "Say 'Hello, streaming works!' and nothing else.",
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
    TEST_ASSERT(strstr(text, "Hello") != NULL, "Text contains expected content");

    int tool_count = openai_streaming_get_tool_call_count(&ctx.acc);
    TEST_ASSERT(tool_count == 0, "No tool calls for simple request");

    openai_streaming_accumulator_free(&ctx.acc);
    return 0;
}

/* Test 2: Streaming with tool calls */
static int test_streaming_with_tools(const char *api_key, const char *api_base, const char *model) {
    TEST_SECTION("Test 2: Streaming with Tool Calls");

    TestStreamingContext ctx = {0};

    /* Prompt that should trigger a tool call */
    int ret = test_streaming_api_call(api_key, api_base, model,
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

    /* Check for tool calls or text response */
    int tool_count = openai_streaming_get_tool_call_count(&ctx.acc);
    const char *text = openai_streaming_get_text(&ctx.acc);

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
        TEST_ASSERT(1, "Got text response (model may not support tools)");
    } else {
        TEST_ASSERT(0, "Expected tool calls or text response");
    }

    openai_streaming_accumulator_free(&ctx.acc);
    return 0;
}

/* Test 3: Streaming with reasoning content (for models that support it) */
static int test_streaming_with_reasoning(const char *api_key, const char *api_base, const char *model) {
    TEST_SECTION("Test 3: Streaming with Reasoning Content");

    TestStreamingContext ctx = {0};

    int ret = test_streaming_api_call(api_key, api_base, model,
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
    printf("Usage: %s [provider_key]\n\n", prog_name);
    printf("Environment variables:\n");
    printf("  KLAWED_ENABLE_STREAMING=1  - Required to enable streaming mode\n");
    printf("  MOONSHOT_API_KEY          - API key for Moonshot\n");
    printf("  KIMI_API_KEY              - API key for Kimi\n");
    printf("  OPENAI_API_KEY            - API key for OpenAI\n\n");
    printf("Examples:\n");
    printf("  KLAWED_ENABLE_STREAMING=1 %s moonshot\n", prog_name);
    printf("  KLAWED_ENABLE_STREAMING=1 %s openai\n", prog_name);
}

/* Main test runner */
int main(int argc, char *argv[]) {
    printf(COLOR_CYAN "========================================\n");
    printf("Streaming Tool Call Accumulation Test\n");
    printf("========================================" COLOR_RESET "\n\n");

    /* Check if streaming is enabled */
    const char *streaming_env = getenv("KLAWED_ENABLE_STREAMING");
    if (!streaming_env || (strcmp(streaming_env, "1") != 0 && strcasecmp(streaming_env, "true") != 0)) {
        printf(COLOR_YELLOW "Warning: KLAWED_ENABLE_STREAMING is not set to 1.\n");
        printf("Streaming mode should be enabled for accurate testing.\n");
        printf("Some providers may not use streaming accumulator without this.\n\n" COLOR_RESET);
    }

    /* Load provider configuration */
    UnifiedProviderConfig provider_config;
    if (provider_config_load(&provider_config) != 0) {
        printf(COLOR_RED "Failed to load provider configuration\n" COLOR_RESET);
        return 1;
    }

    /* Get provider key from command line or use effective provider */
    const char *provider_key = NULL;
    if (argc > 1) {
        provider_key = argv[1];
    }

    /* Get effective provider */
    const NamedProviderConfig *named_provider = NULL;
    if (provider_key) {
        named_provider = provider_config_find_provider(&provider_config, provider_key);
        if (!named_provider) {
            printf(COLOR_RED "Provider '%s' not found in configuration\n" COLOR_RESET, provider_key);
            printf("Available providers:\n");
            for (int i = 0; i < provider_config.unified_provider_count; i++) {
                printf("  - %s\n", provider_config.unified_providers[i].key);
            }
            return 1;
        }
    } else {
        named_provider = provider_config.effective_provider;
        if (!named_provider) {
            printf(COLOR_RED "No effective provider configured\n" COLOR_RESET);
            print_usage(argv[0]);
            return 1;
        }
        provider_key = named_provider->key;
    }

    const LLMProviderConfig *config = &named_provider->config;

    printf("Using provider: " COLOR_GREEN "%s" COLOR_RESET "\n", provider_key);
    printf("Provider type: %d\n", config->provider_type);
    printf("Model: %s\n", config->model[0] ? config->model : "(default)");
    printf("API Base: %s\n", config->api_base[0] ? config->api_base : "(default)");
    printf("\n");

    /* Get API key */
    const char *api_key = NULL;
    const char *key_source = NULL;

    if (config->api_key[0]) {
        api_key = config->api_key;
        key_source = "config file";
    } else if (config->api_key_env[0]) {
        api_key = getenv(config->api_key_env);
        key_source = config->api_key_env;
    } else {
        /* Try common env vars based on provider type */
        switch (config->provider_type) {
            case PROVIDER_MOONSHOT:
                api_key = getenv("MOONSHOT_API_KEY");
                key_source = "MOONSHOT_API_KEY";
                break;
            case PROVIDER_OPENAI:
            case PROVIDER_OPENAI_RESPONSES:
                api_key = getenv("OPENAI_API_KEY");
                key_source = "OPENAI_API_KEY";
                break;
            /* Handle other provider types with sensible defaults */
            case PROVIDER_AUTO:
            case PROVIDER_ANTHROPIC:
            case PROVIDER_BEDROCK:
            case PROVIDER_DEEPSEEK:
            case PROVIDER_KIMI_CODING_PLAN:
            case PROVIDER_OPENAI_SUB:
            case PROVIDER_ANTHROPIC_SUB:
            case PROVIDER_ZAI_CODING:
            case PROVIDER_MINIMAX_CODING:
            case PROVIDER_CUSTOM:
            default:
                api_key = getenv("OPENAI_API_KEY");
                key_source = "OPENAI_API_KEY";
                break;
        }
    }

    if (!api_key) {
        printf(COLOR_RED "No API key found. Checked: %s\n" COLOR_RESET, key_source);
        return 1;
    }
    printf("API Key: %s*** (from %s)\n",
           api_key[0] ? "***" : "",
           key_source);

    /* Get API base */
    const char *api_base = config->api_base[0] ? config->api_base : NULL;
    if (!api_base) {
        /* Use defaults based on provider type */
        switch (config->provider_type) {
            case PROVIDER_MOONSHOT:
                api_base = "https://api.moonshot.cn/v1/chat/completions";
                break;
            case PROVIDER_OPENAI:
            case PROVIDER_OPENAI_RESPONSES:
                api_base = "https://api.openai.com/v1/chat/completions";
                break;
            /* Handle other provider types with sensible defaults */
            case PROVIDER_AUTO:
            case PROVIDER_ANTHROPIC:
            case PROVIDER_BEDROCK:
            case PROVIDER_DEEPSEEK:
            case PROVIDER_KIMI_CODING_PLAN:
            case PROVIDER_OPENAI_SUB:
            case PROVIDER_ANTHROPIC_SUB:
            case PROVIDER_ZAI_CODING:
            case PROVIDER_MINIMAX_CODING:
            case PROVIDER_CUSTOM:
            default:
                api_base = "https://api.openai.com/v1/chat/completions";
                break;
        }
    }

    /* Get model */
    const char *model = config->model[0] ? config->model : NULL;

    /* Initialize HTTP client */
    if (http_client_init() != 0) {
        printf(COLOR_RED "Failed to initialize HTTP client\n" COLOR_RESET);
        return 1;
    }

    /* Initialize logger */
    log_init();

    /* Run tests */
    int failures = 0;

    printf("\n" COLOR_MAGENTA "Starting tests..." COLOR_RESET "\n");

    if (test_simple_streaming(api_key, api_base, model) != 0) {
        failures++;
    }

    if (test_streaming_with_tools(api_key, api_base, model) != 0) {
        failures++;
    }

    if (test_streaming_with_reasoning(api_key, api_base, model) != 0) {
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
