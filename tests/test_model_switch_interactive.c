/*
 * test_model_switch_interactive.c - Unit tests for mid-conversation model switching
 *
 * Tests provider switching at various points during a conversation in interactive mode.
 * Based on real providers found in api_calls.db:
 *   - Kimi Coding (kimi-for-coding)
 *   - Z.AI/GLM (glm-4.6)
 *   - OpenAI/GPT (gpt-5.4)
 *   - Anthropic Claude
 *
 * Verifies:
 *   - Provider can be switched at any conversation point
 *   - Internal message format is preserved
 *   - Tool call/result ordering is maintained
 *   - Conversation state remains valid after switching
 */

#define _POSIX_C_SOURCE 200809L
#define TEST_BUILD 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <cjson/cJSON.h>
#include <bsd/string.h>
#include <bsd/stdlib.h>

// Include core types
#include "../src/klawed_internal.h"
#include "../src/provider.h"
#include "../src/config.h"

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

#define TEST_ASSERT_EQ(expected, actual, msg) \
    TEST_ASSERT((expected) == (actual), msg)

#define TEST_ASSERT_STR_EQ(expected, actual, msg) \
    TEST_ASSERT((expected) == NULL ? (actual) == NULL : ((actual) != NULL && strcmp((expected), (actual)) == 0), msg)

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
        .type = PROVIDER_OPENAI,  // OpenAI-compatible
        .api_key = "test-kimi-key"
    },
    {
        .name = "zai-glm",
        .model = "glm-4.6",
        .api_base = "https://api.z.ai/api/coding/paas/v4/chat/completions",
        .type = PROVIDER_OPENAI,  // OpenAI-compatible
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
// Mock Conversation State
// ============================================================================

typedef struct {
    InternalMessage messages[MAX_MESSAGES];
    int count;
    Provider *provider;
    char *model;
    char *api_url;
    pthread_mutex_t lock;
} MockConversationState;

static void init_mock_state(MockConversationState *state) {
    memset(state, 0, sizeof(*state));
    state->count = 0;
    state->provider = NULL;
    state->model = NULL;
    state->api_url = NULL;
    pthread_mutex_init(&state->lock, NULL);
}

static void cleanup_mock_state(MockConversationState *state) {
    // Free all message contents
    for (int i = 0; i < state->count; i++) {
        InternalMessage *msg = &state->messages[i];
        for (int j = 0; j < msg->content_count; j++) {
            InternalContent *c = &msg->contents[j];
            free(c->text);
            free(c->tool_id);
            free(c->tool_name);
            if (c->tool_params) cJSON_Delete(c->tool_params);
            if (c->tool_output) cJSON_Delete(c->tool_output);
        }
        free(msg->contents);
    }

    if (state->provider && state->provider->cleanup) {
        state->provider->cleanup(state->provider);
    }
    free(state->model);
    free(state->api_url);
    pthread_mutex_destroy(&state->lock);
}

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
// Helper: Switch provider on conversation state
// ============================================================================

static int switch_provider(MockConversationState *state,
                           const TestProviderConfig *new_config) {
    pthread_mutex_lock(&state->lock);

    // Clean up old provider
    if (state->provider && state->provider->cleanup) {
        state->provider->cleanup(state->provider);
    }
    free(state->api_url);

    // Create new provider config
    LLMProviderConfig config;
    create_provider_config(new_config, &config);

    // Initialize new provider
    ProviderInitResult result = {0};
    provider_init_from_config(new_config->name, &config, &result);

    if (!result.provider) {
        pthread_mutex_unlock(&state->lock);
        fprintf(stderr, "Failed to initialize provider '%s': %s\n",
                new_config->name, result.error_message ? result.error_message : "unknown");
        free(result.error_message);
        free(result.api_url);
        return -1;
    }

    // Update state
    state->provider = result.provider;
    state->api_url = result.api_url;

    // Update model
    free(state->model);
    state->model = strdup(new_config->model);

    free(result.error_message);

    pthread_mutex_unlock(&state->lock);
    return 0;
}

// ============================================================================
// Helper: Add messages to conversation
// ============================================================================

static int mock_add_user_message(MockConversationState *state, const char *text) {
    pthread_mutex_lock(&state->lock);

    if (state->count >= MAX_MESSAGES) {
        pthread_mutex_unlock(&state->lock);
        return -1;
    }

    InternalMessage *msg = &state->messages[state->count++];
    msg->role = MSG_USER;
    msg->content_count = 1;
    msg->contents = calloc(1, sizeof(InternalContent));
    if (!msg->contents) {
        state->count--;
        pthread_mutex_unlock(&state->lock);
        return -1;
    }

    msg->contents[0].type = INTERNAL_TEXT;
    msg->contents[0].text = strdup(text);

    pthread_mutex_unlock(&state->lock);
    return 0;
}

static int mock_add_assistant_message(MockConversationState *state, const char *text) {
    pthread_mutex_lock(&state->lock);

    if (state->count >= MAX_MESSAGES) {
        pthread_mutex_unlock(&state->lock);
        return -1;
    }

    InternalMessage *msg = &state->messages[state->count++];
    msg->role = MSG_ASSISTANT;
    msg->content_count = 1;
    msg->contents = calloc(1, sizeof(InternalContent));
    if (!msg->contents) {
        state->count--;
        pthread_mutex_unlock(&state->lock);
        return -1;
    }

    msg->contents[0].type = INTERNAL_TEXT;
    msg->contents[0].text = strdup(text);

    pthread_mutex_unlock(&state->lock);
    return 0;
}

static int mock_add_tool_call(MockConversationState *state, const char *tool_id,
                         const char *tool_name, cJSON *params) {
    pthread_mutex_lock(&state->lock);

    if (state->count >= MAX_MESSAGES) {
        pthread_mutex_unlock(&state->lock);
        return -1;
    }

    InternalMessage *msg = &state->messages[state->count++];
    msg->role = MSG_ASSISTANT;
    msg->content_count = 1;
    msg->contents = calloc(1, sizeof(InternalContent));
    if (!msg->contents) {
        state->count--;
        pthread_mutex_unlock(&state->lock);
        return -1;
    }

    msg->contents[0].type = INTERNAL_TOOL_CALL;
    msg->contents[0].tool_id = strdup(tool_id);
    msg->contents[0].tool_name = strdup(tool_name);
    msg->contents[0].tool_params = cJSON_Duplicate(params, 1);

    pthread_mutex_unlock(&state->lock);
    return 0;
}

static int mock_add_tool_result(MockConversationState *state, const char *tool_id,
                           const char *tool_name, cJSON *output, int is_error) {
    pthread_mutex_lock(&state->lock);

    if (state->count >= MAX_MESSAGES) {
        pthread_mutex_unlock(&state->lock);
        return -1;
    }

    InternalMessage *msg = &state->messages[state->count++];
    msg->role = MSG_USER;
    msg->content_count = 1;
    msg->contents = calloc(1, sizeof(InternalContent));
    if (!msg->contents) {
        state->count--;
        pthread_mutex_unlock(&state->lock);
        return -1;
    }

    msg->contents[0].type = INTERNAL_TOOL_RESPONSE;
    msg->contents[0].tool_id = strdup(tool_id);
    msg->contents[0].tool_name = strdup(tool_name);
    msg->contents[0].tool_output = cJSON_Duplicate(output, 1);
    msg->contents[0].is_error = is_error;

    pthread_mutex_unlock(&state->lock);
    return 0;
}

// ============================================================================
// Helper: Verify conversation integrity
// ============================================================================

static int verify_conversation_integrity(MockConversationState *state) {
    pthread_mutex_lock(&state->lock);

    int valid = 1;

    // Check 1: Tool calls must have matching results
    for (int i = 0; i < state->count; i++) {
        InternalMessage *msg = &state->messages[i];
        if (msg->role == MSG_ASSISTANT) {
            for (int j = 0; j < msg->content_count; j++) {
                if (msg->contents[j].type == INTERNAL_TOOL_CALL) {
                    const char *tool_id = msg->contents[j].tool_id;
                    int found_result = 0;

                    // Look for matching result in subsequent messages
                    for (int k = i + 1; k < state->count; k++) {
                        InternalMessage *result_msg = &state->messages[k];
                        if (result_msg->role == MSG_USER) {
                            for (int l = 0; l < result_msg->content_count; l++) {
                                if (result_msg->contents[l].type == INTERNAL_TOOL_RESPONSE &&
                                    result_msg->contents[l].tool_id &&
                                    strcmp(result_msg->contents[l].tool_id, tool_id) == 0) {
                                    found_result = 1;
                                    break;
                                }
                            }
                        }
                        if (found_result) break;
                    }

                    if (!found_result) {
                        printf("  [WARN] Tool call '%s' has no matching result\n", tool_id);
                        // This is a warning, not necessarily a failure (could be pending)
                    }
                }
            }
        }
    }

    // Check 2: All messages must have valid roles
    for (int i = 0; i < state->count; i++) {
        InternalMessage *msg = &state->messages[i];
        if (msg->role != MSG_USER && msg->role != MSG_ASSISTANT && msg->role != MSG_SYSTEM) {
            printf("  [ERROR] Message %d has invalid role\n", i);
            valid = 0;
        }
    }

    pthread_mutex_unlock(&state->lock);
    return valid;
}

// ============================================================================
// Test 1: Switch provider at empty conversation
// ============================================================================

static void test_switch_at_empty(void) {
    printf("\nTest: Switch provider at empty conversation\n");

    MockConversationState state;
    init_mock_state(&state);

    // Switch to first provider
    int result = switch_provider(&state, &TEST_PROVIDERS[0]);
    TEST_ASSERT(result == 0, "Can switch to provider at empty conversation");
    TEST_ASSERT(state.provider != NULL, "Provider is set");
    TEST_ASSERT_STR_EQ(TEST_PROVIDERS[0].model, state.model, "Model matches");

    // Switch to second provider
    result = switch_provider(&state, &TEST_PROVIDERS[1]);
    TEST_ASSERT(result == 0, "Can switch to different provider");
    TEST_ASSERT_STR_EQ(TEST_PROVIDERS[1].model, state.model, "Model updated");

    cleanup_mock_state(&state);
}

// ============================================================================
// Test 2: Switch after user message
// ============================================================================

static void test_switch_after_user_message(void) {
    printf("\nTest: Switch provider after user message\n");

    MockConversationState state;
    init_mock_state(&state);

    // Add user message
    mock_add_user_message(&state, "Hello, can you help me with coding?");
    TEST_ASSERT(state.count == 1, "User message added");

    // Switch provider
    int result = switch_provider(&state, &TEST_PROVIDERS[2]);
    TEST_ASSERT(result == 0, "Can switch after user message");
    TEST_ASSERT(state.count == 1, "Message count preserved");
    TEST_ASSERT_STR_EQ(TEST_PROVIDERS[2].model, state.model, "Model is OpenAI");

    cleanup_mock_state(&state);
}

// ============================================================================
// Test 3: Switch after assistant response
// ============================================================================

static void test_switch_after_assistant(void) {
    printf("\nTest: Switch provider after assistant response\n");

    MockConversationState state;
    init_mock_state(&state);

    // Add conversation
    mock_add_user_message(&state, "Write a Python function to sort a list");
    mock_add_assistant_message(&state, "Here's a Python function to sort a list using bubble sort...");

    TEST_ASSERT(state.count == 2, "Conversation has 2 messages");

    // Switch to all providers sequentially
    for (size_t i = 0; i < NUM_TEST_PROVIDERS; i++) {
        int result = switch_provider(&state, &TEST_PROVIDERS[i]);
        TEST_ASSERT(result == 0, "Can switch to provider after assistant response");
        TEST_ASSERT(state.count == 2, "Messages preserved after switch");

        int integrity = verify_conversation_integrity(&state);
        TEST_ASSERT(integrity, "Conversation integrity maintained");
    }

    cleanup_mock_state(&state);
}

// ============================================================================
// Test 4: Switch with tool calls in conversation
// ============================================================================

static void test_switch_with_tool_calls(void) {
    printf("\nTest: Switch provider with tool calls in conversation\n");

    MockConversationState state;
    init_mock_state(&state);

    // Build conversation with tool calls
    mock_add_user_message(&state, "List files in current directory");

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "ls -la");
    mock_add_tool_call(&state, "call_1", "Bash", params);
    cJSON_Delete(params);

    cJSON *output = cJSON_CreateObject();
    cJSON_AddStringToObject(output, "output", "total 10\ndrwxr-xr-x 3 user user 4096 Mar 26 10:00 .\n...");
    mock_add_tool_result(&state, "call_1", "Bash", output, 0);
    cJSON_Delete(output);

    mock_add_assistant_message(&state, "Here are the files in your directory...");

    TEST_ASSERT(state.count == 4, "Conversation has 4 messages");

    // Switch between all providers
    for (size_t i = 0; i < NUM_TEST_PROVIDERS; i++) {
        int result = switch_provider(&state, &TEST_PROVIDERS[i]);
        TEST_ASSERT(result == 0, "Can switch with tool calls");
        TEST_ASSERT(state.count == 4, "All messages preserved");

        int integrity = verify_conversation_integrity(&state);
        TEST_ASSERT(integrity, "Tool call/result integrity maintained");
    }

    cleanup_mock_state(&state);
}

