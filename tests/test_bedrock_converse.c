/*
 * test_bedrock_converse.c - Unit tests for Bedrock Converse API format conversion
 *
 * Tests the first message validation logic that ensures conversations start
 * with a user message (required by AWS Bedrock Converse API).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>

// Include the bedrock_converse source to test internal functions
// We need to declare the function we're testing
extern char* bedrock_converse_convert_request(const char *openai_request);

// Simple test framework
static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT_TRUE(cond, msg) do { \
    tests_run++; \
    if (cond) { tests_passed++; printf("[PASS] %s\n", msg); } \
    else { printf("[FAIL] %s\n", msg); } \
} while(0)

#define ASSERT_FALSE(cond, msg) ASSERT_TRUE(!(cond), msg)

#define ASSERT_STR_EQ(a, b, msg) do { \
    tests_run++; \
    if (strcmp((a), (b)) == 0) { tests_passed++; printf("[PASS] %s\n", msg); } \
    else { printf("[FAIL] %s: expected '%s', got '%s'\n", msg, (b), (a)); } \
} while(0)

/* Helper: Parse JSON and return the first message's role (caller must free) */
static char* get_first_message_role(const char *json) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return NULL;

    cJSON *messages = cJSON_GetObjectItem(root, "messages");
    if (!messages || !cJSON_IsArray(messages)) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON *first_msg = cJSON_GetArrayItem(messages, 0);
    if (!first_msg) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON *role = cJSON_GetObjectItem(first_msg, "role");
    if (!role || !cJSON_IsString(role)) {
        cJSON_Delete(root);
        return NULL;
    }

    // Need to duplicate since we'll delete root
    char *result = strdup(role->valuestring);
    cJSON_Delete(root);
    return result;
}

/* Helper: Count messages in converted request */
static int count_messages(const char *json) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;

    cJSON *messages = cJSON_GetObjectItem(root, "messages");
    if (!messages || !cJSON_IsArray(messages)) {
        cJSON_Delete(root);
        return -1;
    }

    int count = cJSON_GetArraySize(messages);
    cJSON_Delete(root);
    return count;
}

/* Helper: Check if first message has content */
static int first_message_has_content(const char *json) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return 0;

    cJSON *messages = cJSON_GetObjectItem(root, "messages");
    if (!messages || !cJSON_IsArray(messages)) {
        cJSON_Delete(root);
        return 0;
    }

    cJSON *first_msg = cJSON_GetArrayItem(messages, 0);
    if (!first_msg) {
        cJSON_Delete(root);
        return 0;
    }

    cJSON *content = cJSON_GetObjectItem(first_msg, "content");
    int has_content = (content && cJSON_IsArray(content) && cJSON_GetArraySize(content) > 0);
    cJSON_Delete(root);
    return has_content;
}

static void test_normal_conversation_user_first(void) {
    printf("\nTest: Normal conversation with user message first\n");

    const char *openai_request = "{\n"
        "  \"model\": \"claude-sonnet-4-5\",\n"
        "  \"messages\": [\n"
        "    {\"role\": \"user\", \"content\": \"Hello\"},\n"
        "    {\"role\": \"assistant\", \"content\": \"Hi there!\"},\n"
        "    {\"role\": \"user\", \"content\": \"How are you?\"}\n"
        "  ]\n"
        "}";

    char *result = bedrock_converse_convert_request(openai_request);
    ASSERT_TRUE(result != NULL, "Conversion succeeded");

    if (result) {
        char *first_role = get_first_message_role(result);
        ASSERT_TRUE(first_role != NULL, "First message has a role");
        if (first_role) {
            ASSERT_STR_EQ(first_role, "user", "First message is user");
            free(first_role);
        }

        int msg_count = count_messages(result);
        ASSERT_TRUE(msg_count == 3, "Message count preserved (no placeholder added)");

        free(result);
    }
}

static void test_assistant_first_adds_placeholder(void) {
    printf("\nTest: Assistant message first triggers placeholder insertion\n");

    const char *openai_request = "{\n"
        "  \"model\": \"claude-sonnet-4-5\",\n"
        "  \"messages\": [\n"
        "    {\"role\": \"assistant\", \"content\": \"I will help you\"},\n"
        "    {\"role\": \"user\", \"content\": \"Thanks\"}\n"
        "  ]\n"
        "}";

    char *result = bedrock_converse_convert_request(openai_request);
    ASSERT_TRUE(result != NULL, "Conversion succeeded");

    if (result) {
        char *first_role = get_first_message_role(result);
        ASSERT_TRUE(first_role != NULL, "First message has a role");
        if (first_role) {
            ASSERT_STR_EQ(first_role, "user", "First message is now user (placeholder)");
            free(first_role);
        }

        int msg_count = count_messages(result);
        ASSERT_TRUE(msg_count == 3, "Placeholder added (3 messages total)");

        ASSERT_TRUE(first_message_has_content(result), "Placeholder has content");

        free(result);
    }
}

