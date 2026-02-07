/*
 * oneshot_ui.c - Enhanced UI for oneshot mode with themed colors and boxes
 *
 * Provides beautiful, theme-aware output formatting for oneshot mode execution.
 */

#ifndef __APPLE__
    #define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <time.h>

#include "oneshot_ui.h"
#include "../fallback_colors.h"
#include "../logger.h"

// Use extern declarations for colorscheme globals - must be defined BEFORE including colorscheme.h
#define COLORSCHEME_EXTERN
#include "../colorscheme.h"

// Terminal width cache
static int g_terminal_width = 0;
static int g_unicode_supported = -1;  // -1 = unknown, 0 = no, 1 = yes
static OneshotUIStyle g_ui_style = ONESHOT_UI_STYLE_BOXES;

// Color cache for performance
static struct {
    char tool[32];
    char tool_bold[32];
    char success[32];
    char error[32];
    char warning[32];
    char assistant[32];
    char dim[32];
    char reset[8];
    int initialized;
} g_colors = {0};

// Spinner frames definition
const char *SPINNER_FRAMES[SPINNER_COUNT] = {"\u28f6", "\u28f5", "\u28ef", "\u28df"}; // ⣶⣵⣯⣟

// ANSI codes for dim (not theme-dependent)
#define ANSI_DIM "\033[2m"
#define ANSI_BOLD "\033[1m"
#define ANSI_RESET_CODE "\033[0m"

// Initialize colors from theme
static void init_color_cache(void) {
    if (g_colors.initialized) return;

    // Tool/header color - use status color
    if (get_colorscheme_color(COLORSCHEME_STATUS, g_colors.tool, sizeof(g_colors.tool)) != 0) {
        strlcpy(g_colors.tool, ANSI_FALLBACK_STATUS, sizeof(g_colors.tool));
    }

    // Bold tool color
    // Bold tool color - tool color is already an escape sequence like "\033[38;5;123m"
    // We need to insert the bold code AFTER the opening "\033[" and BEFORE the rest
    // g_colors.tool format: \033[38;5;XXXm
    // g_colors.tool_bold format: \033[1;38;5;XXXm
    if (strncmp(g_colors.tool, "\033[38;5;", 7) == 0) {
        int color_idx = 0;
        if (sscanf(g_colors.tool + 7, "%dm", &color_idx) == 1) {
            snprintf(g_colors.tool_bold, sizeof(g_colors.tool_bold), "\033[1;38;5;%dm", color_idx);
        } else {
            strlcpy(g_colors.tool_bold, g_colors.tool, sizeof(g_colors.tool_bold));
        }
    } else {
        strlcpy(g_colors.tool_bold, g_colors.tool, sizeof(g_colors.tool_bold));
    }

    // Success color - use user color (green in most themes)
    if (get_colorscheme_color(COLORSCHEME_USER, g_colors.success, sizeof(g_colors.success)) != 0) {
        strlcpy(g_colors.success, ANSI_FALLBACK_USER, sizeof(g_colors.success));
    }

    // Error color
    if (get_colorscheme_color(COLORSCHEME_ERROR, g_colors.error, sizeof(g_colors.error)) != 0) {
        strlcpy(g_colors.error, ANSI_FALLBACK_ERROR, sizeof(g_colors.error));
    }

    // Warning color - use status (yellow in most themes)
    if (get_colorscheme_color(COLORSCHEME_STATUS, g_colors.warning, sizeof(g_colors.warning)) != 0) {
        strlcpy(g_colors.warning, ANSI_FALLBACK_YELLOW, sizeof(g_colors.warning));
    }

    // Assistant color
    if (get_colorscheme_color(COLORSCHEME_ASSISTANT, g_colors.assistant, sizeof(g_colors.assistant)) != 0) {
        strlcpy(g_colors.assistant, ANSI_FALLBACK_ASSISTANT, sizeof(g_colors.assistant));
    }

    // Dim color - use foreground at 60% brightness
    char fg[32];
    if (get_colorscheme_color(COLORSCHEME_FOREGROUND, fg, sizeof(fg)) == 0) {
        // Extract the color index from the escape sequence
        // Format: \033[38;5;XXXm
        int color_idx = 0;
        if (sscanf(fg, "\033[38;5;%dm", &color_idx) == 1) {
            // For 256 colors, we can't easily dim, so use ANSI dim
            snprintf(g_colors.dim, sizeof(g_colors.dim), "%s", ANSI_DIM);
        } else {
            strlcpy(g_colors.dim, ANSI_DIM, sizeof(g_colors.dim));
        }
    } else {
        strlcpy(g_colors.dim, ANSI_DIM, sizeof(g_colors.dim));
    }

    strlcpy(g_colors.reset, ANSI_RESET_CODE, sizeof(g_colors.reset));
    g_colors.initialized = 1;
}

