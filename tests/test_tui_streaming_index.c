/*
 * Test suite for TUI Streaming Index tracking functionality
 *
 * Tests the fix for the bug where typing while AI is streaming would
 * incorrectly attribute AI response to the user.
 *
 * The fix adds streaming_entry_index to track which entry is being
 * streamed to, so STREAM_APPEND updates the correct entry even if
 * new entries are added after streaming starts.
 *
 * Tests:
 * - STREAM_START records the correct entry index
 * - STREAM_APPEND updates the tracked entry
 * - User typing during streaming doesn't hijack the stream
 * - Multiple consecutive streaming sessions
 * - Edge cases (invalid index, clear during streaming, etc.)
 *
 * Compilation: make test-tui-streaming-index
 * Usage: ./build/test_tui_streaming_index
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Minimal structures for testing (mirror actual structures)
typedef enum {
    COLOR_PAIR_DEFAULT = 1,
    COLOR_PAIR_USER = 3,
    COLOR_PAIR_ASSISTANT = 4,
    COLOR_PAIR_TOOL = 5
} TUIColorPair;

typedef struct {
    char *prefix;
    char *text;
    TUIColorPair color_pair;
} ConversationEntry;

typedef struct TUIStateStruct {
    ConversationEntry *entries;
    int entries_count;
    int entries_capacity;
    int streaming_entry_index;  // The key field we're testing
    int last_assistant_line;
} TUIState;

// Test counters
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// Test framework macros
#define TEST(name) printf("\n=== Test: %s ===\n", name)
#define PASS() do { tests_passed++; printf("✓ PASS\n"); } while(0)
#define FAIL(msg) do { \
    tests_failed++; \
    printf("✗ FAIL: %s\n", msg); \
} while(0)
#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { FAIL(msg); } else { PASS(); } \
} while(0)
#define ASSERT_INT_EQ(a, b, msg) do { \
    tests_run++; \
    if ((a) != (b)) { \
        printf("  Expected: %d\n", (b)); \
        printf("  Actual:   %d\n", (a)); \
        FAIL(msg); \
    } else { PASS(); } \
} while(0)
#define ASSERT_STR_EQ(a, b, msg) do { \
    tests_run++; \
    if (strcmp((a), (b)) != 0) { \
        printf("  Expected: '%s'\n", (b)); \
        printf("  Actual:   '%s'\n", (a)); \
        FAIL(msg); \
    } else { PASS(); } \
} while(0)

// Helper: Initialize TUI state
static void tui_init_test(TUIState *tui) {
    memset(tui, 0, sizeof(TUIState));
    tui->streaming_entry_index = -1;
    tui->last_assistant_line = -1;
}

// Helper: Free TUI state
static void tui_cleanup_test(TUIState *tui) {
    if (!tui) return;
    for (int i = 0; i < tui->entries_count; i++) {
        free(tui->entries[i].prefix);
        free(tui->entries[i].text);
    }
    free(tui->entries);
    memset(tui, 0, sizeof(TUIState));
}

// Helper: Add conversation entry (simplified from actual implementation)
static int add_conversation_entry(TUIState *tui, const char *prefix,
                                   const char *text, TUIColorPair color) {
    if (tui->entries_count >= tui->entries_capacity) {
        int new_cap = tui->entries_capacity == 0 ? 16 : tui->entries_capacity * 2;
        ConversationEntry *new_entries = realloc(tui->entries,
                                                  (size_t)new_cap * sizeof(ConversationEntry));
        if (!new_entries) return -1;
        tui->entries = new_entries;
        tui->entries_capacity = new_cap;
    }

    ConversationEntry *entry = &tui->entries[tui->entries_count];
    entry->prefix = prefix ? strdup(prefix) : NULL;
    entry->text = text ? strdup(text) : NULL;
    entry->color_pair = color;

    if ((prefix && !entry->prefix) || (text && !entry->text)) {
        free(entry->prefix);
        free(entry->text);
        return -1;
    }

    tui->entries_count++;
    return 0;
}

// Helper: Simulate TUI_MSG_STREAM_START
static void stream_start(TUIState *tui, const char *prefix, TUIColorPair color) {
    add_conversation_entry(tui, prefix, "", color);
    tui->streaming_entry_index = tui->entries_count - 1;
}

// Helper: Simulate TUI_MSG_STREAM_APPEND (with index tracking)
static void stream_append_tracked(TUIState *tui, const char *text) {
    if (tui->streaming_entry_index < 0 ||
        tui->streaming_entry_index >= tui->entries_count) {
        // Invalid index - would log warning in real implementation
        return;
    }

    ConversationEntry *entry = &tui->entries[tui->streaming_entry_index];
    size_t old_len = entry->text ? strlen(entry->text) : 0;
    size_t new_len = strlen(text);
    char *new_text = realloc(entry->text, old_len + new_len + 1);
    if (new_text) {
        if (old_len == 0) new_text[0] = '\0';
        strcat(new_text, text);
        entry->text = new_text;
    }
}

// Helper: Simulate user adding a message
static void add_user_message(TUIState *tui, const char *text) {
    add_conversation_entry(tui, "[User]", text, COLOR_PAIR_USER);
}

// Helper: Simulate clear conversation
static void clear_conversation(TUIState *tui) {
    for (int i = 0; i < tui->entries_count; i++) {
        free(tui->entries[i].prefix);
        free(tui->entries[i].text);
    }
    free(tui->entries);
    tui->entries = NULL;
    tui->entries_count = 0;
    tui->entries_capacity = 0;
    tui->streaming_entry_index = -1;
    tui->last_assistant_line = -1;
}

// ============================================================================
// Tests
// ============================================================================

static void test_stream_start_records_index(void) {
    TEST("STREAM_START records correct entry index");

    TUIState tui;
    tui_init_test(&tui);

    // Initially no streaming entry
    ASSERT_INT_EQ(tui.streaming_entry_index, -1, "Initial index should be -1");

    // Add a user message first (index 0)
    add_user_message(&tui, "Hello");
    ASSERT_INT_EQ(tui.entries_count, 1, "Should have 1 entry");

    // Start streaming (should be index 1)
    stream_start(&tui, "[Assistant]", COLOR_PAIR_ASSISTANT);
    ASSERT_INT_EQ(tui.entries_count, 2, "Should have 2 entries");
    ASSERT_INT_EQ(tui.streaming_entry_index, 1, "Streaming index should be 1");

    tui_cleanup_test(&tui);
}

static void test_stream_append_updates_tracked_entry(void) {
    TEST("STREAM_APPEND updates the tracked entry");

    TUIState tui;
    tui_init_test(&tui);

    stream_start(&tui, "[Assistant]", COLOR_PAIR_ASSISTANT);

    // Append some text
    stream_append_tracked(&tui, "Hello");
    ASSERT_STR_EQ(tui.entries[0].text, "Hello", "Entry should contain 'Hello'");

    // Append more text
    stream_append_tracked(&tui, " World");
    ASSERT_STR_EQ(tui.entries[0].text, "Hello World", "Entry should contain 'Hello World'");

    tui_cleanup_test(&tui);
}

static void test_user_typing_during_streaming(void) {
    TEST("User typing during streaming doesn't hijack stream");

    TUIState tui;
    tui_init_test(&tui);

    // AI starts streaming
    stream_start(&tui, "[Assistant]", COLOR_PAIR_ASSISTANT);
    stream_append_tracked(&tui, "AI is");

    // User types and submits (simulating typing during streaming)
    add_user_message(&tui, "User message during streaming");

    // AI continues streaming - should go to AI's entry, not user's
    stream_append_tracked(&tui, " responding");

    // Check AI entry (index 0)
    ASSERT_STR_EQ(tui.entries[0].text, "AI is responding",
                  "AI entry should have full text");

    // Check user entry (index 1) - should be unchanged
    ASSERT_STR_EQ(tui.entries[1].text, "User message during streaming",
                  "User entry should be unchanged");

    // streaming_entry_index should still point to AI entry
    ASSERT_INT_EQ(tui.streaming_entry_index, 0,
                  "Streaming index should still be 0 (AI entry)");

    tui_cleanup_test(&tui);
}

static void test_multiple_user_messages_during_streaming(void) {
    TEST("Multiple user messages during streaming");

    TUIState tui;
    tui_init_test(&tui);

    // AI starts streaming
    stream_start(&tui, "[Assistant]", COLOR_PAIR_ASSISTANT);
    stream_append_tracked(&tui, "Start");

    // Multiple user messages
    add_user_message(&tui, "User msg 1");
    add_user_message(&tui, "User msg 2");
    add_user_message(&tui, "User msg 3");

    // AI continues
    stream_append_tracked(&tui, " Middle");

    // More user messages
    add_user_message(&tui, "User msg 4");

    // AI finishes
    stream_append_tracked(&tui, " End");

    // Verify AI entry
    ASSERT_STR_EQ(tui.entries[0].text, "Start Middle End",
                  "AI entry should have complete text");

    // Verify user entries are unchanged
    ASSERT_STR_EQ(tui.entries[1].text, "User msg 1", "User 1 unchanged");
    ASSERT_STR_EQ(tui.entries[2].text, "User msg 2", "User 2 unchanged");
    ASSERT_STR_EQ(tui.entries[3].text, "User msg 3", "User 3 unchanged");
    ASSERT_STR_EQ(tui.entries[4].text, "User msg 4", "User 4 unchanged");

    tui_cleanup_test(&tui);
}

static void test_multiple_streaming_sessions(void) {
    TEST("Multiple consecutive streaming sessions");

    TUIState tui;
    tui_init_test(&tui);

    // First AI response
    stream_start(&tui, "[Assistant]", COLOR_PAIR_ASSISTANT);
    stream_append_tracked(&tui, "First response");
    ASSERT_INT_EQ(tui.streaming_entry_index, 0, "First stream index should be 0");

    // User message
    add_user_message(&tui, "User question");

    // Second AI response
    stream_start(&tui, "[Assistant]", COLOR_PAIR_ASSISTANT);
    ASSERT_INT_EQ(tui.streaming_entry_index, 2, "Second stream index should be 2");
    stream_append_tracked(&tui, "Second response");

    // User message during second stream
    add_user_message(&tui, "Another question");

    // Second AI continues
    stream_append_tracked(&tui, " continued");

    // Verify both AI responses are correct
    ASSERT_STR_EQ(tui.entries[0].text, "First response", "First AI response correct");
    ASSERT_STR_EQ(tui.entries[2].text, "Second response continued",
                  "Second AI response correct");

    tui_cleanup_test(&tui);
}

static void test_clear_during_streaming(void) {
    TEST("Clear conversation during streaming resets index");

    TUIState tui;
    tui_init_test(&tui);

    // Start streaming
    stream_start(&tui, "[Assistant]", COLOR_PAIR_ASSISTANT);
    stream_append_tracked(&tui, "Partial");
    ASSERT_INT_EQ(tui.streaming_entry_index, 0, "Index should be 0");

    // Clear conversation
    clear_conversation(&tui);

    // Index should be reset
    ASSERT_INT_EQ(tui.streaming_entry_index, -1, "Index should be -1 after clear");
    ASSERT_INT_EQ(tui.entries_count, 0, "Entries should be empty");

    tui_cleanup_test(&tui);
}

static void test_invalid_index_handling(void) {
    TEST("Invalid index handling (bounds checking)");

    TUIState tui;
    tui_init_test(&tui);

    // Set an invalid index (out of bounds)
    tui.streaming_entry_index = 5;  // No entries yet

    // Try to append - should not crash
    stream_append_tracked(&tui, "Text");

    // No entries should be created
    ASSERT_INT_EQ(tui.entries_count, 0, "No entries should be created");

    // Now add an entry and test negative index
    add_user_message(&tui, "User");
    tui.streaming_entry_index = -1;

    // Try to append with negative index - should not crash
    stream_append_tracked(&tui, "Text");

    // User entry should be unchanged
    ASSERT_STR_EQ(tui.entries[0].text, "User", "User entry unchanged");

    tui_cleanup_test(&tui);
}

static void test_streaming_with_reasoning(void) {
    TEST("Streaming with reasoning content");

    TUIState tui;
    tui_init_test(&tui);

    // Reasoning stream
    stream_start(&tui, "⟨Reasoning⟩", COLOR_PAIR_TOOL);
    stream_append_tracked(&tui, "Thinking...");
    ASSERT_INT_EQ(tui.streaming_entry_index, 0, "Reasoning index should be 0");

    // Assistant response stream
    stream_start(&tui, "[Assistant]", COLOR_PAIR_ASSISTANT);
    stream_append_tracked(&tui, "Response");
    ASSERT_INT_EQ(tui.streaming_entry_index, 1, "Assistant index should be 1");

    // User message
    add_user_message(&tui, "Thanks");

    // Try to append to assistant stream (should work)
    stream_append_tracked(&tui, " continued");
    ASSERT_STR_EQ(tui.entries[1].text, "Response continued",
                  "Assistant text should be updated");

    // Reasoning entry should be unchanged
    ASSERT_STR_EQ(tui.entries[0].text, "Thinking...",
                  "Reasoning text should be unchanged");

    tui_cleanup_test(&tui);
}

static void test_array_reallocation_during_streaming(void) {
    TEST("Array reallocation during streaming");

    TUIState tui;
    tui_init_test(&tui);

    // Start with small capacity, start streaming
    stream_start(&tui, "[Assistant]", COLOR_PAIR_ASSISTANT);
    int first_stream_index = tui.streaming_entry_index;

    // Add many entries to force reallocation
    for (int i = 0; i < 50; i++) {
        add_user_message(&tui, "User message");
    }

    // Continue streaming - index should still work
    stream_append_tracked(&tui, "Text after many users");

    // Check that streaming index still points to correct entry
    ASSERT_INT_EQ(tui.streaming_entry_index, first_stream_index,
                  "Streaming index should still be valid");
    ASSERT_STR_EQ(tui.entries[first_stream_index].text, "Text after many users",
                  "First entry should have the text");

    tui_cleanup_test(&tui);
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    printf("\n========================================\n");
    printf("TUI Streaming Index Test Suite\n");
    printf("========================================\n");
    printf("Testing fix for: typing while streaming\n");
    printf("incorrectly attributes AI response to user\n");

    // Basic functionality
    test_stream_start_records_index();
    test_stream_append_updates_tracked_entry();

    // The main bug fix tests
    test_user_typing_during_streaming();
    test_multiple_user_messages_during_streaming();
    test_multiple_streaming_sessions();

    // Edge cases
    test_clear_during_streaming();
    test_invalid_index_handling();
    test_streaming_with_reasoning();
    test_array_reallocation_during_streaming();

    printf("\n========================================\n");
    printf("Test Results: %d passed, %d failed (total: %d)\n",
           tests_passed, tests_failed, tests_run);
    if (tests_failed == 0) {
        printf("✓ All tests passed!\n");
    } else {
        printf("✗ Some tests failed!\n");
    }
    printf("========================================\n\n");

    return tests_failed > 0 ? 1 : 0;
}
