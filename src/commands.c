/*
 * commands.c - Command Registration and Dispatch Implementation
 */

#include "commands.h"
#include "klawed_internal.h"
#include "logger.h"
#include "fallback_colors.h"
#include "voice_input.h"
#include "tui.h"
#include "theme_explorer.h"
#include "help_modal.h"
#include "config.h"
#include "provider_command.h"
#include "config_command.h"
#define COLORSCHEME_EXTERN
#include "colorscheme.h"
#include <bsd/string.h>
#include <bsd/stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <glob.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <cjson/cJSON.h>

// ============================================================================
// Command Registry
// ============================================================================

#define MAX_COMMANDS 32
static const Command *command_registry[MAX_COMMANDS];
static int command_count = 0;

// TUI mode flag - when true, suppress stdout/stderr output
static int tui_mode_enabled = 0;

// ============================================================================
// Helper Functions
// ============================================================================

static void print_status(const char *text) {
    // In TUI mode, don't print to stdout (it corrupts ncurses)
    // The caller (klawed.c) will handle UI feedback
    if (tui_mode_enabled) {
        LOG_DEBUG("Status (TUI): %s", text);
        return;
    }

    char color_buf[32];
    const char *status_color;
    if (get_colorscheme_color(COLORSCHEME_STATUS, color_buf, sizeof(color_buf)) == 0) {
        status_color = color_buf;
    } else {
        LOG_WARN("Using fallback ANSI color for STATUS (commands)");
        status_color = ANSI_FALLBACK_STATUS;
    }
    printf("%s[Status]%s %s\n", status_color, ANSI_RESET, text);
    fflush(stdout);
}

static void print_error(const char *text) {
    // In TUI mode, don't print to stderr (it corrupts ncurses)
    // The caller (klawed.c) will handle UI feedback
    if (tui_mode_enabled) {
        LOG_DEBUG("Error (TUI): %s", text);
        return;
    }

    char color_buf[32];
    const char *error_color;
    if (get_colorscheme_color(COLORSCHEME_ERROR, color_buf, sizeof(color_buf)) == 0) {
        error_color = color_buf;
    } else {
        LOG_WARN("Using fallback ANSI color for ERROR (commands)");
        error_color = ANSI_FALLBACK_ERROR;
    }
    fprintf(stderr, "%s[Error]%s %s\n", error_color, ANSI_RESET, text);
    fflush(stderr);
}

// ============================================================================
// Forward Declarations for Completion Functions
// ============================================================================

static CompletionResult* dir_path_completer(const char *line, int cursor_pos, void *ctx);

// ============================================================================
// Command Handlers
// ============================================================================

static int cmd_exit(ConversationState *state, const char *args) {
    (void)state; (void)args;
    return -2;  // Special code to exit
}

static int cmd_quit(ConversationState *state, const char *args) {
    return cmd_exit(state, args);
}

static int cmd_clear(ConversationState *state, const char *args) {
    (void)args;
    clear_conversation(state);
    print_status("Conversation cleared");
    if (!tui_mode_enabled) printf("\n");
    return 0;
}

static int cmd_add_dir(ConversationState *state, const char *args) {
    // Trim leading whitespace from args
    while (*args == ' ' || *args == '\t') args++;
    if (strlen(args) == 0) {
        print_error("Usage: /add-dir <directory-path>");
        printf("\n");
        return -1;
    }
    if (add_directory(state, args) == 0) {
        print_status("Added directory to context");
        printf("\n");
        return 0;
    } else {
        char err_msg[PATH_MAX + 64];
        snprintf(err_msg, sizeof(err_msg),
                 "Failed to add directory: %s (not found or already added)", args);
        print_error(err_msg);
        printf("\n");
        return -1;
    }
}

