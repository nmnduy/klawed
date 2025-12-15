#ifndef COLORSCHEME_H
#define COLORSCHEME_H

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#pragma GCC diagnostic ignored "-Wimplicit-int"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "logger.h"
#include "fallback_colors.h"

// Colorscheme element types (for ANSI escape code generation)
typedef enum {
    COLORSCHEME_FOREGROUND,  // Main text color for majority of content
    COLORSCHEME_USER,
    COLORSCHEME_ASSISTANT,
    COLORSCHEME_TOOL,
    COLORSCHEME_ERROR,
    COLORSCHEME_STATUS,
    COLORSCHEME_DIFF_ADD,      // Added lines in diffs (green)
    COLORSCHEME_DIFF_REMOVE,   // Removed lines in diffs (red)
    COLORSCHEME_DIFF_HEADER,   // Diff metadata/headers (cyan)
    COLORSCHEME_DIFF_CONTEXT   // Line numbers and context (dim)
} ColorschemeElement;

// RGB color structure (0-255 values)
typedef struct {
    int r;
    int g;
    int b;
} RGB;

// Theme structure to hold parsed Kitty colors
typedef struct {
    RGB foreground_rgb;  // Main text color for majority of content
    RGB assistant_rgb;   // RGB values for ANSI codes
    RGB user_rgb;
    RGB status_rgb;
    RGB error_rgb;
    RGB header_rgb;
    RGB tool_rgb;        // Tool tags (historical). Currently unified with status color.
    RGB diff_add_rgb;    // Added lines (green)
    RGB diff_remove_rgb; // Removed lines (red)
    RGB diff_header_rgb; // Diff metadata (cyan)
    RGB diff_context_rgb; // Line numbers/context (dim gray)
} Theme;

// Global theme state (non-static so it's shared across compilation units)
// Only one file should include this header in implementation mode
#ifndef COLORSCHEME_EXTERN
Theme g_theme = {0};
int g_theme_loaded = 0;
#else
extern Theme g_theme;
extern int g_theme_loaded;
#endif

// Parse hex color "#RRGGBB" to RGB struct
static RGB parse_hex_color(const char *hex) {
    RGB rgb = {0, 0, 0};

    // Skip leading '#' if present
    if (hex[0] == '#') {
        hex++;
    }

    // Parse hex string
    if (strlen(hex) == 6) {
        char r_str[3] = {hex[0], hex[1], '\0'};
        char g_str[3] = {hex[2], hex[3], '\0'};
        char b_str[3] = {hex[4], hex[5], '\0'};

        rgb.r = (int)strtol(r_str, NULL, 16);
        rgb.g = (int)strtol(g_str, NULL, 16);
        rgb.b = (int)strtol(b_str, NULL, 16);

        LOG_DEBUG("[THEME]     Parsed hex %s -> RGB(%d, %d, %d)",
                hex - 1, rgb.r, rgb.g, rgb.b);
    }

    return rgb;
}

// Convert RGB to ANSI 256-color escape code
// Returns foreground color code like "\033[38;5;123m"
static void rgb_to_ansi_code(RGB rgb, char *buf, size_t bufsize) {
    // Convert RGB to nearest 256-color palette index
    // Check if it's grayscale
    int avg = (rgb.r + rgb.g + rgb.b) / 3;
    int r_diff = abs(rgb.r - avg);
    int g_diff = abs(rgb.g - avg);
    int b_diff = abs(rgb.b - avg);

    int color_idx;
    if (r_diff < 10 && g_diff < 10 && b_diff < 10) {
        // Grayscale: use colors 232-255 (24 shades)
        int gray_index = (avg * 23) / 255;
        color_idx = 232 + gray_index;
    } else {
        // RGB cube: 16-231
        int r_idx = (rgb.r * 5) / 255;
        int g_idx = (rgb.g * 5) / 255;
        int b_idx = (rgb.b * 5) / 255;
        color_idx = 16 + (36 * r_idx) + (6 * g_idx) + b_idx;
    }

    snprintf(buf, bufsize, "\033[38;5;%dm", color_idx);
}

