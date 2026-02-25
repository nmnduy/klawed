/*
 * TUI Search Functionality
 *
 * Implements search capabilities in conversation view:
 * - Forward and backward search
 * - Case-insensitive pattern matching
 * - Search result navigation (n/N)
 * - Paragraph navigation
 * - Empty line detection
 */

#ifndef TUI_SEARCH_H
#define TUI_SEARCH_H

// Forward declarations
typedef struct TUIStateStruct TUIState;
typedef struct _win_st WINDOW;

// Perform search in conversation pad
// pattern: Search pattern (case-insensitive)
// direction: 1 for forward, -1 for backward
// Returns line number of match, or -1 if no match found
int tui_search_perform(TUIState *tui, const char *pattern, int direction);

// Find next search match (repeats last search)
// Returns line number of match, or -1 if no match found
int tui_search_next(TUIState *tui);

// Find previous search match (reverses last search)
// Returns line number of match, or -1 if no match found
int tui_search_prev(TUIState *tui);

// Check if a line in pad is empty
// Returns 1 if empty, 0 otherwise
int tui_search_is_line_empty(WINDOW *pad, int line);

// Find next paragraph boundary (empty line)
// start_line: Line to start searching from
// max_lines: Maximum number of lines in pad
// Returns line number of next paragraph start, or -1 if none found
int tui_search_find_next_paragraph(WINDOW *pad, int start_line, int max_lines);

// Find previous paragraph boundary (empty line)
// start_line: Line to start searching from
// max_lines: Maximum number of lines in pad
// Returns line number of previous paragraph start, or -1 if none found
int tui_search_find_prev_paragraph(WINDOW *pad, int start_line, int max_lines);

#endif // TUI_SEARCH_H
