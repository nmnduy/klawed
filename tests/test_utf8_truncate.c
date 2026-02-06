/*
 * Test for UTF-8 aware truncation function
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/util/string_utils.h"

// Check if a string is valid UTF-8
int is_valid_utf8(const char *str) {
    const unsigned char *s = (const unsigned char *)str;
    while (*s) {
        if (*s < 0x80) {
            // ASCII (1 byte)
            s++;
        } else if ((*s & 0xE0) == 0xC0) {
            // 2-byte sequence: 110xxxxx 10xxxxxx
            if ((s[1] & 0xC0) != 0x80) return 0;
            s += 2;
        } else if ((*s & 0xF0) == 0xE0) {
            // 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx
            if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) return 0;
            s += 3;
        } else if ((*s & 0xF8) == 0xF0) {
            // 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
            if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) return 0;
            s += 4;
        } else {
            return 0; // Invalid UTF-8
        }
    }
    return 1;
}

int main() {
    printf("Testing UTF-8 truncation function\n");
    printf("=================================\n\n");

    int passed = 0;
    int failed = 0;

    // Test 1: Simple ASCII
    {
        const char *input = "Hello World";
        char *result = truncate_utf8(input, 5);
        if (result && strcmp(result, "Hello") == 0 && is_valid_utf8(result)) {
            printf("✓ Test 1: ASCII truncation passed\n");
            passed++;
        } else {
            printf("✗ Test 1: ASCII truncation failed\n");
            failed++;
        }
        free(result);
    }

    // Test 2: UTF-8 arrows (→ is 3 bytes: E2 86 92)
    {
        const char *input = "A→B→C→D";  // A \xe2\x86\x92 B \xe2\x86\x92 C \xe2\x86\x92 D
        // Position 4 would split the first arrow (bytes: A=0, E2=1, 86=2, 92=3)
        char *result = truncate_utf8(input, 4);
        if (result && is_valid_utf8(result)) {
            printf("✓ Test 2: UTF-8 arrow truncation (4 bytes) passed: '%s'\n", result);
            passed++;
        } else {
            printf("✗ Test 2: UTF-8 arrow truncation (4 bytes) failed: '%s'\n", result);
            failed++;
        }
        free(result);
    }

    // Test 3: Truncate at exact UTF-8 boundary
    {
        // "A→B" = A + E2 86 92 + B = 5 bytes
        const char *input = "A→B";
        char *result = truncate_utf8(input, 1);
        if (result && is_valid_utf8(result)) {
            printf("✓ Test 3: Truncate to 1 byte passed: '%s'\n", result);
            passed++;
        } else {
            printf("✗ Test 3: Truncate to 1 byte failed: '%s'\n", result);
            failed++;
        }
        free(result);
    }

    // Test 4: String already within limit
    {
        const char *input = "Short";
        char *result = truncate_utf8(input, 100);
        if (result && strcmp(result, "Short") == 0) {
            printf("✓ Test 4: String within limit passed\n");
            passed++;
        } else {
            printf("✗ Test 4: String within limit failed\n");
            failed++;
        }
        free(result);
    }

    // Test 5: Empty string
    {
        const char *input = "";
        char *result = truncate_utf8(input, 10);
        if (result && strcmp(result, "") == 0) {
            printf("✓ Test 5: Empty string passed\n");
            passed++;
        } else {
            printf("✗ Test 5: Empty string failed\n");
            failed++;
        }
        free(result);
    }

    // Test 6: NULL input
    {
        char *result = truncate_utf8(NULL, 10);
        if (result == NULL) {
            printf("✓ Test 6: NULL input passed\n");
            passed++;
        } else {
            printf("✗ Test 6: NULL input failed\n");
            failed++;
            free(result);
        }
    }

    // Test 7: Multiple UTF-8 arrows in middle of truncation
    {
        // Create a long string with arrows that would be split at BASH_OUTPUT_MAX_SIZE
        char input[12235];
        memset(input, 'A', 12230);
        input[12230] = '\xe2';
        input[12231] = '\x86';
        input[12232] = '\x92';  // → arrow
        input[12233] = 'B';
        input[12234] = '\0';

        char *result = truncate_utf8(input, 12228);
        if (result && is_valid_utf8(result) && strlen(result) <= 12228) {
            printf("✓ Test 7: Large truncation at boundary passed (len=%zu)\n", strlen(result));
            passed++;
        } else {
            printf("✗ Test 7: Large truncation at boundary failed (len=%zu, valid=%d)\n",
                   result ? strlen(result) : 0, result ? is_valid_utf8(result) : 0);
            failed++;
        }
        free(result);
    }

    // Test 8: 4-byte UTF-8 (emoji)
    {
        // 😀 is 4 bytes: F0 9F 98 80
        const char *input = "A😀B😀C";
        // 6 bytes: A + emoji + B, truncate at 4 would split emoji
        char *result = truncate_utf8(input, 5);
        if (result && is_valid_utf8(result)) {
            printf("✓ Test 8: 4-byte UTF-8 truncation passed: '%s' (len=%zu)\n", result, strlen(result));
            passed++;
        } else {
            printf("✗ Test 8: 4-byte UTF-8 truncation failed: '%s'\n", result);
            failed++;
        }
        free(result);
    }

    printf("\n=================================\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
