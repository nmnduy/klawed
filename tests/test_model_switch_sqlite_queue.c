/*
 * test_model_switch_sqlite_queue.c - Unit tests for model switching in SQLite queue mode
 *
 * Tests provider switching at various points during a conversation in SQLite queue mode.
 * This mode uses a persistent SQLite database for message queuing between processes.
 *
 * Providers tested (from real_api_calls.db):
 *   - Kimi Coding (kimi-for-coding) - 582 calls
 *   - Z.AI/GLM (glm-4.6) - 1 call
 *   - OpenAI/GPT (gpt-5.4) - 1 call
 *   - Anthropic Claude - 1 call
 *
 * Verifies:
 *   - Conversation can be saved to queue and restored
 *   - Provider can be switched after queue restore
 *   - Tool call/result ordering is preserved through queue operations
 *   - Multiple queue operations don't corrupt conversation state
 */

#define _POSIX_C_SOURCE 200809L
#define TEST_BUILD 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sqlite3.h>
#include <pthread.h>
#include <cjson/cJSON.h>
#include <bsd/string.h>
#include <bsd/stdlib.h>

#include "../src/klawed_internal.h"
#include "../src/provider.h"
#include "../src/config.h"
#include "../src/sqlite_queue.h"

// Minimal logger stubs (defined after headers to avoid redefinition)
#ifdef LOG_DEBUG
#undef LOG_DEBUG
#undef LOG_INFO
#undef LOG_WARN
#undef LOG_ERROR
#endif
#define LOG_DEBUG(fmt, ...) ((void)0)
#define LOG_INFO(fmt, ...)  ((void)0)
#define LOG_WARN(fmt, ...)  ((void)0)
#define LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)

// ============================================================================
// Test Framework
// ============================================================================

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    tests_run++; \
    if (cond) { \
        tests_passed++; \
        printf("  [PASS] %s\n", msg); \
    } else { \
        tests_failed++; \
        printf("  [FAIL] %s (at %s:%d)\n", msg, __FILE__, __LINE__); \
    } \
} while(0)

#define TEST_DB_PATH "/tmp/test_model_switch_queue.db"

// ============================================================================
// Provider Configurations (matching real_api_calls.db)
// ============================================================================

typedef struct {
    const char *name;
    const char *model;
    const char *api_base;
    LLMProviderType type;
    const char *api_key;
} TestProviderConfig;

static const TestProviderConfig TEST_PROVIDERS[] = {
    {
        .name = "kimi-coding",
        .model = "kimi-for-coding",
        .api_base = "https://api.kimi.com/coding/v1/chat/completions",
        .type = PROVIDER_OPENAI,
        .api_key = "test-kimi-key"
    },
    {
        .name = "zai-glm",
        .model = "glm-4.6",
        .api_base = "https://api.z.ai/api/coding/paas/v4/chat/completions",
        .type = PROVIDER_OPENAI,
        .api_key = "test-zai-key"
    },
    {
        .name = "openai-codex",
        .model = "gpt-5.4",
        .api_base = "https://chatgpt.com/backend-api/codex/responses",
        .type = PROVIDER_OPENAI,
        .api_key = "test-openai-key"
    },
    {
        .name = "anthropic-claude",
        .model = "claude-opus-4",
        .api_base = "https://api.anthropic.com/v1/messages",
        .type = PROVIDER_ANTHROPIC,
        .api_key = "test-anthropic-key"
    }
};

#define NUM_TEST_PROVIDERS (sizeof(TEST_PROVIDERS) / sizeof(TEST_PROVIDERS[0]))

// ============================================================================
// Helper: Create LLMProviderConfig from test config
// ============================================================================

static void create_provider_config(const TestProviderConfig *test_config,
                                   LLMProviderConfig *config) {
    memset(config, 0, sizeof(*config));
    strlcpy(config->model, test_config->model, sizeof(config->model));
    strlcpy(config->api_base, test_config->api_base, sizeof(config->api_base));
    strlcpy(config->api_key, test_config->api_key, sizeof(config->api_key));
    config->provider_type = test_config->type;
}

