#ifndef HAVE_STRLCPY
#include "compat.h"
#endif/*
 * File Search - Interactive file finder for TUI
 */

#include "file_search.h"
#include "tui.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define INITIAL_PATTERN_CAPACITY 256
#define INITIAL_RESULTS_CAPACITY 100
#define INITIAL_CACHE_CAPACITY 1000
#define FUZZY_MAX_PATTERN 256
#define FUZZY_ADJACENT_BONUS 5
#define FUZZY_SEPARATOR_BONUS 10
#define FUZZY_CASE_MISMATCH_PENALTY 1

// ============================================================================
// Tool Detection
// ============================================================================

static int command_exists(const char *cmd) {
    char check_cmd[256];
    snprintf(check_cmd, sizeof(check_cmd), "command -v %s >/dev/null 2>&1", cmd);
    return system(check_cmd) == 0;
}

static void detect_tools(FileSearchState *state) {
    if (state->tools_detected) {
        return;
    }

    state->has_fd = command_exists("fd");
    state->has_rg = command_exists("rg");
    state->has_fzf = command_exists("fzf");
    state->tools_detected = 1;

    LOG_DEBUG("[FileSearch] Tools: fd=%d, rg=%d, fzf=%d",
              state->has_fd, state->has_rg, state->has_fzf);
}

// ============================================================================
// File Cache Management
// ============================================================================

static void free_file_cache(FileSearchState *state) {
    if (state->file_cache) {
        for (int i = 0; i < state->file_cache_count; i++) {
            free(state->file_cache[i]);
        }
        free(state->file_cache);
        state->file_cache = NULL;
    }
    state->file_cache_count = 0;
    state->file_cache_capacity = 0;
    state->cache_valid = 0;
}

static int add_to_cache(FileSearchState *state, const char *path) {
    if (state->file_cache_count >= state->file_cache_capacity) {
        int new_capacity = state->file_cache_capacity * 2;
        if (new_capacity == 0) new_capacity = INITIAL_CACHE_CAPACITY;

        char **new_cache = realloc(state->file_cache,
                                   sizeof(char *) * (size_t)new_capacity);
        if (!new_cache) {
            LOG_ERROR("[FileSearch] Failed to expand cache");
            return -1;
        }
        state->file_cache = new_cache;
        state->file_cache_capacity = new_capacity;
    }

    state->file_cache[state->file_cache_count] = strdup(path);
    if (!state->file_cache[state->file_cache_count]) {
        return -1;
    }
    state->file_cache_count++;
    return 0;
}

// Read output from file descriptor with timeout
// Returns: number of bytes read, 0 on timeout/eof, -1 on error
static int read_with_timeout(int fd, char *buf, size_t bufsize, int timeout_ms) {
    fd_set readfds;
    struct timeval tv;

    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(fd + 1, &readfds, NULL, NULL, &tv);
    if (ret <= 0) {
        return ret;  // 0 = timeout, -1 = error
    }

    return (int)read(fd, buf, bufsize);
}

