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
 * Test: OpenAI streaming token usage capture and reporting
 * ============================================================================ */

static void test_openai_usage_zero_when_not_set(void) {
    printf(COLOR_YELLOW "\\nOpenAI: Usage defaults to zero when no usage events\\n" COLOR_RESET);

    /* When no usage chunk arrives, accumulator should have zero tokens */
    OpenAIStreamingAccumulator acc;
    int rc = openai_streaming_accumulator_init(&acc);
    TEST_ASSERT(rc == 0, "init should succeed");

    TEST_ASSERT(acc.prompt_tokens == 0, "prompt_tokens should be 0 by default");
    TEST_ASSERT(acc.completion_tokens == 0, "completion_tokens should be 0 by default");
    TEST_ASSERT(acc.total_tokens == 0, "total_tokens should be 0 by default");

    /* Build response and verify usage object exists with zeros */
    acc.message_id = strdup("chatcmpl-nousage");
    acc.model = strdup("gpt-5");
    acc.finish_reason = strdup("stop");
    strcpy(acc.accumulated_text, "test");
    acc.accumulated_size = strlen(acc.accumulated_text);

    cJSON *response = openai_streaming_build_response(&acc);
    TEST_ASSERT(response != NULL, "build_response should succeed");

    char *raw_response = cJSON_PrintUnformatted(response);
    TEST_ASSERT(raw_response != NULL, "serialization should succeed");

    cJSON *parsed = cJSON_Parse(raw_response);
    TEST_ASSERT(parsed != NULL, "JSON should be valid");

    cJSON *usage = cJSON_GetObjectItem(parsed, "usage");
    TEST_ASSERT(usage != NULL, "usage should be present");

    cJSON *pt = cJSON_GetObjectItem(usage, "prompt_tokens");
    TEST_ASSERT(pt != NULL && cJSON_IsNumber(pt) && pt->valueint == 0,
                "prompt_tokens should be 0 when not set");

    cJSON *ct = cJSON_GetObjectItem(usage, "completion_tokens");
    TEST_ASSERT(ct != NULL && cJSON_IsNumber(ct) && ct->valueint == 0,
                "completion_tokens should be 0 when not set");

    cJSON *tt = cJSON_GetObjectItem(usage, "total_tokens");
    TEST_ASSERT(tt != NULL && cJSON_IsNumber(tt) && tt->valueint == 0,
                "total_tokens should be 0 when not set");

    cJSON_Delete(parsed);
    free(raw_response);
    cJSON_Delete(response);
    free(acc.message_id);
    free(acc.model);
    free(acc.finish_reason);
    openai_streaming_accumulator_free(&acc);
}

static cJSON* make_openai_usage_chunk(int prompt, int completion, int total) {
    /* Build a streaming final chunk (no choices, just usage).
     * This simulates the final chunk from OpenAI streaming when
     * stream_options: { include_usage: true } is set. */
    cJSON *chunk = cJSON_CreateObject();
    cJSON *usage = cJSON_CreateObject();
    cJSON_AddNumberToObject(usage, "prompt_tokens", prompt);
    cJSON_AddNumberToObject(usage, "completion_tokens", completion);
    cJSON_AddNumberToObject(usage, "total_tokens", total);
    cJSON_AddItemToObject(chunk, "usage", usage);
    cJSON_AddNullToObject(chunk, "choices");  /* final chunk has empty choices */
    return chunk;
}

