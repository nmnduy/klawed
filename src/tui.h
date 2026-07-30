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
#include "spring.h"
#include "spinner_effects.h"
#include "text_diffusion.h"
#include "commands.h"
#ifndef TEST_BUILD
#include "persistence.h"
#else
// Forward declaration for test builds
struct PersistenceDB;
#endif
// Forward declaration for WINDOW type (not actually used, kept for compatibility)
typedef struct _win_st WINDOW;

typedef struct TUIInputBuffer TUIInputBuffer;
typedef struct VoiceModeState VoiceModeState;

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
    COLOR_PAIR_TODO_PENDING = 11,     // Magenta for pending tasks (distinct from assistant cyan)
    COLOR_PAIR_SEARCH = 12,           // Highlight color for search matches
    COLOR_PAIR_TOOL_DIM = 13,         // Dimmed gray for tool text (arguments)
    COLOR_PAIR_DIFF_CONTEXT = 14,     // Dimmed gray for diff context lines
    COLOR_PAIR_GOAL = 15              // Goal tag using status/tool color with italic text
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
#define NCURSES_PAIR_ASSISTANT_BG 15
#define NCURSES_PAIR_ASSISTANT_BORDER_BG 16  // Assistant border color on assistant background
#define NCURSES_PAIR_TOOL_DIM 17             // Dimmed gray for tool text
#define NCURSES_PAIR_DIFF_CONTEXT 18         // Dimmed gray for diff context lines
#define NCURSES_PAIR_GOAL 19                 // Goal tag (matches tool tag color, italicized)
#define NCURSES_PAIR_ERROR_BG 20             // Error text on error-tinted background
#define NCURSES_PAIR_CODE_BLOCK 21           // Code block text on recessed background
#define NCURSES_PAIR_INLINE_CODE 22          // Inline code with subtle tint
#define NCURSES_PAIR_H1_ACCENT 23            // H1 header accent color for text + rule
#define NCURSES_PAIR_STATUS_BG 24            // Status bar background

// Conversation message entry
typedef struct {
    char *prefix;            // Role prefix (e.g., "[User]", "[Assistant]")
    char *text;              // Message text
    TUIColorPair color_pair; // Color for display
    int pad_start_line;      // Exact line where this entry starts in the current pad
    void *md_cache;          // Opaque markdown pre-parse cache (MDParsedDoc *)
} ConversationEntry;

// TUI Mode (Vim-like)
typedef enum {
    TUI_MODE_NORMAL,      // Normal mode (vim-like navigation, default for conversation viewing)
    TUI_MODE_INSERT,      // Insert mode (text input for sending messages)
    TUI_MODE_COMMAND,     // Command mode (entered with ':' from normal mode)
    TUI_MODE_SEARCH,      // Search mode (entered with '/' or '?' from normal mode)
    TUI_MODE_FILE_SEARCH,   // File search mode (entered with Ctrl+F from insert mode)
    TUI_MODE_HISTORY_SEARCH, // History search mode (entered with Ctrl+R from insert mode)
    TUI_MODE_VOICE,           // Voice mode (entered by holding spacebar in INSERT mode)
    TUI_MODE_COMMAND_PALETTE  // Command palette (entered with Ctrl+K on empty input)
} TUIMode;

// Input box style (visual appearance)
typedef enum {
    INPUT_STYLE_BACKGROUND,  // Background color + left border
    INPUT_STYLE_BORDER,      // Full border with no background
    INPUT_STYLE_HORIZONTAL,  // Top and bottom border only, no left/right borders
    INPUT_STYLE_BLAND        // Just caret '>>>' with text on general background, no padding (default)
} TUIInputBoxStyle;

// Response style (visual appearance of assistant responses)
typedef enum {
    RESPONSE_STYLE_BORDER,   // Left border '│ ' on each line (default)
    RESPONSE_STYLE_CARET,    // Leading '>>> ' caret, no wrapping borders
    RESPONSE_STYLE_ROBOT,    // Robot face header with left border
    RESPONSE_STYLE_CAT,      // Cat face header '=^..^='
    RESPONSE_STYLE_BG        // Background-tinted text (light grey/tinted background)
} TUIResponseStyle;

// AI thinking style (visual appearance of thinking/spinner indicator)
typedef enum {
    THINKING_STYLE_WAVE,      // Rolling waveform visualizer (default)
    THINKING_STYLE_PACMAN    // Pacman eating dots, distance shows context usage
} TUIThinkingStyle;

// Mascot style (which cat art to show in the startup banner)
typedef enum {
    MASCOT_NYAN,     // Nyan cat (boxy face with @ w @ eyes, default)
    MASCOT_CLASSIC   // Classic /\\_/\\ cat
} TUIMascotStyle;

