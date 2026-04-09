#include "settings_menu.h"

#include "config.h"
#include "config_command.h"
#include "logger.h"
#include "tui.h"
#include <bsd/string.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    SETTINGS_ITEM_PROVIDER = 0,
    SETTINGS_ITEM_AUTO_COMPACT,
    SETTINGS_ITEM_THRESHOLD,
    SETTINGS_ITEM_STREAMING,
    SETTINGS_ITEM_THINKING_STYLE,
    SETTINGS_ITEM_DISABLED_TOOLS,
    SETTINGS_ITEM_COUNT
} SettingsItem;

static void get_popup_dimensions(int max_y, int max_x, int *height, int *width, int *start_y, int *start_x) {
    *width = (max_x * 70) / 100;
    if (*width < 64) *width = 64;
    if (*width > max_x - 4) *width = max_x - 4;

    *height = 18;
    if (*height > max_y - 4) *height = max_y - 4;

    *start_y = (max_y - *height) / 2;
    *start_x = (max_x - *width) / 2;
}

static void value_for_item(const KlawedConfig *config, SettingsItem item, char *out, size_t out_size) {
    if (!config || !out || out_size == 0) {
        return;
    }

    switch (item) {
        case SETTINGS_ITEM_PROVIDER:
            strlcpy(out, config->active_provider[0] != '\0' ? config->active_provider : "(none)", out_size);
            break;
        case SETTINGS_ITEM_AUTO_COMPACT:
            strlcpy(out, config->auto_compact_enabled ? "on" : "off", out_size);
            break;
        case SETTINGS_ITEM_THRESHOLD:
            snprintf(out, out_size, "%d%%", config->compaction_threshold_percent);
            break;
        case SETTINGS_ITEM_STREAMING:
            strlcpy(out, config->streaming_enabled ? "on" : "off", out_size);
            break;
        case SETTINGS_ITEM_THINKING_STYLE:
            strlcpy(out, config_thinking_style_to_string(config->thinking_style), out_size);
            break;
        case SETTINGS_ITEM_DISABLED_TOOLS:
            strlcpy(out, config->disabled_tools[0] != '\0' ? config->disabled_tools : "(none)", out_size);
            break;
        case SETTINGS_ITEM_COUNT:
            out[0] = '\0';
            break;
        default:
            out[0] = '\0';
            break;
    }
}

static int cycle_provider(KlawedConfig *config, int direction) {
    if (!config || config->provider_count <= 0) {
        return -1;
    }

    int current_index = 0;
    for (int i = 0; i < config->provider_count; i++) {
        if (strcmp(config->providers[i].key, config->active_provider) == 0) {
            current_index = i;
            break;
        }
    }

    current_index += direction;
    if (current_index < 0) {
        current_index = config->provider_count - 1;
    } else if (current_index >= config->provider_count) {
        current_index = 0;
    }

    strlcpy(config->active_provider, config->providers[current_index].key, sizeof(config->active_provider));
    return 0;
}

static int prompt_disabled_tools(WINDOW *win, int height, int width, KlawedConfig *config) {
    char buffer[CONFIG_DISABLED_TOOLS_MAX];
    strlcpy(buffer, config->disabled_tools, sizeof(buffer));

    mvwhline(win, height - 4, 1, ACS_HLINE, width - 2);
    mvwprintw(win, height - 3, 2, "Disabled tools (comma-separated, blank to clear): ");
    wclrtoeol(win);
    wrefresh(win);

    echo();
    curs_set(1);
    wgetnstr(win, buffer, (int)sizeof(buffer) - 1);
    noecho();
    curs_set(0);

    strlcpy(config->disabled_tools, buffer, sizeof(config->disabled_tools));
    return 0;
}

static int apply_item_change(ConversationState *state, KlawedConfig *config, SettingsItem item, int direction, WINDOW *win, int height, int width, char *status, size_t status_size) {
    char value[CONFIG_DISABLED_TOOLS_MAX];

    switch (item) {
        case SETTINGS_ITEM_PROVIDER:
            if (cycle_provider(config, direction) != 0) {
                strlcpy(status, "No configured providers", status_size);
                return -1;
            }
            return config_apply_setting(state, "llm_provider", config->active_provider, status, status_size);
        case SETTINGS_ITEM_AUTO_COMPACT:
            config->auto_compact_enabled = config->auto_compact_enabled ? 0 : 1;
            strlcpy(value, config->auto_compact_enabled ? "on" : "off", sizeof(value));
            return config_apply_setting(state, "auto_compact", value, status, status_size);
        case SETTINGS_ITEM_THRESHOLD:
            config->compaction_threshold_percent += (direction >= 0) ? 5 : -5;
            if (config->compaction_threshold_percent < 5) config->compaction_threshold_percent = 5;
            if (config->compaction_threshold_percent > 100) config->compaction_threshold_percent = 100;
            snprintf(value, sizeof(value), "%d", config->compaction_threshold_percent);
            return config_apply_setting(state, "compaction_threshold", value, status, status_size);
        case SETTINGS_ITEM_STREAMING:
            config->streaming_enabled = config->streaming_enabled ? 0 : 1;
            strlcpy(value, config->streaming_enabled ? "on" : "off", sizeof(value));
            return config_apply_setting(state, "streaming", value, status, status_size);
        case SETTINGS_ITEM_THINKING_STYLE:
            config->thinking_style = (config->thinking_style == THINKING_STYLE_WAVE) ? THINKING_STYLE_PACMAN : THINKING_STYLE_WAVE;
            strlcpy(value, config_thinking_style_to_string(config->thinking_style), sizeof(value));
            return config_apply_setting(state, "thinking_style", value, status, status_size);
        case SETTINGS_ITEM_DISABLED_TOOLS:
            prompt_disabled_tools(win, height, width, config);
            return config_apply_setting(state, "disabled_tools", config->disabled_tools, status, status_size);
        case SETTINGS_ITEM_COUNT:
            break;
        default:
            break;
    }

    strlcpy(status, "Unsupported setting", status_size);
    return -1;
}