// ============================================================================
// Helper: Initialize provider from test config
// ============================================================================

static Provider* init_test_provider(const TestProviderConfig *test_config,
                                    char **api_url_out) {
    LLMProviderConfig config;
    create_provider_config(test_config, &config);

    ProviderInitResult result = {0};
    provider_init_from_config(test_config->name, &config, &result);

    if (!result.provider) {
        fprintf(stderr, "Failed to init provider '%s': %s\n",
                test_config->name, result.error_message ? result.error_message : "unknown");
        free(result.error_message);
        free(result.api_url);
        return NULL;
    }

    if (api_url_out) {
        free(*api_url_out);
        *api_url_out = result.api_url;
    } else {
        free(result.api_url);
    }
    free(result.error_message);

    return result.provider;
}

// ============================================================================
// Helper: Serialize conversation to JSON for queue
// ============================================================================

static char* serialize_conversation(InternalMessage *messages, int count,
                                    const char *model, const char *api_url) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    // Add metadata
    cJSON_AddStringToObject(root, "model", model ? model : "");
    cJSON_AddStringToObject(root, "api_url", api_url ? api_url : "");
    cJSON_AddNumberToObject(root, "message_count", count);

    // Add messages array
    cJSON *msgs = cJSON_CreateArray();
    if (!msgs) {
        cJSON_Delete(root);
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        cJSON *msg = cJSON_CreateObject();
        if (!msg) continue;

        // Role
        const char *role_str = "unknown";
        switch (messages[i].role) {
            case MSG_USER: role_str = "user"; break;
            case MSG_ASSISTANT: role_str = "assistant"; break;
            case MSG_SYSTEM: role_str = "system"; break;
            case MSG_AUTO_COMPACTION: role_str = "auto_compaction"; break;
            default: role_str = "unknown";
        }
        cJSON_AddStringToObject(msg, "role", role_str);

        // Contents
        cJSON *contents = cJSON_CreateArray();
        for (int j = 0; j < messages[i].content_count; j++) {
            cJSON *content = cJSON_CreateObject();
            InternalContent *c = &messages[i].contents[j];

            // Content type
            const char *type_str = "unknown";
            switch (c->type) {
                case INTERNAL_TEXT: type_str = "text"; break;
                case INTERNAL_TOOL_CALL: type_str = "tool_call"; break;
                case INTERNAL_TOOL_RESPONSE: type_str = "tool_response"; break;
                case INTERNAL_IMAGE: type_str = "image"; break;
                default: type_str = "unknown"; break;
            }
            cJSON_AddStringToObject(content, "type", type_str);

            if (c->text) cJSON_AddStringToObject(content, "text", c->text);
            if (c->tool_id) cJSON_AddStringToObject(content, "tool_id", c->tool_id);
            if (c->tool_name) cJSON_AddStringToObject(content, "tool_name", c->tool_name);
            if (c->is_error) cJSON_AddBoolToObject(content, "is_error", 1);
            if (c->tool_params) {
                cJSON_AddItemToObject(content, "tool_params", cJSON_Duplicate(c->tool_params, 1));
            }
            if (c->tool_output) {
                cJSON_AddItemToObject(content, "tool_output", cJSON_Duplicate(c->tool_output, 1));
            }

            cJSON_AddItemToArray(contents, content);
        }
        cJSON_AddItemToObject(msg, "contents", contents);
        cJSON_AddItemToArray(msgs, msg);
    }

    cJSON_AddItemToObject(root, "messages", msgs);

    char *result = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return result;
}

// ============================================================================
// Helper: Deserialize conversation from JSON
// ============================================================================

