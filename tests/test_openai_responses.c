/*
 * test_openai_responses.c - Unit tests for OpenAI Responses API parsing
 *
 * Tests parsing of the /v1/responses endpoint format, including:
 * - Text-only responses (message items with output_text content)
 * - Tool call responses (function_call items in output array)
 * - Mixed responses (both text and tool calls)
 * - Reasoning items (should be ignored)
 *
 * Compilation: make test-openai-responses
 * Usage: ./test_openai_responses
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>

// Test framework colors
#define COLOR_RESET "\033[0m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_CYAN "\033[36m"

// Test counters
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
            printf(COLOR_RED "  ✗ " COLOR_RESET "%s\n", message); \
        } \
    } while (0)

#define TEST_SUMMARY() \
    do { \
        printf("\n" COLOR_CYAN "Test Summary:" COLOR_RESET "\n"); \
        printf("  Total:  %d\n", tests_run); \
        printf("  Passed: " COLOR_GREEN "%d" COLOR_RESET "\n", tests_passed); \
        printf("  Failed: " COLOR_RED "%d" COLOR_RESET "\n", tests_failed); \
        if (tests_failed == 0) { \
            printf(COLOR_GREEN "\n✓ All tests passed!" COLOR_RESET "\n"); \
            return 0; \
        } else { \
            printf(COLOR_RED "\n✗ Some tests failed!" COLOR_RESET "\n"); \
            return 1; \
        } \
    } while (0)

// ============================================================================
// Test Fixtures (JSON strings for testing)
// ============================================================================

// Response with only text (message item with output_text)
static const char *response_text_only =
    "{\"id\":\"resp_text_only\",\"object\":\"response\",\"status\":\"completed\","
    "\"output\":[{\"id\":\"msg_001\",\"type\":\"message\",\"status\":\"completed\","
    "\"content\":[{\"type\":\"output_text\",\"text\":\"Hello! How can I help you today?\"}],"
    "\"role\":\"assistant\"}]}";

// Response with only tool calls (function_call items, no message)
static const char *response_tool_call_only =
    "{\"id\":\"resp_tool_only\",\"object\":\"response\",\"status\":\"completed\","
    "\"output\":[{\"id\":\"rs_001\",\"type\":\"reasoning\",\"summary\":[]},"
    "{\"id\":\"fc_001\",\"type\":\"function_call\",\"status\":\"completed\","
    "\"arguments\":\"{\\\"command\\\":\\\"ls -la\\\"}\",\"call_id\":\"call_001\",\"name\":\"Bash\"}]}";

// Response with text AND tool calls (both message and function_call)
static const char *response_text_and_tools =
    "{\"id\":\"resp_mixed\",\"object\":\"response\",\"status\":\"completed\","
    "\"output\":[{\"id\":\"msg_002\",\"type\":\"message\",\"status\":\"completed\","
    "\"content\":[{\"type\":\"output_text\",\"text\":\"Let me check the directory for you:\"}],"
    "\"role\":\"assistant\"},"
    "{\"id\":\"fc_002\",\"type\":\"function_call\",\"status\":\"completed\","
    "\"arguments\":\"{\\\"command\\\":\\\"ls -la\\\",\\\"timeout\\\":30}\",\"call_id\":\"call_002\",\"name\":\"Bash\"}]}";

// Response with multiple tool calls
static const char *response_multiple_tools =
    "{\"id\":\"resp_multi_tool\",\"object\":\"response\",\"status\":\"completed\","
    "\"output\":["
    "{\"id\":\"fc_003\",\"type\":\"function_call\",\"status\":\"completed\","
    "\"arguments\":\"{}\",\"call_id\":\"call_tool1\",\"name\":\"Read\"},"
    "{\"id\":\"fc_004\",\"type\":\"function_call\",\"status\":\"completed\","
    "\"arguments\":\"{\\\"pattern\\\":\\\"*.c\\\"}\",\"call_id\":\"call_tool2\",\"name\":\"Glob\"},"
    "{\"id\":\"fc_005\",\"type\":\"function_call\",\"status\":\"completed\","
    "\"arguments\":\"{\\\"pattern\\\":\\\"TODO\\\"}\",\"call_id\":\"call_tool3\",\"name\":\"Grep\"}]}";

// Response with null content in message
static const char *response_null_content =
    "{\"id\":\"resp_null_content\",\"object\":\"response\",\"status\":\"completed\","
    "\"output\":[{\"id\":\"msg_004\",\"type\":\"message\",\"status\":\"completed\","
    "\"content\":null,\"role\":\"assistant\"}]}";

// ============================================================================
// Parsing Functions (simulating the actual implementation)
// ============================================================================

/**
 * Parse a Responses API output array to extract tool calls
 * Returns number of tool calls found
 */