// ============================================================================
// Test 5: Switch with multiple parallel tool calls
// ============================================================================

static void test_switch_with_parallel_tools(void) {
    printf("\nTest: Switch with multiple parallel tool calls\n");

    MockConversationState state;
    init_mock_state(&state);

    // User asks for multiple things
    mock_add_user_message(&state, "Read file.txt and also show me the current directory");

    // Assistant makes two parallel tool calls
    cJSON *params1 = cJSON_CreateObject();
    cJSON_AddStringToObject(params1, "file_path", "file.txt");
    mock_add_tool_call(&state, "call_read", "Read", params1);
    cJSON_Delete(params1);

    cJSON *params2 = cJSON_CreateObject();
    cJSON_AddStringToObject(params2, "command", "pwd");
    mock_add_tool_call(&state, "call_bash", "Bash", params2);
    cJSON_Delete(params2);

    // Both tool results
    cJSON *output1 = cJSON_CreateObject();
    cJSON_AddStringToObject(output1, "content", "Contents of file.txt...");
    mock_add_tool_result(&state, "call_read", "Read", output1, 0);
    cJSON_Delete(output1);

    cJSON *output2 = cJSON_CreateObject();
    cJSON_AddStringToObject(output2, "output", "/home/user/project");
    mock_add_tool_result(&state, "call_bash", "Bash", output2, 0);
    cJSON_Delete(output2);

    // Assistant responds
    mock_add_assistant_message(&state, "The file contains... and you're in /home/user/project");

    TEST_ASSERT(state.count == 6, "Conversation has 6 messages (user + 2 tool calls + 2 tool results + assistant)");

    // Switch between Kimi and Z.AI (most used providers in real db)
    int result = switch_provider(&state, &TEST_PROVIDERS[0]);  // Kimi
    TEST_ASSERT(result == 0, "Can switch to Kimi with parallel tools");

    result = switch_provider(&state, &TEST_PROVIDERS[1]);  // Z.AI
    TEST_ASSERT(result == 0, "Can switch to Z.AI with parallel tools");

    int integrity = verify_conversation_integrity(&state);
    TEST_ASSERT(integrity, "Parallel tool integrity maintained");

    cleanup_mock_state(&state);
}

