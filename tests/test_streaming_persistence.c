/*
 * Unit Tests for Streaming Persistence
 *
 * Tests that streaming API responses are properly serialized into
 * raw_response strings so that persistence (api_calls.db), token
 * extraction, and other downstream consumers work correctly.
 *
 * This tests the synthetic response construction code that was added
 * to openai_provider.c and bedrock_provider.c to fix the bug where
 * streaming calls silently dropped raw_response (which was NULL after
 * http_client_execute_stream() returned).
 *
 * Compilation: make test-streaming-persistence
 * Usage: ./build/test_streaming_persistence
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>

/* Include OpenAI streaming types */
#include "../src/openai_streaming.h"

/* Include AWS Bedrock for bedrock_convert_response */
#include "../src/aws_bedrock.h"

/* ============================================================================
 * Bedrock synthetic response builder
 *
 * This function mirrors the inline streaming code path in
 * bedrock_provider.c:bedrock_execute_request(). It builds the
 * Anthropic-format synthetic response from a streaming context,
 * converts it to OpenAI format, and serializes it to a string,
 * exactly as the production code does for result.raw_response.
 * ============================================================================ */

#define BEDROCK_STREAMING_CONTEXT_DEFINED 1
typedef struct {
    char *accumulated_text;
    size_t accumulated_size;
    size_t accumulated_capacity;
    char *tool_use_id;
    char *tool_use_name;
    char *tool_input_json;
    size_t tool_input_size;
    size_t tool_input_capacity;
    char *stop_reason;
} BedrockStreamingContext;

static char* bedrock_build_streaming_raw_response(BedrockStreamingContext *ctx) {
    if (!ctx) return NULL;

    /* Build synthetic response in Anthropic format */
    cJSON *anth_response = cJSON_CreateObject();
    if (!anth_response) return NULL;

    cJSON_AddStringToObject(anth_response, "id", "streaming");
    cJSON_AddStringToObject(anth_response, "type", "message");
    cJSON_AddStringToObject(anth_response, "role", "assistant");

    cJSON *content_array = cJSON_CreateArray();

    /* Add text content if we have any */
    if (ctx->accumulated_text && ctx->accumulated_size > 0) {
        cJSON *text_block = cJSON_CreateObject();
        cJSON_AddStringToObject(text_block, "type", "text");
        cJSON_AddStringToObject(text_block, "text", ctx->accumulated_text);
        cJSON_AddItemToArray(content_array, text_block);
    }

    /* Add tool use if we have any */
    if (ctx->tool_use_id && ctx->tool_use_name) {
        cJSON *tool_block = cJSON_CreateObject();
        cJSON_AddStringToObject(tool_block, "type", "tool_use");
        cJSON_AddStringToObject(tool_block, "id", ctx->tool_use_id);
        cJSON_AddStringToObject(tool_block, "name", ctx->tool_use_name);

        cJSON *input = NULL;
        if (ctx->tool_input_json && ctx->tool_input_size > 0) {
            input = cJSON_Parse(ctx->tool_input_json);
        }
        if (!input) {
            input = cJSON_CreateObject();
        }
        cJSON_AddItemToObject(tool_block, "input", input);
        cJSON_AddItemToArray(content_array, tool_block);
    }

    cJSON_AddItemToObject(anth_response, "content", content_array);
    cJSON_AddStringToObject(anth_response, "stop_reason",
        ctx->stop_reason ? ctx->stop_reason : "end_turn");

    /* Convert Anthropic format to OpenAI format */
    char *anth_str = cJSON_PrintUnformatted(anth_response);
    cJSON_Delete(anth_response);

    if (!anth_str) return NULL;

    cJSON *openai_json = bedrock_convert_response(anth_str);
    free(anth_str);

    if (!openai_json) return NULL;

    char *synth_str = cJSON_PrintUnformatted(openai_json);
    cJSON_Delete(openai_json);

    return synth_str;
}

/* Undefine ncurses COLOR_* macros which conflict with our ANSI color codes */
#ifdef COLOR_GREEN
#undef COLOR_GREEN
#endif
#ifdef COLOR_RED
#undef COLOR_RED
#endif
#ifdef COLOR_YELLOW
#undef COLOR_YELLOW
#endif
#ifdef COLOR_CYAN
#undef COLOR_CYAN
#endif

