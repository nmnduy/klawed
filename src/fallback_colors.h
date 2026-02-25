/*
 * fallback_colors.h - Centralized Fallback Color Definitions
 *
 * This header provides the single source of truth for all fallback ANSI colors
 * used throughout the application when the colorscheme system is not available
 * or fails to load. All modules should use these constants instead of defining
 * their own ANSI color codes.
 */

#ifndef FALLBACK_COLORS_H
#define FALLBACK_COLORS_H

// ANSI reset code - used to reset all formatting
#define ANSI_RESET "\033[0m"

// Primary color mappings for different UI elements
// These are used when colorscheme colors are not available

// Main text color - Default terminal color (light gray/white)
#define ANSI_FALLBACK_FOREGROUND "\033[37m"

// User messages - Green (standard terminal color)
#define ANSI_FALLBACK_USER "\033[32m"

// Assistant messages - Blue (standard terminal color)
#define ANSI_FALLBACK_ASSISTANT "\033[34m"

// Tool execution - Magenta (distinct from assistant cyan)
#define ANSI_FALLBACK_TOOL "\033[35m"

// Error messages - Red (standard terminal color)
#define ANSI_FALLBACK_ERROR "\033[31m"

// Status messages - Cyan (standard terminal color)
#define ANSI_FALLBACK_STATUS "\033[36m"

// Headers/accents - Bold Cyan (for emphasis)
#define ANSI_FALLBACK_HEADER "\033[1;36m"

// Diff-specific colors
// Added lines (green)
#define ANSI_FALLBACK_DIFF_ADD "\033[32m"
// Removed lines (red)
#define ANSI_FALLBACK_DIFF_REMOVE "\033[31m"
// Diff metadata/headers (cyan)
#define ANSI_FALLBACK_DIFF_HEADER "\033[36m"
// Line numbers and context markers (dim)
#define ANSI_FALLBACK_DIFF_CONTEXT "\033[2m"

// Additional colors for specific use cases
#define ANSI_FALLBACK_GREEN "\033[32m"
#define ANSI_FALLBACK_YELLOW "\033[33m"
#define ANSI_FALLBACK_BLUE "\033[34m"
#define ANSI_FALLBACK_MAGENTA "\033[35m"
#define ANSI_FALLBACK_CYAN "\033[36m"
#define ANSI_FALLBACK_WHITE "\033[37m"

// Bold variants for emphasis
#define ANSI_FALLBACK_BOLD_GREEN "\033[1;32m"
#define ANSI_FALLBACK_BOLD_YELLOW "\033[1;33m"
#define ANSI_FALLBACK_BOLD_BLUE "\033[1;34m"
#define ANSI_FALLBACK_BOLD_RED "\033[1;31m"
#define ANSI_FALLBACK_BOLD_MAGENTA "\033[1;35m"
#define ANSI_FALLBACK_BOLD_CYAN "\033[1;36m"
#define ANSI_FALLBACK_BOLD_WHITE "\033[1;37m"

// Text formatting codes
#define ANSI_FALLBACK_BOLD "\033[1m"
#define ANSI_FALLBACK_DIM "\033[2m"

// Helper function to get fallback color by element type
// This provides a programmatic way to access fallback colors
// Returns the ANSI escape sequence for given element
static inline const char* get_fallback_color(int element_type) {
    switch (element_type) {
        case 0: // FOREGROUND
            return ANSI_FALLBACK_FOREGROUND;
        case 1: // USER
            return ANSI_FALLBACK_USER;
        case 2: // ASSISTANT
            return ANSI_FALLBACK_ASSISTANT;
        case 3: // TOOL
            return ANSI_FALLBACK_TOOL;
        case 4: // ERROR
            return ANSI_FALLBACK_ERROR;
        case 5: // STATUS
            return ANSI_FALLBACK_STATUS;
        case 6: // HEADER
            return ANSI_FALLBACK_HEADER;
        case 7: // DIFF_ADD
            return ANSI_FALLBACK_DIFF_ADD;
        case 8: // DIFF_REMOVE
            return ANSI_FALLBACK_DIFF_REMOVE;
        case 9: // DIFF_HEADER
            return ANSI_FALLBACK_DIFF_HEADER;
        case 10: // DIFF_CONTEXT
            return ANSI_FALLBACK_DIFF_CONTEXT;
        default:
            return "";
    }
}

// Element type constants for use with get_fallback_color()
#define FALLBACK_FOREGROUND 0
#define FALLBACK_USER 1
#define FALLBACK_ASSISTANT 2
#define FALLBACK_TOOL 3
#define FALLBACK_ERROR 4
#define FALLBACK_STATUS 5
#define FALLBACK_HEADER 6
#define FALLBACK_DIFF_ADD 7
#define FALLBACK_DIFF_REMOVE 8
#define FALLBACK_DIFF_HEADER 9
#define FALLBACK_DIFF_CONTEXT 10

#endif // FALLBACK_COLORS_H