// ============================================================================
// Test 6: Rapid switching between providers
// ============================================================================

static void test_rapid_switching(void) {
    printf("\nTest: Rapid switching between providers\n");

    MockConversationState state;
    init_mock_state(&state);

    // Build a longer conversation
    mock_add_user_message(&state, "Message 1");
    mock_add_assistant_message(&state, "Response 1");
    mock_add_user_message(&state, "Message 2");
    mock_add_assistant_message(&state, "Response 2");
    mock_add_user_message(&state, "Message 3");

    // Rapidly switch between all providers multiple times
    for (int round = 0; round < 3; round++) {
        for (size_t i = 0; i < NUM_TEST_PROVIDERS; i++) {
            int result = switch_provider(&state, &TEST_PROVIDERS[i]);
            TEST_ASSERT(result == 0, "Rapid switch successful");
        }
    }

    TEST_ASSERT(state.count == 5, "Messages preserved after rapid switching");

    cleanup_mock_state(&state);
}

// ============================================================================
// Test 7: Switch after error result
// ============================================================================

static void test_switch_after_error_result(void) {
    printf("\nTest: Switch provider after error tool result\n");

    MockConversationState state;
    init_mock_state(&state);

    mock_add_user_message(&state, "Run invalid command");

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", "invalid_cmd");
    mock_add_tool_call(&state, "call_err", "Bash", params);
    cJSON_Delete(params);

    // Error result
    cJSON *output = cJSON_CreateObject();
    cJSON_AddStringToObject(output, "error", "Command not found");
    mock_add_tool_result(&state, "call_err", "Bash", output, 1);  // is_error = 1
    cJSON_Delete(output);

    mock_add_assistant_message(&state, "That command failed, let me try something else...");

    // Switch providers
    int result = switch_provider(&state, &TEST_PROVIDERS[3]);  // Anthropic
    TEST_ASSERT(result == 0, "Can switch after error result");
    TEST_ASSERT(state.count == 4, "All messages including error preserved");

    // Verify error flag is preserved
    InternalMessage *result_msg = &state.messages[2];
    TEST_ASSERT(result_msg->contents[0].is_error == 1, "Error flag preserved");

    cleanup_mock_state(&state);
}