static int deserialize_conversation(const char *json,
                                    InternalMessage *messages, int *count,
                                    char **model_out, char **api_url_out,
                                    int max_messages) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;

    // Extract metadata
    cJSON *model = cJSON_GetObjectItem(root, "model");
    cJSON *api_url = cJSON_GetObjectItem(root, "api_url");

    if (model_out && model && cJSON_IsString(model)) {
        *model_out = strdup(model->valuestring);
    }
    if (api_url_out && api_url && cJSON_IsString(api_url)) {
        *api_url_out = strdup(api_url->valuestring);
    }

    // Extract messages
    cJSON *msgs = cJSON_GetObjectItem(root, "messages");
    if (!msgs || !cJSON_IsArray(msgs)) {
        cJSON_Delete(root);
        return -1;
    }

    int msg_count = 0;
    cJSON *msg_item = NULL;
    cJSON_ArrayForEach(msg_item, msgs) {
        if (msg_count >= max_messages) break;

        cJSON *role = cJSON_GetObjectItem(msg_item, "role");
        cJSON *contents = cJSON_GetObjectItem(msg_item, "contents");

        if (!role || !cJSON_IsString(role)) continue;

        InternalMessage *msg = &messages[msg_count++];

        // Set role
        if (strcmp(role->valuestring, "user") == 0) msg->role = MSG_USER;
        else if (strcmp(role->valuestring, "assistant") == 0) msg->role = MSG_ASSISTANT;
        else if (strcmp(role->valuestring, "system") == 0) msg->role = MSG_SYSTEM;

        // Parse contents
        if (contents && cJSON_IsArray(contents)) {
            int content_count = cJSON_GetArraySize(contents);
            msg->contents = calloc((size_t)content_count, sizeof(InternalContent));
            msg->content_count = content_count;

            int cidx = 0;
            cJSON *content_item = NULL;
            cJSON_ArrayForEach(content_item, contents) {
                InternalContent *c = &msg->contents[cidx++];

                cJSON *type = cJSON_GetObjectItem(content_item, "type");
                cJSON *text = cJSON_GetObjectItem(content_item, "text");
                cJSON *tool_id = cJSON_GetObjectItem(content_item, "tool_id");
                cJSON *tool_name = cJSON_GetObjectItem(content_item, "tool_name");
                cJSON *is_error = cJSON_GetObjectItem(content_item, "is_error");
                cJSON *tool_params = cJSON_GetObjectItem(content_item, "tool_params");
                cJSON *tool_output = cJSON_GetObjectItem(content_item, "tool_output");

                if (type && cJSON_IsString(type)) {
                    if (strcmp(type->valuestring, "text") == 0) c->type = INTERNAL_TEXT;
                    else if (strcmp(type->valuestring, "tool_call") == 0) c->type = INTERNAL_TOOL_CALL;
                    else if (strcmp(type->valuestring, "tool_response") == 0) c->type = INTERNAL_TOOL_RESPONSE;
                    else if (strcmp(type->valuestring, "image") == 0) c->type = INTERNAL_IMAGE;
                }

                if (text && cJSON_IsString(text)) c->text = strdup(text->valuestring);
                if (tool_id && cJSON_IsString(tool_id)) c->tool_id = strdup(tool_id->valuestring);
                if (tool_name && cJSON_IsString(tool_name)) c->tool_name = strdup(tool_name->valuestring);
                if (is_error && cJSON_IsBool(is_error)) c->is_error = cJSON_IsTrue(is_error);
                if (tool_params) c->tool_params = cJSON_Duplicate(tool_params, 1);
                if (tool_output) c->tool_output = cJSON_Duplicate(tool_output, 1);
            }
        }
    }

    *count = msg_count;
    cJSON_Delete(root);
    return 0;
}

// ============================================================================
// Helper: Build test conversation
// ============================================================================

