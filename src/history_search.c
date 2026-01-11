/*
 * History Search - Interactive command history search for TUI
 */

#include "history_search.h"
#include "tui.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <bsd/string.h>
#include <stdio.h>
#include <ctype.h>

#define INITIAL_PATTERN_CAPACITY 256
#define INITIAL_RESULTS_CAPACITY 100
#define FUZZY_MAX_PATTERN 256
#define FUZZY_ADJACENT_BONUS 5
#define FUZZY_SEPARATOR_BONUS 10
#define FUZZY_CASE_MISMATCH_PENALTY 1

// ============================================================================
// Fuzzy Scoring (copied from file_search.c)
// ============================================================================

static int fuzzy_score(const char *haystack, const char *needle) {
    if (!needle || !needle[0]) {
        return 1;  // Empty pattern matches everything with minimal score
    }

    size_t nlen = strnlen(needle, FUZZY_MAX_PATTERN);
    if (nlen == 0) {
        return 1;
    }

    int score = 0;
    size_t hlen = strnlen(haystack, 4096);  // Increased from PATH_MAX for commands
    if (hlen == 0) {
        return 0;
    }
    size_t hidx = 0;
    size_t nidx = 0;
    int consecutive = 0;

    while (hidx < hlen && nidx < nlen) {
        char hc = haystack[hidx];
        char nc = needle[nidx];
        int match = tolower((unsigned char)hc) == tolower((unsigned char)nc);

        if (match) {
            // Base score for a matched character
            score += 10;

            // Bonus for consecutive matches (prefers substrings)
            if (consecutive) {
                score += FUZZY_ADJACENT_BONUS;
            }

            // Bonus for matches after a separator or word boundary
            if (hidx == 0 || haystack[hidx - 1] == ' ' || haystack[hidx - 1] == '_' ||
                haystack[hidx - 1] == '-' || haystack[hidx - 1] == '/') {
                score += FUZZY_SEPARATOR_BONUS;
            }

            // Penalty for case mismatch
            if (hc != nc) {
                score -= FUZZY_CASE_MISMATCH_PENALTY;
            }

            consecutive = 1;
            nidx++;
        } else {
            consecutive = 0;
        }

        hidx++;
    }

    if (nidx < nlen) {
        // Not all pattern consumed -> no match
        return 0;
    }

    // Slight preference for shorter commands when scores tie
    score -= (int)hlen / 100;

    if (score < 1) {
        score = 1;
    }

    return score;
}

// ============================================================================
// Results Management
// ============================================================================

static void free_results(HistorySearchState *state) {
    if (!state) {
        return;
    }

    if (state->results) {
        for (int i = 0; i < state->result_count; i++) {
            free(state->results[i].command);
        }
        free(state->results);
        state->results = NULL;
    }
    state->result_count = 0;
    state->result_capacity = 0;
}

static int add_result(HistorySearchState *state, const char *command, int score, int original_index) {
    if (state->result_count >= state->result_capacity) {
        int new_capacity = state->result_capacity * 2;
        if (new_capacity == 0) new_capacity = INITIAL_RESULTS_CAPACITY;

        HistorySearchResult *new_results = realloc(state->results,
                                   sizeof(HistorySearchResult) * (size_t)new_capacity);
        if (!new_results) {
            LOG_ERROR("[HistorySearch] Failed to expand results array");
            return -1;
        }
        state->results = new_results;
        state->result_capacity = new_capacity;
    }

    state->results[state->result_count].command = strdup(command);
    if (!state->results[state->result_count].command) {
        return -1;
    }
    state->results[state->result_count].score = score;
    state->results[state->result_count].original_index = original_index;
    state->result_count++;
    return 0;
}

static int compare_results(const void *a, const void *b) {
    const HistorySearchResult *ra = (const HistorySearchResult *)a;
    const HistorySearchResult *rb = (const HistorySearchResult *)b;

    if (ra->score != rb->score) {
        return rb->score - ra->score;  // higher score first
    }
    // When scores are equal, sort by reverse chronological order (newest first)
    // Since history_entries is oldest->newest, higher index = newer
    return rb->original_index - ra->original_index;
}

static int filter_results(HistorySearchState *state) {
    free_results(state);

    if (!state->history_entries || state->history_count == 0) {
        return 0;
    }

    const char *pattern = state->search_pattern;

    // Process most recent history entries first to ensure we see recent matches
    // Use a reasonable limit to avoid excessive memory usage
    #define PROCESSING_LIMIT 2000
    int start_idx = state->history_count - PROCESSING_LIMIT;
    if (start_idx < 0) {
        start_idx = 0;
    }

    for (int i = start_idx; i < state->history_count; i++) {
        int score = fuzzy_score(state->history_entries[i], pattern);
        if (score > 0) {
            add_result(state, state->history_entries[i], score, i);
        }
    }

    if (state->result_count > 1) {
        qsort(state->results, (size_t)state->result_count, sizeof(HistorySearchResult), compare_results);
    }

    // Reset selection to first result
    state->selected_index = 0;
    state->scroll_offset = 0;

    return 0;
}

