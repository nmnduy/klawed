/*
 * Unit tests for text wrapping logic used in TUI response rendering
 *
 * Tests the find_wrap_point() function which finds the byte position
 * where text should wrap to fit within a given display width,
 * properly handling UTF-8 multi-byte characters.
 */

#define _XOPEN_SOURCE 700  // For wcwidth

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <wchar.h>
#include <stddef.h>

// Test counters
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_EQ(expected, actual, msg) do { \
    tests_run++; \
    if ((expected) == (actual)) { \
        printf("✓ PASS %s\n", msg); \
        tests_passed++; \
    } else { \
        printf("✗ FAIL %s (expected %zu, got %zu)\n", msg, (size_t)(expected), (size_t)(actual)); \
        tests_failed++; \
    } \
} while(0)

// Copy of find_wrap_point from tui_render.c for testing
// This function finds the byte position where text should wrap to fit
// within max_display_width columns
static size_t find_wrap_point(const char *text, size_t text_len, int max_display_width) {
    if (max_display_width <= 0) {
        return 1;  // At least one byte to make progress
    }

    // Save current locale
    char *old_locale = setlocale(LC_ALL, NULL);
    if (old_locale) {
        old_locale = strdup(old_locale);
    }
    setlocale(LC_ALL, "C.UTF-8");

    size_t bytes_used = 0;
    int display_width = 0;
    mbstate_t state;
    memset(&state, 0, sizeof(state));

    while (bytes_used < text_len && display_width < max_display_width) {
        wchar_t wc;
        size_t char_bytes = mbrtowc(&wc, text + bytes_used, text_len - bytes_used, &state);

        if (char_bytes == 0) {
            // Null character
            break;
        } else if (char_bytes == (size_t)-1 || char_bytes == (size_t)-2) {
            // Invalid sequence or incomplete - treat as single byte
            bytes_used++;
            display_width++;
        } else {
            int char_width = wcwidth(wc);
            if (char_width < 0) char_width = 1;  // Unknown character

            if (display_width + char_width > max_display_width) {
                // This character would exceed the limit
                break;
            }
            bytes_used += char_bytes;
            display_width += char_width;
        }
    }

    // Restore locale
    if (old_locale) {
        setlocale(LC_ALL, old_locale);
        free(old_locale);
    }

    // Ensure we make progress (at least 1 byte)
    return bytes_used > 0 ? bytes_used : 1;
}

// Test basic ASCII text wrapping
static void test_ascii_wrap(void) {
    printf("\n=== ASCII Text Wrapping Tests ===\n");

    // "Hello World" - 11 chars
    const char *text = "Hello World";
    size_t len = strlen(text);

    // Width of 5: should fit "Hello"
    ASSERT_EQ(5, find_wrap_point(text, len, 5), "ASCII: width=5 fits 'Hello'");

    // Width of 11: should fit entire string
    ASSERT_EQ(11, find_wrap_point(text, len, 11), "ASCII: width=11 fits entire string");

    // Width of 20: should fit entire string (more space than needed)
    ASSERT_EQ(11, find_wrap_point(text, len, 20), "ASCII: width=20 fits entire string");

    // Width of 1: should fit 1 char
    ASSERT_EQ(1, find_wrap_point(text, len, 1), "ASCII: width=1 fits 1 char");

    // Width of 0: should return 1 (minimum progress)
    ASSERT_EQ(1, find_wrap_point(text, len, 0), "ASCII: width=0 returns 1 for progress");

    // Negative width: should return 1
    ASSERT_EQ(1, find_wrap_point(text, len, -5), "ASCII: width=-5 returns 1 for progress");
}

// Test UTF-8 multi-byte character wrapping
static void test_utf8_wrap(void) {
    printf("\n=== UTF-8 Text Wrapping Tests ===\n");

    // "café" - 4 display chars, but 'é' is 2 bytes (5 bytes total)
    const char *cafe = "caf\xc3\xa9";  // café in UTF-8
    size_t cafe_len = strlen(cafe);

    // Width of 4: should fit entire "café" (5 bytes, 4 display chars)
    ASSERT_EQ(5, find_wrap_point(cafe, cafe_len, 4), "UTF-8: width=4 fits 'café' (5 bytes)");

    // Width of 3: should fit "caf" (3 bytes, 3 display chars)
    ASSERT_EQ(3, find_wrap_point(cafe, cafe_len, 3), "UTF-8: width=3 fits 'caf' (3 bytes)");

    // Width of 2: should fit "ca"
    ASSERT_EQ(2, find_wrap_point(cafe, cafe_len, 2), "UTF-8: width=2 fits 'ca'");

    // Japanese text: "日本語" - each character is 3 bytes and 2 display columns
    // 日 = \xe6\x97\xa5
    // 本 = \xe6\x9c\xac
    // 語 = \xe8\xaa\x9e
    const char *japanese = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";  // 日本語
    size_t jp_len = strlen(japanese);

    // Width of 6: should fit all 3 chars (9 bytes, 6 display columns)
    ASSERT_EQ(9, find_wrap_point(japanese, jp_len, 6), "UTF-8: width=6 fits '日本語' (9 bytes)");

    // Width of 4: should fit 2 chars (6 bytes, 4 display columns)
    ASSERT_EQ(6, find_wrap_point(japanese, jp_len, 4), "UTF-8: width=4 fits '日本' (6 bytes)");

    // Width of 2: should fit 1 char (3 bytes, 2 display columns)
    ASSERT_EQ(3, find_wrap_point(japanese, jp_len, 2), "UTF-8: width=2 fits '日' (3 bytes)");

    // Width of 1: wide char needs 2 columns, so can't fit - should return 1 byte
    // But since 1 byte isn't a complete character, behavior may vary
    // Actually, the function should NOT split a multi-byte char in the middle
    // Since width=1 can't fit a 2-column char, it should return 0 bytes, then fallback to 1
    // Actually looking at the code: it returns bytes_used which would be 0, then fallback is 1
    size_t result = find_wrap_point(japanese, jp_len, 1);
    if (result == 1) {
        printf("✓ PASS UTF-8: width=1 can't fit wide char, returns 1 for progress\n");
        tests_passed++;
    } else if (result == 3) {
        // Acceptable: fits the character even though it exceeds width
        printf("✓ PASS UTF-8: width=1 includes full wide char (3 bytes)\n");
        tests_passed++;
    } else {
        printf("✗ FAIL UTF-8: width=1 unexpected result %zu\n", result);
        tests_failed++;
    }
    tests_run++;
}

