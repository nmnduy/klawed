/*
 * TUI (Terminal User Interface) - ncurses-based interface for Claude Code
 *
 * Provides a full-screen TUI with:
 * - Scrollable conversation window (top)
 * - Status line (middle)
 * - Input area (bottom)
 */

#ifndef TUI_H
#define TUI_H

#include <stdint.h>
#include "klawed_internal.h"
#include "todo.h"
#include "window_manager.h"
#include "history_file.h"
#include "file_search.h"
#include "history_search.h"
#ifndef TEST_BUILD
#include "persistence.h"
#else
// Forward declaration for test builds
struct PersistenceDB;
#endif
// Forward declaration for WINDOW type (not actually used, kept for compatibility)
typedef struct _win_st WINDOW;

typedef struct TUIInputBuffer TUIInputBuffer;

// TUI Color pairs (public API for conversation entries)
typedef enum {
    COLOR_PAIR_DEFAULT = 1,    // Foreground color for main text
    COLOR_PAIR_FOREGROUND = 2, // Explicit foreground color
    COLOR_PAIR_USER = 3,       // Green for user role names
    COLOR_PAIR_ASSISTANT = 4,  // Blue for assistant role names
    COLOR_PAIR_TOOL = 5,       // Cyan for tool execution indicators (softer)
    COLOR_PAIR_ERROR = 6,      // Red for errors
    COLOR_PAIR_STATUS = 7,     // Cyan for status messages
    COLOR_PAIR_PROMPT = 8,     // Green for input prompt
    COLOR_PAIR_TODO_COMPLETED = 9,   // Green for completed tasks
    COLOR_PAIR_TODO_IN_PROGRESS = 10, // Yellow for in-progress tasks
    COLOR_PAIR_TODO_PENDING = 11,     // Cyan/Blue for pending tasks
    COLOR_PAIR_SEARCH = 12            // Highlight color for search matches
} TUIColorPair;

// Ncurses color pair definitions (internal indices for init_pair/COLOR_PAIR)
// Used by TUI components including file_search
#define NCURSES_PAIR_FOREGROUND 1
#define NCURSES_PAIR_USER 2
#define NCURSES_PAIR_ASSISTANT 3
#define NCURSES_PAIR_STATUS 4
#define NCURSES_PAIR_ERROR 5
#define NCURSES_PAIR_PROMPT 6
#define NCURSES_PAIR_TODO_COMPLETED 7
#define NCURSES_PAIR_TODO_IN_PROGRESS 8
#define NCURSES_PAIR_TODO_PENDING 9
#define NCURSES_PAIR_TOOL 10
#define NCURSES_PAIR_SEARCH 11
#define NCURSES_PAIR_INPUT_BG 12
#define NCURSES_PAIR_INPUT_BORDER 13
#define NCURSES_PAIR_USER_MSG_BG 14

// Conversation message entry
typedef struct {
    char *prefix;            // Role prefix (e.g., "[User]", "[Assistant]")
    char *text;              // Message text
    TUIColorPair color_pair; // Color for display
} ConversationEntry;

// TUI Mode (Vim-like)
typedef enum {
    TUI_MODE_NORMAL,      // Normal mode (vim-like navigation, default for conversation viewing)
    TUI_MODE_INSERT,      // Insert mode (text input for sending messages)
    TUI_MODE_COMMAND,     // Command mode (entered with ':' from normal mode)
    TUI_MODE_SEARCH,      // Search mode (entered with '/' or '?' from normal mode)
    TUI_MODE_FILE_SEARCH,  // File search mode (entered with Ctrl+F from insert mode)
    TUI_MODE_HISTORY_SEARCH  // History search mode (entered with Ctrl+R from insert mode)
} TUIMode;

// Input box style (visual appearance)
typedef enum {
    INPUT_STYLE_BACKGROUND,  // Background color + left border (default)
    INPUT_STYLE_BORDER       // Full border with no background
} TUIInputBoxStyle;