// Get terminal width
static int get_terminal_width(void) {
    if (g_terminal_width > 0) return g_terminal_width;

    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        g_terminal_width = ws.ws_col;
        if (g_terminal_width < 40) g_terminal_width = 40;  // Minimum reasonable width
        return g_terminal_width;
    }

    // Fallback
    const char *cols = getenv("COLUMNS");
    if (cols) {
        g_terminal_width = atoi(cols);
        if (g_terminal_width < 40) g_terminal_width = 40;
        return g_terminal_width;
    }

    g_terminal_width = 80;  // Default
    return g_terminal_width;
}

// Check if terminal supports Unicode
static int check_unicode_support(void) {
    if (g_unicode_supported >= 0) return g_unicode_supported;

    // Check environment variables
    const char *lang = getenv("LANG");
    const char *lc_all = getenv("LC_ALL");
    const char *term = getenv("TERM");

    // Check for UTF-8 in locale
    int has_utf8 = 0;
    if (lc_all && (strstr(lc_all, "UTF-8") || strstr(lc_all, "utf-8"))) {
        has_utf8 = 1;
    } else if (lang && (strstr(lang, "UTF-8") || strstr(lang, "utf-8"))) {
        has_utf8 = 1;
    }

    // Check for known good terminals
    int known_good_term = 0;
    if (term) {
        if (strstr(term, "xterm") || strstr(term, "screen") ||
            strstr(term, "tmux") || strstr(term, "kitty") ||
            strstr(term, "alacritty") || strstr(term, "wezterm") ||
            strstr(term, "ghostty") || strstr(term, "rio")) {
            known_good_term = 1;
        }
    }

    g_unicode_supported = (has_utf8 && known_good_term) ? 1 : 0;
    return g_unicode_supported;
}

void oneshot_ui_init(void) {
    init_color_cache();
    g_ui_style = oneshot_ui_get_style();
    g_terminal_width = get_terminal_width();
    g_unicode_supported = check_unicode_support();

    LOG_DEBUG("[ONESHOT_UI] Initialized: style=%d, width=%d, unicode=%d",
              g_ui_style, g_terminal_width, g_unicode_supported);
}

OneshotUIStyle oneshot_ui_get_style(void) {
    const char *style_env = getenv("KLAWED_ONESHOT_STYLE");
    if (!style_env) {
        return ONESHOT_UI_STYLE_BOXES;  // Default
    }

    if (strcmp(style_env, "compact") == 0 || strcmp(style_env, "minimal") == 0) {
        return ONESHOT_UI_STYLE_COMPACT;
    } else if (strcmp(style_env, "minimal") == 0) {
        return ONESHOT_UI_STYLE_MINIMAL;
    } else if (strcmp(style_env, "boxes") == 0 || strcmp(style_env, "full") == 0) {
        return ONESHOT_UI_STYLE_BOXES;
    }

    return ONESHOT_UI_STYLE_BOXES;
}

int oneshot_ui_supports_unicode(void) {
    return check_unicode_support();
}

void oneshot_ui_reset(void) {
    printf("%s", g_colors.reset);
    fflush(stdout);
}

const char *oneshot_ui_get_status_icon(OneshotStatus status) {
    if (!check_unicode_support()) {
        // ASCII fallback
        switch (status) {
            case ONESHOT_STATUS_SUCCESS: return "[OK]";
            case ONESHOT_STATUS_ERROR: return "[ERR]";
            case ONESHOT_STATUS_WARNING: return "[!]";
            case ONESHOT_STATUS_RUNNING: return "[*]";
            default: return "[?]";
        }
    }

    switch (status) {
        case ONESHOT_STATUS_SUCCESS: return ICON_SUCCESS;
        case ONESHOT_STATUS_ERROR: return ICON_ERROR;
        case ONESHOT_STATUS_WARNING: return ICON_WARNING;
        case ONESHOT_STATUS_RUNNING: return ICON_RUNNING;
        default: return ICON_BULLET;
    }
}

