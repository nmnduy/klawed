/*
 * Test suite for TUI Reasoning Message Detection
 *
 * Tests the centralized tui_conversation_is_reasoning_message() function
 * that identifies reasoning/thinking message prefixes across both Nerd Font
 * and ASCII fallback variants.
 *
 * This test was added to prevent regressions like the one fixed in commit
 * acc7c557 where the resize handler in tui_window.c used a stale hardcoded
 * byte sequence (0xE2 0x9F 0xA8 / U+27E8 mathematical angle bracket) that
 * didn't match any actual reasoning icon.
 *
 * Tests:
 * - Nerd Font reasoning icons ( / )
 * - ASCII fallback icons ("<Reasoning >>>" / "<<< Reasoning>")
 * - Non-reasoning icons not falsely detected (assistant, user, tool)
 * - NULL and empty string boundary cases
 *
 * Compilation: make test-tui-reasoning-detection
 * Usage: ./build/test_tui_reasoning_detection
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ------------------------------------------------------------------ */
/* Test-controlled stub for tui_is_nerd_font_enabled                   */
/* ------------------------------------------------------------------ */

static int g_nerd_font_forced = 0;  /* 0=unforced, 1=forced on, -1=forced off */
static int g_nerd_font_value = 0;

static int tui_is_nerd_font_enabled(void) {
    if (g_nerd_font_forced) {
        return g_nerd_font_value > 0;
    }
    /* Default: Nerd Font enabled (matches normal runtime default) */
    return 1;
}

static void test_set_nerd_font(int enabled) {
    g_nerd_font_forced = 1;
    g_nerd_font_value = enabled ? 1 : -1;
}

static void test_clear_nerd_font(void) {
    g_nerd_font_forced = 0;
    g_nerd_font_value = 0;
}

/* ------------------------------------------------------------------ */
/* Icon functions (identical to src/tui.c implementations)             */
/* ------------------------------------------------------------------ */

static const char* tui_icon_reasoning_open(void) {
    return tui_is_nerd_font_enabled() ? "\xef\x83\xab" : "<Reasoning >>>"; /*  */
}

static const char* tui_icon_reasoning_close(void) {
    return tui_is_nerd_font_enabled() ? "\xef\x80\x8c" : "<<< Reasoning>"; /*  */
}

static const char* tui_icon_assistant(void) {
    return tui_is_nerd_font_enabled() ? "\xef\x81\xb5" : "[Assistant]"; /*  */
}

static const char* tui_icon_user(void) {
    return tui_is_nerd_font_enabled() ? "\xef\x80\x87" : "[User]"; /*  */
}

static const char* tui_icon_tool(void) {
    return tui_is_nerd_font_enabled() ? "\xef\x82\xad" : "\xe2\x97\x8f"; /*  or ● */
}

/* ------------------------------------------------------------------ */
/* Function under test (identical to src/tui_conversation.c)            */
/* ------------------------------------------------------------------ */

static int tui_conversation_is_reasoning_message(const char *prefix) {
    if (!prefix || prefix[0] == '\0') {
        return 0;
    }

    return (strcmp(prefix, tui_icon_reasoning_open()) == 0 ||
            strcmp(prefix, tui_icon_reasoning_close()) == 0);
}

/* ------------------------------------------------------------------ */
/* Test helpers                                                        */
/* ------------------------------------------------------------------ */

static int tests_passed = 0;
static int tests_failed = 0;
static const char *current_test = NULL;

static void test_start(const char *name) {
    current_test = name;
}

static void test_ok(void) {
    tests_passed++;
}

static void test_fail(const char *msg) {
    printf("FAIL: %s - %s\n", current_test, msg);
    tests_failed++;
}

#define CHECK(cond, msg) do { \
    if (cond) { test_ok(); } \
    else { test_fail(msg); } \
} while(0)

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

static void test_nerd_font_open(void) {
    test_start("Nerd Font reasoning open icon detected");
    test_set_nerd_font(1);
    const char *icon = tui_icon_reasoning_open();
    CHECK(tui_conversation_is_reasoning_message(icon) == 1,
          "Nerd Font open icon should be detected as reasoning");
    test_clear_nerd_font();
}

static void test_nerd_font_close(void) {
    test_start("Nerd Font reasoning close icon detected");
    test_set_nerd_font(1);
    const char *icon = tui_icon_reasoning_close();
    CHECK(tui_conversation_is_reasoning_message(icon) == 1,
          "Nerd Font close icon should be detected as reasoning");
    test_clear_nerd_font();
}

static void test_ascii_open(void) {
    test_start("ASCII reasoning open text detected");
    test_set_nerd_font(0);
    const char *icon = tui_icon_reasoning_open();
    CHECK(strcmp(icon, "<Reasoning >>>") == 0,
          "ASCII open should be '<Reasoning >>>'");
    CHECK(tui_conversation_is_reasoning_message(icon) == 1,
          "ASCII open text should be detected as reasoning");
    test_clear_nerd_font();
}

static void test_ascii_close(void) {
    test_start("ASCII reasoning close text detected");
    test_set_nerd_font(0);
    const char *icon = tui_icon_reasoning_close();
    CHECK(strcmp(icon, "<<< Reasoning>") == 0,
          "ASCII close should be '<<< Reasoning>'");
    CHECK(tui_conversation_is_reasoning_message(icon) == 1,
          "ASCII close text should be detected as reasoning");
    test_clear_nerd_font();
}

