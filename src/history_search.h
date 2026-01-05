/*
 * History Search - Interactive command history search for TUI
 *
 * Provides Ctrl+R history search functionality with:
 * - Fuzzy search through command history
 * - Incremental search filtering
 * - Vim-style result navigation
 */

#ifndef HISTORY_SEARCH_H
#define HISTORY_SEARCH_H

#include <ncurses.h>
#include <stddef.h>

// Maximum number of results to display
#define HISTORY_SEARCH_MAX_RESULTS 500

// History search result entry
typedef struct {
    char *command;        // Command text
    int score;            // Fuzzy match score (higher = better)
    int original_index;   // Original index in history array (for chronological sorting)
} HistorySearchResult;

// History search state
typedef struct {
    // Search state
    char *search_pattern;        // Current search pattern
    size_t pattern_len;          // Length of pattern
    size_t pattern_capacity;     // Capacity of pattern buffer

    // Results
    HistorySearchResult *results;   // Array of results
    int result_count;               // Number of results
    int result_capacity;            // Capacity of results array
    int selected_index;             // Currently selected result (0-indexed)
    int scroll_offset;              // Scroll offset for result list

    // Display window
    WINDOW *popup_win;           // Popup window for history search
    int popup_height;            // Height of popup window
    int popup_width;             // Width of popup window
    int popup_y;                 // Y position of popup
    int popup_x;                 // X position of popup

    // History data
    char **history_entries;      // Array of history entries (oldest -> newest)
    int history_count;           // Number of history entries

    // State flags
    int is_active;               // Whether search is active
} HistorySearchState;

// ============================================================================
// Lifecycle
// ============================================================================

// Initialize history search state
// Returns 0 on success, -1 on failure
int history_search_init(HistorySearchState *state);

// Free history search resources
void history_search_free(HistorySearchState *state);

// ============================================================================
// Search Operations
// ============================================================================

// Start history search (creates popup window)
// screen_height/width: terminal dimensions
// history_entries: array of history strings (oldest -> newest)
// history_count: number of history entries
// Returns 0 on success, -1 on failure
int history_search_start(HistorySearchState *state, int screen_height, int screen_width,
                         char **history_entries, int history_count);

// Stop history search (destroys popup window)
void history_search_stop(HistorySearchState *state);

// Update search pattern (triggers new search/filter)
// Returns 0 on success, -1 on failure
int history_search_update_pattern(HistorySearchState *state, const char *pattern);

// Add character to search pattern
// Returns 0 on success, -1 on failure
int history_search_add_char(HistorySearchState *state, char c);

// Remove last character from search pattern (backspace)
// Returns 0 on success, -1 on failure
int history_search_backspace(HistorySearchState *state);

// Clear search pattern
void history_search_clear_pattern(HistorySearchState *state);

// ============================================================================
// Navigation
// ============================================================================

// Move selection up
void history_search_select_prev(HistorySearchState *state);

// Move selection down
void history_search_select_next(HistorySearchState *state);

// Move selection up by half page
void history_search_page_up(HistorySearchState *state);

// Move selection down by half page
void history_search_page_down(HistorySearchState *state);

// Get currently selected command (caller must NOT free)
// Returns NULL if no selection
const char *history_search_get_selected(HistorySearchState *state);

// ============================================================================
// Display
// ============================================================================

// Render the history search popup
void history_search_render(HistorySearchState *state);

// ============================================================================
// Input Handling
// ============================================================================

// Process input character
// Returns:
//   0: continue (character processed)
//   1: selection made (call history_search_get_selected)
//  -1: cancelled (ESC pressed)
int history_search_process_key(HistorySearchState *state, int ch);

#endif // HISTORY_SEARCH_H
