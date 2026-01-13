/*
 * help_modal.c - Help Modal Dialog Implementation
 *
 * Displays a TUI popup with helpful commands and keyboard shortcuts.
 */

#include "help_modal.h"
#include "tui.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>

// Help content structure
typedef struct {
    const char *section;     // Section header (NULL for regular line)
    const char *left;        // Left column (command/key)
    const char *right;       // Right column (description)
} HelpLine;

// Help content data
static const HelpLine help_content[] = {
    // Commands section
    {" COMMANDS", NULL, NULL},
    {NULL, "/help", "Show this help dialog"},
    {NULL, "/clear", "Clear conversation history"},
    {NULL, "/exit, /quit", "Exit the application"},
    {NULL, "/add-dir <path>", "Add directory to working directories"},
    {NULL, "/themes", "Browse and preview color themes"},
    {NULL, "/voice", "Record voice input and transcribe"},
    {NULL, "/vim", "Open vim in current directory"},
    {NULL, "/dump [file-path]", "Dump conversation to file"},

    // Mode switching section
    {" MODE SWITCHING", NULL, NULL},
    {NULL, "Esc / Ctrl+[", "Enter Normal mode (scroll/navigate)"},
    {NULL, "i", "Enter Insert mode (type messages)"},
    {NULL, ":", "Enter Command mode (from Normal)"},
    {NULL, "/ or ?", "Enter Search mode (from Normal)"},
    {NULL, "Shift+Tab", "Toggle Plan mode (read-only tools)"},

    // Insert mode section
    {" INSERT MODE", NULL, NULL},
    {NULL, "Enter", "Send message"},
    {NULL, "Ctrl+C", "Cancel running action"},
    {NULL, "Ctrl+D", "Exit application"},
    {NULL, "Ctrl+F", "Open file search popup"},
    {NULL, "Ctrl+R", "Open history search popup"},
    {NULL, "Tab", "Autocomplete (commands/paths)"},

    // Insert mode editing section
    {" INSERT MODE (EDITING)", NULL, NULL},
    {NULL, "Ctrl+A", "Move to beginning of line"},
    {NULL, "Ctrl+E", "Move to end of line"},
    {NULL, "Ctrl+U", "Delete to beginning of line"},
    {NULL, "Ctrl+K", "Delete to end of line"},
    {NULL, "Ctrl+W", "Delete word backward"},
    {NULL, "Alt+B", "Move word backward"},
    {NULL, "Alt+F", "Move word forward"},
    {NULL, "Alt+D", "Delete word forward"},
    {NULL, "Alt+Backspace", "Delete word backward"},

    // Normal mode section
    {" NORMAL MODE", NULL, NULL},
    {NULL, "j / Down", "Scroll down one line"},
    {NULL, "k / Up", "Scroll up one line"},
    {NULL, "Ctrl+D", "Scroll down half page"},
    {NULL, "Ctrl+U", "Scroll up half page"},
    {NULL, "gg", "Go to top of conversation"},
    {NULL, "G", "Go to bottom of conversation"},
    {NULL, "( / )", "Jump between text blocks"},
    {NULL, "n / N", "Next/previous search match"},

    // Command mode section
    {" COMMAND MODE (:)", NULL, NULL},
    {NULL, ":q, :quit", "Quit application"},
    {NULL, ":w", "Write (no-op, for vim muscle memory)"},
    {NULL, ":wq", "Write and quit"},
    {NULL, ":!<cmd>", "Run shell command"},
    {NULL, ":re !<cmd>", "Insert command output into input"},
    {NULL, ":git", "Open vim-fugitive (if installed)"},

    // Environment section
    {" ENVIRONMENT VARIABLES", NULL, NULL},
    {NULL, "KLAWED_THEME", "Color theme name or path to .conf"},
    {NULL, "KLAWED_LOG_LEVEL", "DEBUG, INFO, WARN, ERROR"},
    {NULL, "KLAWED_BASH_TIMEOUT", "Bash command timeout (default: 30s)"},
    {NULL, "KLAWED_MCP_ENABLED", "Enable MCP tool servers (1/0)"},
    {NULL, "OPENAI_MODEL", "Model name override"},
    {NULL, "KLAWED_DB_PATH", "Database path for API history"},

    // Tips section
    {" TIPS", NULL, NULL},
    {NULL, "", "Token usage stats shown in status bar (Normal mode)"},
    {NULL, "", "API history stored in ./.klawed/api_calls.db"},
    {NULL, "", "Themes: tender, dracula, gruvbox-dark, solarized-dark"},
    {NULL, "", "MCP servers configured in ~/.config/klawed/"},
};