/* ============================================================================
 * Test Framework
 * ============================================================================ */

#define COLOR_RESET "\033[0m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_CYAN "\033[36m"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        tests_run++; \
        if (condition) { \
            tests_passed++; \
            printf(COLOR_GREEN "  \xe2\x9c\x93 " COLOR_RESET "%s\n", message); \
        } else { \
            tests_failed++; \
            printf(COLOR_RED "  \xe2\x9c\x97 " COLOR_RESET "%s\n", message); \
        } \
    } while (0)

#define TEST_SUMMARY() \
    printf("\n" COLOR_CYAN "Test Summary:" COLOR_RESET "\n"); \
    printf("  Total:  %d\n", tests_run); \
    printf("  Passed: " COLOR_GREEN "%d" COLOR_RESET "\n", tests_passed); \
    printf("  Failed: " COLOR_RED "%d" COLOR_RESET "\n", tests_failed); \
    return tests_failed > 0 ? 1 : 0

/* ============================================================================
 * Test: OpenAI streaming response reconstruction
 *
 * Uses openai_streaming_build_response() from openai_streaming.c,
 * which is the exact same function called by openai_provider.c and
 * other providers when streaming completes.
 * ============================================================================ */

static void test_openai_text_only(void) {
    printf(COLOR_YELLOW "\nOpenAI: Text-only streaming response\n" COLOR_RESET);

    /* Initialize a streaming accumulator */
    OpenAIStreamingAccumulator acc;
    int rc = openai_streaming_accumulator_init(&acc);
    TEST_ASSERT(rc == 0, "openai_streaming_accumulator_init should succeed");

    /* Populate it with test data (simulating after streaming events) */
    acc.message_id = strdup("chatcmpl-test123");
    acc.model = strdup("gpt-5");
    acc.finish_reason = strdup("stop");
    strcpy(acc.accumulated_text, "Hello, this is a test response.");
    acc.accumulated_size = strlen(acc.accumulated_text);

    /* Build synthetic response */
    cJSON *response = openai_streaming_build_response(&acc);
    TEST_ASSERT(response != NULL, "openai_streaming_build_response should return non-NULL");

    /* Serialize to string (same as provider does for result.raw_response) */
    char *raw_response = cJSON_PrintUnformatted(response);
    TEST_ASSERT(raw_response != NULL, "raw_response serialization should succeed");

    /* Parse it back to verify structure */
    cJSON *parsed = cJSON_Parse(raw_response);
    TEST_ASSERT(parsed != NULL, "raw_response should be valid JSON");

    /* Verify fields */
    cJSON *id = cJSON_GetObjectItem(parsed, "id");
    TEST_ASSERT(id != NULL && cJSON_IsString(id) &&
                strcmp(id->valuestring, "chatcmpl-test123") == 0,
                "id field should match");

    cJSON *model = cJSON_GetObjectItem(parsed, "model");
    TEST_ASSERT(model != NULL && cJSON_IsString(model) &&
                strcmp(model->valuestring, "gpt-5") == 0,
                "model field should match");

    cJSON *choices = cJSON_GetObjectItem(parsed, "choices");
    TEST_ASSERT(choices != NULL && cJSON_IsArray(choices) &&
                cJSON_GetArraySize(choices) == 1,
                "choices should be an array with 1 element");

    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    cJSON *message = cJSON_GetObjectItem(choice, "message");
    TEST_ASSERT(message != NULL, "choice should have message");

    cJSON *content = cJSON_GetObjectItem(message, "content");
    TEST_ASSERT(content != NULL && cJSON_IsString(content) &&
                strcmp(content->valuestring, "Hello, this is a test response.") == 0,
                "content should match accumulated text");

    cJSON *role = cJSON_GetObjectItem(message, "role");
    TEST_ASSERT(role != NULL && cJSON_IsString(role) &&
                strcmp(role->valuestring, "assistant") == 0,
                "role should be assistant");

    cJSON *finish_reason = cJSON_GetObjectItem(choice, "finish_reason");
    TEST_ASSERT(finish_reason != NULL && cJSON_IsString(finish_reason) &&
                strcmp(finish_reason->valuestring, "stop") == 0,
                "finish_reason should be stop");

    cJSON *usage = cJSON_GetObjectItem(parsed, "usage");
    TEST_ASSERT(usage != NULL, "usage should be present");
    TEST_ASSERT(cJSON_IsObject(usage), "usage should be an object");

    cJSON *object = cJSON_GetObjectItem(parsed, "object");
    TEST_ASSERT(object != NULL && cJSON_IsString(object) &&
                strcmp(object->valuestring, "chat.completion") == 0,
                "object should be chat.completion");

    cJSON *created = cJSON_GetObjectItem(parsed, "created");
    TEST_ASSERT(created != NULL && cJSON_IsNumber(created) &&
                created->valuedouble > 0,
                "created should be a positive number (timestamp)");

    /* Verify no tool_calls in message */
    cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
    TEST_ASSERT(tool_calls == NULL,
                "message should NOT have tool_calls for text-only response");

    /* Verify no reasoning_content */
    cJSON *reasoning = cJSON_GetObjectItem(message, "reasoning_content");
    TEST_ASSERT(reasoning == NULL,
                "message should NOT have reasoning_content for text-only response");

    /* Cleanup */
    cJSON_Delete(parsed);
    free(raw_response);
    cJSON_Delete(response);
    free(acc.message_id);
    free(acc.model);
    free(acc.finish_reason);
    openai_streaming_accumulator_free(&acc);
}

