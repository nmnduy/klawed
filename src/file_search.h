/*
 * File Search - Interactive file finder for TUI
 *
 * Provides Ctrl+F file search functionality with:
 * - Automatic tool detection (fd, rg, fzf, find)
 * - Incremental search filtering
 * - Timeout protection against hanging
 * - Vim-style result navigation
 */

#ifndef FILE_SEARCH_H
#define FILE_SEARCH_H

#include <ncurses.h>
#include <stddef.h>

// Maximum number of results to display
#define FILE_SEARCH_MAX_RESULTS 500

// Default timeout for search command (milliseconds)
#define FILE_SEARCH_DEFAULT_TIMEOUT_MS 3000

// Search result entry
typedef struct {
    char *path;          // File path (relative to search directory)
    int score;           // Fuzzy match score (higher = better)
} FileSearchResult;

// File search state
typedef struct {
    // Search state
    char *search_pattern;        // Current search pattern
    size_t pattern_len;          // Length of pattern
    size_t pattern_capacity;     // Capacity of pattern buffer

    // Results
    FileSearchResult *results;   // Array of results
    int result_count;            // Number of results
    int result_capacity;         // Capacity of results array
    int selected_index;          // Currently selected result (0-indexed)
    int scroll_offset;           // Scroll offset for result list

    // Display window
    WINDOW *popup_win;           // Popup window for file search
    int popup_height;            // Height of popup window
    int popup_width;             // Width of popup window
    int popup_y;                 // Y position of popup
    int popup_x;                 // X position of popup

    // Search configuration
    char *search_dir;            // Directory to search in
    int timeout_ms;              // Timeout for search command

    // Cached file list (for incremental filtering)
    char **file_cache;           // Cached file list
    int file_cache_count;        // Number of files in cache
    int file_cache_capacity;     // Capacity of cache
    int cache_valid;             // Whether cache is still valid

    // Tool detection
    int has_fd;                  // fd (fast find) available
    int has_rg;                  // ripgrep available
    int has_fzf;                 // fzf available
    int tools_detected;          // Whether tool detection has run

    // State flags
    int is_active;               // Whether search is active
} FileSearchState;

// ============================================================================
// Lifecycle
// ============================================================================

// Initialize file search state
// Returns 0 on success, -1 on failure
int file_search_init(FileSearchState *state);

// Free file search resources
void file_search_free(FileSearchState *state);

// ============================================================================
// Search Operations
// ============================================================================

// Start file search (creates popup window)
// screen_height/width: terminal dimensions
// search_dir: directory to search in (NULL for current directory)
// Returns 0 on success, -1 on failure
int file_search_start(FileSearchState *state, int screen_height, int screen_width,
                      const char *search_dir);

// Stop file search (destroys popup window)
void file_search_stop(FileSearchState *state);

// Update search pattern (triggers new search/filter)
// Returns 0 on success, -1 on failure
int file_search_update_pattern(FileSearchState *state, const char *pattern);

// Add character to search pattern
// Returns 0 on success, -1 on failure
int file_search_add_char(FileSearchState *state, char c);

// Remove last character from search pattern (backspace)
// Returns 0 on success, -1 on failure
int file_search_backspace(FileSearchState *state);

// Clear search pattern
void file_search_clear_pattern(FileSearchState *state);

// ============================================================================
// Navigation
// ============================================================================

// Move selection up
void file_search_select_prev(FileSearchState *state);

// Move selection down
void file_search_select_next(FileSearchState *state);

// Move selection up by half page
void file_search_page_up(FileSearchState *state);

// Move selection down by half page
void file_search_page_down(FileSearchState *state);

// Get currently selected path (caller must NOT free)
// Returns NULL if no selection
const char *file_search_get_selected(FileSearchState *state);

// ============================================================================
// Display
// ============================================================================

// Render the file search popup
void file_search_render(FileSearchState *state);

// ============================================================================
// Input Handling
// ============================================================================

// Process input character
// Returns:
//   0: continue (character processed)
//   1: selection made (call file_search_get_selected)
//  -1: cancelled (ESC pressed)
int file_search_process_key(FileSearchState *state, int ch);

#ifdef TEST_BUILD
// Internal functions exposed for testing
int fuzzy_score(const char *haystack, const char *needle);
int compare_results(const void *a, const void *b);
#endif

#endif // FILE_SEARCH_H