#define HELP_LINE_COUNT (sizeof(help_content) / sizeof(help_content[0]))

int help_modal_init(HelpModalState *state) {
    if (!state) {
        return -1;
    }

    memset(state, 0, sizeof(HelpModalState));
    state->content_lines = (int)HELP_LINE_COUNT;

    return 0;
}

void help_modal_cleanup(HelpModalState *state) {
    if (!state) {
        return;
    }

    help_modal_close(state);
}

int help_modal_show(HelpModalState *state, int screen_height, int screen_width) {
    if (!state) {
        return -1;
    }

    // Calculate popup dimensions (centered, 80% width, 80% height)
    state->popup_width = (screen_width * 80) / 100;
    if (state->popup_width < 60) state->popup_width = 60;
    if (state->popup_width > screen_width - 4) state->popup_width = screen_width - 4;
    if (state->popup_width > 90) state->popup_width = 90;  // Cap max width for readability

    state->popup_height = (screen_height * 80) / 100;
    if (state->popup_height < 20) state->popup_height = 20;
    if (state->popup_height > screen_height - 4) state->popup_height = screen_height - 4;

    state->popup_y = (screen_height - state->popup_height) / 2;
    state->popup_x = (screen_width - state->popup_width) / 2;

    // Create popup window
    state->popup_win = newwin(state->popup_height, state->popup_width,
                              state->popup_y, state->popup_x);
    if (!state->popup_win) {
        LOG_ERROR("[HelpModal] Failed to create popup window");
        return -1;
    }

    keypad(state->popup_win, TRUE);
    state->scroll_offset = 0;
    state->is_active = 1;

    LOG_DEBUG("[HelpModal] Opened (popup=%dx%d at %d,%d)",
              state->popup_width, state->popup_height,
              state->popup_x, state->popup_y);

    return 0;
}

void help_modal_close(HelpModalState *state) {
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

    LOG_DEBUG("[HelpModal] Closed");
}

