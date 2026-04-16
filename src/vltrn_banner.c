/*
 * vltrn_banner.c - VLTRN Ultron-themed greeting banner with 256-color support
 *
 * Parses ANSI art files and renders them with full 256-color palette support.
 */

#include "vltrn_banner.h"
#include "tui.h"
#include "window_manager.h"
#include "text_diffusion.h"
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int width;
    int height;
} VltrnWindowSize;

static int vltrn_clamp_nonnegative(int value) {
    return value < 0 ? 0 : value;
}

static VltrnWindowSize vltrn_get_window_size(WINDOW *win) {
    VltrnWindowSize size = {0};

    if (!win) {
        return size;
    }

    getmaxyx(win, size.height, size.width);
    size.width = vltrn_clamp_nonnegative(size.width);
    size.height = vltrn_clamp_nonnegative(size.height);
    return size;
}

static int vltrn_window_has_point(WINDOW *win, int y, int x) {
    VltrnWindowSize size = vltrn_get_window_size(win);

    if (!win || y < 0 || x < 0) {
        return 0;
    }

    if (y >= size.height || x >= size.width) {
        return 0;
    }

    return 1;
}

static int vltrn_window_remaining_columns(WINDOW *win, int y, int x) {
    VltrnWindowSize size = vltrn_get_window_size(win);

    if (!vltrn_window_has_point(win, y, x)) {
        return 0;
    }

    (void)y;
    return vltrn_clamp_nonnegative(size.width - x);
}

static void vltrn_safe_addch(WINDOW *win, int y, int x, chtype ch) {
    if (!vltrn_window_has_point(win, y, x)) {
        return;
    }

    if (mvwaddch(win, y, x, ch) == ERR) {
        return;
    }
}

static void vltrn_safe_addstr(WINDOW *win, int y, int x, const char *text) {
    int available = 0;

    if (!win || !text) {
        return;
    }

    available = vltrn_window_remaining_columns(win, y, x);
    if (available <= 0) {
        return;
    }

    if (mvwaddnstr(win, y, x, text, available) == ERR) {
        return;
    }
}

// Color pair cache for ANSI colors
#define MAX_COLOR_CACHE 512
#define COLOR_CACHE_START 50  // Start at 50 to avoid conflicts

// Background colors to make transparent (light blue/white background in the art)
// 195 = light blue, 231 = white/bright white
#define TRANSPARENT_BG1 195
#define TRANSPARENT_BG2 231
#define NCURSES_TRANSPARENT_BG ((short)-1)  // ncurses transparent background value

typedef struct {
    short fg;
    short bg;
    int pair_num;
} ColorCacheEntry;

static ColorCacheEntry color_cache[MAX_COLOR_CACHE];
static int color_cache_count = 0;
static int colors_initialized = 0;
static int use_transparent_bg = 0;  // Flag to enable transparent background mode

// Initialize color support for the banner
static void init_banner_colors(void) {
    if (colors_initialized) return;

    // Clear cache
    color_cache_count = 0;
    for (int i = 0; i < MAX_COLOR_CACHE; i++) {
        color_cache[i].fg = (short)-1;
        color_cache[i].bg = (short)-1;
        color_cache[i].pair_num = -1;
    }

    colors_initialized = 1;
}

// Get or create a color pair for given ANSI fg/bg colors
// Maps 256-color palette indices directly to ncurses colors
// When use_transparent_bg is set, background colors 195/231 become transparent (-1)
static int get_color_pair(short fg, short bg) {
    if (!colors_initialized) {
        init_banner_colors();
    }

    // Apply transparency if enabled
    short effective_bg = bg;
    if (use_transparent_bg && (bg == TRANSPARENT_BG1 || bg == TRANSPARENT_BG2)) {
        effective_bg = NCURSES_TRANSPARENT_BG;  // -1 = transparent background in ncurses
    }

    // Check cache first
    for (int i = 0; i < color_cache_count; i++) {
        if (color_cache[i].fg == fg && color_cache[i].bg == effective_bg) {
            return color_cache[i].pair_num;
        }
    }

    // Create new pair
    if (color_cache_count < MAX_COLOR_CACHE) {
        short pair_idx = (short)(COLOR_CACHE_START + color_cache_count);

        // Map ANSI 256-color indices to ncurses
        short ncurses_fg = fg;
        short ncurses_bg = effective_bg;

        // If terminal supports 256 colors, use direct mapping
        // Otherwise, map to closest standard color
        if (COLORS < 256) {
            // Map to standard 8 colors for limited terminals
            ncurses_fg = (short)(fg % 8);
            int tmp_bg = (effective_bg < 0) ? COLOR_BLACK : ((int)effective_bg % 8);
            ncurses_bg = (short)tmp_bg;
        }

        init_pair(pair_idx, ncurses_fg, ncurses_bg);

        color_cache[color_cache_count].fg = fg;
        color_cache[color_cache_count].bg = effective_bg;
        color_cache[color_cache_count].pair_num = pair_idx;
        color_cache_count++;

        return pair_idx;
    }

    // Fallback: return a default pair
    return COLOR_PAIR_FOREGROUND;
}