static int parse_tool_calls_from_output(cJSON *output, char ***ids, char ***names, char ***arguments) {
    if (!output || !cJSON_IsArray(output)) {
        return 0;
    }

    // First pass: count tool calls
    int tool_count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, output) {
        cJSON *type = cJSON_GetObjectItem(item, "type");
        if (!type || !cJSON_IsString(type)) continue;

        if (strcmp(type->valuestring, "function_call") == 0) {
            tool_count++;
        }
    }

    if (tool_count == 0) {
        return 0;
    }

    // Allocate arrays
    *ids = calloc((size_t)tool_count, sizeof(char*));
    *names = calloc((size_t)tool_count, sizeof(char*));
    *arguments = calloc((size_t)tool_count, sizeof(char*));

    if (!*ids || !*names || !*arguments) {
        free(*ids);
        free(*names);
        free(*arguments);
        *ids = *names = *arguments = NULL;
        return 0;
    }

    // Second pass: extract tool calls
    int idx = 0;
    cJSON_ArrayForEach(item, output) {
        cJSON *type = cJSON_GetObjectItem(item, "type");
        if (!type || !cJSON_IsString(type)) continue;

        if (strcmp(type->valuestring, "function_call") == 0) {
            cJSON *id = cJSON_GetObjectItem(item, "call_id");
            cJSON *name = cJSON_GetObjectItem(item, "name");
            cJSON *args = cJSON_GetObjectItem(item, "arguments");

            if (id && cJSON_IsString(id)) {
                (*ids)[idx] = strdup(id->valuestring);
            }
            if (name && cJSON_IsString(name)) {
                (*names)[idx] = strdup(name->valuestring);
            }
            if (args && cJSON_IsString(args)) {
                (*arguments)[idx] = strdup(args->valuestring);
            }
            idx++;
        }
    }

    return tool_count;
}

/**
 * Extract text content from a Responses API message item
 * Returns allocated string (caller must free), or NULL if no text
 */
static char* extract_text_from_message(cJSON *message_item) {
    if (!message_item) return NULL;

    cJSON *content = cJSON_GetObjectItem(message_item, "content");
    if (!content || !cJSON_IsArray(content)) return NULL;

    char *combined_text = NULL;
    size_t text_capacity = 0;
    size_t text_length = 0;

    cJSON *content_item = NULL;
    cJSON_ArrayForEach(content_item, content) {
        cJSON *content_type = cJSON_GetObjectItem(content_item, "type");
        if (!content_type || !cJSON_IsString(content_type)) continue;

        if (strcmp(content_type->valuestring, "output_text") == 0) {
            cJSON *text = cJSON_GetObjectItem(content_item, "text");
            if (text && cJSON_IsString(text) && text->valuestring) {
                size_t text_len = strlen(text->valuestring);
                size_t needed = text_length + text_len + 1;

                if (needed > text_capacity) {
                    size_t new_cap = text_capacity ? text_capacity * 2 : 1024;
                    if (new_cap < needed) new_cap = needed;
                    char *new_buf = realloc(combined_text, new_cap);
                    if (!new_buf) {
                        free(combined_text);
                        return NULL;
                    }
                    combined_text = new_buf;
                    text_capacity = new_cap;
                }

                if (text_length == 0) {
                    memcpy(combined_text, text->valuestring, text_len + 1);
                } else {
                    memcpy(combined_text + text_length, text->valuestring, text_len + 1);
                }
                text_length += text_len;
            }
        }
    }

    return combined_text;
}

