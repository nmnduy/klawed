/*
 * commands.h - Command Registration and Dispatch System
 *
 * Provides a table-driven command system for slash commands like:
 * /exit, /quit, /clear, /add-dir, /help
 */

#ifndef COMMANDS_H
#define COMMANDS_H

#include "ncurses_input.h"  // For CompletionResult and CompletionFn types
#include "claude_internal.h"

// ============================================================================
// Command Definition
// ============================================================================

typedef struct {
    const char *name;         // Command name (without '/' prefix), e.g., "add-dir"
    const char *usage;        // Usage string, e.g., "/add-dir <path>"
    const char *description;  // One-line description for /help
    int (*handler)(ConversationState *state, const char *args);  // Handler function
    CompletionFn completer;   // Optional: tab completion for arguments
    int needs_terminal;       // Whether command needs terminal interaction (e.g., /voice)
} Command;

// ============================================================================
// API Functions
// ============================================================================

/**
 * Initialize the command system
 * Registers all built-in commands
 */
void commands_init(void);

/**
 * Set TUI mode flag
 * When true, commands won't print to stdout/stderr (prevents ncurses corruption)
 *
 * @param enabled  1 for TUI mode, 0 for single-command mode
 */
void commands_set_tui_mode(int enabled);

/**
 * Register a new command
 *
 * @param cmd  Pointer to Command struct (must remain valid)
 */
void commands_register(const Command *cmd);

/**
 * Execute a command from user input
 *
 * @param state  Conversation state
 * @param input  Full input line (including '/' prefix)
 * @param cmd_out Optional: pointer to receive command that was executed
 * @return       0 on success, -1 if command not found, -2 to exit
 */
int commands_execute(ConversationState *state, const char *input, const Command **cmd_out);

/**
 * Get list of all registered commands
 *
 * @param count  Pointer to int that will receive command count
 * @return       Array of Command pointers
 */
const Command** commands_list(int *count);

/**
 * Look up a command by name without executing it
 *
 * @param name  Command name (without '/' prefix)
 * @return      Command pointer or NULL if not found
 */
const Command* commands_lookup(const char *name);


/**
 * Tab completion dispatcher for commands
 *
 * @param line        Full input line
 * @param cursor_pos  Cursor position in line
 * @param ctx         ConversationState pointer
 * @return            CompletionResult* or NULL
 */
CompletionResult* commands_tab_completer(const char *line, int cursor_pos, void *ctx);

#endif // COMMANDS_H