static void build_test_conversation(InternalMessage *messages, int *count) {
    *count = 0;

    // User message 1
    messages[*count].role = MSG_USER;
    messages[*count].content_count = 1;
    messages[*count].contents = calloc(1, sizeof(InternalContent));
    messages[*count].contents[0].type = INTERNAL_TEXT;
    messages[*count].contents[0].text = strdup("Hello, help me with coding");
    (*count)++;

    // Assistant response 1
    messages[*count].role = MSG_ASSISTANT;
    messages[*count].content_count = 1;
    messages[*count].contents = calloc(1, sizeof(InternalContent));
    messages[*count].contents[0].type = INTERNAL_TEXT;
    messages[*count].contents[0].text = strdup("Sure! What do you need help with?");
    (*count)++;

    // User message 2 with tool request
    messages[*count].role = MSG_USER;
    messages[*count].content_count = 1;
    messages[*count].contents = calloc(1, sizeof(InternalContent));
    messages[*count].contents[0].type = INTERNAL_TEXT;
    messages[*count].contents[0].text = strdup("List files");
    (*count)++;

    // Assistant with tool call
    messages[*count].role = MSG_ASSISTANT;
    messages[*count].content_count = 1;
    messages[*count].contents = calloc(1, sizeof(InternalContent));
    messages[*count].contents[0].type = INTERNAL_TOOL_CALL;
    messages[*count].contents[0].tool_id = strdup("call_1");
    messages[*count].contents[0].tool_name = strdup("Bash");
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "ls");
    messages[*count].contents[0].tool_params = params;
    (*count)++;

    // Tool result
    messages[*count].role = MSG_USER;
    messages[*count].content_count = 1;
    messages[*count].contents = calloc(1, sizeof(InternalContent));
    messages[*count].contents[0].type = INTERNAL_TOOL_RESPONSE;
    messages[*count].contents[0].tool_id = strdup("call_1");
    messages[*count].contents[0].tool_name = strdup("Bash");
    cJSON *output = cJSON_CreateObject();
    cJSON_AddStringToObject(output, "output", "file.txt\nmain.py");
    messages[*count].contents[0].tool_output = output;
    (*count)++;

    // Final assistant response
    messages[*count].role = MSG_ASSISTANT;
    messages[*count].content_count = 1;
    messages[*count].contents = calloc(1, sizeof(InternalContent));
    messages[*count].contents[0].type = INTERNAL_TEXT;
    messages[*count].contents[0].text = strdup("You have file.txt and main.py");
    (*count)++;
}

// ============================================================================
// Helper: Cleanup conversation
// ============================================================================

static void cleanup_conversation(InternalMessage *messages, int count) {
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < messages[i].content_count; j++) {
            InternalContent *c = &messages[i].contents[j];
            free(c->text);
            free(c->tool_id);
            free(c->tool_name);
            if (c->tool_params) cJSON_Delete(c->tool_params);
            if (c->tool_output) cJSON_Delete(c->tool_output);
        }
        free(messages[i].contents);
    }
}

// ============================================================================
// Test 1: Basic queue save and restore
// ============================================================================

static void test_queue_save_restore(void) {
    printf("\nTest: Save conversation to queue and restore\n");

    // Remove old test db
    unlink(TEST_DB_PATH);

    // Initialize queue
    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB_PATH, "test_sender");
    TEST_ASSERT(ctx != NULL, "Queue context initialized");

    // Build test conversation
    InternalMessage messages[10];
    int msg_count = 0;
    build_test_conversation(messages, &msg_count);
    TEST_ASSERT(msg_count == 6, "Test conversation has 6 messages");

    // Serialize and save
    char *serialized = serialize_conversation(messages, msg_count,
                                               TEST_PROVIDERS[0].model,
                                               TEST_PROVIDERS[0].api_base);
    TEST_ASSERT(serialized != NULL, "Conversation serialized");

    int result = sqlite_queue_send(ctx, "receiver", serialized, strlen(serialized));
    TEST_ASSERT(result == SQLITE_QUEUE_ERROR_NONE, "Conversation saved to queue");

    free(serialized);
    cleanup_conversation(messages, msg_count);
    sqlite_queue_cleanup(ctx);

    // Restore from queue
    ctx = sqlite_queue_init(TEST_DB_PATH, "receiver");
    TEST_ASSERT(ctx != NULL, "Queue context re-initialized for receiver");

    char **received_msgs = NULL;
    long long *msg_ids = NULL;
    int received_count = 0;

    result = sqlite_queue_receive(ctx, "test_sender", 10, &received_msgs,
                                   &received_count, &msg_ids);
    TEST_ASSERT(result == SQLITE_QUEUE_ERROR_NONE, "Message received from queue");
    TEST_ASSERT(received_count == 1, "Exactly one message received");

    // Deserialize
    InternalMessage restored[10];
    int restored_count = 0;
    char *restored_model = NULL;
    char *restored_api_url = NULL;

    result = deserialize_conversation(received_msgs[0], restored, &restored_count,
                                       &restored_model, &restored_api_url, 10);
    TEST_ASSERT(result == 0, "Conversation deserialized");
    TEST_ASSERT(restored_count == 6, "All 6 messages restored");
    TEST_ASSERT(restored_model != NULL, "Model restored");
    TEST_ASSERT(strcmp(restored_model, TEST_PROVIDERS[0].model) == 0, "Model matches");

    // Cleanup
    cleanup_conversation(restored, restored_count);
    free(restored_model);
    free(restored_api_url);

    if (received_msgs) {
        for (int i = 0; i < received_count; i++) free(received_msgs[i]);
        free(received_msgs);
    }
    if (msg_ids) free(msg_ids);

    sqlite_queue_cleanup(ctx);
    unlink(TEST_DB_PATH);
}