static int cmd_voice(ConversationState *state, const char *args) {
    (void)state; (void)args;

    // Check if voice input is available with detailed error reporting
    const char *api_key = getenv("OPENAI_API_KEY");
    if (!api_key || !*api_key) {
        if (!tui_mode_enabled) {
            print_error("Voice input unavailable: OPENAI_API_KEY environment variable not set");
            fprintf(stderr, "Set your API key with: export OPENAI_API_KEY=\"your-key-here\"\n");
            printf("\n");
        }
        return -1;
    }

    if (!voice_input_available()) {
        if (!tui_mode_enabled) {
            print_error("Voice input unavailable: PortAudio not installed or no microphone detected");
            fprintf(stderr, "Install PortAudio:\n");
            fprintf(stderr, "  macOS:         brew install portaudio\n");
            fprintf(stderr, "  Ubuntu/Debian: sudo apt-get install portaudio19-dev\n");
            fprintf(stderr, "  Fedora/RHEL:   sudo yum install portaudio-devel\n");
            fprintf(stderr, "\nEnsure your system has a working microphone.\n");
            printf("\n");
        }
        return -1;
    }

    char *transcription = NULL;
    int result = voice_input_record_and_transcribe(&transcription);

    if (result == 0 && transcription) {
        // Check if we're in TUI mode
        if (tui_mode_enabled && state && state->tui) {
            // In TUI mode: insert transcription into input buffer
            if (tui_insert_input_text(state->tui, transcription) == 0) {
                // Successfully inserted into input buffer
                // Don't print to stdout in TUI mode (corrupts ncurses)
                // The transcription is now in the input buffer for the user to see/edit
            } else {
                // Failed to insert into input buffer, fall back to adding as message
                add_user_message(state, transcription);
            }
        } else {
            // Not in TUI mode: print transcription and add as message
            printf("\n");
            print_status("Transcription:");
            printf("%s\n\n", transcription);

            // Add transcription to conversation as user message
            add_user_message(state, transcription);
        }

        free(transcription);
        return 0;
    } else if (result == -2) {
        if (!tui_mode_enabled) {
            print_error("No audio recorded");
            fprintf(stderr, "Make sure you speak into the microphone before pressing ENTER.\n");
            printf("\n");
        }
        return -1;
    } else if (result == -3) {
        if (!tui_mode_enabled) {
            print_error("Recording was silent (no audio detected)");
            fprintf(stderr, "Check that:\n");
            fprintf(stderr, "  - Microphone is not muted\n");
            fprintf(stderr, "  - Correct input device is selected in system settings\n");
            fprintf(stderr, "  - Microphone volume is adequate\n");
            fprintf(stderr, "  - Application has microphone permissions (macOS/Linux)\n");
            printf("\n");
        }
        return -1;
    } else {
        if (!tui_mode_enabled) {
            print_error("Voice transcription failed");
            fprintf(stderr, "This could be due to:\n");
            fprintf(stderr, "  - Network connectivity issues\n");
            fprintf(stderr, "  - OpenAI API service problems\n");
            fprintf(stderr, "  - Invalid API key\n");
            fprintf(stderr, "Check logs for more details.\n");
            printf("\n");
        }
        return -1;
    }
}

static int cmd_help(ConversationState *state, const char *args) {
    (void)args;

    // In TUI mode, show help modal
    if (tui_mode_enabled && state && state->tui) {
        // Suspend the TUI temporarily
        if (tui_suspend(state->tui) != 0) {
            LOG_ERROR("[CMD_HELP] Failed to suspend TUI");
            return -1;
        }

        // Initialize and run help modal
        HelpModalState help_state;
        if (help_modal_init(&help_state) != 0) {
            LOG_ERROR("[CMD_HELP] Failed to initialize help modal");
            tui_resume(state->tui);
            return -1;
        }

        // Get terminal dimensions
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);

        // Run the modal
        help_modal_run(&help_state, max_y, max_x);

        // Clean up and resume TUI
        help_modal_cleanup(&help_state);
        tui_resume(state->tui);

        return 0;
    }

    // Non-TUI mode: print help text to stdout
    char help_text[2048];
    size_t pos = 0;

    // Header
    const char *header = "Available commands:\n";
    strlcpy(help_text, header, sizeof(help_text));
    pos = strlen(help_text);

    // List all commands
    for (int i = 0; i < command_count; i++) {
        const Command *cmd = command_registry[i];
        char line[256];
        snprintf(line, sizeof(line), "  %-12s %s\n", cmd->usage, cmd->description);

        // Check if we have space
        if (pos + strlen(line) < sizeof(help_text) - 1) {
            strlcpy(help_text + pos, line, sizeof(help_text) - pos);
            pos += strlen(line);
        } else {
            // Truncate with ellipsis
            strlcpy(help_text + pos, "  ... (more commands)\n", sizeof(help_text) - pos);
            break;
        }
    }

    // Footer
    const char *footer = "\nType /help to see this list again.";
    if (pos + strlen(footer) < sizeof(help_text) - 1) {
        strlcpy(help_text + pos, footer, sizeof(help_text) - pos);
    }

    print_status(help_text);
    return 0;
}