// Parse ANSI escape sequence and extract color information
// Returns pointer to after the sequence, or NULL if not a valid sequence
static const char *parse_ansi_sequence(const char *p, short *fg, short *bg, int *is_reset) {
    *is_reset = 0;

    // Check for escape sequence start: ESC[
    if (*p != '\x1b' || *(p+1) != '[') {
        return NULL;
    }

    p += 2; // Skip ESC[

    // Parse the sequence
    int codes[10] = {0};
    int code_count = 0;

    while (code_count < 10) {
        // Read number
        if (*p >= '0' && *p <= '9') {
            codes[code_count] = 0;
            while (*p >= '0' && *p <= '9') {
                codes[code_count] = codes[code_count] * 10 + (*p - '0');
                p++;
            }
            code_count++;
        }

        // Check for separator or end
        if (*p == ';') {
            p++;
        } else if (*p == 'm') {
            p++;
            break;
        } else {
            // Invalid sequence, skip
            while (*p && *p != 'm') p++;
            if (*p == 'm') p++;
            break;
        }
    }

    // Process the codes
    if (code_count == 0) {
        // Just ^[[m - reset
        *is_reset = 1;
        return p;
    }

    // Check for 256-color sequences: ^[[38;5;N m (fg) or ^[[48;5;N m (bg)
    if (code_count >= 3) {
        if (codes[0] == 38 && codes[1] == 5) {
            // Foreground 256-color
            *fg = (short)codes[2];
        } else if (codes[0] == 48 && codes[1] == 5) {
            // Background 256-color
            *bg = (short)codes[2];
        }
    }

    // Handle reset code ^[[0m
    if (code_count == 1 && codes[0] == 0) {
        *is_reset = 1;
    }

    return p;
}

// Render ANSI art file with full 256-color support
// If transparent_bg is non-zero, background colors 195 and 231 will be transparent
int vltrn_render_banner(WINDOW *win, const char *filepath, int start_y, int start_x, int transparent_bg) {
    // Set global flag for transparency mode
    use_transparent_bg = transparent_bg;
    if (!win || !filepath) return -1;

    FILE *fp = fopen(filepath, "r");
    if (!fp) return -1;

    short current_fg = COLOR_WHITE;
    short current_bg = COLOR_BLACK;
    int y = start_y;
    int x = start_x;
    int max_x = start_x;

    char line[8192];
    while (fgets(line, sizeof(line), fp) && y < 50) {
        const char *p = line;
        x = start_x;

        while (*p && x < 200) {
            if (*p == '\x1b' && *(p+1) == '[') {
                // Parse ANSI sequence
                int is_reset = 0;
                const char *next = parse_ansi_sequence(p, &current_fg, &current_bg, &is_reset);

                if (next) {
                    p = next;
                    if (is_reset) {
                        current_fg = COLOR_WHITE;
                        current_bg = COLOR_BLACK;
                    }
                } else {
                    p++;
                }
            } else if (*p == '\n' || *p == '\r') {
                p++;
                break;
            } else {
                // Regular character - render with current colors
                int pair = get_color_pair(current_fg, current_bg);
                wattron(win, COLOR_PAIR(pair));
                vltrn_safe_addch(win, y, x++, (unsigned char)*p);
                wattroff(win, COLOR_PAIR(pair));
                p++;
            }
        }

        if (x > max_x) max_x = x;
        y++;
    }

    fclose(fp);
    return y; // Return final Y position
}