/**
 * Full parsing of Responses API response - returns text and tool count
 */
typedef struct {
    char *text;
    char **tool_ids;
    char **tool_names;
    char **tool_arguments;
    int tool_count;
} ParsedResponse;

static void free_parsed_response(ParsedResponse *resp) {
    if (!resp) return;
    free(resp->text);
    if (resp->tool_ids) {
        for (int i = 0; i < resp->tool_count; i++) {
            free(resp->tool_ids[i]);
            free(resp->tool_names[i]);
            free(resp->tool_arguments[i]);
        }
        free(resp->tool_ids);
        free(resp->tool_names);
        free(resp->tool_arguments);
    }
}

static ParsedResponse parse_responses_response(const char *json_str) {
    ParsedResponse resp = {0};
    if (!json_str) return resp;

    cJSON *json = cJSON_Parse(json_str);
    if (!json) return resp;

    cJSON *output = cJSON_GetObjectItem(json, "output");
    if (!output || !cJSON_IsArray(output)) {
        cJSON_Delete(json);
        return resp;
    }

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, output) {
        cJSON *type = cJSON_GetObjectItem(item, "type");
        if (!type || !cJSON_IsString(type)) continue;

        // Extract text from message items
        if (strcmp(type->valuestring, "message") == 0 && !resp.text) {
            resp.text = extract_text_from_message(item);
        }
    }

    // Extract tool calls (function_call items)
    resp.tool_count = parse_tool_calls_from_output(output,
        &resp.tool_ids, &resp.tool_names, &resp.tool_arguments);

    cJSON_Delete(json);
    return resp;
}

// ============================================================================
// Test Cases
// ============================================================================

static void test_text_only_response(void) {
    printf(COLOR_YELLOW "\nTest: Text-only response parsing\n" COLOR_RESET);

    ParsedResponse resp = parse_responses_response(response_text_only);

    TEST_ASSERT(resp.text != NULL, "Should extract text from message item");
    TEST_ASSERT(strstr(resp.text, "Hello!") != NULL, "Text should contain greeting");
    TEST_ASSERT(resp.tool_count == 0, "Should have no tool calls");
    TEST_ASSERT(resp.tool_ids == NULL, "Tool IDs should be NULL");

    free_parsed_response(&resp);
}

static void test_tool_call_only_response(void) {
    printf(COLOR_YELLOW "\nTest: Tool call-only response parsing\n" COLOR_RESET);

    ParsedResponse resp = parse_responses_response(response_tool_call_only);

    TEST_ASSERT(resp.text == NULL, "Should have no text content");
    TEST_ASSERT(resp.tool_count == 1, "Should have exactly 1 tool call");
    TEST_ASSERT(resp.tool_ids != NULL, "Tool IDs should not be NULL");
    TEST_ASSERT(resp.tool_names != NULL, "Tool names should not be NULL");
    TEST_ASSERT(strcmp(resp.tool_names[0], "Bash") == 0, "Tool name should be 'Bash'");
    TEST_ASSERT(strstr(resp.tool_arguments[0], "ls -la") != NULL, "Tool arguments should contain 'ls -la'");
    TEST_ASSERT(resp.tool_ids[0] != NULL, "Tool should have call_id");

    free_parsed_response(&resp);
}

static void test_text_and_tools_response(void) {
    printf(COLOR_YELLOW "\nTest: Mixed text and tool call response parsing\n" COLOR_RESET);

    ParsedResponse resp = parse_responses_response(response_text_and_tools);

    TEST_ASSERT(resp.text != NULL, "Should extract text from message item");
    TEST_ASSERT(strstr(resp.text, "directory") != NULL, "Text should mention directory");
    TEST_ASSERT(resp.tool_count == 1, "Should have exactly 1 tool call");
    TEST_ASSERT(strcmp(resp.tool_names[0], "Bash") == 0, "Tool name should be 'Bash'");
    TEST_ASSERT(strstr(resp.tool_arguments[0], "timeout") != NULL, "Tool arguments should include timeout");

    free_parsed_response(&resp);
}