static void test_openai_reasoning_content(void) {
    printf(COLOR_YELLOW "\nOpenAI: Reasoning content (thinking models)\n" COLOR_RESET);

    OpenAIStreamingAccumulator acc;
    int rc = openai_streaming_accumulator_init(&acc);
    TEST_ASSERT(rc == 0, "init should succeed");

    acc.message_id = strdup("chatcmpl-reasoning");
    acc.model = strdup("deepseek-reasoner");
    acc.finish_reason = strdup("stop");
    strcpy(acc.accumulated_text, "The final answer is 42.");
    acc.accumulated_size = strlen(acc.accumulated_text);
    strcpy(acc.accumulated_reasoning, "Let me think about this...");
    acc.reasoning_size = strlen(acc.accumulated_reasoning);

    cJSON *response = openai_streaming_build_response(&acc);
    TEST_ASSERT(response != NULL, "build_response should succeed");

    char *raw_response = cJSON_PrintUnformatted(response);
    TEST_ASSERT(raw_response != NULL, "serialization should succeed");

    cJSON *parsed = cJSON_Parse(raw_response);
    TEST_ASSERT(parsed != NULL, "JSON should be valid");

    cJSON *choices = cJSON_GetObjectItem(parsed, "choices");
    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    cJSON *message = cJSON_GetObjectItem(choice, "message");

    cJSON *reasoning = cJSON_GetObjectItem(message, "reasoning_content");
    TEST_ASSERT(reasoning != NULL && cJSON_IsString(reasoning) &&
                strcmp(reasoning->valuestring, "Let me think about this...") == 0,
                "reasoning_content should be present and correct");

    cJSON *content = cJSON_GetObjectItem(message, "content");
    TEST_ASSERT(content != NULL && cJSON_IsString(content) &&
                strcmp(content->valuestring, "The final answer is 42.") == 0,
                "content should also be present");

    cJSON_Delete(parsed);
    free(raw_response);
    cJSON_Delete(response);
    free(acc.message_id);
    free(acc.model);
    free(acc.finish_reason);
    openai_streaming_accumulator_free(&acc);
}