static int cmd_themes(ConversationState *state, const char *args) {
    (void)args;

    // Theme explorer requires TUI mode
    if (!tui_mode_enabled || !state || !state->tui) {
        if (!tui_mode_enabled) {
            fprintf(stderr, "Theme explorer requires TUI mode. ");
            fprintf(stderr, "Run klawed without arguments to enter interactive mode.\n");
            fprintf(stderr, "\nAvailable themes:\n");
            int count = theme_explorer_get_theme_count();
            for (int i = 0; i < count; i++) {
                const char *name = theme_explorer_get_theme_name(i);
                if (name) {
                    fprintf(stderr, "  - %s\n", name);
                }
            }
            fprintf(stderr, "\nSet theme with: export KLAWED_THEME=\"<name>\"\n");
        }
        return -1;
    }

    // Suspend the TUI temporarily
    if (tui_suspend(state->tui) != 0) {
        LOG_ERROR("[CMD_THEMES] Failed to suspend TUI");
        return -1;
    }

    // Initialize theme explorer
    ThemeExplorerState explorer;
    if (theme_explorer_init(&explorer) != 0) {
        LOG_ERROR("[CMD_THEMES] Failed to initialize theme explorer");
        tui_resume(state->tui);
        return -1;
    }

    // Run the explorer
    ThemeExplorerResult result = theme_explorer_run(&explorer);

    if (result == THEME_EXPLORER_SELECTED) {
        const char *selected = theme_explorer_get_selected(&explorer);
        if (selected) {
            LOG_INFO("[CMD_THEMES] User selected theme: %s", selected);

            // Save theme to config (preserve other fields)
            KlawedConfig cfg;
            if (config_load(&cfg) != 0) {
                config_init_defaults(&cfg);
            }
            strlcpy(cfg.theme, selected, sizeof(cfg.theme));
            if (config_save(&cfg) == 0) {
                LOG_INFO("[CMD_THEMES] Theme saved to config file");
            }

            // Try to reload the theme immediately
            if (init_colorscheme(selected) == 0) {
                g_theme_loaded = 1;
                LOG_INFO("[CMD_THEMES] Theme applied successfully");
                // Redraw the TUI to reflect the new theme
                tui_resume(state->tui);
                tui_refresh(state->tui);
                tui_update_status(state->tui, "Theme applied successfully");
                theme_explorer_cleanup(&explorer);
                return 0;
            } else {
                LOG_WARN("[CMD_THEMES] Failed to reload theme immediately, will apply on next restart");
            }

            // If we couldn't reload immediately, show instructions
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Selected theme: %s\n"
                     "Theme saved to config - restart klawed to apply, or set KLAWED_THEME env var.",
                     selected);

            // Clean up explorer before resuming TUI
            theme_explorer_cleanup(&explorer);
            tui_resume(state->tui);

            // Show message in TUI
            tui_update_status(state->tui, msg);
            return 0;
        }
    }

    // Clean up
    theme_explorer_cleanup(&explorer);
    tui_resume(state->tui);

    if (result == THEME_EXPLORER_CANCELLED) {
        LOG_DEBUG("[CMD_THEMES] Theme selection cancelled");
    }

    return 0;
}

static int cmd_vim(ConversationState *state, const char *args) {
    (void)state; (void)args;

    // Check if vim is available
    if (system("which vim >/dev/null 2>&1") != 0) {
        if (!tui_mode_enabled) {
            print_error("vim not found in PATH");
            fprintf(stderr, "Install vim to use this command.\n");
        }
        return -1;
    }

    // In TUI mode, we need to suspend the TUI before running vim
    if (tui_mode_enabled && state && state->tui) {
        if (tui_suspend(state->tui) != 0) {
            LOG_ERROR("[CMD_VIM] Failed to suspend TUI");
            return -1;
        }
    }

    // Run vim in the current directory
    int result = system("vim");

    // Resume TUI if we suspended it
    if (tui_mode_enabled && state && state->tui) {
        if (tui_resume(state->tui) != 0) {
            LOG_ERROR("[CMD_VIM] Failed to resume TUI");
            // Continue anyway
        }
    }

    if (result != 0) {
        if (!tui_mode_enabled) {
            print_error("vim exited with non-zero status");
        }
        return -1;
    }

    return 0;
}

// Helper function to get current timestamp
static const char* get_current_timestamp(void) {
    static char timestamp[64];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    return timestamp;
}