// Run file listing command and populate cache
static int populate_file_cache(FileSearchState *state) {
    detect_tools(state);
    free_file_cache(state);

    const char *cmd;
    char cmd_buf[1024];
    const char *dir = state->search_dir ? state->search_dir : ".";

    // Choose best available tool
    if (state->has_fd) {
        // fd is fastest and respects .gitignore by default
        snprintf(cmd_buf, sizeof(cmd_buf),
                 "fd --type f --hidden --exclude .git --color never . '%s' 2>/dev/null",
                 dir);
        cmd = cmd_buf;
    } else if (state->has_rg) {
        // ripgrep --files lists files it would search
        snprintf(cmd_buf, sizeof(cmd_buf),
                 "rg --files --hidden --glob '!.git' '%s' 2>/dev/null",
                 dir);
        cmd = cmd_buf;
    } else {
        // Fallback to find
        snprintf(cmd_buf, sizeof(cmd_buf),
                 "find '%s' -type f -not -path '*/.git/*' 2>/dev/null | head -n %d",
                 dir, FILE_SEARCH_MAX_RESULTS * 2);
        cmd = cmd_buf;
    }

    LOG_DEBUG("[FileSearch] Running: %s", cmd);

    // Use pipe + fork for timeout control
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        LOG_ERROR("[FileSearch] pipe() failed: %s", strerror(errno));
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        LOG_ERROR("[FileSearch] fork() failed: %s", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        // Child process
        close(pipefd[0]);  // Close read end
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    // Parent process
    close(pipefd[1]);  // Close write end

    // Set non-blocking
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    // Read output with timeout
    char buf[4096];
    char line[1024];
    int line_len = 0;
    int total_timeout = state->timeout_ms;
    int elapsed = 0;
    const int chunk_timeout = 100;  // 100ms chunks

    while (elapsed < total_timeout && state->file_cache_count < FILE_SEARCH_MAX_RESULTS) {
        int n = read_with_timeout(pipefd[0], buf, sizeof(buf) - 1, chunk_timeout);

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                elapsed += chunk_timeout;
                continue;
            }
            break;  // Real error
        }

        if (n == 0) {
            elapsed += chunk_timeout;
            // Check if child exited
            int status;
            pid_t result = waitpid(pid, &status, WNOHANG);
            if (result == pid) {
                break;  // Child done
            }
            continue;
        }

        buf[n] = '\0';

        // Parse lines
        for (int i = 0; i < n; i++) {
            if (buf[i] == '\n' || buf[i] == '\0') {
                line[line_len] = '\0';
                if (line_len > 0) {
                    // Strip leading ./ if present
                    char *path = line;
                    if (path[0] == '.' && path[1] == '/') {
                        path += 2;
                    }
                    add_to_cache(state, path);
                }
                line_len = 0;
            } else if (line_len < (int)sizeof(line) - 1) {
                line[line_len++] = buf[i];
            }
        }
    }

    // Handle remaining partial line
    if (line_len > 0) {
        line[line_len] = '\0';
        char *path = line;
        if (path[0] == '.' && path[1] == '/') {
            path += 2;
        }
        add_to_cache(state, path);
    }

    close(pipefd[0]);

    // Kill child if still running
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);

    state->cache_valid = 1;
    LOG_INFO("[FileSearch] Cached %d files", state->file_cache_count);

    return 0;
}

// ============================================================================
// Result Management
// ============================================================================

static void free_results(FileSearchState *state) {
    if (state->results) {
        for (int i = 0; i < state->result_count; i++) {
            free(state->results[i].path);
        }
        free(state->results);
        state->results = NULL;
    }
    state->result_count = 0;
    state->result_capacity = 0;
    state->selected_index = 0;
    state->scroll_offset = 0;
}

static int add_result(FileSearchState *state, const char *path, int score) {
    if (state->result_count >= state->result_capacity) {
        int new_capacity = state->result_capacity * 2;
        if (new_capacity == 0) new_capacity = INITIAL_RESULTS_CAPACITY;

        FileSearchResult *new_results = realloc(state->results,
                                                sizeof(FileSearchResult) * (size_t)new_capacity);
        if (!new_results) {
            return -1;
        }
        state->results = new_results;
        state->result_capacity = new_capacity;
    }

    state->results[state->result_count].path = strdup(path);
    if (!state->results[state->result_count].path) {
        return -1;
    }
    state->results[state->result_count].score = score;
    state->result_count++;
    return 0;
}