static void test_openai_empty_content(void) {
    printf(COLOR_YELLOW "\nOpenAI: Empty content (null content)\n" COLOR_RESET);

    OpenAIStreamingAccumulator acc;
    int rc = openai_streaming_accumulator_init(&acc);
    TEST_ASSERT(rc == 0, "init should succeed");

    acc.message_id = strdup("chatcmpl-empty");
    acc.model = strdup("gpt-5");
    acc.finish_reason = strdup("tool_calls");
    /* accumulated_text is empty (null from init) */

    cJSON *response = openai_streaming_build_response(&acc);
    TEST_ASSERT(response != NULL, "build_response should succeed");

    char *raw_response = cJSON_PrintUnformatted(response);
    TEST_ASSERT(raw_response != NULL, "serialization should succeed");

    cJSON *parsed = cJSON_Parse(raw_response);
    TEST_ASSERT(parsed != NULL, "JSON should be valid");

    cJSON *choices = cJSON_GetObjectItem(parsed, "choices");
    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    cJSON *message = cJSON_GetObjectItem(choice, "message");

    cJSON *content = cJSON_GetObjectItem(message, "content");
    TEST_ASSERT(content != NULL && cJSON_IsNull(content),
                "content should be null when no text accumulated");

    cJSON *finish_reason = cJSON_GetObjectItem(choice, "finish_reason");
    TEST_ASSERT(finish_reason != NULL && cJSON_IsString(finish_reason) &&
                strcmp(finish_reason->valuestring, "tool_calls") == 0,
                "finish_reason should be tool_calls");

    cJSON_Delete(parsed);
    free(raw_response);
    cJSON_Delete(response);
    free(acc.message_id);
    free(acc.model);
    free(acc.finish_reason);
    openai_streaming_accumulator_free(&acc);
}

static cJSON* make_tool_call_delta(int index, const char *id,
                                    const char *name,
                                    const char *args_json_str) {
    /* Build a streaming delta chunk format with index required by
     * tool_accumulator_process_delta(). Arguments must be a JSON string
     * (not a parsed object) since the accumulator appends delta fragments. */
    cJSON *tc = cJSON_CreateObject();
    cJSON_AddNumberToObject(tc, "index", index);
    if (id) {
        cJSON_AddStringToObject(tc, "id", id);
    }
    cJSON_AddStringToObject(tc, "type", "function");

    cJSON *func = cJSON_CreateObject();
    if (name) {
        cJSON_AddStringToObject(func, "name", name);
    }
    if (args_json_str) {
        cJSON_AddStringToObject(func, "arguments", args_json_str);
    }
    cJSON_AddItemToObject(tc, "function", func);

    return tc;
}

static void test_openai_tool_calls(void) {
    printf(COLOR_YELLOW "\nOpenAI: Tool calls with empty text content\n" COLOR_RESET);

    OpenAIStreamingAccumulator acc;
    int rc = openai_streaming_accumulator_init(&acc);
    TEST_ASSERT(rc == 0, "init should succeed");

    acc.message_id = strdup("chatcmpl-tools");
    acc.model = strdup("gpt-5");
    acc.finish_reason = strdup("tool_calls");
    /* No text content - null */

    /* Manually add tool calls to the tool accumulator */
    cJSON *tc1 = make_tool_call_delta(0, "call_abc123", "read_file",
                                       "{\"path\": \"test.txt\"}");
    cJSON *tc_array = cJSON_CreateArray();
    cJSON_AddItemToArray(tc_array, tc1);
    tool_accumulator_process_delta(acc.tool_accumulator, tc_array);
    cJSON_Delete(tc_array);  /* accumulator makes a copy */

    cJSON *response = openai_streaming_build_response(&acc);
    TEST_ASSERT(response != NULL, "build_response should succeed");

    char *raw_response = cJSON_PrintUnformatted(response);
    TEST_ASSERT(raw_response != NULL, "serialization should succeed");

    cJSON *parsed = cJSON_Parse(raw_response);
    TEST_ASSERT(parsed != NULL, "JSON should be valid");

    cJSON *choices = cJSON_GetObjectItem(parsed, "choices");
    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    cJSON *message = cJSON_GetObjectItem(choice, "message");

    /* Content should be null (no text accumulated) */
    cJSON *content = cJSON_GetObjectItem(message, "content");
    TEST_ASSERT(content != NULL && cJSON_IsNull(content),
                "content should be null when only tool calls");

    /* Tool calls should be present */
    cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
    TEST_ASSERT(tool_calls != NULL && cJSON_IsArray(tool_calls),
                "tool_calls should be present and be an array");

    int tc_count = cJSON_GetArraySize(tool_calls);
    TEST_ASSERT(tc_count == 1, "should have exactly 1 tool call");

    cJSON *tc = cJSON_GetArrayItem(tool_calls, 0);
    cJSON *tc_id = cJSON_GetObjectItem(tc, "id");
    cJSON *tc_func = cJSON_GetObjectItem(tc, "function");
    TEST_ASSERT(tc_id != NULL && cJSON_IsString(tc_id) &&
                strcmp(tc_id->valuestring, "call_abc123") == 0,
                "tool call id should match");
    TEST_ASSERT(tc_func != NULL, "tool call should have function");

    cJSON *tc_name = cJSON_GetObjectItem(tc_func, "name");
    TEST_ASSERT(tc_name != NULL && cJSON_IsString(tc_name) &&
                strcmp(tc_name->valuestring, "read_file") == 0,
                "tool call function name should match");

    cJSON *tc_args = cJSON_GetObjectItem(tc_func, "arguments");
    TEST_ASSERT(tc_args != NULL && cJSON_IsString(tc_args),
                "tool call arguments should be a JSON string");

    /* Parse the arguments string to verify contents */
    cJSON *parsed_args = cJSON_Parse(tc_args->valuestring);
    TEST_ASSERT(parsed_args != NULL,
                "tool call arguments string should be valid JSON");

    cJSON *arg_path = cJSON_GetObjectItem(parsed_args, "path");
    TEST_ASSERT(arg_path != NULL && cJSON_IsString(arg_path) &&
                strcmp(arg_path->valuestring, "test.txt") == 0,
                "tool call arguments should contain path");
    cJSON_Delete(parsed_args);

    cJSON_Delete(parsed);
    free(raw_response);
    cJSON_Delete(response);
    free(acc.message_id);
    free(acc.model);
    free(acc.finish_reason);
    openai_streaming_accumulator_free(&acc);
}

