#include "command_dispatch.h"
#include "../logger.h"
#include "../ui/ui_output.h"
#include "../conversation/conversation_state.h"
#include "../util/string_utils.h"
#include "../util/env_utils.h"
#include "../tools/tool_bash.h"
#include "../tool_utils.h"
#include "../process_utils.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>

int handle_vim_command(TUIState *tui, TUIMessageQueue *queue, const char *command) {
    if (!tui || !queue || !command) {
        return 0;
    }

    // Skip the leading ':'
    const char *cmd = command + 1;

    // Handle empty command (just ':')
    if (cmd[0] == '\0') {
        return 0;
    }

    // Create a copy of the command to trim trailing whitespace
    char *cmd_copy = strdup(cmd);
    if (!cmd_copy) {
        LOG_ERROR("Failed to allocate memory for command copy");
        return 0;
    }

    // Trim trailing whitespace from the command copy
    trim_trailing_whitespace(cmd_copy);

    // Check for quit commands
    if (strcmp(cmd_copy, "q") == 0 || strcmp(cmd_copy, "quit") == 0 || strcmp(cmd_copy, "wq") == 0) {
        free(cmd_copy);
        return 1;  // Signal to exit
    }

    // Check for clear command
    if (strcmp(cmd_copy, "clear") == 0) {
        // Get the conversation state from the TUI
        ConversationState *state = tui->conversation_state;
        if (state) {
            // Clear the conversation state
            clear_conversation(state);
            // Clear the TUI conversation display
            tui_clear_conversation(tui, VERSION, state->model, state->working_dir);
            ui_append_line(tui, queue, "[Status]", "Conversation cleared", COLOR_PAIR_STATUS);
        } else {
            ui_show_error(tui, queue, "Failed to clear conversation: no state available");
        }
        free(cmd_copy);
        return 0;
    }

    // Check for help command
    if (strcmp(cmd_copy, "help") == 0) {
        ui_append_line(tui, queue, "[Help]", "Vim-style commands:", COLOR_PAIR_STATUS);
        ui_append_line(tui, queue, "[Help]", "  :q, :quit, :wq - Exit klawed", COLOR_PAIR_STATUS);
        ui_append_line(tui, queue, "[Help]", "  :clear - Clear conversation history", COLOR_PAIR_STATUS);
        ui_append_line(tui, queue, "[Help]", "  :!<cmd> - Execute shell command (e.g., :!ls)", COLOR_PAIR_STATUS);
        ui_append_line(tui, queue, "[Help]", "  :re !<cmd> - Execute command and insert output into input box", COLOR_PAIR_STATUS);
        ui_append_line(tui, queue, "[Help]", "  :vim - Open vim editor (shortcut for :!vim)", COLOR_PAIR_STATUS);
        ui_append_line(tui, queue, "[Help]", "  :git - Open vim-fugitive (requires vim-fugitive plugin)", COLOR_PAIR_STATUS);
        ui_append_line(tui, queue, "[Help]", "  :help - Show this help", COLOR_PAIR_STATUS);
        free(cmd_copy);
        return 0;
    }

    // Check for shell escape: :!<cmd>
    if (cmd_copy[0] == '!') {
        const char *shell_cmd = cmd_copy + 1;
        // Skip leading whitespace
        while (*shell_cmd == ' ' || *shell_cmd == '\t') {
            shell_cmd++;
        }

        if (shell_cmd[0] == '\0') {
            ui_show_error(tui, queue, "No command specified after :!");
            free(cmd_copy);
            return 0;
        }

        // Show what we're executing
        char status_msg[256];
        snprintf(status_msg, sizeof(status_msg), "Executing: %s", shell_cmd);
        ui_set_status(tui, queue, status_msg);

        // Suspend TUI to run command
        if (tui_suspend(tui) != 0) {
            ui_show_error(tui, queue, "Failed to suspend TUI for shell command");
            free(cmd_copy);
            return 0;
        }

        // Run the command
        int rc = system(shell_cmd);
        (void)rc;  // We don't display the return code

        // Note: No "press any key" prompt here - vim is an interactive editor
        // that manages its own screen, unlike regular shell commands

        // Resume TUI
        if (tui_resume(tui) != 0) {
            ui_show_error(tui, queue, "Failed to resume TUI after shell command");
        }

        ui_set_status(tui, queue, "");
        free(cmd_copy);
        return 0;
    }

    // Check for read command output: :re !<cmd>
    if (strncmp(cmd_copy, "re !", 4) == 0) {
        const char *shell_cmd = cmd_copy + 4;
        if (shell_cmd[0] == '\0') {
            ui_show_error(tui, queue, "No command specified after :re !");
            free(cmd_copy);
            return 0;
        }

        // Show what we're executing
        char status_msg[256];
        snprintf(status_msg, sizeof(status_msg), "Reading output from: %s", shell_cmd);
        ui_set_status(tui, queue, status_msg);

        // Execute command and capture output
        int timed_out = 0;
        char *output = NULL;
        size_t output_size = 0;
        int interrupt_requested = 0;

        int exit_code = execute_command_with_timeout(
            shell_cmd,
            30,  // 30 second timeout
            &timed_out,
            &output,
            &output_size,
            &interrupt_requested
        );

        if (timed_out) {
            ui_show_error(tui, queue, "Command timed out after 30 seconds");
        } else if (exit_code != 0) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Command failed with exit code %d", exit_code);
            ui_show_error(tui, queue, error_msg);
        } else if (output && output_size > 0) {
            // Success - put output in input buffer
            // Clear current input first
            tui_clear_input_buffer(tui);

            // Copy output to input buffer
            size_t output_len = output_size;
            if (output_len > 0 && output[output_len-1] == '\n') {
                output_len--;  // Remove trailing newline
            }

            // Truncate if too long (avoid buffer overflow)
            // We'll insert in chunks if needed
            const size_t MAX_INSERT = 4096;  // Reasonable limit
            if (output_len > MAX_INSERT) {
                output_len = MAX_INSERT;
                char trunc_msg[256];
                snprintf(trunc_msg, sizeof(trunc_msg),
                         "Command output truncated to %zu chars", output_len);
                ui_set_status(tui, queue, trunc_msg);
            }

            if (output_len > 0) {
                // Create a null-terminated copy of the output
                char *output_copy = malloc(output_len + 1);
                if (output_copy) {
                    memcpy(output_copy, output, output_len);
                    output_copy[output_len] = '\0';

                    // Insert into input buffer
                    if (tui_insert_input_text(tui, output_copy) == 0) {
                        char success_msg[256];
                        snprintf(success_msg, sizeof(success_msg),
                                 "Command output (%zu chars) loaded into input buffer", output_len);
                        ui_set_status(tui, queue, success_msg);
                        // Redraw input to show the inserted text
                        tui_redraw_input(tui, NULL);
                    } else {
                        ui_show_error(tui, queue, "Failed to insert command output into input buffer");
                    }
                    free(output_copy);
                } else {
                    ui_show_error(tui, queue, "Memory allocation failed for command output");
                }
            } else {
                ui_set_status(tui, queue, "Command produced no output");
            }
        }

        if (output) {
            free(output);
        }

        ui_set_status(tui, queue, "");
        free(cmd_copy);
        // Return -1 to indicate "don't clear input buffer"
        return -1;
    }

    // Check for :vim command (shortcut for :!vim)
    if (strcmp(cmd_copy, "vim") == 0) {
        // Treat as :!vim
        const char *shell_cmd = "vim";
        // Show what we're executing
        char status_msg[256];
        snprintf(status_msg, sizeof(status_msg), "Executing: %s", shell_cmd);
        ui_set_status(tui, queue, status_msg);

        // Suspend TUI to run command
        if (tui_suspend(tui) != 0) {
            ui_show_error(tui, queue, "Failed to suspend TUI for shell command");
            free(cmd_copy);
            return 0;
        }

        // Run the command
        int rc = system(shell_cmd);
        (void)rc;  // We don't display the return code

        // Vim-style: wait for Enter before resuming
        printf("\nPress ENTER or type command to continue");
        fflush(stdout);

        // Read a line from stdin
        char *line = NULL;
        size_t linecap = 0;
        ssize_t linelen = getline(&line, &linecap, stdin);

        if (linelen == -1) {
            // Handle EOF or error
            LOG_DEBUG("EOF or error reading from stdin after shell command");
        } else if (linelen > 0 && line[linelen-1] == '\n') {
            line[linelen-1] = '\0';
            linelen--;
        }

        // If user typed a command (not just Enter), run it
        if (linelen > 0) {
            int rc2 = system(line);
            (void)rc2;
        }

        free(line);

        // Resume TUI
        if (tui_resume(tui) != 0) {
            ui_show_error(tui, queue, "Failed to resume TUI after shell command");
        }

        ui_set_status(tui, queue, "");
        free(cmd_copy);
        return 0;
    }

    // Check for :git command (opens vim-fugitive if available)
    if (strcmp(cmd_copy, "git") == 0) {
        // Check cached vim-fugitive availability first
        int fugitive_available = tui_get_vim_fugitive_available(tui);

        // If not checked yet, run the check synchronously (this will be slow)
        if (fugitive_available == -1) {
            ui_set_status(tui, queue, "Checking vim-fugitive availability...");

            char test_cmd[512];
            snprintf(test_cmd, sizeof(test_cmd),
                     "vim -c \"if exists(':Git') | q | else | cquit 1 | endif\" -c \"q\" 2>&1");

            FILE *fp = popen(test_cmd, "r");
            if (!fp) {
                ui_show_error(tui, queue, "Failed to check vim-fugitive availability");
                ui_set_status(tui, queue, "");
                free(cmd_copy);
                return 0;
            }

            char buffer[256];
            // Read output to check for errors
            while (fgets(buffer, sizeof(buffer), fp) != NULL) {
                // Just consume output
            }

            int rc = pclose(fp);
            fugitive_available = (rc == 0) ? 1 : 0;

            // Cache the result for future use
            if (tui->vim_fugitive_mutex_initialized) {
                pthread_mutex_lock(&tui->vim_fugitive_mutex);
                tui->vim_fugitive_available = fugitive_available;
                pthread_mutex_unlock(&tui->vim_fugitive_mutex);
            }

            ui_set_status(tui, queue, "");
        }

        if (fugitive_available == 1) {
            // vim-fugitive is available, open vim with :Git command
            const char *shell_cmd = "vim -c Git";
            char status_msg[256];
            snprintf(status_msg, sizeof(status_msg), "Opening vim-fugitive: %s", shell_cmd);
            ui_set_status(tui, queue, status_msg);

            // Suspend TUI to run command
            if (tui_suspend(tui) != 0) {
                ui_show_error(tui, queue, "Failed to suspend TUI for shell command");
                free(cmd_copy);
                return 0;
            }

            // Run vim with Git command
            int vim_rc = system(shell_cmd);
            (void)vim_rc;

            // Note: No "press any key" prompt here - vim is an interactive editor
            // that manages its own screen, unlike regular shell commands

            // Resume TUI
            if (tui_resume(tui) != 0) {
                ui_show_error(tui, queue, "Failed to resume TUI after shell command");
            }
        } else if (fugitive_available == 0) {
            // vim-fugitive is not available
            ui_append_line(tui, queue, "[Info]", "vim-fugitive plugin is not available", COLOR_PAIR_STATUS);
            ui_append_line(tui, queue, "[Info]", "Install it with your vim plugin manager:", COLOR_PAIR_STATUS);
            ui_append_line(tui, queue, "[Info]", "  vim-plug: Plug 'tpope/vim-fugitive'", COLOR_PAIR_STATUS);
            ui_append_line(tui, queue, "[Info]", "  Vundle: Plugin 'tpope/vim-fugitive'", COLOR_PAIR_STATUS);
            ui_append_line(tui, queue, "[Info]", "  Pathogen: git clone https://github.com/tpope/vim-fugitive ~/.vim/bundle/vim-fugitive", COLOR_PAIR_STATUS);
        }

        ui_set_status(tui, queue, "");
        free(cmd_copy);
        return 0;
    }

    // Unknown command
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "Unknown vim command: %s", cmd_copy);
    ui_show_error(tui, queue, error_msg);
    free(cmd_copy);
    return 0;
}
