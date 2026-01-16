#ifndef COMMAND_DISPATCH_H
#define COMMAND_DISPATCH_H

#include "../tui.h"
#include "../message_queue.h"

/**
 * Handle vim-style commands from input box
 * 
 * Processes commands starting with ':' including:
 * - :q, :quit, :wq - Exit klawed
 * - :clear - Clear conversation history
 * - :help - Show help
 * - :!<cmd> - Execute shell command
 * - :re !<cmd> - Execute command and insert output into input buffer
 * - :vim - Open vim editor
 * - :git - Open vim-fugitive
 * 
 * @param tui       TUI state
 * @param queue     Message queue for async updates
 * @param command   Command string (starting with ':')
 * @return          1 to exit, 0 to continue, -1 to not clear input buffer
 */
int handle_vim_command(TUIState *tui, TUIMessageQueue *queue, const char *command);

#endif // COMMAND_DISPATCH_H