static int cmd_dump(ConversationState *state, const char *args) {
    if (!state) {
        if (!tui_mode_enabled) {
            print_error("No conversation state available");
        }
        return -1;
    }

    // Trim leading whitespace from args
    while (*args == ' ' || *args == '\t') args++;

    char file_path[PATH_MAX];
    int use_default_name = 0;

    if (strlen(args) == 0) {
        // No file path provided, use default name based on session ID
        if (!state->session_id || strlen(state->session_id) == 0) {
            if (!tui_mode_enabled) {
                print_error("No session ID available for default filename");
                fprintf(stderr, "Please specify a file path: /dump <file-path>\n");
            }
            return -1;
        }

        // Create default filename: conversation-<session_id>.md
        snprintf(file_path, sizeof(file_path), "conversation-%s.md", state->session_id);
        use_default_name = 1;
    } else {
        // Use provided file path
        strlcpy(file_path, args, sizeof(file_path));
    }

    // Open the file for writing
    FILE *fp = fopen(file_path, "w");
    if (!fp) {
        char err_msg[PATH_MAX + 64];
        snprintf(err_msg, sizeof(err_msg),
                 "Failed to open file for writing: %s", file_path);
        print_error(err_msg);
        return -1;
    }

    // Write header
    fprintf(fp, "# Conversation Dump\n\n");
    fprintf(fp, "**Session ID:** %s\n", state->session_id ? state->session_id : "unknown");
    fprintf(fp, "**Timestamp:** %s\n\n", get_current_timestamp());

    // Dump all messages in the conversation
    int message_count = 0;
    for (int i = 0; i < state->count; i++) {
        InternalMessage *msg = &state->messages[i];
        if (!msg) continue;

        // Write message header based on role
        const char *role_str = "UNKNOWN";
        switch (msg->role) {
            case MSG_USER: role_str = "USER"; break;
            case MSG_ASSISTANT: role_str = "ASSISTANT"; break;
            case MSG_SYSTEM: role_str = "SYSTEM"; break;
            case MSG_AUTO_COMPACTION: role_str = "AUTO_COMPACTION"; break;
            default: break; // Keep as UNKNOWN
        }

        fprintf(fp, "## Message %d - %s\n\n", ++message_count, role_str);

        // Write all content blocks
        if (msg->contents && msg->content_count > 0) {
            for (int j = 0; j < msg->content_count; j++) {
                InternalContent *content = &msg->contents[j];
                if (!content) continue;

                switch (content->type) {
                    case INTERNAL_TEXT:
                        fprintf(fp, "%s\n\n", content->text ? content->text : "(empty text)");
                        break;

                    case INTERNAL_TOOL_CALL:
                        fprintf(fp, "**[TOOL CALL: %s", content->tool_name ? content->tool_name : "unknown");
                        if (content->tool_id) {
                            fprintf(fp, " (id: %s)", content->tool_id);
                        }
                        fprintf(fp, "]**\n\n");
                        break;

                    case INTERNAL_TOOL_RESPONSE:
                        fprintf(fp, "**[TOOL RESULT");
                        if (content->tool_id) {
                            fprintf(fp, " for %s", content->tool_id);
                        }
                        fprintf(fp, "]**\n\n");
                        // Tool results might have text content
                        if (content->text) {
                            fprintf(fp, "%s\n\n", content->text);
                        }
                        break;

                    case INTERNAL_IMAGE:
                        fprintf(fp, "**[IMAGE: %s]**\n\n", content->image_path ? content->image_path : "unknown image");
                        break;

                    default:
                        fprintf(fp, "**[UNKNOWN CONTENT TYPE: %d]**\n\n", content->type);
                        break;
                }
            }
        } else {
            fprintf(fp, "*No content in this message.*\n\n");
        }

        fprintf(fp, "---\n\n");
    }

    if (message_count == 0) {
        fprintf(fp, "*No messages in conversation.*\n\n");
    }

    fclose(fp);

    // Show success message
    char success_msg[PATH_MAX + 64];
    if (use_default_name) {
        snprintf(success_msg, sizeof(success_msg),
                 "Conversation dumped to default file: %s", file_path);
    } else {
        snprintf(success_msg, sizeof(success_msg),
                 "Conversation dumped to: %s", file_path);
    }
    print_status(success_msg);

    return 0;
}

// ============================================================================
// Command Definitions
// ============================================================================