// Get ANSI color code for a colorscheme element
// Returns 0 on success, -1 if no theme loaded
#if defined(__GNUC__)
__attribute__((unused))
#endif
static int get_colorscheme_color(ColorschemeElement element, char *buf, size_t bufsize) {
    if (!g_theme_loaded) {
        static int warned = 0;
        if (!warned) {
            LOG_DEBUG("[THEME] get_colorscheme_color() called but no theme loaded - using fallbacks");
            warned = 1;
        }
        return -1;  // No theme loaded
    }

    RGB rgb = {0, 0, 0};
    switch (element) {
        case COLORSCHEME_FOREGROUND:
            rgb = g_theme.foreground_rgb;
            break;
        case COLORSCHEME_USER:
            rgb = g_theme.user_rgb;
            break;
        case COLORSCHEME_ASSISTANT:
            rgb = g_theme.assistant_rgb;
            break;
        case COLORSCHEME_TOOL:
            // Unify tool tag color with STATUS to reduce color variance
            rgb = g_theme.status_rgb;
            break;
        case COLORSCHEME_ERROR:
            rgb = g_theme.error_rgb;
            break;
        case COLORSCHEME_STATUS:
            rgb = g_theme.status_rgb;
            break;
        case COLORSCHEME_DIFF_ADD:
            rgb = g_theme.diff_add_rgb;
            break;
        case COLORSCHEME_DIFF_REMOVE:
            rgb = g_theme.diff_remove_rgb;
            break;
        case COLORSCHEME_DIFF_HEADER:
            rgb = g_theme.diff_header_rgb;
            break;
        case COLORSCHEME_DIFF_CONTEXT:
            rgb = g_theme.diff_context_rgb;
            break;
        default:
            return -1;
    }

    rgb_to_ansi_code(rgb, buf, bufsize);
    return 0;
}

#include "builtin_themes.h"

// Load Kitty theme from built-in string (for embedded themes)
static int load_kitty_theme_buf(const char *buf_data, Theme *theme) {
    // Suppress cast-qual warning for fmemopen
    /* pragma to ignore cast-qual warning */
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wcast-qual"
    LOG_INFO("[THEME] Loading built-in Kitty theme");
    // Use fmemopen to treat string as FILE*
    FILE *f = fmemopen((void *)buf_data, strlen(buf_data), "r");
    if (!f) {
        LOG_ERROR("[THEME] ERROR: Failed to open built-in theme buffer");
        return 0;
    }
    // Delegate to existing load logic by temporarily writing to a temp file
    // For simplicity, reimplement parsing here:
    char line[1024];
    int line_num = 0; (void)line_num;
    int parsed_count = 0;
    int found_foreground = 0;
    while (fgets(line, sizeof(line), f)) {
        line_num++;
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        char *p = line;
        while (*p && isspace(*p)) p++;
        if (*p == '\0' || *p == '#') continue;
        char key[64], value[32];
        if (sscanf(p, "%63s %31s", key, value) == 2) {
            RGB rgb = parse_hex_color(value);
            if (strcmp(key, "foreground") == 0) {
                theme->foreground_rgb = rgb;
                theme->assistant_rgb = rgb;
                parsed_count++; found_foreground = 1;
            } else if (strcmp(key, "color2") == 0) {
                theme->user_rgb = rgb; parsed_count++;
            } else if (strcmp(key, "color3") == 0) {
                theme->status_rgb = rgb; parsed_count++;
            } else if (strcmp(key, "color12") == 0) {
                // Use bright blue (color12) for tools across most themes
                theme->tool_rgb = rgb; parsed_count++;
            } else if (strcmp(key, "color1") == 0) {
                theme->error_rgb = rgb; parsed_count++;
            } else if (strcmp(key, "color6") == 0) {
                theme->header_rgb = rgb;
                if (found_foreground) theme->assistant_rgb = rgb;
                parsed_count++;
            } else if (strcmp(key, "color4") == 0) {
                theme->header_rgb = rgb; parsed_count++;
            }
        }
    }
    // Map diff colors
    theme->diff_add_rgb = theme->user_rgb;
    theme->diff_remove_rgb = theme->error_rgb;
    theme->diff_header_rgb = theme->header_rgb;
    theme->diff_context_rgb = theme->foreground_rgb;
    int avg = (theme->diff_context_rgb.r + theme->diff_context_rgb.g + theme->diff_context_rgb.b) / 3;
    if (avg > 100) {
        theme->diff_context_rgb.r = (theme->diff_context_rgb.r * 6) / 10;
        theme->diff_context_rgb.g = (theme->diff_context_rgb.g * 6) / 10;
        theme->diff_context_rgb.b = (theme->diff_context_rgb.b * 6) / 10;
    }
        fclose(f);
    #pragma GCC diagnostic pop
    return parsed_count > 0;
}