static void test_assistant_not_reasoning(void) {
    test_start("Assistant icon NOT detected as reasoning (Nerd Font)");
    test_set_nerd_font(1);
    CHECK(tui_conversation_is_reasoning_message(tui_icon_assistant()) == 0,
          "Assistant Nerd Font icon should not be reasoning");
    test_clear_nerd_font();
}

static void test_user_not_reasoning(void) {
    test_start("User icon NOT detected as reasoning (Nerd Font)");
    test_set_nerd_font(1);
    CHECK(tui_conversation_is_reasoning_message(tui_icon_user()) == 0,
          "User Nerd Font icon should not be reasoning");
    test_clear_nerd_font();
}

static void test_tool_not_reasoning(void) {
    test_start("Tool icon NOT detected as reasoning (Nerd Font)");
    test_set_nerd_font(1);
    CHECK(tui_conversation_is_reasoning_message(tui_icon_tool()) == 0,
          "Tool Nerd Font icon should not be reasoning");
    test_clear_nerd_font();
}

static void test_tool_not_reasoning_ascii(void) {
    test_start("Tool icon NOT detected as reasoning (ASCII fallback)");
    test_set_nerd_font(0);
    const char *tool_icon = tui_icon_tool(); /* ● */
    CHECK(tui_conversation_is_reasoning_message(tool_icon) == 0,
          "Tool ASCII icon (●) should not be reasoning");
    test_clear_nerd_font();
}

static void test_null_prefix(void) {
    test_start("NULL prefix returns 0");
    CHECK(tui_conversation_is_reasoning_message(NULL) == 0,
          "NULL prefix should return 0 (not reasoning)");
}

static void test_empty_prefix(void) {
    test_start("Empty prefix returns 0");
    CHECK(tui_conversation_is_reasoning_message("") == 0,
          "Empty string prefix should return 0 (not reasoning)");
}

static void test_arbitrary_text_not_reasoning(void) {
    test_start("Arbitrary text NOT detected as reasoning");
    CHECK(tui_conversation_is_reasoning_message("hello world") == 0,
          "Random text should not be reasoning");
    CHECK(tui_conversation_is_reasoning_message(">>>") == 0,
          "Angle brackets alone should not be reasoning");
    CHECK(tui_conversation_is_reasoning_message("[Assistant]") == 0,
          "ASCII assistant should not be reasoning");
}

/* ------------------------------------------------------------------ */
/* The specific regression: stale byte sequence from tui_window.c      */
/* ------------------------------------------------------------------ */

static void test_stale_byte_sequence_not_reasoning(void) {
    /*
     * The old tui_window.c code checked for 0xE2 0x9F 0xA8 (U+27E8,
     * Mathematical Left Angle Bracket '⟨'). This sequence does NOT
     * match any current icon, so it should never be detected as
     * reasoning. This test proves the old check was meaningless
     * and that the new centralized function correctly ignores it.
     */
    test_start("Stale byte sequence (0xE2 0x9F 0xA8) NOT detected as reasoning");
    const char *stale = "\xe2\x9f\xa8"; /* U+27E8 '⟨' */
    CHECK(tui_conversation_is_reasoning_message(stale) == 0,
          "Stale byte sequence U+27E8 should not be reasoning (it never matched any icon)");
}

/* ------------------------------------------------------------------ */
/* Cross-variant consistency: both variants return the same answer     */
/* ------------------------------------------------------------------ */

static void test_cross_variant_consistency(void) {
    /*
     * Regardless of Nerd Font setting, the function should correctly
     * identify reasoning icons returned by tui_icon_reasoning_open/close.
     */
    test_start("Cross-variant consistency (both Nerd Font and ASCII)");

    test_set_nerd_font(1);
    const char *nf_open = tui_icon_reasoning_open();
    const char *nf_close = tui_icon_reasoning_close();
    int nf_open_ok = tui_conversation_is_reasoning_message(nf_open);
    int nf_close_ok = tui_conversation_is_reasoning_message(nf_close);

    test_set_nerd_font(0);
    const char *ascii_open = tui_icon_reasoning_open();
    const char *ascii_close = tui_icon_reasoning_close();
    int ascii_open_ok = tui_conversation_is_reasoning_message(ascii_open);
    int ascii_close_ok = tui_conversation_is_reasoning_message(ascii_close);

    test_clear_nerd_font();

    CHECK(nf_open_ok == 1, "Nerd Font open should be detected");
    CHECK(nf_close_ok == 1, "Nerd Font close should be detected");
    CHECK(ascii_open_ok == 1, "ASCII open should be detected");
    CHECK(ascii_close_ok == 1, "ASCII close should be detected");
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== TUI Reasoning Detection Tests ===\n\n");

    test_nerd_font_open();
    test_nerd_font_close();
    test_ascii_open();
    test_ascii_close();
    test_assistant_not_reasoning();
    test_user_not_reasoning();
    test_tool_not_reasoning();
    test_tool_not_reasoning_ascii();
    test_null_prefix();
    test_empty_prefix();
    test_arbitrary_text_not_reasoning();
    test_stale_byte_sequence_not_reasoning();
    test_cross_variant_consistency();

    printf("\n---\n");
    printf("Passed: %d, Failed: %d\n", tests_passed, tests_failed);

    if (tests_failed > 0) {
        printf("❌ Some tests FAILED\n");
        return 1;
    }

    printf("✅ All tests passed\n");
    return 0;
}