// Test mixed ASCII and UTF-8
static void test_mixed_wrap(void) {
    printf("\n=== Mixed ASCII/UTF-8 Wrapping Tests ===\n");

    // "Hello 日本" - "Hello " is 6 chars, "日" is 3 bytes/2 cols, "本" is 3 bytes/2 cols
    // Total: 6 ASCII + 6 UTF-8 bytes = 12 bytes, 6 + 4 = 10 display columns
    const char *mixed = "Hello \xe6\x97\xa5\xe6\x9c\xac";
    size_t mixed_len = strlen(mixed);

    // Width of 10: should fit entire string
    ASSERT_EQ(12, find_wrap_point(mixed, mixed_len, 10), "Mixed: width=10 fits entire string (12 bytes)");

    // Width of 8: should fit "Hello " + "日" (6 + 3 = 9 bytes, 6 + 2 = 8 cols)
    ASSERT_EQ(9, find_wrap_point(mixed, mixed_len, 8), "Mixed: width=8 fits 'Hello 日' (9 bytes)");

    // Width of 6: should fit "Hello " only (6 bytes, 6 cols)
    ASSERT_EQ(6, find_wrap_point(mixed, mixed_len, 6), "Mixed: width=6 fits 'Hello ' (6 bytes)");

    // Width of 7: should fit "Hello " + can't fit "日" (needs 2 cols)
    // So should return 6 bytes
    ASSERT_EQ(6, find_wrap_point(mixed, mixed_len, 7), "Mixed: width=7 fits 'Hello ' (can't fit 2-col char)");
}

// Test edge cases
static void test_edge_cases(void) {
    printf("\n=== Edge Case Tests ===\n");

    // Empty string
    const char *empty = "";
    ASSERT_EQ(1, find_wrap_point(empty, 0, 10), "Empty string returns 1 for progress");

    // Single character
    const char *single = "X";
    ASSERT_EQ(1, find_wrap_point(single, 1, 10), "Single char fits in width=10");
    ASSERT_EQ(1, find_wrap_point(single, 1, 1), "Single char fits in width=1");

    // Long ASCII string
    const char *long_text = "This is a very long line of text that should be wrapped at various points";
    size_t long_len = strlen(long_text);

    ASSERT_EQ(20, find_wrap_point(long_text, long_len, 20), "Long text: width=20");
    ASSERT_EQ(40, find_wrap_point(long_text, long_len, 40), "Long text: width=40");
    ASSERT_EQ(long_len, find_wrap_point(long_text, long_len, 100), "Long text: width=100 fits all");

    // String with embedded newlines (should not affect byte counting)
    const char *with_newline = "Line1\nLine2";
    size_t nl_len = strlen(with_newline);
    ASSERT_EQ(5, find_wrap_point(with_newline, nl_len, 5), "String with newline: width=5");
    ASSERT_EQ(11, find_wrap_point(with_newline, nl_len, 20), "String with newline: width=20 fits all");
}

// Test the border string width calculation
static void test_border_width(void) {
    printf("\n=== Border Width Tests ===\n");

    // The border "│ " uses the box-drawing character │ (U+2502)
    // which is 3 bytes in UTF-8 and typically 1 display column
    const char *border = "\xe2\x94\x82 ";  // │ followed by space
    size_t border_len = strlen(border);

    // This should be 4 bytes (3 for │, 1 for space)
    tests_run++;
    if (border_len == 4) {
        printf("✓ PASS Border string is 4 bytes\n");
        tests_passed++;
    } else {
        printf("✗ FAIL Border string expected 4 bytes, got %zu\n", border_len);
        tests_failed++;
    }

    // Display width should be 2 (1 for │, 1 for space)
    // We can test this indirectly by checking that text after a 2-col border
    // fits correctly

    // If pad width is 80 and border is 2 cols, content width should be 78
    // A 78-char ASCII string should fit entirely
    char content[79];
    memset(content, 'a', 78);
    content[78] = '\0';

    ASSERT_EQ(78, find_wrap_point(content, 78, 78), "78 chars fit in width=78");
}

int main(void) {
    // Set UTF-8 locale for proper wide character handling
    setlocale(LC_ALL, "C.UTF-8");

    printf("=== Text Wrap Logic Unit Tests ===\n");

    test_ascii_wrap();
    test_utf8_wrap();
    test_mixed_wrap();
    test_edge_cases();
    test_border_width();

    printf("\n=== Test Summary ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);

    if (tests_failed == 0) {
        printf("\n✓ All tests passed!\n");
        return 0;
    } else {
        printf("\n✗ Some tests failed!\n");
        return 1;
    }
}