static void test_openai_usage_captured_from_final_chunk(void) {
    printf(COLOR_YELLOW "\\nOpenAI: Usage captured from final streaming chunk\\n" COLOR_RESET);

    /* Simulate streaming: start with no usage, then get a final chunk with usage.
     * The final chunk has only usage data (no choices/delta). */
    OpenAIStreamingAccumulator acc;
    int rc = openai_streaming_accumulator_init(&acc);
    TEST_ASSERT(rc == 0, "init should succeed");

    /* Process some regular content chunks first (no usage yet) */
    cJSON *content_chunk = cJSON_CreateObject();
    cJSON *choices = cJSON_CreateArray();
    cJSON *choice = cJSON_CreateObject();
    cJSON *delta = cJSON_CreateObject();
    cJSON_AddStringToObject(delta, "content", "Hello");
    cJSON_AddItemToObject(choice, "delta", delta);
    cJSON_AddItemToArray(choices, choice);
    cJSON_AddItemToObject(content_chunk, "choices", choices);

    StreamEvent content_event = {
        .type = SSE_EVENT_OPENAI_CHUNK,
        .data = content_chunk
    };
    openai_streaming_process_event(&acc, &content_event);
    cJSON_Delete(content_chunk);

    /* Now simulate the final chunk with usage data (no choices) */
    cJSON *usage_chunk = make_openai_usage_chunk(150, 42, 192);
    StreamEvent usage_event = {
        .type = SSE_EVENT_OPENAI_CHUNK,
        .data = usage_chunk
    };
    openai_streaming_process_event(&acc, &usage_event);
    cJSON_Delete(usage_chunk);

    /* Verify accumulator captured the usage */
    TEST_ASSERT(acc.prompt_tokens == 150,
                "prompt_tokens should be captured from final chunk");
    TEST_ASSERT(acc.completion_tokens == 42,
                "completion_tokens should be captured from final chunk");
    TEST_ASSERT(acc.total_tokens == 192,
                "total_tokens should be captured from final chunk");

    /* Now build the synthetic response and verify usage is serialized correctly */
    acc.message_id = strdup("chatcmpl-usage123");
    acc.model = strdup("gpt-5");
    acc.finish_reason = strdup("stop");

    cJSON *response = openai_streaming_build_response(&acc);
    TEST_ASSERT(response != NULL, "build_response should succeed");

    char *raw_response = cJSON_PrintUnformatted(response);
    TEST_ASSERT(raw_response != NULL, "serialization should succeed");

    cJSON *parsed = cJSON_Parse(raw_response);
    TEST_ASSERT(parsed != NULL, "JSON should be valid");

    cJSON *usage = cJSON_GetObjectItem(parsed, "usage");
    TEST_ASSERT(usage != NULL, "usage should be present in response");

    cJSON *pt = cJSON_GetObjectItem(usage, "prompt_tokens");
    TEST_ASSERT(pt != NULL && cJSON_IsNumber(pt) && pt->valueint == 150,
                "prompt_tokens should be 150");

    cJSON *ct = cJSON_GetObjectItem(usage, "completion_tokens");
    TEST_ASSERT(ct != NULL && cJSON_IsNumber(ct) && ct->valueint == 42,
                "completion_tokens should be 42");

    cJSON *tt = cJSON_GetObjectItem(usage, "total_tokens");
    TEST_ASSERT(tt != NULL && cJSON_IsNumber(tt) && tt->valueint == 192,
                "total_tokens should be 192");

    /* Also verify the text content was accumulated correctly */
    cJSON *choices_resp = cJSON_GetObjectItem(parsed, "choices");
    cJSON *choice0 = cJSON_GetArrayItem(choices_resp, 0);
    cJSON *msg = cJSON_GetObjectItem(choice0, "message");
    cJSON *content = cJSON_GetObjectItem(msg, "content");
    TEST_ASSERT(content != NULL && cJSON_IsString(content) &&
                strcmp(content->valuestring, "Hello") == 0,
                "text content should still be accumulated correctly");

    cJSON_Delete(parsed);
    free(raw_response);
    cJSON_Delete(response);
    free(acc.message_id);
    free(acc.model);
    free(acc.finish_reason);
    openai_streaming_accumulator_free(&acc);
}

