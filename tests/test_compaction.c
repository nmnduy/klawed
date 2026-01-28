/*
 * Test suite for context compaction functionality
 *
 * Compilation: make test-compaction
 * Usage: ./build/test_compaction
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/klawed_internal.h"
#include "../src/compaction.h"

// Test helper macros
#define TEST(name) printf("\n=== Test: %s ===\n", name)
#define PASS() printf("✓ PASS\n")
#define FAIL(msg) do { printf("✗ FAIL: %s\n", msg); exit(1); } while(0)

// Helper to create a test message
static void create_test_message(InternalMessage *msg, MessageRole role, const char *text) {
    memset(msg, 0, sizeof(InternalMessage));
    msg->role = role;
    msg->contents = malloc(sizeof(InternalContent));
    if (msg->contents) {
        msg->content_count = 1;
        msg->contents[0].type = INTERNAL_TEXT;
        msg->contents[0].text = strdup(text);
    }
}

// Helper to free a test message
static void free_test_message(InternalMessage *msg) {
    if (msg->contents) {
        for (int i = 0; i < msg->content_count; i++) {
            free(msg->contents[i].text);
        }
        free(msg->contents);
    }
    memset(msg, 0, sizeof(InternalMessage));
}

// Helper to create a minimal ConversationState for testing
static ConversationState* create_test_state(int message_count) {
    ConversationState *state = calloc(1, sizeof(ConversationState));
    if (!state) return NULL;

    state->count = message_count;

    // Create system message at position 0
    if (message_count > 0) {
        create_test_message(&state->messages[0], MSG_SYSTEM, "You are a helpful assistant.");
    }

    // Create alternating user/assistant messages
    for (int i = 1; i < message_count; i++) {
        if (i % 2 == 1) {
            char buf[64];
            snprintf(buf, sizeof(buf), "User message %d", i);
            create_test_message(&state->messages[i], MSG_USER, buf);
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "Assistant response %d", i);
            create_test_message(&state->messages[i], MSG_ASSISTANT, buf);
        }
    }

    return state;
}

// Helper to free test state
static void free_test_state(ConversationState *state) {
    if (!state) return;

    for (int i = 0; i < state->count; i++) {
        free_test_message(&state->messages[i]);
    }
    free(state);
}

// ============================================================================
// Tests for compaction_init_config
// ============================================================================

static void test_init_config_defaults(void) {
    TEST("Initialize config with defaults");

    CompactionConfig config;
    compaction_init_config(&config, 1, NULL);

    // When memvid is not available, enabled should be forced to 0
#ifdef HAVE_MEMVID
    assert(config.enabled == 1);
#else
    assert(config.enabled == 0);  // Forced off without memvid
#endif
    assert(config.threshold_percent == 60);  // Default threshold
    assert(config.keep_recent == 20);        // Default keep_recent
    assert(config.last_compacted_index == -1);

    PASS();
}

static void test_init_config_disabled(void) {
    TEST("Initialize config disabled");

    CompactionConfig config;
    compaction_init_config(&config, 0, NULL);

    assert(config.enabled == 0);
    assert(config.threshold_percent == 60);
    assert(config.keep_recent == 20);

    PASS();
}

static void test_init_config_env_override(void) {
    TEST("Initialize config with environment variable overrides");

    // Set environment variables
    setenv("KLAWED_COMPACT_THRESHOLD", "75", 1);
    setenv("KLAWED_COMPACT_KEEP_RECENT", "30", 1);

    CompactionConfig config;
    compaction_init_config(&config, 1, NULL);

    assert(config.threshold_percent == 75);
    assert(config.keep_recent == 30);

    // Clean up
    unsetenv("KLAWED_COMPACT_THRESHOLD");
    unsetenv("KLAWED_COMPACT_KEEP_RECENT");

    PASS();
}

static void test_init_config_invalid_env(void) {
    TEST("Initialize config with invalid environment values");

    // Set invalid (zero/negative) values - should fall back to defaults
    setenv("KLAWED_COMPACT_THRESHOLD", "0", 1);
    setenv("KLAWED_COMPACT_KEEP_RECENT", "-5", 1);

    CompactionConfig config;
    compaction_init_config(&config, 1, NULL);

    // Should use defaults when env values are invalid
    assert(config.threshold_percent == 60);
    assert(config.keep_recent == 20);

    // Clean up
    unsetenv("KLAWED_COMPACT_THRESHOLD");
    unsetenv("KLAWED_COMPACT_KEEP_RECENT");

    PASS();
}

static void test_init_config_null(void) {
    TEST("Initialize config with NULL pointer");

    // Should not crash
    compaction_init_config(NULL, 1, NULL);

    PASS();
}

// ============================================================================
// Tests for compaction_should_trigger
// ============================================================================

static void test_should_trigger_disabled(void) {
    TEST("Should not trigger when disabled");

    CompactionConfig config = {
        .enabled = 0,
        .threshold_percent = 60,
        .keep_recent = 20,
        .last_compacted_index = -1,
        .model_token_limit = 125000,
        .current_tokens = 80000       // Above threshold but disabled
    };

    ConversationState *state = create_test_state(100);
    assert(state != NULL);

    int result = compaction_should_trigger(state, &config);
    assert(result == 0);  // Should not trigger when disabled

    free_test_state(state);
    PASS();
}

static void test_should_trigger_below_threshold(void) {
    TEST("Should not trigger below threshold");

    CompactionConfig config = {
        .enabled = 1,
        .threshold_percent = 60,
        .keep_recent = 20,
        .last_compacted_index = -1,
        .model_token_limit = 125000,  // 125k tokens
        .current_tokens = 10000       // Way below 60% of 125k (75k)
    };

    ConversationState *state = create_test_state(100);
    assert(state != NULL);

    int result = compaction_should_trigger(state, &config);
    // Without HAVE_MEMVID, always returns 0
#ifdef HAVE_MEMVID
    assert(result == 0);  // Below threshold
#else
    assert(result == 0);  // No memvid
#endif

    free_test_state(state);
    PASS();
}

static void test_should_trigger_at_threshold(void) {
    TEST("Should trigger at threshold");

    CompactionConfig config = {
        .enabled = 1,
        .threshold_percent = 60,
        .keep_recent = 20,
        .last_compacted_index = -1,
        .model_token_limit = 125000,  // 125k tokens
        .current_tokens = 80000       // Above 60% of 125k (75k)
    };

    ConversationState *state = create_test_state(100);
    assert(state != NULL);

    int result = compaction_should_trigger(state, &config);
#ifdef HAVE_MEMVID
    assert(result == 1);  // At threshold, should trigger
#else
    assert(result == 0);  // No memvid
#endif

    free_test_state(state);
    PASS();
}

static void test_should_trigger_null_params(void) {
    TEST("Should not trigger with NULL parameters");

    CompactionConfig config = {
        .enabled = 1,
        .threshold_percent = 60,
        .keep_recent = 20,
        .last_compacted_index = -1,
        .model_token_limit = 125000,
        .current_tokens = 80000
    };

    ConversationState *state = create_test_state(100);

    // NULL state
    assert(compaction_should_trigger(NULL, &config) == 0);

    // NULL config
    assert(compaction_should_trigger(state, NULL) == 0);

    // Both NULL
    assert(compaction_should_trigger(NULL, NULL) == 0);

    free_test_state(state);
    PASS();
}

// ============================================================================
// Tests for compaction_perform (only meaningful with HAVE_MEMVID)
// ============================================================================

static void test_perform_without_memvid(void) {
    TEST("Perform compaction without memvid");

#ifndef HAVE_MEMVID
    ConversationState *state = create_test_state(100);
    CompactionConfig config = {
        .enabled = 1,
        .threshold_percent = 60,
        .keep_recent = 20,
        .last_compacted_index = -1,
        .model_token_limit = 125000,
        .current_tokens = 80000
    };

    int result = compaction_perform(state, &config, "test-session", NULL);

    // Should return -1 (error) without memvid
    assert(result == -1);

    free_test_state(state);
#else
    printf("  (skipped - memvid is available)\n");
#endif
    PASS();
}

static void test_perform_null_params(void) {
    TEST("Perform compaction with NULL parameters");

    CompactionConfig config = {
        .enabled = 1,
        .threshold_percent = 60,
        .keep_recent = 20,
        .last_compacted_index = -1,
        .model_token_limit = 125000,
        .current_tokens = 80000
    };

    ConversationState *state = create_test_state(100);

    // NULL state
    int result = compaction_perform(NULL, &config, "test-session", NULL);
    assert(result == -1);

    // NULL config
    result = compaction_perform(state, NULL, "test-session", NULL);
    assert(result == -1);

    free_test_state(state);
    PASS();
}

static void test_perform_not_enough_messages(void) {
    TEST("Perform compaction with not enough messages");

    CompactionConfig config = {
        .enabled = 1,
        .threshold_percent = 60,
        .keep_recent = 20,
        .last_compacted_index = -1,
        .model_token_limit = 125000,
        .current_tokens = 80000
    };

    // Only 10 messages - less than keep_recent + 1 (system)
    ConversationState *state = create_test_state(10);
    assert(state != NULL);

    int original_count = state->count;

#ifdef HAVE_MEMVID
    int result = compaction_perform(state, &config, "test-session", NULL);
    // Should return 0 (success but no action) when not enough messages
    assert(result == 0);
    assert(state->count == original_count);  // Count unchanged
#else
    int result = compaction_perform(state, &config, "test-session", NULL);
    assert(result == -1);  // Error without memvid
    (void)original_count;
#endif

    free_test_state(state);
    PASS();
}

// ============================================================================
// Main test runner
// ============================================================================

int main(void) {
    printf("======================================\n");
    printf("Compaction Module Test Suite\n");
    printf("======================================\n");

#ifdef HAVE_MEMVID
    printf("HAVE_MEMVID: defined\n");
#else
    printf("HAVE_MEMVID: not defined\n");
#endif

    // Config initialization tests
    test_init_config_defaults();
    test_init_config_disabled();
    test_init_config_env_override();
    test_init_config_invalid_env();
    test_init_config_null();

    // Should trigger tests
    test_should_trigger_disabled();
    test_should_trigger_below_threshold();
    test_should_trigger_at_threshold();
    test_should_trigger_null_params();

    // Perform tests
    test_perform_without_memvid();
    test_perform_null_params();
    test_perform_not_enough_messages();

    printf("\n======================================\n");
    printf("All tests passed!\n");
    printf("======================================\n");

    return 0;
}