// Lightweight fuzzy match scored search (case-insensitive, subsequence-based)
// Inspired by fzf-like scoring but simplified for C and no dynamic allocations.
// Returns >0 score for match, 0 for no match. Higher is better.
#ifdef TEST_BUILD
int fuzzy_score(const char *haystack, const char *needle) {
#else
static int fuzzy_score(const char *haystack, const char *needle) {
#endif
    if (!needle || !needle[0]) {
        return 1;  // Empty pattern matches everything with minimal score
    }

    size_t nlen = strnlen(needle, FUZZY_MAX_PATTERN);
    if (nlen == 0) {
        return 1;
    }

    int score = 0;
    size_t hlen = strnlen(haystack, PATH_MAX);
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

            // Bonus for matches after a separator or path boundary
            if (hidx == 0 || haystack[hidx - 1] == '/' || haystack[hidx - 1] == '_' || haystack[hidx - 1] == '-') {
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

    // Slight preference for shorter paths when scores tie
    score -= (int)hlen / 100;

    if (score < 1) {
        score = 1;
    }

    return score;
}

// Filter cache based on pattern
#ifdef TEST_BUILD
int compare_results(const void *a, const void *b) {
#else
static int compare_results(const void *a, const void *b) {
#endif
    const FileSearchResult *ra = (const FileSearchResult *)a;
    const FileSearchResult *rb = (const FileSearchResult *)b;

    if (ra->score != rb->score) {
        return rb->score - ra->score;  // higher score first
    }
    return strcasecmp(ra->path, rb->path);
}

static int filter_results(FileSearchState *state) {
    free_results(state);

    if (!state->cache_valid) {
        return -1;
    }

    const char *pattern = state->search_pattern;

    for (int i = 0; i < state->file_cache_count && state->result_count < FILE_SEARCH_MAX_RESULTS; i++) {
        int score = fuzzy_score(state->file_cache[i], pattern);
        if (score > 0) {
            add_result(state, state->file_cache[i], score);
        }
    }

    if (state->result_count > 1) {
        qsort(state->results, (size_t)state->result_count, sizeof(FileSearchResult), compare_results);
    }

    // Reset selection
    state->selected_index = 0;
    state->scroll_offset = 0;

    return 0;
}

// ============================================================================
// Lifecycle
// ============================================================================

int file_search_init(FileSearchState *state) {
    if (!state) {
        return -1;
    }

    memset(state, 0, sizeof(FileSearchState));

    state->search_pattern = calloc(1, INITIAL_PATTERN_CAPACITY);
    if (!state->search_pattern) {
        return -1;
    }
    state->search_pattern[0] = '\0';
    state->pattern_capacity = INITIAL_PATTERN_CAPACITY;

    state->timeout_ms = FILE_SEARCH_DEFAULT_TIMEOUT_MS;

    return 0;
}

void file_search_free(FileSearchState *state) {
    if (!state) {
        return;
    }

    file_search_stop(state);
    free_results(state);
    free_file_cache(state);

    free(state->search_pattern);
    state->search_pattern = NULL;

    free(state->search_dir);
    state->search_dir = NULL;
}

// ============================================================================
// Search Operations
// ============================================================================

int file_search_start(FileSearchState *state, int screen_height, int screen_width,
                      const char *search_dir) {
    if (!state) {
        return -1;
    }

    // Set search directory
    free(state->search_dir);
    state->search_dir = search_dir ? strdup(search_dir) : NULL;

    // Calculate popup dimensions (centered, 80% of screen)
    state->popup_width = (screen_width * 80) / 100;
    if (state->popup_width < 40) state->popup_width = 40;
    if (state->popup_width > screen_width - 4) state->popup_width = screen_width - 4;

    state->popup_height = (screen_height * 60) / 100;
    if (state->popup_height < 10) state->popup_height = 10;
    if (state->popup_height > screen_height - 4) state->popup_height = screen_height - 4;

    state->popup_y = (screen_height - state->popup_height) / 2;
    state->popup_x = (screen_width - state->popup_width) / 2;

    // Create popup window
    state->popup_win = newwin(state->popup_height, state->popup_width,
                              state->popup_y, state->popup_x);
    if (!state->popup_win) {
        LOG_ERROR("[FileSearch] Failed to create popup window");
        return -1;
    }

    keypad(state->popup_win, TRUE);

    // Clear pattern
    state->search_pattern[0] = '\0';
    state->pattern_len = 0;

    // Populate file cache (this may take a moment)
    populate_file_cache(state);

    // Initial filter (show all)
    filter_results(state);

    state->is_active = 1;

    LOG_DEBUG("[FileSearch] Started (popup=%dx%d at %d,%d)",
              state->popup_width, state->popup_height,
              state->popup_x, state->popup_y);

    return 0;
}

void file_search_stop(FileSearchState *state) {
    if (!state) {
        return;
    }

    if (state->popup_win) {
        werase(state->popup_win);
        wrefresh(state->popup_win);
        delwin(state->popup_win);
        state->popup_win = NULL;
    }

    state->is_active = 0;

    LOG_DEBUG("[FileSearch] Stopped");
}

int file_search_update_pattern(FileSearchState *state, const char *pattern) {
    if (!state || !pattern) {
        return -1;
    }

    size_t len = strlen(pattern);
    if (len >= state->pattern_capacity) {
        size_t new_cap = len + 1 + INITIAL_PATTERN_CAPACITY;
        char *new_buf = realloc(state->search_pattern, new_cap);
        if (!new_buf) {
            return -1;
        }
        state->search_pattern = new_buf;
        state->search_pattern[new_cap - 1] = '\0';
        state->pattern_capacity = new_cap;
    }

    strlcpy(state->search_pattern, pattern, state->pattern_capacity);
    state->pattern_len = len;

    return filter_results(state);
}

int file_search_add_char(FileSearchState *state, char c) {
    if (!state) {
        return -1;
    }

    if (state->pattern_len + 1 >= state->pattern_capacity) {
        size_t new_cap = state->pattern_capacity * 2;
        char *new_buf = realloc(state->search_pattern, new_cap);
        if (!new_buf) {
            return -1;
        }
        state->search_pattern = new_buf;
        state->search_pattern[new_cap - 1] = '\0';
        state->pattern_capacity = new_cap;
    }

    state->search_pattern[state->pattern_len++] = c;
    state->search_pattern[state->pattern_len] = '\0';

    return filter_results(state);
}

int file_search_backspace(FileSearchState *state) {
    if (!state || state->pattern_len == 0) {
        return 0;
    }

    state->pattern_len--;
    state->search_pattern[state->pattern_len] = '\0';

    return filter_results(state);
}

void file_search_clear_pattern(FileSearchState *state) {
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

void file_search_select_prev(FileSearchState *state) {
    if (!state || state->result_count == 0) {
        return;
    }

    if (state->selected_index > 0) {
        state->selected_index--;

        // Adjust scroll if needed
        if (state->selected_index < state->scroll_offset) {
            state->scroll_offset = state->selected_index;
        }
    }
}

void file_search_select_next(FileSearchState *state) {
    if (!state || state->result_count == 0) {
        return;
    }

    if (state->selected_index < state->result_count - 1) {
        state->selected_index++;

        // Calculate visible lines (popup height minus header and footer)
        int visible_lines = state->popup_height - 4;
        if (visible_lines < 1) visible_lines = 1;

        // Adjust scroll if needed
        if (state->selected_index >= state->scroll_offset + visible_lines) {
            state->scroll_offset = state->selected_index - visible_lines + 1;
        }
    }
}

void file_search_page_up(FileSearchState *state) {
    if (!state || state->result_count == 0) {
        return;
    }

    int visible_lines = state->popup_height - 4;
    if (visible_lines < 1) visible_lines = 1;
    int half_page = visible_lines / 2;

    state->selected_index -= half_page;
    if (state->selected_index < 0) {
        state->selected_index = 0;
    }

    state->scroll_offset -= half_page;
    if (state->scroll_offset < 0) {
        state->scroll_offset = 0;
    }
}

void file_search_page_down(FileSearchState *state) {
    if (!state || state->result_count == 0) {
        return;
    }

    int visible_lines = state->popup_height - 4;
    if (visible_lines < 1) visible_lines = 1;
    int half_page = visible_lines / 2;

    state->selected_index += half_page;
    if (state->selected_index >= state->result_count) {
        state->selected_index = state->result_count - 1;
    }

    // Adjust scroll
    if (state->selected_index >= state->scroll_offset + visible_lines) {
        state->scroll_offset = state->selected_index - visible_lines + 1;
    }
}

const char *file_search_get_selected(FileSearchState *state) {
    if (!state || state->result_count == 0) {
        return NULL;
    }

    if (state->selected_index >= 0 && state->selected_index < state->result_count) {
        return state->results[state->selected_index].path;
    }

    return NULL;
}

// ============================================================================
// Display
// ============================================================================

void file_search_render(FileSearchState *state) {
    if (!state || !state->popup_win || !state->is_active) {
        return;
    }

    WINDOW *win = state->popup_win;
    int width = state->popup_width;
    int height = state->popup_height;
    int use_colors = has_colors();

    // Clear and draw border with theme color
    werase(win);
    if (use_colors) {
        wattron(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
    }
    box(win, 0, 0);

    // Title (with status color and bold)
    const char *title = " Find File (Ctrl+F) ";
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

    // Search prompt ">" (with prompt/user color - green)
    if (use_colors) {
        wattron(win, COLOR_PAIR(NCURSES_PAIR_PROMPT) | A_BOLD);
    }
    mvwprintw(win, 1, 2, "> ");
    if (use_colors) {
        wattroff(win, COLOR_PAIR(NCURSES_PAIR_PROMPT) | A_BOLD);
    }

    // Search pattern text (with foreground color)
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

    // Result count (with status color)
    char count_str[32];
    snprintf(count_str, sizeof(count_str), " %d/%d ",
             state->result_count, state->file_cache_count);
    if (use_colors) {
        wattron(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
    }
    mvwprintw(win, 1, width - (int)strlen(count_str) - 1, "%s", count_str);
    if (use_colors) {
        wattroff(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
    }

    // Separator (with status color)
    if (use_colors) {
        wattron(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
    }
    mvwhline(win, 2, 1, ACS_HLINE, width - 2);
    if (use_colors) {
        wattroff(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
    }

    // Results
    int visible_lines = height - 4;
    if (visible_lines < 1) visible_lines = 1;

    for (int i = 0; i < visible_lines; i++) {
        int result_idx = state->scroll_offset + i;
        if (result_idx >= state->result_count) {
            break;
        }

        int y = 3 + i;

        // Highlight selected item (with user/green color for selection)
        if (result_idx == state->selected_index) {
            if (use_colors) {
                wattron(win, COLOR_PAIR(NCURSES_PAIR_USER) | A_REVERSE);
            } else {
                wattron(win, A_REVERSE);
            }
        } else {
            // Non-selected items use foreground color
            if (use_colors) {
                wattron(win, COLOR_PAIR(NCURSES_PAIR_FOREGROUND));
            }
        }

        // Truncate long paths
        const char *path = state->results[result_idx].path;
        int max_path = width - 4;

        mvwprintw(win, y, 2, " ");
        if ((int)strlen(path) > max_path) {
            // Show end of path
            mvwprintw(win, y, 3, "...%s", path + strlen(path) - max_path + 3);
        } else {
            mvwprintw(win, y, 3, "%s", path);
        }

        // Fill rest of line with spaces (for reverse highlight)
        int path_len = (int)strlen(path);
        if (path_len > max_path) path_len = max_path;
        for (int j = path_len + 1; j < max_path + 1; j++) {
            waddch(win, ' ');
        }

        if (result_idx == state->selected_index) {
            if (use_colors) {
                wattroff(win, COLOR_PAIR(NCURSES_PAIR_USER) | A_REVERSE);
            } else {
                wattroff(win, A_REVERSE);
            }
        } else {
            if (use_colors) {
                wattroff(win, COLOR_PAIR(NCURSES_PAIR_FOREGROUND));
            }
        }
    }

    // Footer with hints (with status color)
    const char *hints = " ESC:cancel  Enter:select  j/k:navigate  Alt+B/F:word  Alt+D/⌫:del";
    int hints_x = (width - (int)strlen(hints)) / 2;
    if (hints_x < 1) hints_x = 1;
    if (use_colors) {
        wattron(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
    }
    mvwprintw(win, height - 1, hints_x, "%s", hints);
    if (use_colors) {
        wattroff(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
    }

    // Position cursor at end of search pattern
    wmove(win, 1, 4 + (int)state->pattern_len);

    wrefresh(win);
}

// ============================================================================
// Word Movement and Deletion Helpers
// ============================================================================

// Move cursor backward one word in search pattern
static size_t move_backward_word_in_pattern(FileSearchState *state) {
    if (!state || state->pattern_len == 0) {
        return 0;
    }

    size_t cursor = state->pattern_len;  // Current cursor is at end of pattern
    const char *pattern = state->search_pattern;

    // Skip trailing whitespace
    while (cursor > 0 && isspace((unsigned char)pattern[cursor - 1])) {
        cursor--;
    }

    // Skip word characters
    while (cursor > 0 && !isspace((unsigned char)pattern[cursor - 1])) {
        cursor--;
    }

    return cursor;
}

// Move cursor forward one word in search pattern
static size_t move_forward_word_in_pattern(FileSearchState *state) {
    if (!state || state->pattern_len == 0) {
        return state->pattern_len;
    }

    size_t cursor = state->pattern_len;  // Current cursor is at end of pattern
    const char *pattern = state->search_pattern;

    // If at end of pattern, return current position
    if (cursor >= state->pattern_len) {
        return cursor;
    }

    // Skip current word if we're in the middle of one
    while (cursor < state->pattern_len && !isspace((unsigned char)pattern[cursor])) {
        cursor++;
    }

    // Skip whitespace
    while (cursor < state->pattern_len && isspace((unsigned char)pattern[cursor])) {
        cursor++;
    }

    return cursor;
}

// Delete from cursor to end of current word (forward)
static size_t delete_word_forward(FileSearchState *state) {
    if (!state || state->pattern_len == 0) {
        return 0;
    }

    size_t cursor = state->pattern_len;  // Current cursor is at end of pattern
    const char *pattern = state->search_pattern;

    // If at end of pattern, nothing to delete
    if (cursor >= state->pattern_len) {
        return 0;
    }

    // Find end of word to delete
    size_t word_end = cursor;
    while (word_end < state->pattern_len && !isspace((unsigned char)pattern[word_end])) {
        word_end++;
    }

    // Delete the word
    size_t delete_len = word_end - cursor;
    if (delete_len > 0) {
        // Shift characters left
        for (size_t i = cursor; i < state->pattern_len - delete_len; i++) {
            state->search_pattern[i] = state->search_pattern[i + delete_len];
        }
        state->pattern_len -= delete_len;
        state->search_pattern[state->pattern_len] = '\0';
        return delete_len;
    }

    return 0;
}

// Delete from cursor to start of current word (backward)
static size_t delete_word_backward(FileSearchState *state) {
    if (!state || state->pattern_len == 0) {
        return 0;
    }

    size_t cursor = state->pattern_len;  // Current cursor is at end of pattern
    const char *pattern = state->search_pattern;

    // Skip trailing whitespace
    size_t word_start = cursor;
    while (word_start > 0 && isspace((unsigned char)pattern[word_start - 1])) {
        word_start--;
    }

    // Skip word characters
    while (word_start > 0 && !isspace((unsigned char)pattern[word_start - 1])) {
        word_start--;
    }

    // Delete the word
    size_t delete_len = cursor - word_start;
    if (delete_len > 0) {
        // Shift characters left
        for (size_t i = word_start; i < state->pattern_len - delete_len; i++) {
            state->search_pattern[i] = state->search_pattern[i + delete_len];
        }
        state->pattern_len -= delete_len;
        state->search_pattern[state->pattern_len] = '\0';
        return delete_len;
    }

    return 0;
}

// Delete from cursor to beginning of line (Ctrl+U)
static void delete_to_beginning_of_line(FileSearchState *state) {
    if (!state || state->pattern_len == 0) {
        return;
    }

    state->pattern_len = 0;
    state->search_pattern[0] = '\0';
}

// Delete from cursor to end of line (Ctrl+K)
static void delete_to_end_of_line(FileSearchState *state) {
    if (!state || state->pattern_len == 0) {
        return;
    }

    // Since cursor is always at end in current implementation,
    // this does nothing, but we keep it for completeness
    // In a future implementation with cursor movement within pattern,
    // this would delete from cursor to end
}

// ============================================================================
// Input Handling
// ============================================================================

int file_search_process_key(FileSearchState *state, int ch) {
    if (!state || !state->is_active) {
        return -1;
    }

    switch (ch) {
        case 27:  // ESC - could be cancel or start of Alt key sequence
            // Check if there's a following character (Alt key)
            nodelay(state->popup_win, TRUE);
            int next_ch = wgetch(state->popup_win);
            nodelay(state->popup_win, FALSE);

            if (next_ch == ERR) {
                // Standalone ESC - cancel
                return -1;
            }

            // Handle Alt key combinations (readline shortcuts)
            switch (next_ch) {
                case 'b':  // Alt+b: move cursor backward one word
                case 'B':
                    {
                        size_t new_pos = move_backward_word_in_pattern(state);
                        if (new_pos != state->pattern_len) {
                            // For now, we delete from new position to end
                            // In a future implementation with cursor movement within pattern,
                            // we would just move the cursor
                            state->pattern_len = new_pos;
                            state->search_pattern[state->pattern_len] = '\0';
                            filter_results(state);
                        }
                    }
                    break;

                case 'f':  // Alt+f: move cursor forward one word
                case 'F':
                    {
                        size_t new_pos = move_forward_word_in_pattern(state);
                        if (new_pos != state->pattern_len) {
                            // For now, we delete from cursor to new position
                            // In a future implementation with cursor movement within pattern,
                            // we would just move the cursor
                            // Since cursor is at end, this does nothing
                        }
                    }
                    break;

                case 'd':  // Alt+d: delete next word
                case 'D':
                    if (delete_word_forward(state) > 0) {
                        filter_results(state);
                    }
                    break;

                case 127:  // Alt+Backspace: delete previous word
                case 8:
                    if (delete_word_backward(state) > 0) {
                        filter_results(state);
                    }
                    break;

                default:
                    // Unknown Alt combination, treat as cancel
                    return -1;
            }
            break;

        case '\n':
        case '\r':
        case KEY_ENTER:  // Enter - select
            return 1;

        case KEY_UP:
        case 16:  // Ctrl+P
            file_search_select_prev(state);
            break;

        case KEY_DOWN:
        case 14:  // Ctrl+N
            file_search_select_next(state);
            break;

        case KEY_PPAGE:
        case 21:  // Ctrl+U - delete to beginning of line
            delete_to_beginning_of_line(state);
            filter_results(state);
            break;

        case KEY_NPAGE:
        case 4:   // Ctrl+D
            file_search_page_down(state);
            break;

        case KEY_BACKSPACE:
        case 127:
        case 8:
            file_search_backspace(state);
            break;

        case 11:  // Ctrl+K - delete to end of line
            delete_to_end_of_line(state);
            filter_results(state);
            break;

        case 21 + 64:  // Ctrl+U (alternative)
        case 12:       // Ctrl+L - clear pattern
            file_search_clear_pattern(state);
            break;

        default:
            // Handle vim-style j/k navigation
            if (ch == 'j' && state->pattern_len == 0) {
                file_search_select_next(state);
            } else if (ch == 'k' && state->pattern_len == 0) {
                file_search_select_prev(state);
            } else if (ch >= 32 && ch < 127) {
                // Printable character - add to pattern
                file_search_add_char(state, (char)ch);
            }
            break;
    }

    return 0;
}