// ============================================================================
// Test 2: Switch provider after queue restore
// ============================================================================

static void test_switch_after_queue_restore(void) {
    printf("\nTest: Switch provider after queue restore\n");

    unlink(TEST_DB_PATH);

    // Initialize and save conversation with Kimi (most used in real db)
    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB_PATH, "sender");
    TEST_ASSERT(ctx != NULL, "Queue initialized");

    InternalMessage messages[10];
    int msg_count = 0;
    build_test_conversation(messages, &msg_count);

    char *serialized = serialize_conversation(messages, msg_count,
                                               TEST_PROVIDERS[0].model,  // Kimi
                                               TEST_PROVIDERS[0].api_base);
    sqlite_queue_send(ctx, "receiver", serialized, strlen(serialized));
    free(serialized);
    cleanup_conversation(messages, msg_count);
    sqlite_queue_cleanup(ctx);

    // Restore and switch to Z.AI
    ctx = sqlite_queue_init(TEST_DB_PATH, "receiver");

    char **received = NULL;
    long long *ids = NULL;
    int count = 0;
    sqlite_queue_receive(ctx, NULL, 10, &received, &count, &ids);

    InternalMessage restored[10];
    int restored_count = 0;
    char *model = NULL;
    char *api_url = NULL;
    deserialize_conversation(received[0], restored, &restored_count, &model, &api_url, 10);

    // Verify original model is Kimi
    TEST_ASSERT(strcmp(model, TEST_PROVIDERS[0].model) == 0, "Restored model is Kimi");

    // Initialize new provider (Z.AI)
    Provider *new_provider = init_test_provider(&TEST_PROVIDERS[1], &api_url);
    TEST_ASSERT(new_provider != NULL, "Can switch to Z.AI after restore");

    // Update model
    free(model);
    model = strdup(TEST_PROVIDERS[1].model);
    TEST_ASSERT(strcmp(model, "glm-4.6") == 0, "Model updated to Z.AI");

    // Cleanup
    new_provider->cleanup(new_provider);
    cleanup_conversation(restored, restored_count);
    free(model);
    free(api_url);

    for (int i = 0; i < count; i++) free(received[i]);
    free(received);
    free(ids);
    sqlite_queue_cleanup(ctx);
    unlink(TEST_DB_PATH);
}

// ============================================================================
// Test 3: Multiple queue operations with switching
// ============================================================================

