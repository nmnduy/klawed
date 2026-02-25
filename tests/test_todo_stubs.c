/*
 * Stub implementations for TODO test suite
 *
 * Provides minimal implementations of dependencies needed by todo.c
 */

#include <stdio.h>
#include <stdarg.h>
#include "../src/logger.h"

// Colorscheme stubs - provide minimal theme support
// Must match the actual Theme structure from colorscheme.h
typedef struct {
    int r;
    int g;
    int b;
} RGB;

typedef struct {
    RGB foreground_rgb;  // Main text color for majority of content
    RGB assistant_rgb;   // RGB values for ANSI codes
    RGB user_rgb;
    RGB status_rgb;
    RGB error_rgb;
    RGB header_rgb;
    RGB diff_add_rgb;    // Added lines (green)
    RGB diff_remove_rgb; // Removed lines (red)
    RGB diff_header_rgb; // Diff metadata (cyan)
    RGB diff_context_rgb; // Line numbers/context (dim gray)
} Theme;

Theme g_theme = {
    .foreground_rgb = {255, 255, 255},
    .assistant_rgb = {0, 255, 255},
    .user_rgb = {0, 255, 0},
    .status_rgb = {255, 255, 0},
    .error_rgb = {255, 0, 0},
    .header_rgb = {0, 255, 255},
    .diff_add_rgb = {0, 255, 0},
    .diff_remove_rgb = {255, 0, 0},
    .diff_header_rgb = {0, 255, 255},
    .diff_context_rgb = {170, 170, 170},
};

int g_theme_loaded = 1;

// Logger stub - matches signature from logger.h
void log_message(LogLevel level, const char *file, int line,
                const char *func, const char *fmt, ...) {
    (void)level;
    (void)file;
    (void)line;
    (void)func;
    (void)fmt;
    // Suppress log output in tests
}