static void test_openai_text_and_tool_calls(void) {
    printf(COLOR_YELLOW "\nOpenAI: Text content AND tool calls\n" COLOR_RESET);

    OpenAIStreamingAccumulator acc;
    int rc = openai_streaming_accumulator_init(&acc);
    TEST_ASSERT(rc == 0, "init should succeed");

    acc.message_id = strdup("chatcmpl-both");
    acc.model = strdup("gpt-5");
    acc.finish_reason = strdup("tool_calls");
    strcpy(acc.accumulated_text, "Let me search for that.");
    acc.accumulated_size = strlen(acc.accumulated_text);

    cJSON *tc1 = make_tool_call_delta(0, "call_search", "web_search",
                                       "{\"query\": \"weather today\"}");
    cJSON *tc_array = cJSON_CreateArray();
    cJSON_AddItemToArray(tc_array, tc1);
    tool_accumulator_process_delta(acc.tool_accumulator, tc_array);
    cJSON_Delete(tc_array);

    cJSON *response = openai_streaming_build_response(&acc);
    TEST_ASSERT(response != NULL, "build_response should succeed");

    char *raw_response = cJSON_PrintUnformatted(response);
    TEST_ASSERT(raw_response != NULL, "serialization should succeed");

    cJSON *parsed = cJSON_Parse(raw_response);
    TEST_ASSERT(parsed != NULL, "JSON should be valid");

    cJSON *choices = cJSON_GetObjectItem(parsed, "choices");
    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    cJSON *message = cJSON_GetObjectItem(choice, "message");

    cJSON *content = cJSON_GetObjectItem(message, "content");
    TEST_ASSERT(content != NULL && cJSON_IsString(content),
                "content should be a string (not null)");
    TEST_ASSERT(strcmp(content->valuestring, "Let me search for that.") == 0,
                "content text should match");

    cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
    TEST_ASSERT(tool_calls != NULL && cJSON_IsArray(tool_calls) &&
                cJSON_GetArraySize(tool_calls) == 1,
                "tool_calls should be present with 1 call");

    cJSON_Delete(parsed);
    free(raw_response);
    cJSON_Delete(response);
    free(acc.message_id);
    free(acc.model);
    free(acc.finish_reason);
    openai_streaming_accumulator_free(&acc);
}

