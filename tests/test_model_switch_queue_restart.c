/*
 * test_model_switch_queue_restart.c - Test model switching via daemon restart
 *
 * This test verifies that you can switch models by:
 * 1. Starting klawed with Provider A and SQLite queue
 * 2. Having a conversation (messages saved to queue)
 * 3. Stopping klawed
 * 4. Restarting klawed with Provider B pointing to same queue file
 * 5. Continuing conversation with Provider B
 *
 * This is the recommended way to switch providers in SQLite queue mode.
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

// Minimal logger stubs
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

#define TEST_DB_PATH "/tmp/test_model_switch_restart.db"

// ============================================================================
// Provider Configurations
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
        .name = "anthropic-claude",
        .model = "claude-opus-4",
        .api_base = "https://api.anthropic.com/v1/messages",
        .type = PROVIDER_ANTHROPIC,
        .api_key = "test-anthropic-key"
    }
};

#define NUM_TEST_PROVIDERS (sizeof(TEST_PROVIDERS) / sizeof(TEST_PROVIDERS[0]))

// ============================================================================
// Helper: Simulate klawed daemon start with specific provider
// ============================================================================

typedef struct {
    SQLiteQueueContext *queue_ctx;
    ConversationState *state;
    Provider *provider;
    char *model;
    char *api_url;
} SimulatedDaemon;

static int init_daemon_with_provider(SimulatedDaemon *daemon,
                                     const TestProviderConfig *provider_config,
                                     const char *db_path) {
    memset(daemon, 0, sizeof(*daemon));

    // Initialize queue context
    daemon->queue_ctx = sqlite_queue_init(db_path, "klawed");
    if (!daemon->queue_ctx) {
        return -1;
    }

    // Allocate conversation state
    daemon->state = calloc(1, sizeof(ConversationState));
    if (!daemon->state) {
        sqlite_queue_cleanup(daemon->queue_ctx);
        return -1;
    }

    // Initialize conversation state
    pthread_mutex_init(&daemon->state->conv_mutex, NULL);
    daemon->state->conv_mutex_initialized = 1;
    daemon->state->max_tokens = 16384;

    // Initialize provider
    LLMProviderConfig config;
    memset(&config, 0, sizeof(config));
    strlcpy(config.model, provider_config->model, sizeof(config.model));
    strlcpy(config.api_base, provider_config->api_base, sizeof(config.api_base));
    strlcpy(config.api_key, provider_config->api_key, sizeof(config.api_key));
    config.provider_type = provider_config->type;

    ProviderInitResult result = {0};
    provider_init_from_config(provider_config->name, &config, &result);

    if (!result.provider) {
        free(daemon->state);
        sqlite_queue_cleanup(daemon->queue_ctx);
        return -1;
    }

    daemon->provider = result.provider;
    daemon->api_url = result.api_url;
    daemon->model = strdup(provider_config->model);

    daemon->state->provider = daemon->provider;
    daemon->state->model = strdup(provider_config->model);
    daemon->state->api_url = result.api_url ? strdup(result.api_url) : NULL;

    free(result.error_message);

    return 0;
}

static void cleanup_daemon(SimulatedDaemon *daemon) {
    if (!daemon) return;

    if (daemon->state) {
        // Clean up provider first
        if (daemon->state->provider) {
            daemon->state->provider->cleanup(daemon->state->provider);
        }

        // Free messages
        for (int i = 0; i < daemon->state->count; i++) {
            for (int j = 0; j < daemon->state->messages[i].content_count; j++) {
                InternalContent *c = &daemon->state->messages[i].contents[j];
                free(c->text);
                free(c->tool_id);
                free(c->tool_name);
                if (c->tool_params) cJSON_Delete(c->tool_params);
                if (c->tool_output) cJSON_Delete(c->tool_output);
            }
            free(daemon->state->messages[i].contents);
        }

        if (daemon->state->conv_mutex_initialized) {
            pthread_mutex_destroy(&daemon->state->conv_mutex);
        }
        free(daemon->state->model);
        free(daemon->state->api_url);
        free(daemon->state);
    }

    // Note: don't cleanup queue_ctx here - it persists across restarts

    free(daemon->model);
    free(daemon->api_url);
    // Provider is in state, already cleaned up via state->provider->cleanup

    memset(daemon, 0, sizeof(*daemon));
}

// ============================================================================
// Helper: Add messages to daemon's conversation and save to queue
// ============================================================================

static int add_user_message_to_queue(SimulatedDaemon *daemon, const char *text) {
    // Add to conversation state
    if (daemon->state->count >= MAX_MESSAGES) return -1;

    InternalMessage *msg = &daemon->state->messages[daemon->state->count++];
    msg->role = MSG_USER;
    msg->content_count = 1;
    msg->contents = calloc(1, sizeof(InternalContent));
    msg->contents[0].type = INTERNAL_TEXT;
    msg->contents[0].text = strdup(text);

    // Save to queue as "external" message (from client, not klawed)
    // Manually insert with "client" as sender
    sqlite3 *db = (sqlite3 *)daemon->queue_ctx->db_handle;
    if (!db) return -1;

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "messageType", "TEXT");
    cJSON_AddStringToObject(json, "content", text);
    char *serialized = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    const char *sql = "INSERT INTO messages (sender, receiver, message, sent) VALUES (?, ?, ?, 0);";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        free(serialized);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, "client", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, "klawed", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, serialized, -1, SQLITE_TRANSIENT);
    int result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    free(serialized);

    return result == SQLITE_DONE ? 0 : -1;
}

static int add_assistant_message_to_queue(SimulatedDaemon *daemon, const char *text) {
    // Add to conversation state
    if (daemon->state->count >= MAX_MESSAGES) return -1;

    InternalMessage *msg = &daemon->state->messages[daemon->state->count++];
    msg->role = MSG_ASSISTANT;
    msg->content_count = 1;
    msg->contents = calloc(1, sizeof(InternalContent));
    msg->contents[0].type = INTERNAL_TEXT;
    msg->contents[0].text = strdup(text);

    // Save to queue
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "messageType", "TEXT");
    cJSON_AddStringToObject(json, "content", text);

    char *serialized = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    int result = sqlite_queue_send(daemon->queue_ctx, "client", serialized, strlen(serialized));
    free(serialized);

    return result == SQLITE_QUEUE_ERROR_NONE ? 0 : -1;
}

static int add_tool_call_to_queue(SimulatedDaemon *daemon, const char *tool_id,
                                   const char *tool_name, cJSON *params) {
    // Add to conversation state
    if (daemon->state->count >= MAX_MESSAGES) return -1;

    InternalMessage *msg = &daemon->state->messages[daemon->state->count++];
    msg->role = MSG_ASSISTANT;
    msg->content_count = 1;
    msg->contents = calloc(1, sizeof(InternalContent));
    msg->contents[0].type = INTERNAL_TOOL_CALL;
    msg->contents[0].tool_id = strdup(tool_id);
    msg->contents[0].tool_name = strdup(tool_name);
    msg->contents[0].tool_params = cJSON_Duplicate(params, 1);

    // Save to queue
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "messageType", "TOOL");
    cJSON_AddStringToObject(json, "toolId", tool_id);
    cJSON_AddStringToObject(json, "toolName", tool_name);
    cJSON_AddItemToObject(json, "toolParameters", cJSON_Duplicate(params, 1));

    char *serialized = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    int result = sqlite_queue_send(daemon->queue_ctx, "client", serialized, strlen(serialized));
    free(serialized);

    return result == SQLITE_QUEUE_ERROR_NONE ? 0 : -1;
}

static int add_tool_result_to_queue(SimulatedDaemon *daemon, const char *tool_id,
                                     const char *tool_name, cJSON *output, int is_error) {
    // Add to conversation state
    if (daemon->state->count >= MAX_MESSAGES) return -1;

    InternalMessage *msg = &daemon->state->messages[daemon->state->count++];
    msg->role = MSG_USER;
    msg->content_count = 1;
    msg->contents = calloc(1, sizeof(InternalContent));
    msg->contents[0].type = INTERNAL_TOOL_RESPONSE;
    msg->contents[0].tool_id = strdup(tool_id);
    msg->contents[0].tool_name = strdup(tool_name);
    msg->contents[0].tool_output = cJSON_Duplicate(output, 1);
    msg->contents[0].is_error = is_error;

    // Save to queue
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "messageType", "TOOL_RESULT");
    cJSON_AddStringToObject(json, "toolId", tool_id);
    cJSON_AddStringToObject(json, "toolName", tool_name);
    cJSON_AddItemToObject(json, "toolOutput", cJSON_Duplicate(output, 1));
    if (is_error) cJSON_AddBoolToObject(json, "isError", 1);

    char *serialized = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    int result = sqlite_queue_send(daemon->queue_ctx, "client", serialized, strlen(serialized));
    free(serialized);

    return result == SQLITE_QUEUE_ERROR_NONE ? 0 : -1;
}

// ============================================================================
// Helper: Restore conversation from queue (simulates daemon restart)
// ============================================================================

static int restore_conversation_from_queue(SimulatedDaemon *daemon) {
    // This simulates sqlite_queue_restore_conversation
    // Note: state should be empty (fresh daemon), we populate it from queue
    sqlite3 *db = (sqlite3 *)daemon->queue_ctx->db_handle;
    if (!db) {
        return -1;
    }

    const char *sql = "SELECT sender, message FROM messages ORDER BY id;";
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *sender = (const char *)sqlite3_column_text(stmt, 0);
        const char *message = (const char *)sqlite3_column_text(stmt, 1);

        if (!sender || !message) continue;

        cJSON *json = cJSON_Parse(message);
        if (!json) continue;

        cJSON *jtype = cJSON_GetObjectItem(json, "messageType");
        if (!jtype || !cJSON_IsString(jtype)) {
            cJSON_Delete(json);
            continue;
        }

        int from_klawed = (strcmp(sender, "klawed") == 0);

        if (strcmp(jtype->valuestring, "TEXT") == 0) {
            cJSON *jcontent = cJSON_GetObjectItem(json, "content");
            if (jcontent && cJSON_IsString(jcontent)) {
                if (daemon->state->count >= MAX_MESSAGES) {
                    cJSON_Delete(json);
                    break;
                }

                InternalMessage *msg = &daemon->state->messages[daemon->state->count++];
                msg->role = from_klawed ? MSG_ASSISTANT : MSG_USER;
                msg->content_count = 1;
                msg->contents = calloc(1, sizeof(InternalContent));
                msg->contents[0].type = INTERNAL_TEXT;
                msg->contents[0].text = strdup(jcontent->valuestring);
            }
        } else if (strcmp(jtype->valuestring, "TOOL") == 0 && from_klawed) {
            cJSON *jtool_id = cJSON_GetObjectItem(json, "toolId");
            cJSON *jtool_name = cJSON_GetObjectItem(json, "toolName");
            cJSON *jtool_params = cJSON_GetObjectItem(json, "toolParameters");

            if (jtool_id && cJSON_IsString(jtool_id) &&
                jtool_name && cJSON_IsString(jtool_name)) {

                if (daemon->state->count >= MAX_MESSAGES) {
                    cJSON_Delete(json);
                    break;
                }

                InternalMessage *msg = &daemon->state->messages[daemon->state->count++];
                msg->role = MSG_ASSISTANT;
                msg->content_count = 1;
                msg->contents = calloc(1, sizeof(InternalContent));
                msg->contents[0].type = INTERNAL_TOOL_CALL;
                msg->contents[0].tool_id = strdup(jtool_id->valuestring);
                msg->contents[0].tool_name = strdup(jtool_name->valuestring);
                msg->contents[0].tool_params = jtool_params ? cJSON_Duplicate(jtool_params, 1) : cJSON_CreateObject();
            }
        } else if (strcmp(jtype->valuestring, "TOOL_RESULT") == 0 && from_klawed) {
            cJSON *jtool_id = cJSON_GetObjectItem(json, "toolId");
            cJSON *jtool_name = cJSON_GetObjectItem(json, "toolName");
            cJSON *jtool_output = cJSON_GetObjectItem(json, "toolOutput");
            cJSON *jis_error = cJSON_GetObjectItem(json, "isError");

            if (jtool_id && cJSON_IsString(jtool_id)) {
                if (daemon->state->count >= MAX_MESSAGES) {
                    cJSON_Delete(json);
                    break;
                }

                InternalMessage *msg = &daemon->state->messages[daemon->state->count++];
                msg->role = MSG_USER;
                msg->content_count = 1;
                msg->contents = calloc(1, sizeof(InternalContent));
                msg->contents[0].type = INTERNAL_TOOL_RESPONSE;
                msg->contents[0].tool_id = strdup(jtool_id->valuestring);
                msg->contents[0].tool_name = strdup(jtool_name && cJSON_IsString(jtool_name) ?
                                                    jtool_name->valuestring : "unknown");
                msg->contents[0].tool_output = jtool_output ? cJSON_Duplicate(jtool_output, 1) : cJSON_CreateObject();
                msg->contents[0].is_error = (jis_error && cJSON_IsBool(jis_error)) ? jis_error->valueint : 0;
            }
        }

        cJSON_Delete(json);
    }

    sqlite3_finalize(stmt);
    return 0;
}

// ============================================================================
// Test 1: Simple restart with provider switch
// ============================================================================

static void test_simple_restart_with_switch(void) {
    printf("\nTest: Simple daemon restart with provider switch\n");

    unlink(TEST_DB_PATH);

    // Phase 1: Start with Kimi
    SimulatedDaemon daemon1;
    TEST_ASSERT(init_daemon_with_provider(&daemon1, &TEST_PROVIDERS[0], TEST_DB_PATH) == 0,
                "Daemon 1 started with Kimi");

    // Add some conversation
    add_user_message_to_queue(&daemon1, "Hello, help me with Python");
    add_assistant_message_to_queue(&daemon1, "Sure! What do you need help with?");
    add_user_message_to_queue(&daemon1, "How do I read a file?");

    TEST_ASSERT(daemon1.state->count == 3, "3 messages in daemon 1");
    TEST_ASSERT(strcmp(daemon1.model, "kimi-for-coding") == 0, "Daemon 1 uses Kimi");

    // Phase 2: Stop daemon 1, start daemon 2 with Z.AI (same queue file)
    sqlite_queue_cleanup(daemon1.queue_ctx);  // Keep the DB file
    cleanup_daemon(&daemon1);

    SimulatedDaemon daemon2;
    TEST_ASSERT(init_daemon_with_provider(&daemon2, &TEST_PROVIDERS[1], TEST_DB_PATH) == 0,
                "Daemon 2 started with Z.AI (restart)");

    // Restore conversation from queue
    TEST_ASSERT(restore_conversation_from_queue(&daemon2) == 0,
                "Conversation restored from queue");

    TEST_ASSERT(daemon2.state->count == 3, "All 3 messages restored");
    TEST_ASSERT(strcmp(daemon2.model, "glm-4.6") == 0, "Daemon 2 uses Z.AI");

    // Verify messages are preserved
    TEST_ASSERT(daemon2.state->messages[0].role == MSG_USER, "Message 0 is user");
    TEST_ASSERT(daemon2.state->messages[1].role == MSG_ASSISTANT, "Message 1 is assistant");
    TEST_ASSERT(daemon2.state->messages[2].role == MSG_USER, "Message 2 is user");

    // Can continue conversation with new provider
    add_assistant_message_to_queue(&daemon2, "Use open() function...");
    TEST_ASSERT(daemon2.state->count == 4, "Can add new message with Z.AI");

    sqlite_queue_cleanup(daemon2.queue_ctx);
    cleanup_daemon(&daemon2);
    unlink(TEST_DB_PATH);
}

// ============================================================================
// Test 2: Restart with tool calls in conversation
// ============================================================================

static void test_restart_with_tool_calls(void) {
    printf("\nTest: Restart with tool calls in conversation\n");

    unlink(TEST_DB_PATH);

    // Phase 1: Kimi with tools
    SimulatedDaemon daemon1;
    init_daemon_with_provider(&daemon1, &TEST_PROVIDERS[0], TEST_DB_PATH);

    add_user_message_to_queue(&daemon1, "List files");

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "ls -la");
    add_tool_call_to_queue(&daemon1, "call_1", "Bash", params);
    cJSON_Delete(params);

    cJSON *output = cJSON_CreateObject();
    cJSON_AddStringToObject(output, "output", "file.txt\nmain.py");
    add_tool_result_to_queue(&daemon1, "call_1", "Bash", output, 0);
    cJSON_Delete(output);

    add_assistant_message_to_queue(&daemon1, "You have file.txt and main.py");

    TEST_ASSERT(daemon1.state->count == 4, "4 messages with tool call");

    // Phase 2: Restart with Anthropic
    sqlite_queue_cleanup(daemon1.queue_ctx);
    cleanup_daemon(&daemon1);

    SimulatedDaemon daemon2;
    init_daemon_with_provider(&daemon2, &TEST_PROVIDERS[2], TEST_DB_PATH);
    restore_conversation_from_queue(&daemon2);

    TEST_ASSERT(daemon2.state->count == 4, "All 4 messages restored");
    TEST_ASSERT(strcmp(daemon2.model, "claude-opus-4") == 0, "Now using Anthropic");

    // Verify tool call/result ordering preserved
    TEST_ASSERT(daemon2.state->messages[1].role == MSG_ASSISTANT, "Msg 1 is assistant");
    TEST_ASSERT(daemon2.state->messages[1].contents[0].type == INTERNAL_TOOL_CALL, "Msg 1 is tool call");
    TEST_ASSERT(strcmp(daemon2.state->messages[1].contents[0].tool_id, "call_1") == 0, "Tool ID preserved");

    TEST_ASSERT(daemon2.state->messages[2].role == MSG_USER, "Msg 2 is user");
    TEST_ASSERT(daemon2.state->messages[2].contents[0].type == INTERNAL_TOOL_RESPONSE, "Msg 2 is tool result");
    TEST_ASSERT(strcmp(daemon2.state->messages[2].contents[0].tool_id, "call_1") == 0, "Tool result ID matches");

    sqlite_queue_cleanup(daemon2.queue_ctx);
    cleanup_daemon(&daemon2);
    unlink(TEST_DB_PATH);
}

// ============================================================================
// Test 3: Multiple restarts with different providers
// ============================================================================

static void test_multiple_restarts(void) {
    printf("\nTest: Multiple restarts with different providers\n");

    unlink(TEST_DB_PATH);

    SimulatedDaemon daemons[3];
    const char *models[] = {"kimi-for-coding", "glm-4.6", "claude-opus-4"};

    // Start with Kimi
    init_daemon_with_provider(&daemons[0], &TEST_PROVIDERS[0], TEST_DB_PATH);
    add_user_message_to_queue(&daemons[0], "Message 1");
    add_assistant_message_to_queue(&daemons[0], "Response 1");

    sqlite_queue_cleanup(daemons[0].queue_ctx);
    cleanup_daemon(&daemons[0]);

    // Restart with Z.AI
    init_daemon_with_provider(&daemons[1], &TEST_PROVIDERS[1], TEST_DB_PATH);
    restore_conversation_from_queue(&daemons[1]);
    add_user_message_to_queue(&daemons[1], "Message 2");
    add_assistant_message_to_queue(&daemons[1], "Response 2");

    TEST_ASSERT(strcmp(daemons[1].model, models[1]) == 0, "Daemon 2 uses Z.AI");
    TEST_ASSERT(daemons[1].state->count == 4, "4 messages after daemon 2");

    sqlite_queue_cleanup(daemons[1].queue_ctx);
    cleanup_daemon(&daemons[1]);

    // Restart with Anthropic
    init_daemon_with_provider(&daemons[2], &TEST_PROVIDERS[2], TEST_DB_PATH);
    restore_conversation_from_queue(&daemons[2]);

    TEST_ASSERT(strcmp(daemons[2].model, models[2]) == 0, "Daemon 3 uses Anthropic");
    TEST_ASSERT(daemons[2].state->count == 4, "All 4 messages preserved");

    // Verify message ordering through all switches
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT(daemons[2].state->messages[i].role == (i % 2 == 0 ? MSG_USER : MSG_ASSISTANT),
                    "Message ordering preserved");
    }

    sqlite_queue_cleanup(daemons[2].queue_ctx);
    cleanup_daemon(&daemons[2]);
    unlink(TEST_DB_PATH);
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    printf("=== Model Switch via Daemon Restart Tests ===\n");
    printf("Testing scenario: Start klawed -> Conversation -> Stop -> Restart with different provider\n");
    printf("Providers tested: Kimi -> Z.AI -> Anthropic\n\n");

    test_simple_restart_with_switch();
    test_restart_with_tool_calls();
    test_multiple_restarts();

    printf("\n=== Results ===\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);

    if (tests_failed == 0) {
        printf("\n✓ Model switching via daemon restart WORKS!\n");
        printf("  You can switch providers by stopping klawed and restarting with different config.\n");
    }

    sqlite3_shutdown();
    return tests_failed > 0 ? 1 : 0;
}
