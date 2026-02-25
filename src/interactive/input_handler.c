#include "input_handler.h"
#include "command_dispatch.h"
#include "response_processor.h"
#include "../logger.h"
#include "../ui/ui_output.h"
#include "../conversation/message_builder.h"
#include "../api/api_client.h"
#include "../commands.h"
#include "../context/system_prompt.h"
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <cjson/cJSON.h>

int interrupt_callback(void *user_data) {
    InteractiveContext *ctx = (InteractiveContext *)user_data;
    if (!ctx || !ctx->state) {
        return 0;
    }

    ConversationState *state = ctx->state;
    TUIMessageQueue *queue = ctx->tui_queue;
    AIInstructionQueue *instr_queue = ctx->instruction_queue;

    // Check if there's work in progress
    int queue_depth = instr_queue ? ai_queue_depth(instr_queue) : 0;
    int work_in_progress = (queue_depth > 0);

    // Debug log the queue depth
    LOG_DEBUG("interrupt_callback: queue_depth=%d, work_in_progress=%d", queue_depth, work_in_progress);

    // Ctrl+C always sets the interrupt flag to cancel any ongoing operations
    // It never exits the application - use Ctrl+D or :q/:quit command to exit
    // Always set the interrupt flag regardless of queue state
    state->interrupt_requested = 1;

    if (work_in_progress) {
        // There's work in the queue - inform user we're canceling
        LOG_INFO("User requested interrupt (Ctrl+C pressed) - canceling ongoing operations");
        ui_set_status(NULL, queue, "Interrupt requested - canceling operations...");
    } else {
        // No work in queue, but interrupt flag is set for any ongoing operations
        LOG_INFO("User pressed Ctrl+C - interrupt flag set for any ongoing operations");
        ui_set_status(NULL, queue, "Interrupted");
    }

    return 0;  // Always continue running (never exit on Ctrl+C)
}