void help_modal_render(HelpModalState *state) {
    if (!state || !state->popup_win || !state->is_active) {
        return;
    }

    WINDOW *win = state->popup_win;
    int width = state->popup_width;
    int height = state->popup_height;
    int use_colors = has_colors();

    // Clear and draw border
    werase(win);
    if (use_colors) {
        wattron(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
    }
    box(win, 0, 0);

    // Title
    const char *title = " Help ";
    int title_x = (width - (int)strlen(title)) / 2;
    if (title_x < 1) title_x = 1;
    wattron(win, A_BOLD);
    mvwprintw(win, 0, title_x, "%s", title);
    wattroff(win, A_BOLD);

    if (use_colors) {
        wattroff(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
    }

    // Content area dimensions
    int content_start_y = 2;
    int content_height = height - 4;  // Leave room for border, title, and footer
    int left_col_width = 22;  // Width for command/key column
    int left_col_x = 2;
    int right_col_x = left_col_x + left_col_width + 2;
    int right_col_width = width - right_col_x - 2;

    // Render visible content
    int visible_lines = 0;
    for (int i = state->scroll_offset; i < state->content_lines && visible_lines < content_height; i++) {
        const HelpLine *line = &help_content[i];
        int y = content_start_y + visible_lines;

        if (line->section) {
            // Section header
            if (use_colors) {
                wattron(win, COLOR_PAIR(NCURSES_PAIR_ASSISTANT) | A_BOLD);
            } else {
                wattron(win, A_BOLD);
            }
            mvwprintw(win, y, left_col_x, "%s", line->section);
            if (use_colors) {
                wattroff(win, COLOR_PAIR(NCURSES_PAIR_ASSISTANT) | A_BOLD);
            } else {
                wattroff(win, A_BOLD);
            }
        } else {
            // Regular line with command/key and description
            if (use_colors) {
                wattron(win, COLOR_PAIR(NCURSES_PAIR_USER));
            }
            mvwprintw(win, y, left_col_x, "%-*.*s", left_col_width, left_col_width,
                      line->left ? line->left : "");
            if (use_colors) {
                wattroff(win, COLOR_PAIR(NCURSES_PAIR_USER));
            }

            // Description (truncate if needed)
            if (line->right) {
                mvwprintw(win, y, right_col_x, "%.*s",
                          right_col_width, line->right);
            }
        }

        visible_lines++;
    }

    // Footer with instructions and scroll indicator
    int footer_y = height - 1;
    if (use_colors) {
        wattron(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
    }

    // Scroll indicators
    int total_scrollable = state->content_lines - content_height;
    if (total_scrollable > 0) {
        int percent = (state->scroll_offset * 100) / total_scrollable;
        if (state->scroll_offset == 0) {
            mvwprintw(win, footer_y, 2, " j/↓:Down  k/↑:Up  q/ESC/Enter:Close  [Top] ");
        } else if (state->scroll_offset >= total_scrollable) {
            mvwprintw(win, footer_y, 2, " j/↓:Down  k/↑:Up  q/ESC/Enter:Close  [Bottom] ");
        } else {
            mvwprintw(win, footer_y, 2, " j/↓:Down  k/↑:Up  q/ESC/Enter:Close  [%d%%] ", percent);
        }
    } else {
        mvwprintw(win, footer_y, 2, " Press q, ESC, or Enter to close ");
    }

    if (use_colors) {
        wattroff(win, COLOR_PAIR(NCURSES_PAIR_STATUS));
    }

    wrefresh(win);
}

int help_modal_handle_key(HelpModalState *state, int ch) {
    if (!state || !state->is_active) {
        return 1;  // Close if not active
    }

    int content_height = state->popup_height - 4;
    int max_scroll = state->content_lines - content_height;
    if (max_scroll < 0) max_scroll = 0;

    switch (ch) {
        case 'q':
        case 27:  // ESC
        case '\n':
        case '\r':
        case KEY_ENTER:
            return 1;  // Close modal

        case 'j':
        case KEY_DOWN:
            if (state->scroll_offset < max_scroll) {
                state->scroll_offset++;
            }
            break;

        case 'k':
        case KEY_UP:
            if (state->scroll_offset > 0) {
                state->scroll_offset--;
            }
            break;

        case 4:  // Ctrl+D - half page down
        case KEY_NPAGE:
            state->scroll_offset += content_height / 2;
            if (state->scroll_offset > max_scroll) {
                state->scroll_offset = max_scroll;
            }
            break;

        case 21:  // Ctrl+U - half page up
        case KEY_PPAGE:
            state->scroll_offset -= content_height / 2;
            if (state->scroll_offset < 0) {
                state->scroll_offset = 0;
            }
            break;

        case 'g':
            // gg - go to top
            state->scroll_offset = 0;
            break;

        case 'G':
            // G - go to bottom
            state->scroll_offset = max_scroll;
            break;

        default:
            // Don't close on unknown keys - just ignore them
            break;
    }

    return 0;  // Don't close
}

int help_modal_run(HelpModalState *state, int screen_height, int screen_width) {
    if (!state) {
        return -1;
    }

    // Show the modal
    if (help_modal_show(state, screen_height, screen_width) != 0) {
        return -1;
    }

    // Initial render
    help_modal_render(state);

    // Event loop
    int running = 1;
    while (running) {
        int ch = wgetch(state->popup_win);
        if (help_modal_handle_key(state, ch)) {
            running = 0;
        } else {
            help_modal_render(state);
        }
    }

    // Clean up
    help_modal_close(state);

    return 0;
}