static void render_settings_menu(WINDOW *win, const KlawedConfig *config, int selected, const char *status) {
    static const char *labels[SETTINGS_ITEM_COUNT] = {
        "Provider slot",
        "Auto-compaction",
        "Compaction threshold",
        "Streaming",
        "Thinking style",
        "Disabled tools"
    };

    int height = getmaxy(win);
    int width = getmaxx(win);
    werase(win);
    box(win, 0, 0);

    wattron(win, A_BOLD);
    mvwprintw(win, 0, (width - 10) / 2, " Settings ");
    wattroff(win, A_BOLD);

    mvwprintw(win, 1, 2, "Use up/down to select, left/right or Enter to change, q/Esc to close.");

    for (int i = 0; i < SETTINGS_ITEM_COUNT; i++) {
        char value[CONFIG_DISABLED_TOOLS_MAX];
        value_for_item(config, (SettingsItem)i, value, sizeof(value));
        if (i == selected) {
            wattron(win, A_REVERSE | A_BOLD);
        }
        mvwprintw(win, 3 + i * 2, 3, "%-22s %s", labels[i], value);
        if (i == selected) {
            wattroff(win, A_REVERSE | A_BOLD);
        }
    }

    mvwhline(win, height - 3, 1, ACS_HLINE, width - 2);
    mvwprintw(win, height - 2, 2, "%.*s", width - 4, status ? status : "");
    wrefresh(win);
}

int cmd_settings(ConversationState *state, const char *args) {
    (void)args;

    if (!state) {
        return -1;
    }

    if (!state->tui) {
        printf("Use /config for non-TUI configuration changes.\n");
        return 0;
    }

    int max_y = 0;
    int max_x = 0;
    getmaxyx(stdscr, max_y, max_x);

    int height = 0;
    int width = 0;
    int start_y = 0;
    int start_x = 0;
    get_popup_dimensions(max_y, max_x, &height, &width, &start_y, &start_x);

    WINDOW *win = newwin(height, width, start_y, start_x);
    if (!win) {
        LOG_ERROR("[Settings] Failed to create settings window");
        return -1;
    }

    keypad(win, TRUE);
    curs_set(0);

    KlawedConfig config;
    if (config_load(&config) != 0) {
        config_init_defaults(&config);
    }

    int selected = 0;
    char status[256];
    strlcpy(status, "Changes apply immediately and are saved to .klawed/config.json.", sizeof(status));

    render_settings_menu(win, &config, selected, status);

    for (;;) {
        int ch = wgetch(win);
        if (ch == 'q' || ch == 27) {
            break;
        }

        if (ch == KEY_UP || ch == 'k') {
            selected = (selected == 0) ? SETTINGS_ITEM_COUNT - 1 : selected - 1;
        } else if (ch == KEY_DOWN || ch == 'j') {
            selected = (selected + 1) % SETTINGS_ITEM_COUNT;
        } else if (ch == KEY_LEFT || ch == 'h') {
            apply_item_change(state, &config, (SettingsItem)selected, -1, win, height, width, status, sizeof(status));
            if (config_load(&config) != 0) {
                config_init_defaults(&config);
            }
        } else if (ch == KEY_RIGHT || ch == 'l' || ch == '\n' || ch == KEY_ENTER) {
            apply_item_change(state, &config, (SettingsItem)selected, 1, win, height, width, status, sizeof(status));
            if (config_load(&config) != 0) {
                config_init_defaults(&config);
            }
        } else if (ch == KEY_RESIZE) {
            getmaxyx(stdscr, max_y, max_x);
            get_popup_dimensions(max_y, max_x, &height, &width, &start_y, &start_x);
            wresize(win, height, width);
            mvwin(win, start_y, start_x);
        }

        render_settings_menu(win, &config, selected, status);
    }

    werase(win);
    wrefresh(win);
    delwin(win);
    touchwin(stdscr);
    refresh();
    return 0;
}