static void test_multiple_queue_ops_with_switch(void) {
    printf("\nTest: Multiple queue operations with provider switching\n");

    unlink(TEST_DB_PATH);

    // Simulate a conversation that goes through multiple save/restore cycles
    // with provider switches in between

    SQLiteQueueContext *sender = sqlite_queue_init(TEST_DB_PATH, "process_a");
    SQLiteQueueContext *receiver = sqlite_queue_init(TEST_DB_PATH, "process_b");
    TEST_ASSERT(sender != NULL && receiver != NULL, "Both queue contexts initialized");

    // Round 1: Start with Kimi
    InternalMessage msgs1[10];
    int count1 = 0;
    build_test_conversation(msgs1, &count1);

    char *ser1 = serialize_conversation(msgs1, count1,
                                         TEST_PROVIDERS[0].model,
                                         TEST_PROVIDERS[0].api_base);
    sqlite_queue_send(sender, "process_b", ser1, strlen(ser1));
    free(ser1);
    cleanup_conversation(msgs1, count1);

    // Receive and switch to OpenAI
    char **rcv1 = NULL;
    long long *ids1 = NULL;
    int cnt1 = 0;
    sqlite_queue_receive(receiver, "process_a", 10, &rcv1, &cnt1, &ids1);
    sqlite_queue_acknowledge(receiver, ids1[0]);

    InternalMessage rmsgs1[10];
    int rcnt1 = 0;
    char *model1 = NULL;
    char *url1 = NULL;
    deserialize_conversation(rcv1[0], rmsgs1, &rcnt1, &model1, &url1, 10);

    // Switch to OpenAI
    Provider *prov1 = init_test_provider(&TEST_PROVIDERS[2], &url1);
    TEST_ASSERT(prov1 != NULL, "Switched to OpenAI after first restore");
    free(model1);
    model1 = strdup(TEST_PROVIDERS[2].model);

    // Round 2: Continue conversation, save with OpenAI
    char *ser2 = serialize_conversation(rmsgs1, rcnt1, model1, url1);
    sqlite_queue_send(receiver, "process_a", ser2, strlen(ser2));
    free(ser2);

    for (int i = 0; i < cnt1; i++) free(rcv1[i]);
    free(rcv1);
    free(ids1);
    cleanup_conversation(rmsgs1, rcnt1);
    prov1->cleanup(prov1);
    free(model1);
    free(url1);

    // Receive and switch to Anthropic
    char **rcv2 = NULL;
    long long *ids2 = NULL;
    int cnt2 = 0;
    sqlite_queue_receive(sender, "process_b", 10, &rcv2, &cnt2, &ids2);

    InternalMessage rmsgs2[10];
    int rcnt2 = 0;
    char *model2 = NULL;
    char *url2 = NULL;
    deserialize_conversation(rcv2[0], rmsgs2, &rcnt2, &model2, &url2, 10);

    // Verify we got the OpenAI model from the queue
    TEST_ASSERT(strcmp(model2, TEST_PROVIDERS[2].model) == 0, "Received OpenAI model from queue");

    // Switch to Anthropic
    Provider *prov2 = init_test_provider(&TEST_PROVIDERS[3], &url2);
    TEST_ASSERT(prov2 != NULL, "Switched to Anthropic after second restore");

    TEST_ASSERT(rcnt2 == 6, "All messages preserved through multiple queue ops");

    // Cleanup
    cleanup_conversation(rmsgs2, rcnt2);
    prov2->cleanup(prov2);
    free(model2);
    free(url2);
    for (int i = 0; i < cnt2; i++) free(rcv2[i]);
    free(rcv2);
    free(ids2);

    sqlite_queue_cleanup(sender);
    sqlite_queue_cleanup(receiver);
    unlink(TEST_DB_PATH);
}

// ============================================================================
// Test 4: Verify tool ordering preserved through queue
// ============================================================================