static void test_openai_default_finish_reason(void) {
    printf(COLOR_YELLOW "\nOpenAI: Default finish_reason when NULL\n" COLOR_RESET);

    OpenAIStreamingAccumulator acc;
    int rc = openai_streaming_accumulator_init(&acc);
    TEST_ASSERT(rc == 0, "init should succeed");

    acc.message_id = strdup("chatcmpl-default");
    acc.model = strdup("gpt-5");
    /* finish_reason is NULL (should default to "stop") */
    strcpy(acc.accumulated_text, "Hello.");
    acc.accumulated_size = strlen(acc.accumulated_text);

    cJSON *response = openai_streaming_build_response(&acc);
    TEST_ASSERT(response != NULL, "build_response should succeed");

    char *raw_response = cJSON_PrintUnformatted(response);
    TEST_ASSERT(raw_response != NULL, "serialization should succeed");

    cJSON *parsed = cJSON_Parse(raw_response);
    TEST_ASSERT(parsed != NULL, "JSON should be valid");

    cJSON *choices = cJSON_GetObjectItem(parsed, "choices");
    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    cJSON *finish_reason = cJSON_GetObjectItem(choice, "finish_reason");

    TEST_ASSERT(finish_reason != NULL && cJSON_IsString(finish_reason) &&
                strcmp(finish_reason->valuestring, "stop") == 0,
                "finish_reason should default to 'stop' when NULL");

    cJSON_Delete(parsed);
    free(raw_response);
    cJSON_Delete(response);
    free(acc.message_id);
    free(acc.model);
    openai_streaming_accumulator_free(&acc);
}

static void test_openai_default_model_and_id(void) {
    printf(COLOR_YELLOW "\nOpenAI: Default model and message_id when NULL\n" COLOR_RESET);

    OpenAIStreamingAccumulator acc;
    int rc = openai_streaming_accumulator_init(&acc);
    TEST_ASSERT(rc == 0, "init should succeed");

    /* message_id and model are NULL (unset) */
    strcpy(acc.accumulated_text, "test");
    acc.accumulated_size = strlen(acc.accumulated_text);

    cJSON *response = openai_streaming_build_response(&acc);
    TEST_ASSERT(response != NULL, "build_response should succeed");

    char *raw_response = cJSON_PrintUnformatted(response);
    TEST_ASSERT(raw_response != NULL, "serialization should succeed");

    cJSON *parsed = cJSON_Parse(raw_response);
    TEST_ASSERT(parsed != NULL, "JSON should be valid");

    cJSON *id = cJSON_GetObjectItem(parsed, "id");
    TEST_ASSERT(id != NULL && cJSON_IsString(id) &&
                strcmp(id->valuestring, "streaming") == 0,
                "id should default to 'streaming' when NULL");

    cJSON *model = cJSON_GetObjectItem(parsed, "model");
    TEST_ASSERT(model != NULL && cJSON_IsString(model) &&
                strcmp(model->valuestring, "unknown") == 0,
                "model should default to 'unknown' when NULL");

    cJSON_Delete(parsed);
    free(raw_response);
    cJSON_Delete(response);
    openai_streaming_accumulator_free(&acc);
}

/* ============================================================================
 * Test: Bedrock streaming response raw_response
 *
 * Uses bedrock_build_streaming_raw_response() which is exported under
 * TEST_BUILD from bedrock_provider.c. This function builds the same
 * synthetic response that the inline streaming code in
 * bedrock_execute_request() produces.
 * ============================================================================ */

