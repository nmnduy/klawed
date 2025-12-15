/**
 * test_diff_colors.c - Unit tests for diff colorization
 *
 * Tests the show_diff() function with different color themes
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Test colorscheme functionality
#define TEST_BUILD 1
#include "../src/colorscheme.h"
#include "../src/fallback_colors.h"
#include "../src/builtin_themes.h"

void test_fallback_colors(void) {
    printf("=== Testing Fallback ANSI Colors ===\n\n");

    printf("Diff colors:\n");
    printf("  ADD:     %s+++ Added line%s\n", ANSI_FALLBACK_DIFF_ADD, ANSI_RESET);
    printf("  REMOVE:  %s--- Removed line%s\n", ANSI_FALLBACK_DIFF_REMOVE, ANSI_RESET);
    printf("  HEADER:  %s=== Header line ===%s\n", ANSI_FALLBACK_DIFF_HEADER, ANSI_RESET);
    printf("  CONTEXT: %s@@ -1,3 +1,3 @@%s\n", ANSI_FALLBACK_DIFF_CONTEXT, ANSI_RESET);

    printf("\n");
}

void test_theme_colors(const char *theme_path) {
    printf("=== Testing Theme Colors: %s ===\n\n", theme_path);

    Theme theme = {0};
    int result = load_kitty_theme(theme_path, &theme);

    if (result < 0) {
        printf("❌ Failed to load theme: %s\n", theme_path);
        return;
    }

    printf("✓ Theme loaded successfully (%d colors parsed)\n\n", result);

    // Test diff colors
    char add_color[32], remove_color[32], header_color[32], context_color[32];

    if (get_colorscheme_color(COLORSCHEME_DIFF_ADD, add_color, sizeof(add_color)) == 0) {
        printf("  ADD:     %s+++ Added line%s (RGB: %d,%d,%d)\n",
               add_color, ANSI_RESET,
               theme.diff_add_rgb.r, theme.diff_add_rgb.g, theme.diff_add_rgb.b);
    }

    if (get_colorscheme_color(COLORSCHEME_DIFF_REMOVE, remove_color, sizeof(remove_color)) == 0) {
        printf("  REMOVE:  %s--- Removed line%s (RGB: %d,%d,%d)\n",
               remove_color, ANSI_RESET,
               theme.diff_remove_rgb.r, theme.diff_remove_rgb.g, theme.diff_remove_rgb.b);
    }

    if (get_colorscheme_color(COLORSCHEME_DIFF_HEADER, header_color, sizeof(header_color)) == 0) {
        printf("  HEADER:  %s=== Header line ===%s (RGB: %d,%d,%d)\n",
               header_color, ANSI_RESET,
               theme.diff_header_rgb.r, theme.diff_header_rgb.g, theme.diff_header_rgb.b);
    }

    if (get_colorscheme_color(COLORSCHEME_DIFF_CONTEXT, context_color, sizeof(context_color)) == 0) {
        printf("  CONTEXT: %s@@ -1,3 +1,3 @@%s (RGB: %d,%d,%d)\n",
               context_color, ANSI_RESET,
               theme.diff_context_rgb.r, theme.diff_context_rgb.g, theme.diff_context_rgb.b);
    }

    printf("\n");
}

void test_diff_output_simulation(void) {
    printf("=== Simulating Colorized Diff Output ===\n\n");

    // Simulate a diff output with colors
    const char *lines[] = {
        "--- original.txt",
        "+++ modified.txt",
        "@@ -1,5 +1,5 @@",
        " Line 1: unchanged",
        "-Line 2: removed",
        "+Line 2: added",
        " Line 3: unchanged"
    };

    char add_color[32], remove_color[32], header_color[32], context_color[32];
    const char *add_start, *remove_start, *header_start, *context_start;

    // Try to get colors from theme, fall back to ANSI
    if (get_colorscheme_color(COLORSCHEME_DIFF_ADD, add_color, sizeof(add_color)) == 0) {
        add_start = add_color;
    } else {
        add_start = ANSI_FALLBACK_DIFF_ADD;
    }

    if (get_colorscheme_color(COLORSCHEME_DIFF_REMOVE, remove_color, sizeof(remove_color)) == 0) {
        remove_start = remove_color;
    } else {
        remove_start = ANSI_FALLBACK_DIFF_REMOVE;
    }

    if (get_colorscheme_color(COLORSCHEME_DIFF_HEADER, header_color, sizeof(header_color)) == 0) {
        header_start = header_color;
    } else {
        header_start = ANSI_FALLBACK_DIFF_HEADER;
    }

    if (get_colorscheme_color(COLORSCHEME_DIFF_CONTEXT, context_color, sizeof(context_color)) == 0) {
        context_start = context_color;
    } else {
        context_start = ANSI_FALLBACK_DIFF_CONTEXT;
    }

    // Print colorized diff
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        const char *line = lines[i];

        if (line[0] == '+' && line[1] != '+') {
            printf("%s%s%s\n", add_start, line, ANSI_RESET);
        } else if (line[0] == '-' && line[1] != '-') {
            printf("%s%s%s\n", remove_start, line, ANSI_RESET);
        } else if (strncmp(line, "@@", 2) == 0) {
            printf("%s%s%s\n", context_start, line, ANSI_RESET);
        } else if (strncmp(line, "---", 3) == 0 || strncmp(line, "+++", 3) == 0) {
            printf("%s%s%s\n", header_start, line, ANSI_RESET);
        } else {
            printf("%s\n", line);
        }
    }

    printf("\n");
}

int main(void) {
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║   Diff Colorization Tests             ║\n");
    printf("╚════════════════════════════════════════╝\n\n");

    // Test 1: Fallback colors
    test_fallback_colors();

    // Test 2: Dracula built-in theme
    test_theme_colors("dracula");

    // Test 3: Gruvbox built-in theme
    test_theme_colors("gruvbox-dark");

    // Test 4: Simulated diff output (no theme loaded)
    test_diff_output_simulation();

    // Test 5: Simulated diff with Dracula theme
    Theme theme = {0};
    const char *builtin = get_builtin_theme_content("dracula");
    if (builtin && load_kitty_theme_buf(builtin, &theme)) {
        printf("=== With Dracula Theme ===\n\n");
        test_diff_output_simulation();
    }

    printf("╔════════════════════════════════════════╗\n");
    printf("║   All Tests Completed                 ║\n");
    printf("╚════════════════════════════════════════╝\n\n");

    return 0;
}
