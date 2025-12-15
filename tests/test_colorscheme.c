/*
 * test_colorscheme.c - Test the colorscheme system
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../src/colorscheme.h"

int main(void) {
    printf("Testing colorscheme system...\n\n");

    // Test 1: Load built-in kitty-default theme
    printf("Test 1: Loading built-in kitty-default theme\n");
    int result = init_colorscheme("kitty-default");

    if (result == 0) {
        printf("✓ Theme loaded successfully\n");
        printf("  Theme loaded flag: %d\n", g_theme_loaded);

        // Print the loaded colors
        printf("  foreground: RGB(%d, %d, %d)\n",
               g_theme.foreground_rgb.r, g_theme.foreground_rgb.g, g_theme.foreground_rgb.b);
        printf("  assistant: RGB(%d, %d, %d)\n",
               g_theme.assistant_rgb.r, g_theme.assistant_rgb.g, g_theme.assistant_rgb.b);
        printf("  user (color2): RGB(%d, %d, %d)\n",
               g_theme.user_rgb.r, g_theme.user_rgb.g, g_theme.user_rgb.b);
        printf("  status (color3): RGB(%d, %d, %d)\n",
               g_theme.status_rgb.r, g_theme.status_rgb.g, g_theme.status_rgb.b);
        printf("  error (color1): RGB(%d, %d, %d)\n",
               g_theme.error_rgb.r, g_theme.error_rgb.g, g_theme.error_rgb.b);
        printf("  header (color6): RGB(%d, %d, %d)\n",
               g_theme.header_rgb.r, g_theme.header_rgb.g, g_theme.header_rgb.b);

        // Test ANSI code generation
        char buf[32];
        if (get_colorscheme_color(COLORSCHEME_ASSISTANT, buf, sizeof(buf)) == 0) {
            printf("  Assistant ANSI code: %s\n", buf);
        }
    } else {
        printf("✗ Theme failed to load (expected to succeed)\n");
        return 1;
    }

    // Test 2: Try to load non-existent theme
    printf("\nTest 2: Loading non-existent theme\n");
    g_theme_loaded = 0;  // Reset
    result = init_colorscheme("nonexistent-theme");

    if (result == -1) {
        printf("✓ Correctly failed to load non-existent theme\n");
        printf("  Theme loaded flag: %d (should be 0)\n", g_theme_loaded);
    } else {
        printf("✗ Should have returned -1 for non-existent theme\n");
        return 1;
    }

    // Test 3: Load theme without CLAUDE_C_THEME env var
    printf("\nTest 3: Loading with NULL filepath\n");
    g_theme_loaded = 0;  // Reset
    result = init_colorscheme(NULL);

    if (result == -1) {
        printf("✓ Correctly returned -1 for NULL filepath\n");
        printf("  Theme loaded flag: %d (should be 0)\n", g_theme_loaded);
    } else {
        printf("✗ Should have returned -1 for NULL filepath\n");
        return 1;
    }

    // Test 4: Verify get_colorscheme_color returns -1 when no theme loaded
    printf("\nTest 4: get_colorscheme_color with no theme\n");
    char buf[32];
    result = get_colorscheme_color(COLORSCHEME_USER, buf, sizeof(buf));
    if (result == -1) {
        printf("✓ Correctly returned -1 when no theme loaded\n");
    } else {
        printf("✗ Should have returned -1 when no theme loaded\n");
        return 1;
    }

    printf("\n✓ All tests passed!\n");
    return 0;
}