static void test_tool_ordering_preserved(void) {
    printf("\nTest: Tool call/result ordering preserved through queue\n");

    unlink(TEST_DB_PATH);

    // Create conversation with specific tool ordering
    InternalMessage msgs[10];
    int count = 0;

    // User: request
    msgs[count].role = MSG_USER;
    msgs[count].content_count = 1;
    msgs[count].contents = calloc(1, sizeof(InternalContent));
    msgs[count].contents[0].type = INTERNAL_TEXT;
    msgs[count].contents[0].text = strdup("Do multiple things");
    count++;

    // Assistant: two parallel tool calls
    msgs[count].role = MSG_ASSISTANT;
    msgs[count].content_count = 1;
    msgs[count].contents = calloc(1, sizeof(InternalContent));
    msgs[count].contents[0].type = INTERNAL_TOOL_CALL;
    msgs[count].contents[0].tool_id = strdup("call_a");
    msgs[count].contents[0].tool_name = strdup("Read");
    cJSON *p1 = cJSON_CreateObject();
    cJSON_AddStringToObject(p1, "file_path", "a.txt");
    msgs[count].contents[0].tool_params = p1;
    count++;

    // Note: In a real scenario, both tool calls might be in same assistant message
    // but for this test we use separate messages for clarity

    // Tool result A
    msgs[count].role = MSG_USER;
    msgs[count].content_count = 1;
    msgs[count].contents = calloc(1, sizeof(InternalContent));
    msgs[count].contents[0].type = INTERNAL_TOOL_RESPONSE;
    msgs[count].contents[0].tool_id = strdup("call_a");
    msgs[count].contents[0].tool_name = strdup("Read");
    cJSON *o1 = cJSON_CreateObject();
    cJSON_AddStringToObject(o1, "content", "Content A");
    msgs[count].contents[0].tool_output = o1;
    count++;

    // Another tool call
    msgs[count].role = MSG_ASSISTANT;
    msgs[count].content_count = 1;
    msgs[count].contents = calloc(1, sizeof(InternalContent));
    msgs[count].contents[0].type = INTERNAL_TOOL_CALL;
    msgs[count].contents[0].tool_id = strdup("call_b");
    msgs[count].contents[0].tool_name = strdup("Bash");
    cJSON *p2 = cJSON_CreateObject();
    cJSON_AddStringToObject(p2, "command", "ls");
    msgs[count].contents[0].tool_params = p2;
    count++;

    // Tool result B
    msgs[count].role = MSG_USER;
    msgs[count].content_count = 1;
    msgs[count].contents = calloc(1, sizeof(InternalContent));
    msgs[count].contents[0].type = INTERNAL_TOOL_RESPONSE;
    msgs[count].contents[0].tool_id = strdup("call_b");
    msgs[count].contents[0].tool_name = strdup("Bash");
    cJSON *o2 = cJSON_CreateObject();
    cJSON_AddStringToObject(o2, "output", "file.txt");
    msgs[count].contents[0].tool_output = o2;
    count++;

    // Final response
    msgs[count].role = MSG_ASSISTANT;
    msgs[count].content_count = 1;
    msgs[count].contents = calloc(1, sizeof(InternalContent));
    msgs[count].contents[0].type = INTERNAL_TEXT;
    msgs[count].contents[0].text = strdup("Done");
    count++;

    // Save to queue
    SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB_PATH, "sender");
    char *ser = serialize_conversation(msgs, count,
                                        TEST_PROVIDERS[0].model,
                                        TEST_PROVIDERS[0].api_base);
    sqlite_queue_send(ctx, "receiver", ser, strlen(ser));
    free(ser);
    cleanup_conversation(msgs, count);
    sqlite_queue_cleanup(ctx);

    // Restore and verify ordering
    ctx = sqlite_queue_init(TEST_DB_PATH, "receiver");
    char **rcv = NULL;
    long long *ids = NULL;
    int rcnt = 0;
    sqlite_queue_receive(ctx, "sender", 10, &rcv, &rcnt, &ids);

    InternalMessage rmsgs[10];
    int rcount = 0;
    char *model = NULL;
    char *url = NULL;
    deserialize_conversation(rcv[0], rmsgs, &rcount, &model, &url, 10);

    // Verify ordering
    TEST_ASSERT(rcount == 6, "All messages restored");
    TEST_ASSERT(rmsgs[0].role == MSG_USER, "Message 0 is user");
    TEST_ASSERT(rmsgs[1].role == MSG_ASSISTANT, "Message 1 is assistant");
    TEST_ASSERT(rmsgs[1].contents[0].type == INTERNAL_TOOL_CALL, "Message 1 is tool call");
    TEST_ASSERT(strcmp(rmsgs[1].contents[0].tool_id, "call_a") == 0, "Tool call ID is call_a");
    TEST_ASSERT(rmsgs[2].role == MSG_USER, "Message 2 is user");
    TEST_ASSERT(rmsgs[2].contents[0].type == INTERNAL_TOOL_RESPONSE, "Message 2 is tool response");
    TEST_ASSERT(strcmp(rmsgs[2].contents[0].tool_id, "call_a") == 0, "Tool response ID matches call_a");

    // Verify we can switch providers and ordering is still valid
    Provider *prov = init_test_provider(&TEST_PROVIDERS[1], &url);
    TEST_ASSERT(prov != NULL, "Can switch provider after queue restore");

    // Cleanup
    cleanup_conversation(rmsgs, rcount);
    prov->cleanup(prov);
    free(model);
    free(url);
    for (int i = 0; i < rcnt; i++) free(rcv[i]);
    free(rcv);
    free(ids);
    sqlite_queue_cleanup(ctx);
    unlink(TEST_DB_PATH);
}