static void test_tool_result_first_adds_placeholder(void) {
    printf("\nTest: Tool result message first triggers placeholder insertion\n");

    const char *openai_request = "{\n"
        "  \"model\": \"claude-sonnet-4-5\",\n"
        "  \"messages\": [\n"
        "    {\"role\": \"tool\", \"tool_call_id\": \"call_123\", \"content\": \"tool output\"},\n"
        "    {\"role\": \"user\", \"content\": \"What did the tool say?\"}\n"
        "  ]\n"
        "}";

    char *result = bedrock_converse_convert_request(openai_request);
    ASSERT_TRUE(result != NULL, "Conversion succeeded");

    if (result) {
        char *first_role = get_first_message_role(result);
        ASSERT_TRUE(first_role != NULL, "First message has a role");
        if (first_role) {
            ASSERT_STR_EQ(first_role, "user", "First message is now user (placeholder)");
            free(first_role);
        }

        free(result);
    }
}

static void test_empty_messages_adds_placeholder(void) {
    printf("\nTest: Empty messages array triggers placeholder insertion\n");

    const char *openai_request = "{\n"
        "  \"model\": \"claude-sonnet-4-5\",\n"
        "  \"messages\": []\n"
        "}";

    char *result = bedrock_converse_convert_request(openai_request);
    ASSERT_TRUE(result != NULL, "Conversion succeeded");

    if (result) {
        int msg_count = count_messages(result);
        ASSERT_TRUE(msg_count == 1, "Placeholder user message added to empty conversation");

        char *first_role = get_first_message_role(result);
        if (first_role) {
            ASSERT_STR_EQ(first_role, "user", "Single message is user placeholder");
            free(first_role);
        }

        free(result);
    }
}

static void test_system_messages_extracted_and_user_first(void) {
    printf("\nTest: System messages extracted, user message becomes first\n");

    const char *openai_request = "{\n"
        "  \"model\": \"claude-sonnet-4-5\",\n"
        "  \"messages\": [\n"
        "    {\"role\": \"system\", \"content\": \"You are helpful\"},\n"
        "    {\"role\": \"user\", \"content\": \"Hello\"}\n"
        "  ]\n"
        "}";

    char *result = bedrock_converse_convert_request(openai_request);
    ASSERT_TRUE(result != NULL, "Conversion succeeded");

    if (result) {
        // Parse result to check system was extracted
        cJSON *root = cJSON_Parse(result);
        ASSERT_TRUE(root != NULL, "Result is valid JSON");

        if (root) {
            cJSON *system = cJSON_GetObjectItem(root, "system");
            ASSERT_TRUE(system != NULL, "System field exists (extracted from messages)");

            char *first_role = get_first_message_role(result);
            if (first_role) {
                ASSERT_STR_EQ(first_role, "user", "First message is user after system extraction");
                free(first_role);
            }

            cJSON_Delete(root);
        }

        free(result);
    }
}

static void test_no_messages_field_adds_placeholder(void) {
    printf("\nTest: Missing messages field triggers placeholder insertion\n");

    const char *openai_request = "{\n"
        "  \"model\": \"claude-sonnet-4-5\"\n"
        "}";

    char *result = bedrock_converse_convert_request(openai_request);
    ASSERT_TRUE(result != NULL, "Conversion succeeded");

    if (result) {
        int msg_count = count_messages(result);
        ASSERT_TRUE(msg_count == 1, "Placeholder user message added when messages missing");

        free(result);
    }
}

static void test_complex_conversation_with_tools(void) {
    printf("\nTest: Complex conversation with tools ensures user first\n");

    const char *openai_request = "{\n"
        "  \"model\": \"claude-sonnet-4-5\",\n"
        "  \"messages\": [\n"
        "    {\"role\": \"system\", \"content\": \"You are helpful\"},\n"
        "    {\"role\": \"user\", \"content\": \"Use the tool\"},\n"
        "    {\"role\": \"assistant\", \"content\": \"I'll help\", \"tool_calls\": [{\"id\": \"call_1\", \"type\": \"function\", \"function\": {\"name\": \"test\", \"arguments\": \"{}\"}}]},\n"
        "    {\"role\": \"tool\", \"tool_call_id\": \"call_1\", \"content\": \"result\"},\n"
        "    {\"role\": \"assistant\", \"content\": \"Done!\"}\n"
        "  ],\n"
        "  \"tools\": [{\"type\": \"function\", \"function\": {\"name\": \"test\", \"description\": \"A test tool\"}}]\n"
        "}";

    char *result = bedrock_converse_convert_request(openai_request);
    ASSERT_TRUE(result != NULL, "Conversion succeeded");

    if (result) {
        char *first_role = get_first_message_role(result);
        if (first_role) {
            ASSERT_STR_EQ(first_role, "user", "First non-system message is user");
            free(first_role);
        }

        // Verify we have the expected number of messages (system extracted, 4 conversation messages)
        int msg_count = count_messages(result);
        ASSERT_TRUE(msg_count == 4, "Correct message count after system extraction");

        free(result);
    }
}

int main(void) {
    printf("=== Bedrock Converse Format Tests ===\n");

    test_normal_conversation_user_first();
    test_assistant_first_adds_placeholder();
    test_tool_result_first_adds_placeholder();
    test_empty_messages_adds_placeholder();
    test_system_messages_extracted_and_user_first();
    test_no_messages_field_adds_placeholder();
    test_complex_conversation_with_tools();

    printf("\n=== Results ===\n");
    printf("Tests run: %d, passed: %d, failed: %d\n",
           tests_run, tests_passed, tests_run - tests_passed);

    return (tests_run == tests_passed) ? 0 : 1;
}
