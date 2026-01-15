# TUI Refactoring: Function Mapping Guide

This document maps functions from `src/tui.c` to their destination modules based on the refactoring plan.

## Module: tui_input.c

### Static Functions (internal)
- `utf8_char_length()` → `tui_input_utf8_char_length()`
- `is_word_boundary()` → `tui_input_is_word_boundary()`
- `move_backward_word()` → `tui_input_move_backward_word()`
- `move_forward_word()` → `tui_input_move_forward_word()`
- `input_init()` → `tui_input_init()`
- `input_free()` → `tui_input_free()`
- `input_insert_char()` → `tui_input_insert_char()`
- `input_insert_string()` → `tui_input_insert_string()`
- `input_delete_char()` → `tui_input_delete_char()`
- `input_backspace()` → `tui_input_backspace()`
- `input_delete_word_backward()` → `tui_input_delete_word_backward()`
- `input_delete_word_forward()` → `tui_input_delete_word_forward()`

### Dependencies
- TUIInputBuffer structure definition
- Input buffer management constants

---

## Module: tui_conversation.c

### Static Functions (internal)
- `get_message_type()` → `tui_conversation_get_message_type()`
- `add_conversation_entry()` → `tui_conversation_add_entry()`
- `free_conversation_entries()` → `tui_conversation_free_entries()`
- `infer_color_from_prefix()` → `tui_conversation_infer_color_from_prefix()`

### Public Functions
- `tui_add_conversation_line()` - Keep as is (wraps `tui_conversation_add_entry()`)
- `tui_update_last_conversation_line()` - Keep as is

### Dependencies
- MessageType enum definition
- ConversationEntry structure (in tui.h)

---

## Module: tui_history.c

### Public Functions
Functions to be extracted and implemented:
- `tui_history_navigate_prev()` - Navigate to previous history entry
- `tui_history_navigate_next()` - Navigate to next history entry
- `tui_history_save_current_input()` - Save current input to history
- `tui_history_reset_navigation()` - Reset history navigation state
- `tui_history_load_from_file()` - Load history from file
- `tui_history_free_entries()` - Free history entries

### Current Implementation
These functions are currently embedded in:
- `tui_process_input_char()` - Ctrl+P/N handling
- `tui_init()` - History loading
- `tui_cleanup()` - History cleanup

### Dependencies
- HistoryFile structure (history_file.h)
- Input history arrays in TUIState

---

## Module: tui_paste.c

### Static Functions (internal)
- `input_finalize_paste()` → `tui_paste_finalize()`
- `check_paste_timeout()` → `tui_paste_check_timeout()`

### New Functions
- `tui_paste_start_mode()` - Start paste mode
- `tui_paste_end_mode()` - End paste mode
- `tui_paste_is_active()` - Check if in paste mode
- `tui_paste_init_config()` - Load paste config from environment

### Current Implementation
Paste logic is currently in:
- `tui_process_input_char()` - Bracketed paste sequence handling
- `input_insert_char()` - Paste mode buffering
- `tui_event_loop()` - Paste timeout checking

### Dependencies
- Paste detection constants (g_paste_gap_ms, etc.)
- TUIInputBuffer paste fields

---

## Module: tui_window.c

### Static Functions (internal)
- `validate_tui_windows()` → `tui_window_validate()`
- `handle_resize()` → Keep as signal handler (internal)
- `tui_resize_pending()` → `tui_window_resize_pending()`
- `tui_clear_resize_flag()` → `tui_window_clear_resize_flag()`
- `calculate_needed_lines()` → `tui_window_calculate_needed_lines()`
- `resize_input_window()` → `tui_window_resize_input()`
- `refresh_conversation_viewport()` → `tui_window_refresh_conversation_viewport()`

### Public Functions
- `tui_handle_resize()` - Keep as is (wraps resize logic)

### New Functions
- `tui_window_install_resize_handler()` - Install SIGWINCH handler

### Dependencies
- WindowManager structure
- Global resize flag (g_resize_flag)

---

## Module: tui_render.c

### Static Functions (internal)
- `rgb_to_ncurses()` → `tui_render_rgb_to_ncurses()`
- `init_ncurses_colors()` → `tui_render_init_colors()`
- `render_status_window()` → `tui_render_status()`
- `status_spinner_variant()` → `tui_render_status_spinner_variant()`
- `status_spinner_interval_ns()` → `tui_render_status_spinner_interval_ns()`
- `monotonic_time_ns()` → `tui_render_monotonic_time_ns()`
- `status_message_wants_spinner()` → `tui_render_status_message_wants_spinner()`
- `status_spinner_start()` → `tui_render_status_spinner_start()`
- `status_spinner_stop()` → `tui_render_status_spinner_stop()`
- `status_spinner_tick()` → `tui_render_status_spinner_tick()`
- `render_text_with_search_highlight()` → `tui_render_text_with_search_highlight()`
- `render_entry_to_pad()` → `tui_render_entry_to_pad()`
- `redraw_conversation()` → `tui_render_redraw_conversation()`
- `input_redraw()` → `tui_render_input()`

### Public Functions
- `tui_render_todo_list()` - Rename to `tui_render_todo_list_impl()`
- `tui_render_active_subagents()` - Rename to `tui_render_active_subagents_impl()`
- `tui_redraw_input()` - Keep as wrapper

### Dependencies
- Color scheme integration
- Indicator/spinner system
- Window manager

---

## Module: tui_search.c

