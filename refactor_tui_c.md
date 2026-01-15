After reading this ~5200-line file, here's how I would break it tui.c down into smaller, more manageable components:

## Proposed Refactoring Structure

### 1. **tui_window.c** - Window Management
- All window layout/sizing logic
- `window_manager_*` interactions
- `resize_input_window()`
- Window validation functions
- Viewport refresh logic

### 2. **tui_input.c** - Input Buffer Management
- `TUIInputBuffer` struct and functions
- `input_init()`, `input_free()`
- `input_insert_char()`, `input_insert_string()`
- `input_delete_char()`, `input_backspace()`
- Word movement/deletion functions
- UTF-8 handling helpers

### 3. **tui_render.c** - Rendering & Display
- `input_redraw()` - the complex input rendering
- `render_entry_to_pad()`
- `render_text_with_search_highlight()`
- `render_status_window()`
- Color initialization (`init_ncurses_colors()`)
- Conversation rendering (`redraw_conversation()`)

### 4. **tui_paste.c** - Paste Detection & Handling
- Bracketed paste mode handling
- Heuristic paste detection
- `input_finalize_paste()`
- `check_paste_timeout()`
- Paste content tracking

### 5. **tui_modes.c** - Mode Handling
- `handle_normal_mode_input()`
- `handle_command_mode_input()`
- `handle_search_mode_input()`
- Mode switching logic
- Command/search buffer management

### 6. **tui_search.c** - Search Functionality
- `perform_search()`
- Search pattern management
- Paragraph navigation (`find_next_paragraph()`, `find_prev_paragraph()`)
- Search highlighting

### 7. **tui_conversation.c** - Conversation Management
- `add_conversation_entry()`
- `free_conversation_entries()`
- `tui_add_conversation_line()`
- `tui_update_last_conversation_line()`
- Message type detection
- Spacing logic

### 8. **tui_history.c** - Input History
- History navigation (Ctrl+P/N)
- History file integration
- History search integration

### 9. **tui_event_loop.c** - Event Processing
- `tui_event_loop()` (main loop)
- `tui_process_input_char()` (input dispatcher)
- `tui_poll_input()`
- External input handling
- Message queue processing

### 10. **tui_core.c** - Core Initialization
- `tui_init()`, `tui_cleanup()`
- `tui_suspend()`, `tui_resume()`
- Status updates
- Spinner management
- Banner/startup display

### 11. **tui_completion.c** - Tab Completion
- `handle_tab_completion()`
- `find_command_matches()`
- Command completion arrays

### Keep in tui.h (shared header):
- Struct definitions
- Color pair enums
- Function prototypes
- Constants

This breaks the monolith into ~11 focused files, each under 500 lines, with clear responsibilities and minimal cross-dependencies.

---