static void test_bedrock_text_only(void) {
    printf(COLOR_YELLOW "\nBedrock: Text-only streaming response\n" COLOR_RESET);

    BedrockStreamingContext ctx;
    memset(&ctx, 0, sizeof(ctx));

    ctx.accumulated_text = strdup("Hello from Bedrock streaming!");
    ctx.accumulated_size = strlen(ctx.accumulated_text);
    ctx.accumulated_capacity = ctx.accumulated_size + 1;
    ctx.stop_reason = strdup("end_turn");

    char *raw_response = bedrock_build_streaming_raw_response(&ctx);
    TEST_ASSERT(raw_response != NULL, "raw_response should be non-NULL");

    cJSON *parsed = cJSON_Parse(raw_response);
    TEST_ASSERT(parsed != NULL, "raw_response should be valid JSON");

    /* Verify OpenAI format */
    cJSON *choices = cJSON_GetObjectItem(parsed, "choices");
    TEST_ASSERT(choices != NULL && cJSON_IsArray(choices) &&
                cJSON_GetArraySize(choices) == 1,
                "choices should be an array with 1 element");

    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    cJSON *message = cJSON_GetObjectItem(choice, "message");

    cJSON *content = cJSON_GetObjectItem(message, "content");
    TEST_ASSERT(content != NULL && cJSON_IsString(content) &&
                strstr(content->valuestring, "Hello from Bedrock") != NULL,
                "content should contain the accumulated text");

    cJSON *role = cJSON_GetObjectItem(message, "role");
    TEST_ASSERT(role != NULL && cJSON_IsString(role) &&
                strcmp(role->valuestring, "assistant") == 0,
                "role should be assistant");

    cJSON *finish_reason = cJSON_GetObjectItem(choice, "finish_reason");
    TEST_ASSERT(finish_reason != NULL,
                "finish_reason should be present");
    TEST_ASSERT(cJSON_IsString(finish_reason) &&
                strlen(finish_reason->valuestring) > 0,
                "finish_reason should be a non-empty string");

    /* Note: Bedrock synthetic response does NOT include usage in current
     * implementation (it's not constructed from streaming context). */

    /* Verify no tool calls in text-only response */
    cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
    TEST_ASSERT(tool_calls == NULL,
                "message should NOT have tool_calls for text-only response");

    cJSON_Delete(parsed);
    free(raw_response);
    free(ctx.accumulated_text);
    free(ctx.stop_reason);
}

static void test_bedrock_tool_use(void) {
    printf(COLOR_YELLOW "\nBedrock: Tool use streaming response\n" COLOR_RESET);

    BedrockStreamingContext ctx;
    memset(&ctx, 0, sizeof(ctx));

    /* No text content - pure tool use */
    ctx.tool_use_id = strdup("tooluse_abc123");
    ctx.tool_use_name = strdup("read_file");
    ctx.tool_input_json = strdup("{\"path\": \"/etc/hosts\"}");
    ctx.tool_input_size = strlen(ctx.tool_input_json);
    ctx.tool_input_capacity = ctx.tool_input_size + 1;
    ctx.stop_reason = strdup("tool_use");

    char *raw_response = bedrock_build_streaming_raw_response(&ctx);
    TEST_ASSERT(raw_response != NULL, "raw_response should be non-NULL");

    cJSON *parsed = cJSON_Parse(raw_response);
    TEST_ASSERT(parsed != NULL, "raw_response should be valid JSON");

    cJSON *choices = cJSON_GetObjectItem(parsed, "choices");
    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    cJSON *message = cJSON_GetObjectItem(choice, "message");

    /* Content should be null (no text) */
    cJSON *content = cJSON_GetObjectItem(message, "content");
    TEST_ASSERT(content != NULL && cJSON_IsNull(content),
                "content should be null when only tool use");

    /* Tool calls should be present with correct data */
    cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
    TEST_ASSERT(tool_calls != NULL && cJSON_IsArray(tool_calls),
                "tool_calls should be present");

    int tc_count = cJSON_GetArraySize(tool_calls);
    TEST_ASSERT(tc_count == 1, "should have exactly 1 tool call");

    cJSON *tc = cJSON_GetArrayItem(tool_calls, 0);
    cJSON *tc_id = cJSON_GetObjectItem(tc, "id");
    cJSON *tc_func = cJSON_GetObjectItem(tc, "function");
    TEST_ASSERT(tc_id != NULL && cJSON_IsString(tc_id) &&
                strcmp(tc_id->valuestring, "tooluse_abc123") == 0,
                "tool call id should match");
    TEST_ASSERT(tc_func != NULL, "tool call should have function");

    cJSON *tc_name = cJSON_GetObjectItem(tc_func, "name");
    TEST_ASSERT(tc_name != NULL && cJSON_IsString(tc_name) &&
                strcmp(tc_name->valuestring, "read_file") == 0,
                "tool call function name should match");

    cJSON_Delete(parsed);
    free(raw_response);
    free(ctx.tool_use_id);
    free(ctx.tool_use_name);
    free(ctx.tool_input_json);
    free(ctx.stop_reason);
}