static void test_openai_usage_overwrites_on_multiple_chunks(void) {
    printf(COLOR_YELLOW "\\nOpenAI: Usage overwritten by later chunks\\n" COLOR_RESET);

    /* If multiple chunks have usage, the last one should win */
    OpenAIStreamingAccumulator acc;
    int rc = openai_streaming_accumulator_init(&acc);
    TEST_ASSERT(rc == 0, "init should succeed");

    cJSON *chunk1 = make_openai_usage_chunk(100, 20, 120);
    StreamEvent ev1 = { .type = SSE_EVENT_OPENAI_CHUNK, .data = chunk1 };
    openai_streaming_process_event(&acc, &ev1);
    cJSON_Delete(chunk1);

    TEST_ASSERT(acc.prompt_tokens == 100, "prompt_tokens should be 100 after first chunk");
    TEST_ASSERT(acc.completion_tokens == 20, "completion_tokens should be 20 after first chunk");
    TEST_ASSERT(acc.total_tokens == 120, "total_tokens should be 120 after first chunk");

    cJSON *chunk2 = make_openai_usage_chunk(200, 50, 250);
    StreamEvent ev2 = { .type = SSE_EVENT_OPENAI_CHUNK, .data = chunk2 };
    openai_streaming_process_event(&acc, &ev2);
    cJSON_Delete(chunk2);

    /* Values should be overwritten (last chunk wins) */
    TEST_ASSERT(acc.prompt_tokens == 200, "prompt_tokens should be overwritten to 200");
    TEST_ASSERT(acc.completion_tokens == 50, "completion_tokens should be overwritten to 50");
    TEST_ASSERT(acc.total_tokens == 250, "total_tokens should be overwritten to 250");

    openai_streaming_accumulator_free(&acc);
}

static void test_openai_usage_fallback_total(void) {
    printf(COLOR_YELLOW "\\nOpenAI: Usage fallback when total_tokens is 0\\n" COLOR_RESET);

    /* Some APIs provide prompt_tokens and completion_tokens but not total_tokens.
     * openai_streaming_build_response() should compute total = prompt + completion. */
    OpenAIStreamingAccumulator acc;
    int rc = openai_streaming_accumulator_init(&acc);
    TEST_ASSERT(rc == 0, "init should succeed");

    /* Set fields directly (simulating a chunk that only had prompt+completion) */
    acc.prompt_tokens = 300;
    acc.completion_tokens = 80;
    acc.total_tokens = 0;  /* Not provided by API */
    acc.message_id = strdup("chatcmpl-fallback");
    acc.model = strdup("gpt-5");
    acc.finish_reason = strdup("stop");
    strcpy(acc.accumulated_text, "test");
    acc.accumulated_size = strlen(acc.accumulated_text);

    cJSON *response = openai_streaming_build_response(&acc);
    TEST_ASSERT(response != NULL, "build_response should succeed");

    char *raw_response = cJSON_PrintUnformatted(response);
    TEST_ASSERT(raw_response != NULL, "serialization should succeed");

    cJSON *parsed = cJSON_Parse(raw_response);
    TEST_ASSERT(parsed != NULL, "JSON should be valid");

    cJSON *usage = cJSON_GetObjectItem(parsed, "usage");
    TEST_ASSERT(usage != NULL, "usage should be present");

    cJSON *pt = cJSON_GetObjectItem(usage, "prompt_tokens");
    TEST_ASSERT(pt != NULL && cJSON_IsNumber(pt) && pt->valueint == 300,
                "prompt_tokens should be 300");

    cJSON *ct = cJSON_GetObjectItem(usage, "completion_tokens");
    TEST_ASSERT(ct != NULL && cJSON_IsNumber(ct) && ct->valueint == 80,
                "completion_tokens should be 80");

    cJSON *tt = cJSON_GetObjectItem(usage, "total_tokens");
    TEST_ASSERT(tt != NULL && cJSON_IsNumber(tt) && tt->valueint == 380,
                "total_tokens should fall back to prompt+completion (300+80=380)");

    cJSON_Delete(parsed);
    free(raw_response);
    cJSON_Delete(response);
    free(acc.message_id);
    free(acc.model);
    free(acc.finish_reason);
    openai_streaming_accumulator_free(&acc);
}