// ============================================================================
// Pattern Management
// ============================================================================

static int ensure_pattern_capacity(HistorySearchState *state, size_t needed_len) {
    if (needed_len + 1 <= state->pattern_capacity) {
        return 0;
    }

    size_t new_capacity = state->pattern_capacity * 2;
    if (new_capacity < needed_len + 1) {
        new_capacity = needed_len + 1;
    }

    char *new_pattern = realloc(state->search_pattern, new_capacity);
    if (!new_pattern) {
        LOG_ERROR("[HistorySearch] Failed to expand pattern buffer");
        return -1;
    }

    state->search_pattern = new_pattern;
    state->pattern_capacity = new_capacity;
    return 0;
}

// ============================================================================
// Lifecycle
// ============================================================================

int history_search_init(HistorySearchState *state) {
    if (!state) {
        LOG_ERROR("[HistorySearch] Invalid state pointer");
        return -1;
    }

    memset(state, 0, sizeof(HistorySearchState));

    state->search_pattern = calloc(1, INITIAL_PATTERN_CAPACITY);
    if (!state->search_pattern) {
        LOG_ERROR("[HistorySearch] Failed to allocate pattern buffer");
        return -1;
    }
    state->search_pattern[0] = '\0';
    state->pattern_capacity = INITIAL_PATTERN_CAPACITY;

    LOG_DEBUG("[HistorySearch] Initialized successfully");
    return 0;
}

void history_search_free(HistorySearchState *state) {
    if (!state) {
        return;
    }

    history_search_stop(state);
    free_results(state);

    free(state->search_pattern);
    state->search_pattern = NULL;

    // Note: we don't own history_entries, just reference them
    state->history_entries = NULL;
    state->history_count = 0;
}

// ============================================================================
// Search Operations
// ============================================================================
int history_search_start(HistorySearchState *state, int screen_height, int screen_width,
                         char **history_entries, int history_count) {
    if (!state) {
        LOG_ERROR("[HistorySearch] Invalid state pointer");
        return -1;
    }

    LOG_DEBUG("[HistorySearch] Starting history search with %d history entries", history_count);

    // Store history reference (can be NULL if no history yet)
    state->history_entries = history_entries;
    state->history_count = history_count;

    // Create popup window (similar to file search)
    state->popup_height = screen_height / 3;
    if (state->popup_height < 5) state->popup_height = 5;
    if (state->popup_height > screen_height - 2) state->popup_height = screen_height - 2;

    state->popup_width = screen_width * 2 / 3;
    if (state->popup_width < 20) state->popup_width = 20;
    if (state->popup_width > screen_width - 2) state->popup_width = screen_width - 2;

    state->popup_y = (screen_height - state->popup_height) / 2;
    state->popup_x = (screen_width - state->popup_width) / 2;

    state->popup_win = newwin(state->popup_height, state->popup_width,
                              state->popup_y, state->popup_x);
    if (!state->popup_win) {
        LOG_ERROR("[HistorySearch] Failed to create popup window");
        return -1;
    }

    LOG_DEBUG("[HistorySearch] Created popup window: %dx%d at (%d,%d)",
              state->popup_width, state->popup_height, state->popup_x, state->popup_y);

    keypad(state->popup_win, TRUE);
    state->is_active = 1;

    // Initial search with empty pattern
    history_search_update_pattern(state, "");

    LOG_DEBUG("[HistorySearch] History search started successfully");
    return 0;
}
void history_search_stop(HistorySearchState *state) {
    if (!state) {
        return;
    }

    if (state->popup_win) {
        delwin(state->popup_win);
        state->popup_win = NULL;
    }

    state->is_active = 0;
    state->history_entries = NULL;
    state->history_count = 0;
}

int history_search_update_pattern(HistorySearchState *state, const char *pattern) {
    if (!state) {
        return -1;
    }

    size_t len = strnlen(pattern, 1024);
    if (ensure_pattern_capacity(state, len) != 0) {
        return -1;
    }

    strlcpy(state->search_pattern, pattern, state->pattern_capacity);
    state->pattern_len = len;

    return filter_results(state);
}