// TUI State
typedef struct TUIStateStruct {
    // Centralized window manager (owns ncurses windows)
    WindowManager wm;

    // Input buffer state
    TUIInputBuffer *input_buffer;

    // Conversation entries (source of truth used to rebuild pad on resize)
    ConversationEntry *entries;
    int entries_count;
    int entries_capacity;

    // Status state
    char *status_message;    // Current status text (owned by TUI)
    int status_visible;      // Whether status should be shown
    int status_spinner_active;        // Spinner animation active flag
    int status_spinner_frame;         // Current spinner frame index
    uint64_t status_spinner_last_update_ns; // Last spinner frame update timestamp



    // Database connection for real-time token usage queries
    struct PersistenceDB *persistence_db;  // Database connection for token queries
    char *session_id;                     // Current session ID for token queries

    // Reference to conversation state (source of truth for plan_mode and other state)
    ConversationState *conversation_state;

    // Modes
    TUIMode mode;            // Current input mode (NORMAL, INSERT, or COMMAND)
    TUIInputBoxStyle input_box_style; // Current input box visual style
    int normal_mode_last_key; // Previous key in normal mode (for gg, G combos)
    char *command_buffer;    // Buffer for command mode input (starts with ':')
    int command_buffer_len;  // Length of command buffer
    int command_buffer_capacity; // Capacity of command buffer

    // Search state
    char *search_buffer;     // Buffer for search pattern input
    int search_buffer_len;   // Length of search buffer
    int search_buffer_capacity; // Capacity of search buffer
    int search_direction;    // 1 for forward ('/'), -1 for backward ('?')
    int last_search_match_line; // Line number of last search match
    char *last_search_pattern;  // Last search pattern used

    int is_initialized;      // Whether TUI has been set up

    // Persistent input history (memory + DB)
    char **input_history;    // Array of history strings (oldest -> newest)
    int input_history_count; // Number of entries loaded
    int input_history_capacity; // Capacity of array
    int input_history_index; // Current position when browsing history (-1 = not browsing)
    char *input_saved_before_history; // Input saved before starting history navigation

    // History database
    HistoryFile *history_file;

    // Subagent display state
    int subagent_display_active;      // Whether subagent display is active
    int subagent_display_scroll_offset; // Scroll offset for subagent display
    int subagent_display_max_lines;   // Max lines to show in subagent display

    // TODO list display state
    int todo_display_active;          // Whether TODO list is being displayed
    int todo_display_scroll_offset;   // Scroll offset for TODO list

    // Cursor position for normal mode
    int normal_cursor_line;           // Current line in conversation (0-indexed)
    int normal_cursor_col;            // Current column in line (0-indexed)
    int normal_viewport_top_line;     // Top line of viewport (for scrolling)

    // Mouse support
    int mouse_enabled;                // Whether mouse events are enabled

    // Terminal state for suspend/resume
    int terminal_suspended;           // Whether terminal is currently suspended

    // File search state (Ctrl+F)
    FileSearchState file_search;      // File search popup state
    HistorySearchState history_search;  // History search popup state

    // Vim-fugitive availability (cached to avoid slow checks)
    int vim_fugitive_available;       // -1 = unknown, 0 = not available, 1 = available
    pthread_mutex_t vim_fugitive_mutex; // Mutex for thread-safe access
    int vim_fugitive_mutex_initialized; // Tracks mutex initialization
} TUIState;

// Initialize TUI (must be called before any other TUI functions)
// tui: TUI state structure (caller-allocated)
// state: Conversation state (for plan_mode and other state queries)
// Returns 0 on success, -1 on failure
int tui_init(TUIState *tui, ConversationState *state);

// Clean up TUI resources
void tui_cleanup(TUIState *tui);

// Add a line to the conversation display
// prefix: Role prefix (e.g., "[User]", "[Assistant]")
// text: Message text
// color_pair: Color to use for display
void tui_add_conversation_line(TUIState *tui, const char *prefix, const char *text, TUIColorPair color_pair);

// Update the last conversation line (for streaming responses)
void tui_update_last_conversation_line(TUIState *tui, const char *text);

// Update status message
void tui_update_status(TUIState *tui, const char *status_text);

// Refresh the display (call after making changes)
void tui_refresh(TUIState *tui);

// Clear conversation display and show mascot banner
void tui_clear_conversation(TUIState *tui, const char *version, const char *model, const char *working_dir);

// Handle terminal resize
void tui_handle_resize(TUIState *tui);

// Show startup banner with version, model, and working directory
void tui_show_startup_banner(TUIState *tui, const char *version, const char *model, const char *working_dir);

// Scroll conversation up/down
// direction: positive = scroll down, negative = scroll up
void tui_scroll_conversation(TUIState *tui, int direction);

// Poll for input (non-blocking)
// Returns character code if input available, -1 otherwise
int tui_poll_input(TUIState *tui);

// Process a single input character
// Returns 0 if character was processed, -1 on error
int tui_process_input_char(TUIState *tui, int ch, const char *prompt, void *user_data);

// Get current input buffer contents (caller must not free)
const char* tui_get_input_buffer(TUIState *tui);

// Clear input buffer
void tui_clear_input_buffer(TUIState *tui);

// Insert text into input buffer at cursor position
// Returns 0 on success, -1 on error
int tui_insert_input_text(TUIState *tui, const char *text);

// Redraw input area with prompt
void tui_redraw_input(TUIState *tui, const char *prompt);

// Main event loop
// Returns 0 on normal exit, 1 on exit request (Ctrl+D or :q)
int tui_event_loop(TUIState *tui, const char *prompt,
                   int (*submit_callback)(const char *input, void *user_data),
                   int (*interrupt_callback)(void *user_data),
                   int (*keypress_callback)(void *user_data),
                   int (*external_input_callback)(void *user_data, char *buffer, int buffer_size),
                   void *user_data,
                   void *msg_queue_ptr);

// Drain any remaining messages after the event loop stops
void tui_drain_message_queue(TUIState *tui, const char *prompt, void *msg_queue);

// Render a TODO list with colored items based on status
// list: TodoList to render
// Each item will be rendered with its status-specific color
void tui_render_todo_list(TUIState *tui, const TodoList *list);

// Render active subagent processes with their status and log tail
// This should be called during event loop redraws to show real-time subagent output
void tui_render_active_subagents(TUIState *tui);

// Update token usage counts displayed in status bar
// prompt_tokens: Total input tokens used

// Suspend TUI (restore terminal to normal mode for external commands)
// Returns 0 on success, -1 on error
int tui_suspend(TUIState *tui);

// Resume TUI (restore terminal to program mode after external commands)
// Returns 0 on success, -1 on error
int tui_resume(TUIState *tui);

// Check if vim-fugitive is available (cached result)
// Returns: -1 = unknown/not checked yet, 0 = not available, 1 = available
int tui_get_vim_fugitive_available(TUIState *tui);

// Start background check for vim-fugitive availability
// This spawns a thread to check without blocking the main thread
void tui_start_vim_fugitive_check(TUIState *tui);

#endif // TUI_H