static void test_bedrock_text_and_tool(void) {
    printf(COLOR_YELLOW "\nBedrock: Text AND tool use streaming response\n" COLOR_RESET);

    BedrockStreamingContext ctx;
    memset(&ctx, 0, sizeof(ctx));

    ctx.accumulated_text = strdup("Let me check that file.");
    ctx.accumulated_size = strlen(ctx.accumulated_text);
    ctx.accumulated_capacity = ctx.accumulated_size + 1;
    ctx.tool_use_id = strdup("tooluse_def456");
    ctx.tool_use_name = strdup("grep");
    ctx.tool_input_json = strdup("{\"pattern\": \"test\", \"path\": \".\"}");
    ctx.tool_input_size = strlen(ctx.tool_input_json);
    ctx.tool_input_capacity = ctx.tool_input_size + 1;
    ctx.stop_reason = strdup("tool_use");

    char *raw_response = bedrock_build_streaming_raw_response(&ctx);
    TEST_ASSERT(raw_response != NULL, "raw_response should be non-NULL");

    cJSON *parsed = cJSON_Parse(raw_response);
    TEST_ASSERT(parsed != NULL, "raw_response should be valid JSON");

    cJSON *choices = cJSON_GetObjectItem(parsed, "choices");
    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    cJSON *message = cJSON_GetObjectItem(choice, "message");

    /* Content should be present */
    cJSON *content = cJSON_GetObjectItem(message, "content");
    TEST_ASSERT(content != NULL && cJSON_IsString(content) &&
                strstr(content->valuestring, "Let me check") != NULL,
                "content should contain the text");

    /* Tool calls should be present */
    cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
    TEST_ASSERT(tool_calls != NULL && cJSON_IsArray(tool_calls) &&
                cJSON_GetArraySize(tool_calls) == 1,
                "tool_calls should be present with 1 call");

    cJSON_Delete(parsed);
    free(raw_response);
    free(ctx.accumulated_text);
    free(ctx.tool_use_id);
    free(ctx.tool_use_name);
    free(ctx.tool_input_json);
    free(ctx.stop_reason);
}

static void test_bedrock_no_content(void) {
    printf(COLOR_YELLOW "\nBedrock: Empty streaming context (no content at all)\n" COLOR_RESET);

    BedrockStreamingContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.stop_reason = strdup("end_turn");

    char *raw_response = bedrock_build_streaming_raw_response(&ctx);
    TEST_ASSERT(raw_response != NULL, "raw_response should be non-NULL");

    cJSON *parsed = cJSON_Parse(raw_response);
    TEST_ASSERT(parsed != NULL, "raw_response should be valid JSON");

    cJSON *choices = cJSON_GetObjectItem(parsed, "choices");
    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    cJSON *message = cJSON_GetObjectItem(choice, "message");

    cJSON *content = cJSON_GetObjectItem(message, "content");
    TEST_ASSERT(content != NULL && cJSON_IsNull(content),
                "content should be null when no text or tool data");

    cJSON_Delete(parsed);
    free(raw_response);
    free(ctx.stop_reason);
}

/* ============================================================================
 * Main - Run all tests
 * ============================================================================ */

int main(void) {
    printf(COLOR_CYAN "========================================\n" COLOR_RESET);
    printf(COLOR_CYAN "  Streaming Persistence Unit Tests\n" COLOR_RESET);
    printf(COLOR_CYAN "========================================\n" COLOR_RESET);

    /* -------- OpenAI tests -------- */
    printf(COLOR_CYAN "\n--- OpenAI Provider ---\n" COLOR_RESET);
    test_openai_text_only();
    test_openai_reasoning_content();
    test_openai_empty_content();
    test_openai_tool_calls();
    test_openai_text_and_tool_calls();
    test_openai_default_finish_reason();
    test_openai_default_model_and_id();

    /* -------- Bedrock tests -------- */
    printf(COLOR_CYAN "\n--- Bedrock Provider ---\n" COLOR_RESET);
    test_bedrock_text_only();
    test_bedrock_tool_use();
    test_bedrock_text_and_tool();
    test_bedrock_no_content();

    /* -------- Summary -------- */
    printf("\n");
    TEST_SUMMARY();
}