int history_search_add_char(HistorySearchState *state, char c) {
    if (!state) {
        return -1;
    }

    if (ensure_pattern_capacity(state, state->pattern_len + 1) != 0) {
        return -1;
    }

    state->search_pattern[state->pattern_len] = c;
    state->pattern_len++;
    state->search_pattern[state->pattern_len] = '\0';

    return filter_results(state);
}

int history_search_backspace(HistorySearchState *state) {
    if (!state || state->pattern_len == 0) {
        return -1;
    }

    state->pattern_len--;
    state->search_pattern[state->pattern_len] = '\0';

    return filter_results(state);
}

void history_search_clear_pattern(HistorySearchState *state) {
    if (!state) {
        return;
    }

    state->search_pattern[0] = '\0';
    state->pattern_len = 0;
    filter_results(state);
}

// ============================================================================
// Navigation
// ============================================================================

void history_search_select_prev(HistorySearchState *state) {
    if (!state || state->result_count == 0) {
        return;
    }

    if (state->selected_index > 0) {
        state->selected_index--;
    }

    // Adjust scroll if needed
    if (state->selected_index < state->scroll_offset) {
        state->scroll_offset = state->selected_index;
    }
}

void history_search_select_next(HistorySearchState *state) {
    if (!state || state->result_count == 0) {
        return;
    }

    if (state->selected_index < state->result_count - 1) {
        state->selected_index++;
    }

    // Adjust scroll if needed
    int visible_lines = state->popup_height - 4;  // border + prompt + separator
    if (state->selected_index >= state->scroll_offset + visible_lines) {
        state->scroll_offset = state->selected_index - visible_lines + 1;
    }
}

void history_search_page_up(HistorySearchState *state) {
    if (!state || state->result_count == 0) {
        return;
    }

    int visible_lines = state->popup_height - 4;  // border + prompt + separator
    state->selected_index -= visible_lines / 2;
    if (state->selected_index < 0) {
        state->selected_index = 0;
    }

    if (state->selected_index < state->scroll_offset) {
        state->scroll_offset = state->selected_index;
    }
}

void history_search_page_down(HistorySearchState *state) {
    if (!state || state->result_count == 0) {
        return;
    }

    int visible_lines = state->popup_height - 4;  // border + prompt + separator
    state->selected_index += visible_lines / 2;
    if (state->selected_index >= state->result_count) {
        state->selected_index = state->result_count - 1;
    }

    if (state->selected_index >= state->scroll_offset + visible_lines) {
        state->scroll_offset = state->selected_index - visible_lines + 1;
    }
}

const char *history_search_get_selected(HistorySearchState *state) {
    if (!state || !state->is_active || state->result_count == 0 ||
        state->selected_index < 0 || state->selected_index >= state->result_count) {
        return NULL;
    }

    return state->results[state->selected_index].command;
}

// ============================================================================
// Display
// ============================================================================