// ============================================================================
// Test 8: Verify provider-specific API URLs
// ============================================================================

static void test_provider_api_urls(void) {
    printf("\nTest: Verify provider-specific API URLs\n");

    MockConversationState state;
    init_mock_state(&state);

    // Test each provider sets correct API URL
    for (size_t i = 0; i < NUM_TEST_PROVIDERS; i++) {
        int result = switch_provider(&state, &TEST_PROVIDERS[i]);
        TEST_ASSERT(result == 0, "Provider initialized");
        TEST_ASSERT(state.api_url != NULL, "API URL is set");
        TEST_ASSERT(strstr(state.api_url, TEST_PROVIDERS[i].api_base) != NULL ||
                    strstr(TEST_PROVIDERS[i].api_base, state.api_url) != NULL,
                    "API URL matches provider config");
    }

    cleanup_mock_state(&state);
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    printf("=== Model Switch Tests (Interactive Mode) ===\n");
    printf("Testing providers from real_api_calls.db:\n");
    for (size_t i = 0; i < NUM_TEST_PROVIDERS; i++) {
        printf("  - %s (%s)\n", TEST_PROVIDERS[i].name, TEST_PROVIDERS[i].model);
    }

    test_switch_at_empty();
    test_switch_after_user_message();
    test_switch_after_assistant();
    test_switch_with_tool_calls();
    test_switch_with_parallel_tools();
    test_rapid_switching();
    test_switch_after_error_result();
    test_provider_api_urls();

    printf("\n=== Results ===\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