// Vim-style marks for navigating the conversation
#define MAX_MARKS 26  // a-z

typedef struct {
    int line_number;   // Pad content line (-1 if not set)
    char name;         // Mark name ('a' - 'z')
    char is_set;       // 1 if mark is set, 0 otherwise
} TUIMark;

typedef struct {
    TUIMark marks[MAX_MARKS];  // Marks a-z (index 0='a', 1='b', etc.)
    char pending;              // 'm' when waiting for mark char, '\'' when waiting for jump char, 0 otherwise
} TUIMarkState;

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

    // Track the starting line of the last assistant message for scroll-to-response feature
    int last_assistant_line;  // Content line where last [Assistant] message starts (-1 if none)

    // Status state
    char *status_message;    // Current status text (owned by TUI)
    int status_visible;      // Whether status should be shown
    int status_spinner_active;        // Spinner animation active flag
    int status_spinner_frame;         // Current spinner frame index
    uint64_t status_spinner_last_update_ns; // Last spinner frame update timestamp

    // Spring physics for smooth spinner animation
    double status_spinner_pos;        // Current angular position (float, for smooth animation)
    double status_spinner_vel;        // Current angular velocity
    Spring status_spinner_spring;     // Spring physics configuration
    int status_spinner_spring_initialized; // Whether spring has been initialized

    // Enhanced spinner effects
    SpinnerEffectConfig status_spinner_effect; // Spinner effect configuration

    // Text diffusion effect for status messages
    TextDiffusionConfig status_text_diffusion; // Diffusion animation config

    // Pacman thinking style state
    int pacman_dots_eaten;       // Animation sweep position (0..anchor, loops when working)
    int pacman_direction;        // 1 = moving right, -1 = moving left (unused, kept for compat)
    int pacman_max_dots;         // Total bar width (dots + pac-man + sentinel)
    int pacman_anim_frame;       // Sub-frame counter for mouth open/close animation
    uint64_t pacman_last_step_ns; // Timestamp of last sweep step (for time-based animation)


    // Database connection for real-time token usage queries
    struct PersistenceDB *persistence_db;  // Database connection for token queries
    char *session_id;                     // Current session ID for token queries

    // Reference to conversation state (source of truth for plan_mode and other state)
    ConversationState *conversation_state;

    // Modes
    TUIMode mode;            // Current input mode (NORMAL, INSERT, or COMMAND)
    TUIInputBoxStyle input_box_style; // Current input box visual style
    TUIResponseStyle response_style;  // Current response visual style
    TUIThinkingStyle thinking_style;  // Current thinking/spinner visual style
    TUIMascotStyle mascot_style;      // Current mascot style for startup banner
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

    // Auto-complete state for command input (triggered by ; or : prefix in INSERT mode)
    int cmd_autocomplete_active;       // 1 when showing autocomplete dropdown
    char *cmd_autocomplete_filter;     // Filter text (what user typed after prefix)
    int cmd_autocomplete_selected;     // Currently selected index (0-based, -1 = none)
    char **cmd_autocomplete_options;   // Matching command name suggestions
    int cmd_autocomplete_count;        // Number of matching options
    int cmd_autocomplete_prefix_type;  // 0 = slash commands ';'→'/', 1 = colon commands ':'

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
    // TODO banner render cache — skip re-render when state unchanged
    size_t todo_banner_last_in_progress;
    size_t todo_banner_last_pending;
    size_t todo_banner_last_completed;
    int todo_banner_last_was_visible; // 1 if last render showed the banner

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

    // Tool output connection tracking (for └─ tree connector)
    char *last_tool_name;             // Last rendered tool name (for tree connector)

    // Wrap toggle state
    int wrap_enabled;                 // 1 = text wraps at screen width (default), 0 = horizontal scroll for long lines

    // Streaming state tracking (prevents user input from hijacking AI streaming)
    int streaming_entry_index;        // Index of entry currently being streamed to (-1 if none)

    // Flag set when a non-last entry is updated via streaming (e.g., reasoning trace
    // after user submits a message). Triggers a full conversation pad rebuild so the
    // streaming content remains visible even after new entries are added after it.
    int needs_conv_pad_rebuild;       // 1 if pad needs rebuild after deferred updates

    // Vim-style marks (m{a-z} to set, '{a-z} to jump)
    TUIMarkState marks;               // Marks state for conversation navigation

    // Command palette state (Ctrl+K on empty input)
    int cmd_palette_active;           // 1 when command palette is visible
    char *cmd_palette_filter;         // Filter text buffer
    int cmd_palette_filter_len;       // Length of filter text
    int cmd_palette_filter_capacity;  // Capacity of filter buffer
    int cmd_palette_selected;         // Currently selected command index (0-based)
    const Command **cmd_palette_commands; // Array of matching command pointers
    int cmd_palette_count;            // Number of total commands
    int cmd_palette_matched_count;    // Number of commands after filtering
    int cmd_palette_matched_indices[MAX_COMMANDS]; // Indices of matched commands

    // Voice mode state (push-to-talk recording + transcription)
    VoiceModeState *voice_mode;       // Voice mode state (NULL if not initialized)

    // Auto-scroll state: 1 = scroll to bottom when new content arrives,
    // 0 = stay at current position. Set to 1 on init/submit/reaching-bottom,
    // cleared to 0 when user scrolls up. Replaces the old per-event
    // "was_at_bottom" position check with persistent user-intent tracking.
    int auto_scroll_enabled;
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

