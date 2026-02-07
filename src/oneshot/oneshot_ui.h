/*
 * oneshot_ui.h - Enhanced UI for oneshot mode with themed colors and boxes
 *
 * Provides beautiful, theme-aware output formatting for oneshot mode execution.
 * Features:
 * - Unicode box-drawing borders
 * - Theme-aware colors from Kitty themes
 * - Status icons and indicators
 * - Compact mode option
 */

#ifndef ONESHOT_UI_H
#define ONESHOT_UI_H

#include <cjson/cJSON.h>

// Output style modes
typedef enum {
    ONESHOT_UI_STYLE_BOXES = 0,     // Full box-drawing borders (default)
    ONESHOT_UI_STYLE_COMPACT = 1,   // Minimal output, single line per tool
    ONESHOT_UI_STYLE_MINIMAL = 2    // Ultra-minimal, just results
} OneshotUIStyle;

// Status indicators for tool execution
typedef enum {
    ONESHOT_STATUS_SUCCESS = 0,
    ONESHOT_STATUS_ERROR = 1,
    ONESHOT_STATUS_WARNING = 2,
    ONESHOT_STATUS_RUNNING = 3
} OneshotStatus;

// Box-drawing characters (Unicode)
#define BOX_TOP_LEFT     "\u250c"  // ┌
#define BOX_TOP_RIGHT    "\u2510"  // ┐
#define BOX_BOTTOM_LEFT  "\u2514"  // └
#define BOX_BOTTOM_RIGHT "\u2518"  // ┘
#define BOX_HORIZONTAL   "\u2500"  // ─
#define BOX_VERTICAL     "\u2502"  // │
#define BOX_T_LEFT       "\u251c"  // ├
#define BOX_T_RIGHT      "\u2524"  // ┤
#define BOX_T_TOP        "\u252c"  // ┬
#define BOX_T_BOTTOM     "\u2534"  // ┴
#define BOX_CROSS        "\u253c"  // ┼

// Status icons
#define ICON_SUCCESS     "\u2713"  // ✓
#define ICON_ERROR       "\u2717"  // ✗
#define ICON_WARNING     "\u26a0"  // ⚠
#define ICON_RUNNING     "\u25cb"  // ○ (will animate)
#define ICON_ARROW       "\u2192"  // →
#define ICON_BULLET      "\u2022"  // ●
#define ICON_DASH        "\u2500"  // ─

// Spinner frames for running state (for future animation support)
#define SPINNER_COUNT 4
extern const char *SPINNER_FRAMES[SPINNER_COUNT];

/**
 * Initialize the oneshot UI system
 * Detects terminal capabilities and loads appropriate style
 */
void oneshot_ui_init(void);

/**
 * Get the current UI style from environment
 * Checks KLAWED_ONESHOT_STYLE environment variable
 * @return Selected UI style, defaults to ONESHOT_UI_STYLE_BOXES
 */
OneshotUIStyle oneshot_ui_get_style(void);

/**
 * Check if terminal supports Unicode box-drawing
 * @return 1 if supported, 0 otherwise
 */
int oneshot_ui_supports_unicode(void);

/**
 * Print a styled tool execution header
 * @param tool_name Name of the tool being executed
 * @param tool_details Optional details about the tool call
 * @param is_compact Use compact style (single line)
 */
void oneshot_ui_print_tool_header(const char *tool_name, const char *tool_details, int is_compact);

/**
 * Print a styled tool execution footer with status
 * @param status Execution status
 * @param summary Optional summary text (e.g., "3 matches found")
 * @param is_compact Use compact style
 */
void oneshot_ui_print_tool_footer(OneshotStatus status, const char *summary, int is_compact);

/**
 * Print tool result content with proper indentation and styling
 * @param content The content to print
 * @param is_output Whether this is command output (affects styling)
 */
void oneshot_ui_print_content(const char *content, int is_output);

/**
 * Print assistant message with themed styling
 * @param text The assistant's message
 */
void oneshot_ui_print_assistant(const char *text);

/**
 * Print error message with themed styling
 * @param text The error message
 */
void oneshot_ui_print_error(const char *text);

/**
 * Print final summary with token usage and timing
 * @param prompt_tokens Number of prompt tokens used
 * @param completion_tokens Number of completion tokens used
 * @param cached_tokens Number of cached tokens (if any)
 * @param duration_ms Duration in milliseconds
 */
void oneshot_ui_print_summary(int prompt_tokens, int completion_tokens, int cached_tokens, long duration_ms);

/**
 * Print a horizontal separator line
 * @param width Width of the line (0 for auto-detect)
 */
void oneshot_ui_print_separator(int width);

/**
 * Get the appropriate icon for a status
 * @param status The status to get icon for
 * @return The icon string
 */
const char *oneshot_ui_get_status_icon(OneshotStatus status);

/**
 * Print a box with content - for important messages
 * @param title Box title (can be NULL)
 * @param content Content to display in the box
 * @param width Box width (0 for auto)
 */
void oneshot_ui_print_box(const char *title, const char *content, int width);

/**
 * Reset all colors and formatting
 */
void oneshot_ui_reset(void);

#endif // ONESHOT_UI_H