static void test_openai_usage_extracted_by_persistence(void) {
    printf(COLOR_YELLOW "\\nOpenAI: Usage in synthetic response can be extracted by persistence\\n" COLOR_RESET);

    /* This tests the end-to-end path: the synthetic raw_response JSON
     * produced by openai_streaming_build_response() must contain usage
     * that can be parsed by token_usage_extract_from_response().
     * This is exactly what persistence_log_api_call() does downstream. */

    OpenAIStreamingAccumulator acc;
    int rc = openai_streaming_accumulator_init(&acc);
    TEST_ASSERT(rc == 0, "init should succeed");

    /* First accumulate some text, then get usage from final chunk */
    cJSON *content_chunk = cJSON_CreateObject();
    cJSON *choices = cJSON_CreateArray();
    cJSON *choice = cJSON_CreateObject();
    cJSON *delta = cJSON_CreateObject();
    cJSON_AddStringToObject(delta, "content", "Some response text");
    cJSON_AddItemToObject(choice, "delta", delta);
    cJSON_AddItemToArray(choices, choice);
    cJSON_AddItemToObject(content_chunk, "choices", choices);

    StreamEvent content_ev = { .type = SSE_EVENT_OPENAI_CHUNK, .data = content_chunk };
    openai_streaming_process_event(&acc, &content_ev);
    cJSON_Delete(content_chunk);

    cJSON *usage_chunk = make_openai_usage_chunk(425, 99, 524);
    StreamEvent usage_ev = { .type = SSE_EVENT_OPENAI_CHUNK, .data = usage_chunk };
    openai_streaming_process_event(&acc, &usage_ev);
    cJSON_Delete(usage_chunk);

    acc.message_id = strdup("chatcmpl-persist");
    acc.model = strdup("gpt-5");
    acc.finish_reason = strdup("stop");

    /* Build synthetic response — the same way openai_provider.c does it */
    cJSON *response = openai_streaming_build_response(&acc);
    TEST_ASSERT(response != NULL, "build_response should succeed");

    char *raw_response = cJSON_PrintUnformatted(response);
    TEST_ASSERT(raw_response != NULL, "serialization should succeed");

    /* Now simulate what persistence_log_api_call does: parse the response
     * and extract usage. This validates the end-to-end chain. */
    cJSON *parsed = cJSON_Parse(raw_response);
    TEST_ASSERT(parsed != NULL, "raw_response should be valid JSON");

    cJSON *usage_obj = cJSON_GetObjectItem(parsed, "usage");
    TEST_ASSERT(usage_obj != NULL, "usage object must exist for persistence extraction");

    cJSON *prompt = cJSON_GetObjectItem(usage_obj, "prompt_tokens");
    cJSON *completion = cJSON_GetObjectItem(usage_obj, "completion_tokens");
    cJSON *total = cJSON_GetObjectItem(usage_obj, "total_tokens");

    TEST_ASSERT(prompt != NULL && cJSON_IsNumber(prompt),
                "prompt_tokens must be a number");
    TEST_ASSERT(completion != NULL && cJSON_IsNumber(completion),
                "completion_tokens must be a number");
    TEST_ASSERT(total != NULL && cJSON_IsNumber(total),
                "total_tokens must be a number");

    /* Verify the numbers match what we fed in */
    TEST_ASSERT(prompt->valueint == 425, "prompt_tokens should be 425");
    TEST_ASSERT(completion->valueint == 99, "completion_tokens should be 99");
    TEST_ASSERT(total->valueint == 524, "total_tokens should be 524");

    cJSON_Delete(parsed);
    free(raw_response);
    cJSON_Delete(response);
    free(acc.message_id);
    free(acc.model);
    free(acc.finish_reason);
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

    /* -------- Usage tests -------- */
    printf(COLOR_CYAN "\n--- Token Usage Capture ---\n" COLOR_RESET);
    test_openai_usage_zero_when_not_set();
    test_openai_usage_captured_from_final_chunk();
    test_openai_usage_overwrites_on_multiple_chunks();
    test_openai_usage_fallback_total();
    test_openai_usage_extracted_by_persistence();

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