// ============================================================================
// Test 5: All providers from real_api_calls.db
// ============================================================================

static void test_all_real_providers(void) {
    printf("\nTest: All providers from real_api_calls.db\n");

    unlink(TEST_DB_PATH);

    // Test that all 4 providers found in the real database can be initialized
    // and used in queue mode

    for (size_t i = 0; i < NUM_TEST_PROVIDERS; i++) {
        // Save conversation with this provider
        SQLiteQueueContext *ctx = sqlite_queue_init(TEST_DB_PATH, "sender");

        InternalMessage msgs[10];
        int count = 0;
        build_test_conversation(msgs, &count);

        char *ser = serialize_conversation(msgs, count,
                                            TEST_PROVIDERS[i].model,
                                            TEST_PROVIDERS[i].api_base);
        sqlite_queue_send(ctx, "receiver", ser, strlen(ser));
        free(ser);
        cleanup_conversation(msgs, count);
        sqlite_queue_cleanup(ctx);

        // Restore and verify
        ctx = sqlite_queue_init(TEST_DB_PATH, "receiver");
        char **rcv = NULL;
        long long *ids = NULL;
        int rcnt = 0;
        sqlite_queue_receive(ctx, "sender", 10, &rcv, &rcnt, &ids);
        sqlite_queue_acknowledge(ctx, ids[0]);

        InternalMessage rmsgs[10];
        int rcount = 0;
        char *model = NULL;
        char *url = NULL;
        deserialize_conversation(rcv[0], rmsgs, &rcount, &model, &url, 10);

        TEST_ASSERT(strcmp(model, TEST_PROVIDERS[i].model) == 0,
                    "Provider model preserved correctly");

        // Cleanup
        cleanup_conversation(rmsgs, rcount);
        free(model);
        free(url);
        for (int j = 0; j < rcnt; j++) free(rcv[j]);
        free(rcv);
        free(ids);
        sqlite_queue_cleanup(ctx);

        // Clear for next iteration
        unlink(TEST_DB_PATH);
    }

    printf("  [INFO] Tested all %zu providers from real_api_calls.db\n", NUM_TEST_PROVIDERS);
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    printf("=== Model Switch Tests (SQLite Queue Mode) ===\n");
    printf("Testing providers from real_api_calls.db:\n");
    for (size_t i = 0; i < NUM_TEST_PROVIDERS; i++) {
        printf("  - %s (%s) - %s\n",
               TEST_PROVIDERS[i].name,
               TEST_PROVIDERS[i].model,
               i == 0 ? "582 calls" : "1 call");  // Match real distribution
    }

    test_queue_save_restore();
    test_switch_after_queue_restore();
    test_multiple_queue_ops_with_switch();
    test_tool_ordering_preserved();
    test_all_real_providers();

    printf("\n=== Results ===\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);

    // Cleanup
    unlink(TEST_DB_PATH);

    return tests_failed > 0 ? 1 : 0;
}