void history_search_render(HistorySearchState *state) {
    if (!state || !state->popup_win || !state->is_active) {
        return;
    }

    werase(state->popup_win);

    WINDOW *win = state->popup_win;
    int width = state->popup_width;
    int height = state->popup_height;
    int use_colors = has_colors();

    // Clear and draw border with theme color (match file search look)
    if (use_colors) {
        wattron(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
    }
    box(win, 0, 0);

    // Title (centered)
    const char *title = " History Search (Ctrl+R) ";
    int title_x = (width - (int)strlen(title)) / 2;
    if (title_x < 1) title_x = 1;
    if (use_colors) {
        wattron(win, A_BOLD);
    }
    mvwprintw(win, 0, title_x, "%s", title);
    if (use_colors) {
        wattroff(win, A_BOLD);
        wattroff(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
    }

    // Search prompt ">" (reuse prompt color)
    if (use_colors) {
        wattron(win, COLOR_PAIR(NCURSES_PAIR_PROMPT) | A_BOLD);
    }
    mvwprintw(win, 1, 2, "> ");
    if (use_colors) {
        wattroff(win, COLOR_PAIR(NCURSES_PAIR_PROMPT) | A_BOLD);
    }

    // Search pattern text
    if (state->pattern_len > 0) {
        if (use_colors) {
            wattron(win, COLOR_PAIR(NCURSES_PAIR_FOREGROUND));
        }
        int max_pattern = width - 6;
        if ((int)state->pattern_len > max_pattern) {
            mvwprintw(win, 1, 4, "...%s",
                      state->search_pattern + state->pattern_len - max_pattern + 3);
        } else {
            mvwprintw(win, 1, 4, "%s", state->search_pattern);
        }
        if (use_colors) {
            wattroff(win, COLOR_PAIR(NCURSES_PAIR_FOREGROUND));
        }
    }

    // Separator line
    if (use_colors) {
        wattron(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
    }
    mvwhline(win, 2, 1, ACS_HLINE, width - 2);
    if (use_colors) {
        wattroff(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
    }

    // Show cursor at end of pattern
    wmove(win, 1, 4 + (int)state->pattern_len);

    // Results
    int visible_lines = height - 4;
    if (visible_lines < 1) visible_lines = 1;
    int start = state->scroll_offset;
    int end = start + visible_lines;
    if (end > state->result_count) end = state->result_count;

    for (int i = start; i < end; i++) {
        int line = i - start + 3;  // +3 for border, prompt, separator

        if (i == state->selected_index) {
            if (use_colors) {
                wattron(win, COLOR_PAIR(NCURSES_PAIR_USER) | A_REVERSE);
            } else {
                wattron(win, A_REVERSE);
            }
        } else if (use_colors) {
            wattron(win, COLOR_PAIR(NCURSES_PAIR_FOREGROUND));
        }

        // Truncate command if too long
        const char *cmd = state->results[i].command;
        int max_len = width - 4;
        if ((int)strlen(cmd) > max_len) {
            mvwprintw(win, line, 2, "...%s", cmd + strlen(cmd) - max_len + 3);
        } else {
            mvwprintw(win, line, 2, "%s", cmd);
        }

        // Pad to end for reverse highlight
        int path_len = (int)strlen(cmd);
        if (path_len > max_len) path_len = max_len;
        for (int j = path_len + 1; j < max_len + 1; j++) {
            waddch(win, ' ');
        }

        if (i == state->selected_index) {
            if (use_colors) {
                wattroff(win, COLOR_PAIR(NCURSES_PAIR_USER) | A_REVERSE);
            } else {
                wattroff(win, A_REVERSE);
            }
        } else if (use_colors) {
            wattroff(win, COLOR_PAIR(NCURSES_PAIR_FOREGROUND));
        }
    }

    // Footer with hints
    const char *hints = " ESC:cancel  Enter:select  j/k:move  PgUp/PgDn:scroll ";
    int hints_x = (width - (int)strlen(hints)) / 2;
    if (hints_x < 1) hints_x = 1;
    if (use_colors) {
        wattron(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
    }
    mvwprintw(win, height - 1, hints_x, "%s", hints);
    if (use_colors) {
        wattroff(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
    }

    // Status line above footer
    if (state->result_count == 0) {
        mvwprintw(win, height - 2, 2, "No matches");
    } else {
        mvwprintw(win, height - 2, 2, "%d/%d matches", state->selected_index + 1, state->result_count);
    }

    wrefresh(win);
}

// ============================================================================
// Input Handling
// ============================================================================

int history_search_process_key(HistorySearchState *state, int ch) {
    if (!state || !state->is_active) {
        return 0;
    }

    switch (ch) {
        case 27:  // ESC
            return -1;

        case '\r':  // Enter key (with nonl() mode)
        case '\n':  // Enter/Return
        case KEY_ENTER:
            return 1;

        case KEY_UP:
        case 16:  // Ctrl+P - previous (like readline)
            history_search_select_prev(state);
            break;

        case KEY_DOWN:
        case 14:  // Ctrl+N - next (like readline)
            history_search_select_next(state);
            break;

        case KEY_PPAGE:
            history_search_page_up(state);
            break;

        case KEY_NPAGE:
        case 4:   // Ctrl+D - page down (like file search)
            history_search_page_down(state);
            break;

        case 8:     // Backspace
        case 127:   // Delete (backspace on some systems)
        case KEY_BACKSPACE:
            history_search_backspace(state);
            break;

        case KEY_DC:  // Delete
            // Clear pattern on delete when at end?
            break;

        case 11:  // Ctrl+K - delete to end of line
            // Since cursor is always at end in current implementation,
            // this does nothing, but we keep it for consistency
            break;

        case 21:  // Ctrl+U - delete to beginning of line
            history_search_clear_pattern(state);
            break;

        case 12:  // Ctrl+L - clear pattern
            history_search_clear_pattern(state);
            break;

        case KEY_HOME:
            state->selected_index = 0;
            state->scroll_offset = 0;
            break;

        case KEY_END:
            if (state->result_count > 0) {
                state->selected_index = state->result_count - 1;
                int visible_lines = state->popup_height - 2;
                if (state->selected_index >= visible_lines) {
                    state->scroll_offset = state->selected_index - visible_lines + 1;
                }
            }
            break;

        default:
            // Printable characters
            if (isprint(ch)) {
                history_search_add_char(state, (char)ch);
            }
            break;
    }

    return 0;
}