void oneshot_ui_print_tool_header(const char *tool_name, const char *tool_details, int is_compact) {
    if (!g_colors.initialized) init_color_cache();

    if (is_compact || g_ui_style == ONESHOT_UI_STYLE_COMPACT) {
        // Compact style: Tool name and details on one line with arrow
        printf("%s%s%s %s%s%s",
               g_colors.tool, ICON_ARROW, g_colors.reset,
               g_colors.tool_bold, tool_name, g_colors.reset);
        if (tool_details && tool_details[0]) {
            printf(" %s%s%s", g_colors.dim, tool_details, g_colors.reset);
        }
        printf(" ");
        fflush(stdout);
        return;
    }

    if (g_ui_style == ONESHOT_UI_STYLE_MINIMAL) {
        // Minimal: just show we're doing something
        return;
    }

    // Full box style
    int width = get_terminal_width();
    int max_content_width = width - 4;  // Account for borders and padding

    // Build the header content
    char header[1024];
    if (tool_details && tool_details[0]) {
        snprintf(header, sizeof(header), "%s %s", tool_name, tool_details);
    } else {
        strlcpy(header, tool_name, sizeof(header));
    }

    // Truncate if necessary
    size_t header_len = strlen(header);
    if ((int)header_len > max_content_width - 4) {
        header[max_content_width - 7] = '.';
        header[max_content_width - 6] = '.';
        header[max_content_width - 5] = '.';
        header[max_content_width - 4] = '\0';
    }

    if (check_unicode_support()) {
        // Print top border with title
        printf("%s%s%s %s%s%s %s",
               g_colors.dim, BOX_TOP_LEFT, g_colors.reset,
               g_colors.tool_bold, header, g_colors.reset,
               g_colors.dim);

        // Fill rest of top border
        int fill_width = max_content_width - (int)header_len - 1;
        for (int i = 0; i < fill_width; i++) {
            printf("%s", BOX_HORIZONTAL);
        }
        printf("%s%s\n", BOX_TOP_RIGHT, g_colors.reset);
    } else {
        // ASCII fallback
        printf("+-- %s%s%s --", g_colors.tool_bold, header, g_colors.reset);
        int fill_width = max_content_width - (int)header_len - 5;
        for (int i = 0; i < fill_width; i++) {
            printf("-");
        }
        printf("+\n");
    }

    fflush(stdout);
}

void oneshot_ui_print_tool_footer(OneshotStatus status, const char *summary, int is_compact) {
    if (!g_colors.initialized) init_color_cache();

    if (is_compact || g_ui_style == ONESHOT_UI_STYLE_COMPACT) {
        // Compact: just show status icon and summary on same line
        const char *icon = oneshot_ui_get_status_icon(status);
        const char *color = g_colors.success;
        if (status == ONESHOT_STATUS_ERROR) color = g_colors.error;
        else if (status == ONESHOT_STATUS_WARNING) color = g_colors.warning;

        printf("%s%s%s", color, icon, g_colors.reset);
        if (summary && summary[0]) {
            printf(" %s%s%s", g_colors.dim, summary, g_colors.reset);
        }
        printf("\n");
        fflush(stdout);
        return;
    }

    if (g_ui_style == ONESHOT_UI_STYLE_MINIMAL) {
        return;
    }

    // Full box style
    int width = get_terminal_width();
    int max_content_width = width - 4;

    // Build footer content
    char footer[512];
    const char *icon = oneshot_ui_get_status_icon(status);
    const char *color = g_colors.success;
    if (status == ONESHOT_STATUS_ERROR) color = g_colors.error;
    else if (status == ONESHOT_STATUS_WARNING) color = g_colors.warning;

    if (summary && summary[0]) {
        snprintf(footer, sizeof(footer), "%s %s", icon, summary);
    } else {
        strlcpy(footer, icon, sizeof(footer));
    }

    if (check_unicode_support()) {
        // Print bottom border with status
        printf("%s%s%s", g_colors.dim, BOX_T_LEFT, g_colors.reset);

        // Content with color
        int content_width = (int)strlen(footer) + 2;  // +2 for spaces
        printf(" %s%s%s%s ", color, footer, g_colors.reset, g_colors.dim);

        // Fill rest of border
        int fill_width = max_content_width - content_width;
        for (int i = 0; i < fill_width; i++) {
            printf("%s", BOX_HORIZONTAL);
        }
        printf("%s%s\n", BOX_BOTTOM_RIGHT, g_colors.reset);
    } else {
        // ASCII fallback
        printf("|  %s%s%s%s  ", color, footer, g_colors.reset, g_colors.dim);
        int fill_width = max_content_width - (int)strlen(footer) - 4;
        for (int i = 0; i < fill_width; i++) {
            printf("-");
        }
        printf("|\n%s", g_colors.reset);
    }

    fflush(stdout);
}

