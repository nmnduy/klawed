/*
 * TUI Search Functionality
 *
 * Implements search capabilities in conversation view.
 */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "tui_search.h"
#include "tui.h"
#include "logger.h"
#include "window_manager.h"
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

typedef struct {
    int width;
    int height;
} TuiSearchPadSize;

static TuiSearchPadSize tui_search_get_pad_size(WINDOW *pad) {
    TuiSearchPadSize size = {0};

    if (!pad) {
        return size;
    }

    getmaxyx(pad, size.height, size.width);
    if (size.width < 0) {
        size.width = 0;
    }
    if (size.height < 0) {
        size.height = 0;
    }

    return size;
}

static int tui_search_pad_has_point(WINDOW *pad, int line, int col) {
    TuiSearchPadSize size = tui_search_get_pad_size(pad);

    if (!pad || line < 0 || col < 0) {
        return 0;
    }

    if (line >= size.height || col >= size.width) {
        return 0;
    }

    return 1;
}

static int tui_search_safe_mvwinch(WINDOW *pad, int line, int col, chtype *out_ch) {
    chtype ch = 0;

    if (!out_ch) {
        return 0;
    }

    if (!tui_search_pad_has_point(pad, line, col)) {
        return 0;
    }

    ch = mvwinch(pad, line, col);
    if (ch == (chtype)ERR) {
        return 0;
    }

    *out_ch = ch;
    return 1;
}

// Check if a line in pad is empty
int tui_search_is_line_empty(WINDOW *pad, int line) {
    if (!pad || line < 0) {
        return 0;
    }

    TuiSearchPadSize size = tui_search_get_pad_size(pad);

    if (line >= size.height) {
        return 0;
    }

    // Check first 100 columns (or pad width, whichever is smaller)
    int cols_to_check = size.width < 100 ? size.width : 100;

    for (int col = 0; col < cols_to_check; col++) {
        chtype ch = 0;
        char c = 0;

        if (!tui_search_safe_mvwinch(pad, line, col, &ch)) {
            continue;
        }

        c = (char)(ch & A_CHARTEXT);

        // If we find any non-whitespace character, line is not empty
        if (c != ' ' && c != '\t' && c != '\0' && c != '\n') {
            return 0;
        }
    }

    return 1;  // Line is empty
}

// Find next paragraph boundary (empty line) going down
int tui_search_find_next_paragraph(WINDOW *pad, int start_line, int max_lines) {
    if (!pad || start_line < 0 || max_lines <= 0) {
        return -1;
    }

    int pad_height, pad_width;
    getmaxyx(pad, pad_height, pad_width);
    (void)pad_height;  // May be unused in some paths
    (void)pad_width;  // Unused

    // Start searching from the line after start_line
    // Skip current position if it's already on an empty line
    int search_start = start_line + 1;

    // If we're on an empty line, skip past consecutive empty lines first
    while (search_start < max_lines && search_start < pad_height &&
           tui_search_is_line_empty(pad, search_start)) {
        search_start++;
    }

    // Now find the next empty line
    for (int line = search_start; line < max_lines && line < pad_height; line++) {
        if (tui_search_is_line_empty(pad, line)) {
            return line;
        }
    }

    // No paragraph boundary found, return max_lines (scroll to end)
    return max_lines - 1;
}

// Find previous paragraph boundary (empty line) going up
int tui_search_find_prev_paragraph(WINDOW *pad, int start_line, int max_lines) {
    if (!pad || start_line < 0 || max_lines <= 0) {
        return 0;
    }

    int pad_height, pad_width;
    getmaxyx(pad, pad_height, pad_width);
    (void)pad_height;  // Unused in this function
    (void)pad_width;  // Unused
    (void)max_lines;  // Unused

    // Start searching from the line before start_line
    // Skip current position if it's already on an empty line
    int search_start = start_line - 1;

    // If we're on an empty line, skip past consecutive empty lines first
    while (search_start >= 0 && tui_search_is_line_empty(pad, search_start)) {
        search_start--;
    }

    // Now find the previous empty line
    for (int line = search_start; line >= 0; line--) {
        if (tui_search_is_line_empty(pad, line)) {
            return line;
        }
    }

    // No paragraph boundary found, return 0 (scroll to top)
    return 0;
}