// Load Kitty theme from file
// Uses ONLY standard Kitty color names: foreground, background, color0-15
static int load_kitty_theme(const char *filepath, Theme *theme) {
    LOG_INFO("[THEME] Loading Kitty theme from: %s", filepath);

    FILE *f = fopen(filepath, "r");
    if (!f) {
        LOG_ERROR("[THEME] ERROR: Failed to open file: %s", filepath);
        return 0;
    }

    LOG_DEBUG("[THEME] File opened successfully");

    char line[1024];
    int line_num = 0;
    int parsed_count = 0;

    // Track which required colors we found
    int found_foreground = 0;
    int found_color1 = 0;  // red (errors)
    int found_color2 = 0;  // green (user)
    int found_color3 = 0;  // yellow (status)
    int found_color12 = 0; // bright blue (tool tags)
    int found_color6 = 0;  // cyan (headers/assistant fallback)

    while (fgets(line, sizeof(line), f)) {
        line_num++;

        // Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }

        // Skip empty lines and comments
        char *p = line;
        while (*p && isspace(*p)) p++;
        if (*p == '\0' || *p == '#') {
            continue;
        }

        // Parse key-value pair
        char key[64], value[32];
        if (sscanf(p, "%63s %31s", key, value) == 2) {
            LOG_DEBUG("[THEME] Line %d: %s = %s", line_num, key, value);

            RGB rgb = parse_hex_color(value);

            // Map ONLY standard Kitty color keys to TUI elements
            if (strcmp(key, "foreground") == 0) {
                theme->foreground_rgb = rgb;
                theme->assistant_rgb = rgb;  // Use foreground for assistant text by default
                parsed_count++;
                found_foreground = 1;
                LOG_DEBUG("[THEME]   -> Set foreground_rgb and assistant_rgb = RGB(%d,%d,%d)", rgb.r, rgb.g, rgb.b);
            }
            else if (strcmp(key, "color2") == 0) {
                theme->user_rgb = rgb;
                parsed_count++;
                found_color2 = 1;
                LOG_DEBUG("[THEME]   -> Set user_rgb = RGB(%d,%d,%d)", rgb.r, rgb.g, rgb.b);
            }
            else if (strcmp(key, "color3") == 0) {
                theme->status_rgb = rgb;
                parsed_count++;
                found_color3 = 1;
                LOG_DEBUG("[THEME]   -> Set status_rgb = RGB(%d,%d,%d)", rgb.r, rgb.g, rgb.b);
            }
            else if (strcmp(key, "color12") == 0) {
                // Prefer bright blue for tool tags; widely available and less overpowering
                theme->tool_rgb = rgb;
                parsed_count++;
                found_color12 = 1;
                LOG_DEBUG("[THEME]   -> Set tool_rgb (color12) = RGB(%d,%d,%d)", rgb.r, rgb.g, rgb.b);
            }
            else if (strcmp(key, "color1") == 0) {
                theme->error_rgb = rgb;
                parsed_count++;
                found_color1 = 1;
                LOG_DEBUG("[THEME]   -> Set error_rgb = RGB(%d,%d,%d)", rgb.r, rgb.g, rgb.b);
            }
            else if (strcmp(key, "color6") == 0) {
                theme->header_rgb = rgb;
                // Also override assistant_rgb with cyan if available (more visible than foreground)
                if (found_foreground) {
                    theme->assistant_rgb = rgb;
                    LOG_DEBUG("[THEME]   -> Overriding assistant_rgb with color6 (cyan) = RGB(%d,%d,%d)", rgb.r, rgb.g, rgb.b);
                }
                parsed_count++;
                found_color6 = 1;
                LOG_DEBUG("[THEME]   -> Set header_rgb = RGB(%d,%d,%d)", rgb.r, rgb.g, rgb.b);
            }
            else if (strcmp(key, "color4") == 0 && !found_color6) {
                // Fallback to color4 (blue) for header if color6 not found
                theme->header_rgb = rgb;
                parsed_count++;
                LOG_DEBUG("[THEME]   -> Set header_rgb (fallback to color4) = RGB(%d,%d,%d)", rgb.r, rgb.g, rgb.b);
            }
        }
    }

    // Map standard Kitty colors to diff elements (after main parsing loop)
    // These use the same color mappings as the primary elements
    theme->diff_add_rgb = theme->user_rgb;        // Green for additions (same as user)
    theme->diff_remove_rgb = theme->error_rgb;    // Red for removals (same as error)
    theme->diff_header_rgb = theme->header_rgb;   // Cyan for diff headers (same as header)
    theme->diff_context_rgb = theme->foreground_rgb;  // Default color for context

    // Make context slightly dimmer by reducing brightness
    int avg = (theme->diff_context_rgb.r + theme->diff_context_rgb.g + theme->diff_context_rgb.b) / 3;
    if (avg > 100) {
        // Dim by ~40%
        theme->diff_context_rgb.r = (theme->diff_context_rgb.r * 6) / 10;
        theme->diff_context_rgb.g = (theme->diff_context_rgb.g * 6) / 10;
        theme->diff_context_rgb.b = (theme->diff_context_rgb.b * 6) / 10;
    }

    fclose(f);
    LOG_DEBUG("[THEME] Parsed %d color mappings", parsed_count);

    // Print warnings for missing critical colors
    if (!found_foreground) {
        fprintf(stderr, "\033[33mWarning: Theme missing 'foreground' color\033[0m\n");
    }
    if (!found_color2) {
        fprintf(stderr, "\033[33mWarning: Theme missing 'color2' (green, used for user text)\033[0m\n");
    }
    if (!found_color3) {
        fprintf(stderr, "\033[33mWarning: Theme missing 'color3' (yellow, used for status)\033[0m\n");
    }
    if (!found_color1) {
        fprintf(stderr, "\033[33mWarning: Theme missing 'color1' (red, used for errors)\033[0m\n");
    }
    if (!found_color6) {
        fprintf(stderr, "\033[33mWarning: Theme missing 'color6' (cyan, used for headers)\033[0m\n");
    }
    if (!found_color12) {
        fprintf(stderr, "\033[33mNote: Theme missing 'color12' (optional tool accent); tool tag uses status color\033[0m\n");
    }

    return parsed_count > 0;
}

