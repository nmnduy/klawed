# TUI Refactoring Progress

## Completed (Step 1)

### ✅ Makefile Updates
- Added `TUI_INPUT_SRC` / `TUI_INPUT_OBJ` definitions
- Added `TUI_CONVERSATION_SRC` / `TUI_CONVERSATION_OBJ` definitions  
- Added `TUI_PASTE_SRC` / `TUI_PASTE_OBJ` definitions (already existed, kept in place)
- Updated `$(TARGET)` dependencies to include new objects
- Updated `$(TARGET)` link command to include new objects
- Added build rules for:
  - `$(TUI_INPUT_OBJ)` - depends on tui_input.h, tui.h, logger.h, array_resize.h
  - `$(TUI_CONVERSATION_OBJ)` - depends on tui_conversation.h, tui.h, logger.h, window_manager.h, array_resize.h
  - `$(TUI_PASTE_OBJ)` - kept existing rule
- Updated `TEST_COMMON_OBJS` to include TUI_INPUT_OBJ and TUI_CONVERSATION_OBJ

### ✅ Source Code Updates
- Updated `src/tui.c`:
  - Changed `render_status_window()` from `static` to public (to match tui.h declaration)
  - Changed `render_entry_to_pad()` from `static` to public (to match tui.h declaration)
  - Updated all calls to conversation functions:
    - `free_conversation_entries()` → `tui_conversation_free_entries()`
    - `get_message_type()` → `tui_conversation_get_message_type()`
    - `add_conversation_entry()` → `tui_conversation_add_entry()`
  - Deleted duplicate implementations of `tui_add_conversation_line()` and `tui_update_last_conversation_line()` (now in tui_conversation.c)
  
- Updated `src/tui_conversation.c`:
  - Removed redundant forward declaration of `render_entry_to_pad()` (now in tui.h)
  - Removed redundant extern declaration of `render_status_window()` (now in tui.h)

### ✅ Build Verification
- Successfully compiled all modules
- No linking errors
- All existing modules (tui_input.c, tui_conversation.c, tui_paste.c) build correctly

## Remaining Tasks (Steps 2-9)

### 2. Create tui_window.c
- Extract window management functions from tui.c:
  - `validate_tui_windows()` → `tui_window_validate()`
  - `handle_resize()` (keep as internal signal handler)
  - `tui_resize_pending()` → `tui_window_resize_pending()`
  - `tui_clear_resize_flag()` → `tui_window_clear_resize_flag()`
  - `calculate_needed_lines()` → `tui_window_calculate_needed_lines()`
  - `resize_input_window()` → `tui_window_resize_input()`
  - `refresh_conversation_viewport()` → `tui_window_refresh_conversation_viewport()`
- Add new function: `tui_window_install_resize_handler()` to wrap SIGWINCH handler installation

### 3. Create tui_search.c
- Extract search functions from tui.c:
  - `perform_search()` → `tui_search_perform()`
  - `is_line_empty()` → `tui_search_is_line_empty()`
  - `find_next_paragraph()` → `tui_search_find_next_paragraph()`
  - `find_prev_paragraph()` → `tui_search_find_prev_paragraph()`
- Extract search logic from `handle_search_mode_input()` and `handle_normal_mode_input()`
- Add new functions: `tui_search_next()`, `tui_search_prev()`

### 4. Create tui_history.c
- Extract history navigation from `tui_process_input_char()` (Ctrl+P/N handling)
- Create new functions:
  - `tui_history_navigate_prev()` - Navigate to previous history entry
  - `tui_history_navigate_next()` - Navigate to next history entry
  - `tui_history_save_current_input()` - Save current input to history
  - `tui_history_reset_navigation()` - Reset history navigation state
  - `tui_history_load_from_file()` - Load history from file
  - `tui_history_free_entries()` - Free history entries

### 5. Create tui_completion.c
- Extract tab completion functions from tui.c:
  - `find_command_matches()` → `tui_completion_find_command_matches()`
  - `handle_tab_completion()` → `tui_completion_handle_tab()`

### 6. Create tui_modes.c
- Extract mode handling functions from tui.c:
  - `handle_normal_mode_input()` → `tui_modes_handle_normal()`
  - `handle_command_mode_input()` → `tui_modes_handle_command()`
  - `handle_search_mode_input()` → `tui_modes_handle_search()`