static Command exit_cmd = {
    .name = "exit",
    .usage = "/exit",
    .description = "Exit interactive mode",
    .handler = cmd_exit,
    .completer = NULL,
    .needs_terminal = 0
};

static Command quit_cmd = {
    .name = "quit",
    .usage = "/quit",
    .description = "Exit interactive mode",
    .handler = cmd_quit,
    .completer = NULL,
    .needs_terminal = 0
};

static Command clear_cmd = {
    .name = "clear",
    .usage = "/clear",
    .description = "Clear conversation history",
    .handler = cmd_clear,
    .completer = NULL,
    .needs_terminal = 0
};

static Command add_dir_cmd = {
    .name = "add-dir",
    .usage = "/add-dir <path>",
    .description = "Add directory to working directories",
    .handler = cmd_add_dir,
    .completer = dir_path_completer,
    .needs_terminal = 0
};

static Command help_cmd = {
    .name = "help",
    .usage = "/help",
    .description = "Show this help",
    .handler = cmd_help,
    .completer = NULL,
    .needs_terminal = 0
};

static Command voice_cmd = {
    .name = "voice",
    .usage = "/voice",
    .description = "Record voice input and transcribe to text",
    .handler = cmd_voice,
    .completer = NULL,
    .needs_terminal = 1
};

static Command themes_cmd = {
    .name = "themes",
    .usage = "/themes",
    .description = "Browse and preview available color themes",
    .handler = cmd_themes,
    .completer = NULL,
    .needs_terminal = 1
};

static Command vim_cmd = {
    .name = "vim",
    .usage = "/vim",
    .description = "Open vim editor in current directory",
    .handler = cmd_vim,
    .completer = NULL,
    .needs_terminal = 1
};

static Command dump_cmd = {
    .name = "dump",
    .usage = "/dump [file-path]",
    .description = "Dump conversation to file (default: conversation-<session_id>.md)",
    .handler = cmd_dump,
    .completer = NULL,
    .needs_terminal = 0
};

static Command provider_cmd = {
    .name = "provider",
    .usage = "/provider [name|list]",
    .description = "View or switch LLM providers (use /provider list to see available)",
    .handler = cmd_provider,
    .completer = provider_completer,
    .needs_terminal = 0
};

static Command config_cmd = {
    .name = "config",
    .usage = "/config <setting> <value>",
    .description = "Modify configuration settings (e.g., /config llm_provider <name>)",
    .handler = cmd_config,
    .completer = NULL,
    .needs_terminal = 0
};

// ============================================================================
// API Implementation
// ============================================================================

void commands_init(void) {
    command_count = 0;
    commands_register(&exit_cmd);
    commands_register(&quit_cmd);
    commands_register(&clear_cmd);
    commands_register(&add_dir_cmd);
    commands_register(&help_cmd);
    commands_register(&voice_cmd);
    commands_register(&themes_cmd);
    commands_register(&vim_cmd);
    commands_register(&dump_cmd);
    commands_register(&provider_cmd);
    commands_register(&config_cmd);
}

void commands_set_tui_mode(int enabled) {
    tui_mode_enabled = enabled;
    LOG_DEBUG("Command system TUI mode: %s", enabled ? "enabled" : "disabled");
}

void commands_register(const Command *cmd) {
    if (command_count < MAX_COMMANDS) {
        command_registry[command_count++] = cmd;
    } else {
        LOG_WARN("Command registry full, cannot register '%s'", cmd->name);
    }
}

int commands_execute(ConversationState *state, const char *input, const Command **cmd_out) {
    if (!input || input[0] != '/') return -1;
    const char *cmd_line = input + 1;
    const char *space = strchr(cmd_line, ' ');
    size_t cmd_len = space ? (size_t)(space - cmd_line) : strlen(cmd_line);
    const char *args = space ? space + 1 : "";
    for (int i = 0; i < command_count; i++) {
        const Command *cmd = command_registry[i];
        if (strlen(cmd->name) == cmd_len && strncmp(cmd->name, cmd_line, cmd_len) == 0) {
            if (cmd_out) {
                *cmd_out = cmd;
            }
            return cmd->handler(state, args);
        }
    }
    // Don't print error here - let the caller (klawed.c) handle it
    // This prevents stderr output from corrupting the ncurses TUI
    LOG_DEBUG("Unknown command: %.*s", (int)cmd_len, cmd_line);
    return -1;
}

const Command** commands_list(int *count) {
    *count = command_count;
    return command_registry;
}