void oneshot_ui_print_content(const char *content, int is_output) {
    (void)is_output;  // Unused for now, reserved for future styling differences
    if (!content || !content[0]) return;
    if (!g_colors.initialized) init_color_cache();

    if (g_ui_style == ONESHOT_UI_STYLE_MINIMAL) {
        printf("%s", content);
        return;
    }

    if (g_ui_style == ONESHOT_UI_STYLE_COMPACT) {
        // In compact mode, just print content with minimal indentation
        // Don't print trailing newlines to keep things tight
        const char *p = content;
        int first = 1;
        while (*p) {
            if (first) {
                first = 0;
            } else if (p == content || *(p-1) == '\n') {
                printf("  ");  // Indent continuation lines
            }
            putchar(*p);
            p++;
        }
        return;
    }

    // Full box style - indent content
    const char *p = content;
    int at_line_start = 1;

    while (*p) {
        if (at_line_start) {
            printf("%s%s%s ", g_colors.dim, BOX_VERTICAL, g_colors.reset);
            at_line_start = 0;
        }

        if (*p == '\n') {
            putchar(*p);
            at_line_start = 1;
        } else {
            putchar(*p);
        }
        p++;
    }

    // Ensure newline at end
    if (!at_line_start && content[strlen(content)-1] != '\n') {
        printf("\n");
    }

    fflush(stdout);
}

void oneshot_ui_print_assistant(const char *text) {
    if (!text || !text[0]) return;
    if (!g_colors.initialized) init_color_cache();

    // Skip whitespace-only content
    const char *p = text;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        p++;
    }
    if (!*p) return;

    if (g_ui_style == ONESHOT_UI_STYLE_COMPACT || g_ui_style == ONESHOT_UI_STYLE_MINIMAL) {
        printf("%s%s%s\n", g_colors.assistant, p, g_colors.reset);
        fflush(stdout);
        return;
    }

    // Box style for assistant messages
    int width = get_terminal_width();
    int max_content_width = width - 4;

    // Print top border
    if (check_unicode_support()) {
        printf("%s%s", g_colors.dim, BOX_TOP_LEFT);
        for (int i = 0; i < max_content_width - 2; i++) {
            printf("%s", BOX_HORIZONTAL);
        }
        printf("%s%s\n", BOX_TOP_RIGHT, g_colors.reset);

        // Print "Assistant" label line
        printf("%s%s%s Assistant %s\n",
               g_colors.dim, BOX_VERTICAL, g_colors.reset,
               g_colors.dim);
    } else {
        printf("+");
        for (int i = 0; i < max_content_width - 2; i++) {
            printf("-");
        }
        printf("+\n| Assistant |\n");
    }

    // Print content
    printf("%s", g_colors.assistant);
    oneshot_ui_print_content(p, 0);
    printf("%s", g_colors.reset);

    // Print bottom border
    if (check_unicode_support()) {
        printf("%s%s", g_colors.dim, BOX_BOTTOM_LEFT);
        for (int i = 0; i < max_content_width - 2; i++) {
            printf("%s", BOX_HORIZONTAL);
        }
        printf("%s%s\n", BOX_BOTTOM_RIGHT, g_colors.reset);
    } else {
        printf("+");
        for (int i = 0; i < max_content_width - 2; i++) {
            printf("-");
        }
        printf("+\n");
    }

    fflush(stdout);
}

void oneshot_ui_print_error(const char *text) {
    if (!text || !text[0]) return;
    if (!g_colors.initialized) init_color_cache();

    fprintf(stderr, "%s%s %s%s%s\n",
            g_colors.error, oneshot_ui_get_status_icon(ONESHOT_STATUS_ERROR),
            g_colors.reset, g_colors.error, text);
    fprintf(stderr, "%s", g_colors.reset);
}