// Initialize colorscheme system with optional Kitty theme file
// If filepath is NULL or file doesn't exist, falls back to standard ANSI colors
// Returns 0 on success, -1 on failure
// Note: This doesn't require ncurses - works with raw ANSI codes
static __attribute__((unused)) int init_colorscheme(const char *filepath) {
    LOG_DEBUG("[THEME] Initializing colorscheme system");

    if (filepath) {
        LOG_DEBUG("[THEME] Theme path provided: %s", filepath);
        // Check for built-in theme
        const char *builtin = get_builtin_theme_content(filepath);
        if (builtin) {
            LOG_DEBUG("[THEME] Using built-in theme for '%s'", filepath);
            if (load_kitty_theme_buf(builtin, &g_theme)) {
                LOG_DEBUG("[THEME] Successfully loaded built-in theme");
                g_theme_loaded = 1;
                return 0;
            }
            LOG_WARN("[THEME] Failed to load built-in theme for '%s'", filepath);
        } else {
            // Try loading external theme file
            LOG_DEBUG("[THEME] Attempting to load external theme from: %s", filepath);
            if (load_kitty_theme(filepath, &g_theme)) {
                LOG_DEBUG("[THEME] Successfully loaded external theme");
                g_theme_loaded = 1;
                return 0;
            }
            LOG_WARN("[THEME] Failed to load external theme from %s", filepath);
        }
    } else {
        LOG_DEBUG("[THEME] No custom theme path provided");
    }

    // No theme loaded - warn user and rely on fallback ANSI colors
    fprintf(stderr, "\033[33mWarning: No Kitty theme loaded, using standard ANSI fallback colors\033[0m\n");
    fprintf(stderr, "  Set CLAUDE_C_THEME environment variable to a .conf file or built-in theme name (e.g., 'dracula') to use custom colors\n");
    fprintf(stderr, "  Example: export CLAUDE_C_THEME=\"dracula\" or /path/to/dracula.conf\n");

    // Don't set g_theme_loaded = 1, so get_colorscheme_color() will return -1
    // and code will fall back to ANSI_FALLBACK_* constants from fallback_colors.h
    g_theme_loaded = 0;

    return -1;  // Indicate no theme loaded
}

#endif // COLORSCHEME_H