// Perform search in conversation pad
int tui_search_perform(TUIState *tui, const char *pattern, int direction) {
    if (!tui || !pattern || !pattern[0]) {
        return -1;
    }

    // Get current scroll position
    int current_scroll = window_manager_get_scroll_offset(&tui->wm);
    int content_lines = window_manager_get_content_lines(&tui->wm);

    // Determine start position for search
    int start_line;
    if (direction == 1) {  // Forward search
        // Start from line after current scroll position
        start_line = current_scroll + 1;
        if (start_line >= content_lines) {
            start_line = 0;  // Wrap around to beginning
        }
    } else {  // Backward search
        // Start from line before current scroll position
        start_line = current_scroll - 1;
        if (start_line < 0) {
            start_line = content_lines - 1;  // Wrap around to end
        }
    }

    // Search through lines in the pad
    int found_line = -1;
    int line = start_line;
    int steps = 0;
    int max_steps = content_lines;

    while (steps < max_steps) {
        // Check if line contains the pattern
        // We need to get the text from the pad
        WINDOW *pad = tui->wm.conv_pad;
        if (!pad) {
            break;
        }

        int pad_height, pad_width;
        getmaxyx(pad, pad_height, pad_width);

        if (line >= pad_height) {
            // Line doesn't exist in pad
            if (direction == 1) {
                line = 0;  // Wrap around
            } else {
                line = pad_height - 1;  // Wrap around
            }
            steps++;
            continue;
        }

        // Read a chunk of text from the pad line
        char line_text[256];
        int col = 0;
        int text_len = 0;

        // Read up to 255 characters from the line
        while (col < pad_width && text_len < 255) {
            chtype ch = 0;
            char c = 0;

            if (!tui_search_safe_mvwinch(pad, line, col, &ch)) {
                break;
            }

            c = (char)(ch & A_CHARTEXT);

            if (c == '\0' || c == '\n') {
                break;
            }

            line_text[text_len++] = c;
            col++;
        }
        line_text[text_len] = '\0';

        // Simple case-insensitive search
        char *pattern_lower = strdup(pattern);
        char *line_lower = strdup(line_text);
        if (pattern_lower && line_lower) {
            // Convert to lowercase for case-insensitive search
            for (int i = 0; pattern_lower[i]; i++) {
                pattern_lower[i] = (char)tolower((unsigned char)pattern_lower[i]);
            }
            for (int i = 0; line_lower[i]; i++) {
                line_lower[i] = (char)tolower((unsigned char)line_lower[i]);
            }

            if (strstr(line_lower, pattern_lower) != NULL) {
                found_line = line;
                free(pattern_lower);
                free(line_lower);
                break;
            }
        }

        free(pattern_lower);
        free(line_lower);

        // Move to next line
        if (direction == 1) {
            line++;
            if (line >= content_lines) {
                line = 0;  // Wrap around
            }
        } else {
            line--;
            if (line < 0) {
                line = content_lines - 1;  // Wrap around
            }
        }
        steps++;
    }

    if (found_line >= 0) {
        // Scroll to found line
        tui_scroll_conversation(tui, found_line - current_scroll);
        tui->last_search_match_line = found_line;

        // Show status message
        char status_msg[256];
        snprintf(status_msg, sizeof(status_msg), "Search: %s (match at line %d)",
                pattern, found_line + 1);
        tui_update_status(tui, status_msg);

        return 1;
    } else {
        // No match found
        char status_msg[256];
        snprintf(status_msg, sizeof(status_msg), "Search: %s (no match)", pattern);
        tui_update_status(tui, status_msg);

        return 0;
    }
}

// Find next search match (repeats last search)
int tui_search_next(TUIState *tui) {
    if (!tui || !tui->last_search_pattern || !tui->last_search_pattern[0]) {
        return -1;
    }
    return tui_search_perform(tui, tui->last_search_pattern, tui->search_direction);
}

// Find previous search match (reverses last search)
int tui_search_prev(TUIState *tui) {
    if (!tui || !tui->last_search_pattern || !tui->last_search_pattern[0]) {
        return -1;
    }
    return tui_search_perform(tui, tui->last_search_pattern, -tui->search_direction);
}