void oneshot_ui_print_summary(int prompt_tokens, int completion_tokens, int cached_tokens, long duration_ms) {
    if (!g_colors.initialized) init_color_cache();

    int total = prompt_tokens + completion_tokens;

    if (g_ui_style == ONESHOT_UI_STYLE_COMPACT || g_ui_style == ONESHOT_UI_STYLE_MINIMAL) {
        // Compact: single line
        printf("%s%s%s ", g_colors.dim, ICON_DASH ICON_DASH ICON_DASH, g_colors.reset);
        printf("Tokens: %s%d%s (%s%d%s prompt, %s%d%s completion",
               g_colors.tool_bold, total, g_colors.reset,
               g_colors.dim, prompt_tokens, g_colors.reset,
               g_colors.dim, completion_tokens, g_colors.reset);
        if (cached_tokens > 0) {
            printf(", %s%d%s cached", g_colors.dim, cached_tokens, g_colors.reset);
        }
        printf(")");
        if (duration_ms > 0) {
            printf(" | %s%ldms%s", g_colors.dim, duration_ms, g_colors.reset);
        }
        printf("\n");
        fflush(stdout);
        return;
    }

    // Full box style
    int width = get_terminal_width();
    int max_content_width = width - 4;

    if (check_unicode_support()) {
        printf("%s%s", g_colors.dim, BOX_BOTTOM_LEFT);
        for (int i = 0; i < max_content_width - 2; i++) {
            printf("%s", BOX_HORIZONTAL);
        }
        printf("%s%s\n", BOX_BOTTOM_RIGHT, g_colors.reset);
    }

    printf("\n%s%s Summary %s\n", g_colors.tool_bold, BOX_HORIZONTAL BOX_HORIZONTAL, g_colors.reset);
    printf("  %sTokens:%s %d total", g_colors.dim, g_colors.reset, total);
    printf(" (%d prompt, %d completion", prompt_tokens, completion_tokens);
    if (cached_tokens > 0) {
        printf(", %d cached free", cached_tokens);
    }
    printf(")\n");

    if (duration_ms > 0) {
        printf("  %sTime:%s %ld.%03lds\n",
               g_colors.dim, g_colors.reset,
               duration_ms / 1000, duration_ms % 1000);
    }

    fflush(stdout);
}

void oneshot_ui_print_separator(int width) {
    if (!g_colors.initialized) init_color_cache();

    if (width <= 0) {
        width = get_terminal_width();
    }

    printf("%s", g_colors.dim);
    for (int i = 0; i < width - 1; i++) {
        if (check_unicode_support()) {
            printf("%s", BOX_HORIZONTAL);
        } else {
            printf("-");
        }
    }
    printf("%s\n", g_colors.reset);
    fflush(stdout);
}

void oneshot_ui_print_box(const char *title, const char *content, int width) {
    if (!content) return;
    if (!g_colors.initialized) init_color_cache();

    if (width <= 0) {
        width = get_terminal_width() - 4;
    }

    if (g_ui_style == ONESHOT_UI_STYLE_COMPACT || g_ui_style == ONESHOT_UI_STYLE_MINIMAL) {
        if (title) {
            printf("%s%s:%s %s\n", g_colors.tool, title, g_colors.reset, content);
        } else {
            printf("%s\n", content);
        }
        fflush(stdout);
        return;
    }

    // Full box
    int content_width = width - 4;  // Borders and padding

    if (check_unicode_support()) {
        // Top border
        printf("%s%s", g_colors.dim, BOX_TOP_LEFT);
        if (title) {
            printf(" %s%s%s ", g_colors.tool_bold, title, g_colors.dim);
            int fill = content_width - (int)strlen(title) - 2;
            for (int i = 0; i < fill; i++) {
                printf("%s", BOX_HORIZONTAL);
            }
        } else {
            for (int i = 0; i < content_width; i++) {
                printf("%s", BOX_HORIZONTAL);
            }
        }
        printf("%s%s\n", BOX_TOP_RIGHT, g_colors.reset);

        // Content (simple version - just print as-is with borders)
        printf("%s%s%s %s%s%s%s\n",
               g_colors.dim, BOX_VERTICAL, g_colors.reset,
               content,
               g_colors.dim, BOX_VERTICAL, g_colors.reset);

        // Bottom border
        printf("%s%s", g_colors.dim, BOX_BOTTOM_LEFT);
        for (int i = 0; i < content_width; i++) {
            printf("%s", BOX_HORIZONTAL);
        }
        printf("%s%s\n", BOX_BOTTOM_RIGHT, g_colors.reset);
    } else {
        // ASCII
        printf("+");
        if (title) {
            printf("- %s -", title);
            int fill = content_width - (int)strlen(title) - 4;
            for (int i = 0; i < fill; i++) printf("-");
        } else {
            for (int i = 0; i < content_width; i++) printf("-");
        }
        printf("+\n| %s |\n+", content);
        for (int i = 0; i < content_width; i++) printf("-");
        printf("+\n");
    }

    fflush(stdout);
}