static void test_multiple_tool_calls(void) {
    printf(COLOR_YELLOW "\nTest: Multiple tool calls response parsing\n" COLOR_RESET);

    ParsedResponse resp = parse_responses_response(response_multiple_tools);

    TEST_ASSERT(resp.text == NULL, "Should have no text content");
    TEST_ASSERT(resp.tool_count == 3, "Should have exactly 3 tool calls");
    TEST_ASSERT(strcmp(resp.tool_names[0], "Read") == 0, "First tool should be 'Read'");
    TEST_ASSERT(strcmp(resp.tool_names[1], "Glob") == 0, "Second tool should be 'Glob'");
    TEST_ASSERT(strcmp(resp.tool_names[2], "Grep") == 0, "Third tool should be 'Grep'");

    free_parsed_response(&resp);
}

static void test_null_content_handling(void) {
    printf(COLOR_YELLOW "\nTest: Null content in message handling\n" COLOR_RESET);

    ParsedResponse resp = parse_responses_response(response_null_content);

    TEST_ASSERT(resp.text == NULL, "Should have no text when content is null");
    TEST_ASSERT(resp.tool_count == 0, "Should have no tool calls");

    free_parsed_response(&resp);
}

static void test_parsing_from_file(void) {
    printf(COLOR_YELLOW "\nTest: Parsing actual test_data/response.json\n" COLOR_RESET);

    FILE *f = fopen("test_data/response.json", "r");
    if (!f) {
        printf(COLOR_RED "  ✗ Could not open test_data/response.json\n" COLOR_RESET);
        tests_run++;
        tests_failed++;
        return;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *json_str = malloc((size_t)fsize + 1);
    fread(json_str, 1, (size_t)fsize, f);
    json_str[fsize] = '\0';
    fclose(f);

    ParsedResponse resp = parse_responses_response(json_str);

    TEST_ASSERT(resp.text != NULL, "Should extract text from file");
    TEST_ASSERT(strstr(resp.text, "Hey!") != NULL, "Text should contain greeting");
    TEST_ASSERT(resp.tool_count == 0, "This specific response has no tool calls");

    free(json_str);
    free_parsed_response(&resp);
}

static void test_parsing_from_db_response(void) {
    printf(COLOR_YELLOW "\nTest: Parsing response with function_call (simulates DB response)\n" COLOR_RESET);

    // This mimics the response we got from the database that had function_call
    const char *db_response =
        "{\"id\":\"resp_from_db\",\"object\":\"response\",\"status\":\"completed\","
        "\"output\":[{\"id\":\"rs_reasoning\",\"type\":\"reasoning\",\"summary\":[]},"
        "{\"id\":\"fc_db\",\"type\":\"function_call\",\"status\":\"completed\","
        "\"arguments\":\"{\\\"command\\\":\\\"git status --short --branch\\\",\\\"timeout\\\":120}\","
        "\"call_id\":\"call_git\",\"name\":\"Bash\"}]}";

    ParsedResponse resp = parse_responses_response(db_response);

    TEST_ASSERT(resp.text == NULL, "Should have no text content");
    TEST_ASSERT(resp.tool_count == 1, "Should have exactly 1 tool call");
    TEST_ASSERT(strcmp(resp.tool_names[0], "Bash") == 0, "Tool name should be 'Bash'");
    TEST_ASSERT(strstr(resp.tool_arguments[0], "git status") != NULL,
                "Tool arguments should contain git command");
    TEST_ASSERT(strstr(resp.tool_arguments[0], "timeout") != NULL,
                "Tool arguments should include timeout");

    free_parsed_response(&resp);
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(void) {
    printf("=== OpenAI Responses API Parsing Tests ===\n");
    printf("Testing /v1/responses endpoint format parsing\n");

    test_text_only_response();
    test_tool_call_only_response();
    test_text_and_tools_response();
    test_multiple_tool_calls();
    test_null_content_handling();
    test_parsing_from_file();
    test_parsing_from_db_response();

    TEST_SUMMARY();
}
