/*
 * File Search - Interactive file finder for TUI
 */

#include "file_search.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>

#define INITIAL_PATTERN_CAPACITY 256
#define INITIAL_RESULTS_CAPACITY 100
#define INITIAL_CACHE_CAPACITY 1000

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

static int add_result(FileSearchState *state, const char *path) {
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
    state->result_count++;
    return 0;
}

// Case-insensitive substring match
static int fuzzy_match(const char *haystack, const char *needle) {
    if (!needle || !needle[0]) {
        return 1;  // Empty pattern matches everything
    }

    // Simple case-insensitive substring search
    const char *h = haystack;
    const char *n = needle;

    while (*h) {
        const char *h_ptr = h;
        const char *n_ptr = n;

        while (*h_ptr && *n_ptr &&
               tolower((unsigned char)*h_ptr) == tolower((unsigned char)*n_ptr)) {
            h_ptr++;
            n_ptr++;
        }

        if (!*n_ptr) {
            return 1;  // Found match
        }
        h++;
    }

    return 0;
}

// Filter cache based on pattern
static int filter_results(FileSearchState *state) {
    free_results(state);

    if (!state->cache_valid) {
        return -1;
    }

    const char *pattern = state->search_pattern;

    for (int i = 0; i < state->file_cache_count && state->result_count < FILE_SEARCH_MAX_RESULTS; i++) {
        if (fuzzy_match(state->file_cache[i], pattern)) {
            add_result(state, state->file_cache[i]);
        }
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

    state->search_pattern = malloc(INITIAL_PATTERN_CAPACITY);
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

    // Clear and draw border
    werase(win);
    box(win, 0, 0);

    // Title
    const char *title = " Find File (Ctrl+F) ";
    int title_x = (width - (int)strlen(title)) / 2;
    if (title_x < 1) title_x = 1;
    mvwprintw(win, 0, title_x, "%s", title);

    // Search prompt (line 1)
    mvwprintw(win, 1, 2, "> ");
    if (state->pattern_len > 0) {
        int max_pattern = width - 6;
        if ((int)state->pattern_len > max_pattern) {
            mvwprintw(win, 1, 4, "...%s",
                      state->search_pattern + state->pattern_len - max_pattern + 3);
        } else {
            mvwprintw(win, 1, 4, "%s", state->search_pattern);
        }
    }

    // Result count
    char count_str[32];
    snprintf(count_str, sizeof(count_str), " %d/%d ",
             state->result_count, state->file_cache_count);
    mvwprintw(win, 1, width - (int)strlen(count_str) - 1, "%s", count_str);

    // Separator
    mvwhline(win, 2, 1, ACS_HLINE, width - 2);

    // Results
    int visible_lines = height - 4;
    if (visible_lines < 1) visible_lines = 1;

    for (int i = 0; i < visible_lines; i++) {
        int result_idx = state->scroll_offset + i;
        if (result_idx >= state->result_count) {
            break;
        }

        int y = 3 + i;

        // Highlight selected
        if (result_idx == state->selected_index) {
            wattron(win, A_REVERSE);
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
            wattroff(win, A_REVERSE);
        }
    }

    // Footer with hints
    const char *hints = " ESC:cancel  Enter:select  j/k:navigate ";
    int hints_x = (width - (int)strlen(hints)) / 2;
    if (hints_x < 1) hints_x = 1;
    mvwprintw(win, height - 1, hints_x, "%s", hints);

    // Position cursor at end of search pattern
    wmove(win, 1, 4 + (int)state->pattern_len);

    wrefresh(win);
}

// ============================================================================
// Input Handling
// ============================================================================

int file_search_process_key(FileSearchState *state, int ch) {
    if (!state || !state->is_active) {
        return -1;
    }

    switch (ch) {
        case 27:  // ESC - cancel
            return -1;

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
        case 21:  // Ctrl+U
            file_search_page_up(state);
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