const Command* commands_lookup(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < command_count; i++) {
        const Command *cmd = command_registry[i];
        if (strcmp(cmd->name, name) == 0) {
            return cmd;
        }
    }
    return NULL;
}

// ============================================================================
// Tab Completion Implementations
// ============================================================================

CompletionResult* commands_tab_completer(const char *line, int cursor_pos, void *ctx) {
    (void)ctx;  // Suppress unused parameter warning
    if (!line || line[0] != '/') return NULL;
    const char *space = strchr(line, ' ');
    int cmd_name_len = space ? (int)(space - line - 1) : ((int)strlen(line) - 1);
    int name_end_pos = cmd_name_len + 1;
    if (cursor_pos <= name_end_pos) {
        // Complete command names
        int match_count = 0;
        for (int i = 0; i < command_count; i++) {
            if (strncmp(command_registry[i]->name, line + 1, (size_t)cmd_name_len) == 0) match_count++;
        }
        if (match_count == 0) return NULL;
        CompletionResult *res = malloc(sizeof(CompletionResult));
        if (!res) return NULL;
        // Use reallocarray for overflow-safe allocation
        res->options = reallocarray(NULL, (size_t)match_count, sizeof(char*));
        res->count = 0; res->selected = 0;
        for (int i = 0; i < command_count; i++) {
            const char *name = command_registry[i]->name;
            if (strncmp(name, line + 1, (size_t)cmd_name_len) == 0) {
                char *opt = malloc(strlen(name) + 2);
                snprintf(opt, strlen(name) + 2, "/%s", name);
                res->options[res->count++] = opt;
            }
        }
        return res;
    } else {
        // Delegate argument completion
        // Identify command name
        char cmd_name[64];
        int clen = cmd_name_len;
        if (clen >= (int)sizeof(cmd_name)) clen = sizeof(cmd_name) - 1;
        memcpy(cmd_name, line + 1, (size_t)clen);
        cmd_name[clen] = '\0';
        for (int i = 0; i < command_count; i++) {
            const Command *cmd = command_registry[i];
            if (strcmp(cmd->name, cmd_name) == 0 && cmd->completer) {
                return cmd->completer(line, cursor_pos, ctx);
            }
        }
        return NULL;
    }
}

static CompletionResult* dir_path_completer(const char *line, int cursor_pos, void *ctx) {
    (void)ctx;
    const char *arg = strchr(line, ' ');
    if (!arg) return NULL;
    arg++;
    int arg_start = (int)(arg - line);
    int arg_len = cursor_pos - arg_start;
    if (arg_len < 0) arg_len = 0;
    char prefix[PATH_MAX];
    int plen = arg_len < PATH_MAX ? arg_len : PATH_MAX - 1;
    memcpy(prefix, arg, (size_t)plen);
    prefix[plen] = '\0';
    char pattern[PATH_MAX];
    if (plen == 0) {
        strlcpy(pattern, "*", sizeof(pattern));
    } else {
        // Build pattern: prefix + '*' with safety against overflow
        size_t max_copy = sizeof(pattern) - 2; // leave space for '*' and '\0'
        size_t to_copy = (size_t)plen < max_copy ? (size_t)plen : max_copy;
        memcpy(pattern, prefix, to_copy);
        pattern[to_copy] = '*';
        pattern[to_copy + 1] = '\0';
    }
    glob_t globbuf;
    int ret = glob(pattern, GLOB_MARK | GLOB_NOSORT, NULL, &globbuf);
    if (ret != 0) { globfree(&globbuf); return NULL; }
    size_t dir_count = 0;
    for (size_t i = 0; i < globbuf.gl_pathc; i++) {
        const char *m = globbuf.gl_pathv[i];
        size_t len = strlen(m);
        if (len > 0 && m[len-1] == '/') dir_count++;
    }
    if (dir_count == 0) { globfree(&globbuf); return NULL; }
    CompletionResult *res = malloc(sizeof(CompletionResult));
    if (!res) { globfree(&globbuf); return NULL; }
    // Use reallocarray for overflow-safe allocation
    res->options = reallocarray(NULL, dir_count, sizeof(char*));
    res->count = 0; res->selected = 0;
    for (size_t i = 0; i < globbuf.gl_pathc; i++) {
        const char *m = globbuf.gl_pathv[i];
        size_t len = strlen(m);
        if (len > 0 && m[len-1] == '/') {
            res->options[res->count++] = strdup(m);
        }
    }
    globfree(&globbuf);
    return res;
}