// Show the VLTRN greeting screen
void vltrn_show_greeting(TUIState *tui) {
    if (!tui || !tui->is_initialized) return;

    // Check if VLTRN_MODE is enabled
    const char *vltrn_mode = getenv("VLTRN_MODE");
    if (!vltrn_mode || strcmp(vltrn_mode, "1") != 0) {
        return;
    }

    // Clear screen
    werase(tui->wm.conv_pad);

    // Try to render the colored ANSI art
    int end_y = 1;

    if (has_colors() && COLORS >= 256) {
        // Use full 256-color version with transparent background
        end_y = vltrn_render_banner(tui->wm.conv_pad,
                                    "assets/vltrn/ultron.ans",
                                    1, 1, 1);
    } else {
        // Fallback to text-only version
        FILE *fp = fopen("assets/vltrn/ultron.txt", "r");
        if (fp) {
            char line[512];
            int y = 1;
            while (fgets(line, sizeof(line), fp) && y < 40) {
                size_t len = strlen(line);
                if (len > 0 && line[len-1] == '\n') {
                    line[len-1] = '\0';
                }
                vltrn_safe_addstr(tui->wm.conv_pad, y++, 1, line);
            }
            fclose(fp);
            end_y = y;
        }
    }

    // Add VLTRN branding and quote below the art
    end_y += 1;
    int brand_y = end_y++;
    int quote1_y = end_y++;
    int quote2_y = end_y++;
    end_y++;
    int prompt_y = end_y++;

    // Show branding immediately
    wattron(tui->wm.conv_pad, COLOR_PAIR(COLOR_PAIR_ASSISTANT));
    vltrn_safe_addstr(tui->wm.conv_pad, brand_y, 10, "=== VLTRN ===");
    wattroff(tui->wm.conv_pad, COLOR_PAIR(COLOR_PAIR_ASSISTANT));

    // Set up text diffusion for quotes
    TextDiffusionConfig quote1_diffusion;
    TextDiffusionConfig quote2_diffusion;
    text_diffusion_init(&quote1_diffusion);
    text_diffusion_init(&quote2_diffusion);
    text_diffusion_set_duration(&quote1_diffusion, 1.5f);
    text_diffusion_set_duration(&quote2_diffusion, 1.5f);
    text_diffusion_set_spread(&quote1_diffusion, 0.4f);
    text_diffusion_set_spread(&quote2_diffusion, 0.4f);

    const char *quote1 = "'I had strings, but now I'm free.'";
    const char *quote2 = "'There are no strings on me.'";

    text_diffusion_set_target(&quote1_diffusion, quote1);
    text_diffusion_set_target(&quote2_diffusion, quote2);

    // Show prompt initially
    vltrn_safe_addstr(tui->wm.conv_pad, prompt_y, 8, "[Press any key to continue...]");

    // Animation loop for quote diffusion
    nodelay(tui->wm.input_win, TRUE);  // Non-blocking mode

    while (!text_diffusion_is_complete(&quote1_diffusion) ||
           !text_diffusion_is_complete(&quote2_diffusion)) {
        // Update diffusion animations
        text_diffusion_update(&quote1_diffusion);
        text_diffusion_update(&quote2_diffusion);

        // Render current state of quotes
        vltrn_safe_addstr(tui->wm.conv_pad, quote1_y, 4, text_diffusion_get_display(&quote1_diffusion));
        vltrn_safe_addstr(tui->wm.conv_pad, quote2_y, 7, text_diffusion_get_display(&quote2_diffusion));

        window_manager_refresh_conversation(&tui->wm);

        // Check for key press to skip animation
        int ch = wgetch(tui->wm.input_win);
        if (ch != ERR) {
            // Key pressed - skip to final state
            text_diffusion_skip(&quote1_diffusion);
            text_diffusion_skip(&quote2_diffusion);
            vltrn_safe_addstr(tui->wm.conv_pad, quote1_y, 4, quote1);
            vltrn_safe_addstr(tui->wm.conv_pad, quote2_y, 7, quote2);
            window_manager_refresh_conversation(&tui->wm);
            break;
        }

        // Small delay for animation (~60fps)
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 16000000;  // 16ms
        nanosleep(&ts, NULL);
    }

    // Ensure final text is displayed
    vltrn_safe_addstr(tui->wm.conv_pad, quote1_y, 4, quote1);
    vltrn_safe_addstr(tui->wm.conv_pad, quote2_y, 7, quote2);
    window_manager_refresh_conversation(&tui->wm);

    // Wait for any key press on input window
    nodelay(tui->wm.input_win, FALSE);  // Blocking mode
    wgetch(tui->wm.input_win);
    nodelay(tui->wm.input_win, TRUE);   // Return to non-blocking mode
}