int submit_input_callback(const char *input, void *user_data) {
    InteractiveContext *ctx = (InteractiveContext *)user_data;
    if (!ctx || !ctx->state || !ctx->tui || !input) {
        return 0;
    }

    if (input[0] == '\0') {
        return 0;
    }

    TUIState *tui = ctx->tui;
    ConversationState *state = ctx->state;
    AIWorkerContext *worker = ctx->worker;
    TUIMessageQueue *queue = ctx->tui_queue;

    // Reset interrupt flag when new input is submitted
    state->interrupt_requested = 0;

    // Socket support removed - use input directly
    char *input_copy = strdup(input);
    if (!input_copy) {
        LOG_ERROR("Failed to allocate memory for input copy");
        return 0;
    }

    // Check for vim-style commands (starting with ':')
    if (input_copy[0] == ':') {
        ui_append_line(tui, queue, "[Command]", input_copy, COLOR_PAIR_STATUS);
        int result = handle_vim_command(tui, queue, input_copy);
        free(input_copy);
        return result;  // 1 means exit, 0 means continue
    }

    if (input_copy[0] == '/') {
        ui_append_line(tui, queue, "[User]", input_copy, COLOR_PAIR_USER);

        // Remember message count before command execution
        int msg_count_before = state->count;

        // Use the command system from commands.c
        const Command *executed_cmd = NULL;

        // Extract command name to check if it needs terminal interaction
        const char *cmd_name = input_copy + 1; // Skip '/'
        const char *space = strchr(cmd_name, ' ');
        size_t cmd_len = space ? (size_t)(space - cmd_name) : strlen(cmd_name);

        // Look up command to check if it needs terminal interaction
        char cmd_name_buf[64];
        if (cmd_len >= sizeof(cmd_name_buf) - 1) {
            cmd_len = sizeof(cmd_name_buf) - 1;
        }
        memcpy(cmd_name_buf, cmd_name, cmd_len);
        cmd_name_buf[cmd_len] = '\0';

        const Command *cmd = commands_lookup(cmd_name_buf);
        int needs_terminal = (cmd && cmd->needs_terminal);

        int cmd_result;

        if (needs_terminal) {
            // For commands that need terminal interaction (like /voice),
            // suspend the TUI to restore normal terminal mode
            if (tui_suspend(tui) != 0) {
                ui_show_error(tui, queue, "Failed to suspend TUI for command");
                free(input_copy);
                return 0;
            }

            // Execute command with normal terminal
            cmd_result = commands_execute(state, input_copy, &executed_cmd);

            // Resume TUI
            if (tui_resume(tui) != 0) {
                ui_show_error(tui, queue, "Failed to resume TUI after command");
                // Continue anyway
            }
        } else {
            // For regular commands, redirect stdout/stderr to /dev/null
            // to prevent TUI corruption
            // Commands use print_error() and printf() which interfere with ncurses
            int saved_stdout = dup(STDOUT_FILENO);
            int saved_stderr = dup(STDERR_FILENO);
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull != -1) {
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }

            cmd_result = commands_execute(state, input_copy, &executed_cmd);

            // Restore stdout/stderr
            if (saved_stdout != -1) {
                dup2(saved_stdout, STDOUT_FILENO);
                close(saved_stdout);
            }
            if (saved_stderr != -1) {
                dup2(saved_stderr, STDERR_FILENO);
                close(saved_stderr);
            }
        }

        // Check if it's an exit command
        if (cmd_result == -2) {
            free(input_copy);
            return 1;  // Exit the program
        }

        // Check if command failed (unknown command or error)
        if (cmd_result == -1) {
            // Extract command name for error message
            const char *err_cmd_line = input_copy + 1;
            const char *err_space = strchr(err_cmd_line, ' ');
            size_t err_cmd_len = err_space ? (size_t)(err_space - err_cmd_line) : strlen(err_cmd_line);

            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg),
                     "Unknown command: /%.*s (type /help for available commands)",
                     (int)err_cmd_len, err_cmd_line);
            ui_show_error(tui, queue, error_msg);
            free(input_copy);
            return 0;
        }

        // For /clear, also clear the TUI
        if (strncmp(input_copy, "/clear", 6) == 0) {
            tui_clear_conversation(tui, VERSION, state->model, state->working_dir);
        }

        // For /add-dir, rebuild system prompt
        if (strncmp(input_copy, "/add-dir ", 9) == 0 && cmd_result == 0) {
            char *new_system_prompt = build_system_prompt(state);
            if (new_system_prompt) {
                if (state->count > 0 && state->messages[0].role == MSG_SYSTEM) {
                    free(state->messages[0].contents[0].text);
                    state->messages[0].contents[0].text = strdup(new_system_prompt);
                    if (!state->messages[0].contents[0].text) {
                        ui_show_error(tui, queue, "Memory allocation failed");
                    }
                }
                free(new_system_prompt);
            } else {
                ui_show_error(tui, queue, "Failed to rebuild system prompt");
            }
        }

        // Check if command added new messages (e.g., /voice adds transcription)
        if (cmd_result == 0 && state->count > msg_count_before) {
            // Display any new user messages that were added
            for (int i = msg_count_before; i < state->count; i++) {
                if (state->messages[i].role == MSG_USER) {
                    // Get the text from the first text content
                    for (int j = 0; j < state->messages[i].content_count; j++) {
                        // Compare against InternalContentType to avoid enum mismatch
                        if (state->messages[i].contents[j].type == INTERNAL_TEXT) {
                            ui_append_line(tui, queue, "[Transcription]",
                                         state->messages[i].contents[j].text,
                                         COLOR_PAIR_USER);
                            break;
                        }
                    }
                }
            }
        }

        free(input_copy);
        return 0;
    }

    ui_append_line(tui, queue, "[User]", input_copy, COLOR_PAIR_USER);
    add_user_message(state, input_copy);

    if (worker) {
        if (ai_worker_submit(worker, input_copy) != 0) {
            ui_show_error(tui, queue, "Failed to queue instruction for processing");
        }
        // Note: No status update needed - spinner is already showing, and
        // users see queued messages appear in the conversation view
    } else {
        ui_set_status_varied(tui, queue, SPINNER_CONTEXT_API_CALL);
        ApiResponse *response = call_api_with_retries(state);
        ui_set_status(tui, queue, "");

        if (!response) {
            ui_show_error(tui, queue, "Failed to get response from API");
            free(input_copy);
            return 0;
        }

        // Check if response contains an error message
        if (response->error_message) {
            ui_show_error(tui, queue, response->error_message);
            api_response_free(response);
            free(input_copy);
            return 0;
        }

        cJSON *error = cJSON_GetObjectItem(response->raw_response, "error");
        if (error && !cJSON_IsNull(error)) {
            cJSON *error_message = cJSON_GetObjectItem(error, "message");
            const char *error_msg = error_message ? error_message->valuestring : "Unknown error";
            ui_show_error(tui, queue, error_msg);
            api_response_free(response);
            free(input_copy);
            return 0;
        }

        process_response(state, response, tui, queue, NULL);
        api_response_free(response);
    }

    free(input_copy);
    return 0;
}