### Static Functions (internal)
- `perform_search()` → `tui_search_perform()`
- `is_line_empty()` → `tui_search_is_line_empty()`
- `find_next_paragraph()` → `tui_search_find_next_paragraph()`
- `find_prev_paragraph()` → `tui_search_find_prev_paragraph()`

### New Functions
- `tui_search_next()` - Repeat last search forward
- `tui_search_prev()` - Repeat last search backward

### Current Implementation
Search logic is currently in:
- `handle_search_mode_input()` - Search input handling
- `handle_normal_mode_input()` - n/N key handling
- Paragraph navigation in normal mode

### Dependencies
- Search state fields in TUIState
- Last search pattern tracking

---

## Module: tui_completion.c

### Static Functions (internal)
- `find_command_matches()` → `tui_completion_find_command_matches()`
- `handle_tab_completion()` → `tui_completion_handle_tab()`

### Dependencies
- Command arrays (may need to be moved from commands.c)
- Tab completion state

---

## Module: tui_modes.c

### Static Functions (internal)
- `handle_normal_mode_input()` → `tui_modes_handle_normal()`
- `handle_command_mode_input()` → `tui_modes_handle_command()`
- `handle_search_mode_input()` → `tui_modes_handle_search()`

### New Functions
- `tui_modes_handle_insert()` - Extract INSERT mode handling from `tui_process_input_char()`
- `tui_modes_enter_insert()` - Mode switching
- `tui_modes_enter_normal()` - Mode switching
- `tui_modes_enter_command()` - Mode switching
- `tui_modes_enter_search()` - Mode switching
- `tui_modes_execute_command()` - Execute command from COMMAND mode

### Current Implementation
Mode handling is currently in:
- `tui_process_input_char()` - Main input dispatcher
- Various `handle_*_mode_input()` functions

### Dependencies
- Mode state in TUIState
- Command/search buffers
- All other TUI subsystems (modes are top-level coordinators)

---

## Module: tui_event_loop.c

### Static Functions (internal)
- `dispatch_tui_message()` → `tui_event_loop_dispatch_message()`
- `process_tui_messages()` → `tui_event_loop_process_messages()`

### Public Functions
- `tui_event_loop()` → `tui_event_loop_run()`
- `tui_process_input_char()` → `tui_event_loop_process_char()`
- `tui_poll_input()` → `tui_event_loop_poll_input()`
- `tui_drain_message_queue()` → `tui_event_loop_drain_messages()`

### Dependencies
- Message queue system
- All mode handlers
- All subsystems (event loop is the top-level coordinator)

---

## Module: tui_core.c

### Static Functions (internal)
- `check_vim_fugitive_thread()` - Keep as internal thread function

### Public Functions
- `tui_init()` → `tui_core_init()`
- `tui_cleanup()` → `tui_core_cleanup()`
- `tui_suspend()` → `tui_core_suspend()`
- `tui_resume()` → `tui_core_resume()`
- `tui_update_status()` → `tui_core_update_status()`
- `tui_refresh()` → `tui_core_refresh()`
- `tui_show_startup_banner()` → `tui_core_show_startup_banner()`
- `tui_clear_conversation()` → `tui_core_clear_conversation()`
- `tui_scroll_conversation()` → `tui_core_scroll_conversation()`
- `tui_get_vim_fugitive_available()` → `tui_core_get_vim_fugitive_available()`
- `tui_start_vim_fugitive_check()` → `tui_core_start_vim_fugitive_check()`

### New Functions
- `tui_core_set_persistence_db()` - Set database connection for token tracking

### Dependencies
- All subsystems (core does initialization/cleanup)
- Window manager
- Persistence database

---

## Refactoring Order

### Phase 1: Low-Level Modules (No Cross-Dependencies)
1. `tui_input.c` - Pure buffer operations
2. `tui_conversation.c` - Entry management
3. `tui_history.c` - History operations

### Phase 2: Mid-Level Modules (Use Phase 1)
4. `tui_paste.c` - Uses input buffer
5. `tui_window.c` - Window management
6. `tui_search.c` - Search operations
7. `tui_completion.c` - Tab completion

### Phase 3: High-Level Modules (Use Most Others)
8. `tui_render.c` - Uses conversation, input, search
9. `tui_modes.c` - Uses most subsystems
10. `tui_event_loop.c` - Uses all subsystems
11. `tui_core.c` - Initializes all subsystems

---

## Function Count by Module

- **tui_input.c**: 12 functions
- **tui_conversation.c**: 6 functions
- **tui_history.c**: 6 functions  
- **tui_paste.c**: 6 functions
- **tui_window.c**: 9 functions
- **tui_render.c**: 18 functions
- **tui_search.c**: 6 functions
- **tui_completion.c**: 2 functions
- **tui_modes.c**: 10 functions
- **tui_event_loop.c**: 7 functions
- **tui_core.c**: 12 functions

**Total**: ~94 functions to refactor

---

## Global Variables to Migrate

### From tui.c to appropriate modules:

- `g_resize_flag` → `tui_window.c`
- `g_enable_paste_heuristic` → `tui_paste.c`
- `g_paste_gap_ms` → `tui_paste.c`
- `g_paste_burst_min` → `tui_paste.c`
- `g_paste_timeout_ms` → `tui_paste.c`

---

## Testing Strategy

After each module is created:
1. Compile module separately
2. Update tui.c to call new functions
3. Run existing unit tests
4. Check for memory leaks with Valgrind
5. Verify no behavioral changes

---

## Notes

- Keep tui.h as the main public API header
- New headers are for internal modularization
- Public functions keep their `tui_*` names
- Internal functions renamed to `tui_module_*` pattern
- Static functions become module-scoped where appropriate