- Create new functions for INSERT mode and mode switching:
  - `tui_modes_handle_insert()` - Extract INSERT mode handling from `tui_process_input_char()`
  - `tui_modes_enter_insert()` - Mode switching
  - `tui_modes_enter_normal()` - Mode switching
  - `tui_modes_enter_command()` - Mode switching
  - `tui_modes_enter_search()` - Mode switching
  - `tui_modes_execute_command()` - Execute command from COMMAND mode

### 7. Create tui_render.c
- Extract rendering functions from tui.c:
  - `rgb_to_ncurses()` → `tui_render_rgb_to_ncurses()`
  - `init_ncurses_colors()` → `tui_render_init_colors()`
  - `render_status_window()` → keep as is (already public)
  - Status spinner functions:
    - `status_spinner_variant()` → `tui_render_status_spinner_variant()`
    - `status_spinner_interval_ns()` → `tui_render_status_spinner_interval_ns()`
    - `monotonic_time_ns()` → `tui_render_monotonic_time_ns()`
    - `status_message_wants_spinner()` → `tui_render_status_message_wants_spinner()`
    - `status_spinner_start()` → `tui_render_status_spinner_start()`
    - `status_spinner_stop()` → `tui_render_status_spinner_stop()`
    - `status_spinner_tick()` → `tui_render_status_spinner_tick()`
  - Text rendering:
    - `render_text_with_search_highlight()` → `tui_render_text_with_search_highlight()`
    - `render_entry_to_pad()` → keep as is (already public)
    - `redraw_conversation()` → `tui_render_redraw_conversation()`
    - `input_redraw()` → `tui_render_input()`
  - TODO/subagent rendering (keep existing names or rename):
    - `tui_render_todo_list()` → keep as is or rename to `tui_render_todo_list_impl()`
    - `tui_render_active_subagents()` → keep as is or rename to `tui_render_active_subagents_impl()`

### 8. Create tui_event_loop.c
- Extract event loop from tui.c:
  - `tui_event_loop()` - Main event loop
  - `tui_process_input_char()` - Input dispatcher
  - `tui_poll_input()` - Keyboard input polling
  - External input handling functions
  - Message queue processing

### 9. Create tui_core.c
- Extract core initialization from tui.c:
  - `tui_init()` - TUI initialization
  - `tui_cleanup()` - TUI cleanup
  - `tui_suspend()` - Terminal suspend
  - `tui_resume()` - Terminal resume
  - Status update functions
  - Spinner management
  - Banner/startup display
  - Vim-fugitive availability checking
  - Refresh operations and scrolling

## Build System Changes Per Module

For each new module:
1. Add `TUI_<MODULE>_SRC = src/tui_<module>.c` to Makefile
2. Add `TUI_<MODULE>_OBJ = $(BUILD_DIR)/tui_<module>.o` to Makefile
3. Add build rule:
   ```makefile
   $(TUI_<MODULE>_OBJ): $(TUI_<MODULE>_SRC) src/tui_<module>.h [dependencies...]
       @mkdir -p $(BUILD_DIR)
       $(CC) $(CFLAGS) -c -o $(TUI_<MODULE>_OBJ) $(TUI_<MODULE>_SRC)
   ```
4. Add object to `$(TARGET)` dependencies and link command
5. Add object to `TEST_COMMON_OBJS`

## Notes

- All header files (tui_*.h) already exist with proper declarations
- Function signatures must remain identical to headers
- Follow C coding standards from KLAWED.md
- Test compilation after adding each module
- Remove extracted functions from tui.c to avoid duplicate symbols
- Update function calls in tui.c to use new exported names
- Check for forward declarations and extern declarations that should be removed (now in headers)

## Testing Strategy

After each module is created:
1. Run `make clean && make` to verify compilation
2. Check for linking errors
3. Verify no duplicate symbols
4. Run basic tests if applicable
5. Commit changes with descriptive message

## Final Steps

Once all modules are created:
1. Review tui.c to ensure all functions have been extracted
2. Review tui.h to clean up any unnecessary declarations
3. Run full test suite: `make test`
4. Run with sanitizers: `make sanitize-all`
5. Test TUI functionality manually
6. Document any breaking changes or API updates
7. Update refactor documentation

## Date

- Started: 2026-01-15
- Makefile updated: 2026-01-15
- Status: Makefile complete, ready for module creation