// Update a specific conversation entry by index (for streaming to tracked entry)
void tui_update_conversation_entry(TUIState *tui, int entry_index, const char *text);

// Update status message
void tui_update_status(TUIState *tui, const char *status_text);

// Refresh the display (call after making changes)
void tui_refresh(TUIState *tui);

// Clear conversation display and show mascot banner
void tui_clear_conversation(TUIState *tui, const char *version, const char *model, const char *working_dir);

// Handle terminal resize
void tui_handle_resize(TUIState *tui);

// Show startup banner with version, model, and working directory
void tui_show_startup_banner(TUIState *tui, const char *version, const char *model, const char *working_dir, const char *session_id);

// Scroll conversation up/down
// direction: positive = scroll down, negative = scroll up
void tui_scroll_conversation(TUIState *tui, int direction);

// Scroll to the last assistant message (for end-of-turn positioning)
// Scrolls so the last [Assistant] message is at the top of the viewport
void tui_scroll_to_last_assistant(TUIState *tui);

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

// Render TODO banner at bottom of screen (between status and input)
// Shows incomplete todos persistently when they exist
// Returns 1 if banner was rendered (incomplete todos exist), 0 otherwise
int tui_render_todo_banner(TUIState *tui, const TodoList *list);

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

// Reload TUI colors from current theme
// Call this after changing the theme to apply new colors immediately
void tui_reload_colors(void);

// Update terminal window title based on current spinner state
// Shows a spinner frame in the title when the AI is working
void tui_update_terminal_title(TUIState *tui);

// ============================================================================
// TUI Icons (selected based on KLAWED_NO_NERD_FONT env var)
// ============================================================================

// Role prefixes
const char* tui_icon_assistant(void);
const char* tui_icon_user(void);

// Reasoning tags
const char* tui_icon_reasoning_open(void);
const char* tui_icon_reasoning_close(void);

// Command icon (e.g. "" or "[Command]")
const char* tui_icon_command(void);

// Tool icon (just the icon character, e.g. "●" or "")
const char* tui_icon_tool(void);

// TODO icons
const char* tui_icon_todo_current(void);
const char* tui_icon_todo_pending(void);
const char* tui_icon_todo_completed(void);

// Status indicators (success, error, warning, active, pending)
const char* tui_icon_success(void);
const char* tui_icon_error(void);
const char* tui_icon_warning(void);
const char* tui_icon_active(void);
const char* tui_icon_pending(void);

// Goal tag (e.g. "" or "[Goal]")
const char* tui_icon_goal(void);

// Folder icon for path display
const char* tui_icon_folder(void);

// Mode label text (returns compact mode name like "NORMAL", "INSERT", etc.)
const char* tui_mode_label(TUIMode mode);

// Internal functions (used across TUI modules during refactoring)
// These will eventually move to their respective specialized modules

// Render a conversation entry to the pad (rendering module)
// Returns 0 on success, -1 on error
int render_entry_to_pad(TUIState *tui, const char *prefix, const char *text, TUIColorPair color_pair,
                        void **md_cache_ptr);

// Render a markdown document to the conversation pad.
// If border_str is non-NULL, each line is prefixed with the border.
// If border_str is NULL, no border is drawn (caret-style).
// md_cache_ptr: optional opaque pointer to a markdown pre-parse cache (MDParsedDoc *).
//               If non-NULL and valid, avoids re-scanning text on rebuilds.
void render_markdown_document(TUIState *tui, const char *text, int text_pair,
                              int border_pair, const char *border_str,
                              void **md_cache_ptr);

// Render the status window (rendering module)
void render_status_window(TUIState *tui);

// Redraw conversation from entries (rendering module)
void redraw_conversation(TUIState *tui);

// Render input window (rendering module)
void input_redraw(TUIState *tui, const char *prompt);

// Update command autocomplete state based on current input buffer content.
// Called from input_redraw when in INSERT mode to keep the dropdown in sync.
void update_cmd_autocomplete(TUIState *tui);

// Refresh conversation viewport after resize (rendering module)
void refresh_conversation_viewport(TUIState *tui);

#endif // TUI_H
