# Window Management Refactor

## Current Issues

1. **Window state scattered across TUIState**: Window dimensions, positions, and states are mixed with other TUI concerns
2. **Complex resize logic**: The `tui_handle_resize()` function is doing too much (destroying, recreating, copying content)
3. **Pad capacity management is ad-hoc**: Expanding pads is done inline without clear abstraction
4. **Inconsistent validation**: Some functions validate windows, others don't
5. **Mixed responsibilities**: Window creation, content rendering, and lifecycle management are intertwined

## Refactoring Goals

1. **Encapsulation**: Create a WindowManager that handles all window lifecycle
2. **Separation of concerns**: Separate window management from content rendering
3. **Defensive programming**: Add validation at all entry points
4. **Clear API**: Provide simple, consistent functions for window operations
5. **Better error handling**: Graceful degradation when operations fail
6. **Maintainability**: Make it easy to add new windows or change layout

## Design

### WindowManager Structure

```c
typedef struct {
    // Screen dimensions
    int screen_width;
    int screen_height;
    
    // Conversation pad (virtual scrollable window)
    WINDOW *conv_pad;
    int conv_pad_capacity;      // Total pad height
    int conv_pad_content_lines; // Actual content lines
    int conv_viewport_height;   // Visible area height
    int conv_scroll_offset;     // Current scroll position
    
    // Status window
    WINDOW *status_win;
    int status_height;
    
    // Input window
    WINDOW *input_win;
    int input_height;
    
    // Layout constants
    int padding;                // Space between windows
    
    // State flags
    int is_initialized;
} WindowManager;
```

### Core Functions

```c
// Lifecycle
int window_manager_init(WindowManager *wm);
void window_manager_destroy(WindowManager *wm);

// Window operations
int window_manager_resize_screen(WindowManager *wm);
int window_manager_ensure_pad_capacity(WindowManager *wm, int needed_lines);
void window_manager_refresh_conversation(WindowManager *wm);
void window_manager_refresh_status(WindowManager *wm);
void window_manager_refresh_input(WindowManager *wm);
void window_manager_refresh_all(WindowManager *wm);

// Scrolling
void window_manager_scroll(WindowManager *wm, int delta);
void window_manager_scroll_to_bottom(WindowManager *wm);
void window_manager_scroll_to_top(WindowManager *wm);

// Input window sizing
int window_manager_resize_input(WindowManager *wm, int desired_lines);

// Validation
int window_manager_validate(WindowManager *wm);
```

## Implementation Strategy

### Phase 1: Extract WindowManager struct
- Create new struct with window-specific fields
- Add to TUIState as a member
- Keep existing API working

### Phase 2: Create window lifecycle functions
- `window_manager_init()` - Initialize all windows
- `window_manager_destroy()` - Clean up all windows
- `window_manager_resize_screen()` - Handle terminal resize

### Phase 3: Add pad management functions
- `window_manager_ensure_pad_capacity()` - Expand pad as needed
- Move pad copying logic into helper function
- Add logging and error handling

### Phase 4: Simplify existing functions
- Refactor `tui_handle_resize()` to use WindowManager
- Simplify `resize_input_window()` to use WindowManager
- Remove duplicate window creation code

### Phase 5: Add validation and error handling
- Create `window_manager_validate()` for debug checks
- Add defensive checks in all public functions
- Ensure graceful degradation

## Benefits

1. **Easier debugging**: All window state in one place
2. **Clearer code flow**: Separation of window management from content
3. **Better testability**: WindowManager can be tested independently
4. **Easier extensions**: Adding new windows is straightforward
5. **Robust resize**: Centralized resize logic reduces bugs

## Migration Path

1. Add WindowManager struct to TUIState
2. Create init/destroy functions
3. Gradually move window operations to WindowManager
4. Keep old functions as thin wrappers (backward compatibility)
5. Eventually deprecate old functions
