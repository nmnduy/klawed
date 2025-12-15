# TODO List Rendering

## Overview

The TODO list rendering has been enhanced with better visual formatting including indentation and colored bullet points.

## Features

### 1. Indentation
All TODO items are now indented by 4 spaces for better readability and visual hierarchy.

### 2. Colored Bullet Points
Each status type has a distinct color for its bullet point:

- **✓** (Completed) - **Green** - Uses `COLORSCHEME_USER` (green from theme)
- **⋯** (In Progress) - **Yellow** - Uses `COLORSCHEME_STATUS` (yellow from theme)
- **○** (Pending) - **Cyan** - Uses `COLORSCHEME_ASSISTANT` (cyan from theme)

### 3. Text Foreground Color
The task text itself uses the foreground color from the colorscheme (`COLORSCHEME_FOREGROUND`), ensuring it's always readable regardless of the theme.

## Example Output

```
Here are the current tasks:
    ✓ Build the project
    ⋯ Running unit tests
    ○ Fix failing tests
    ○ Update documentation
    ○ Create release
```

Where:
- `✓` appears in green
- `⋯` appears in yellow
- `○` appears in cyan
- Task text appears in the theme's foreground color

## Implementation

The rendering logic is in `src/todo.c` in the `todo_render_to_string()` function:

1. **Color code extraction**: Gets ANSI color codes from the colorscheme system
2. **Fallback handling**: Uses standard ANSI colors if no theme is loaded
3. **Formatted output**: Applies indentation and colored bullets while keeping text readable

### Color Mappings

| Status | Bullet | Color Source | Fallback |
|--------|--------|--------------|----------|
| Completed | ✓ | `COLORSCHEME_USER` | Green (`\033[32m`) |
| In Progress | ⋯ | `COLORSCHEME_STATUS` | Yellow (`\033[33m`) |
| Pending | ○ | `COLORSCHEME_ASSISTANT` | Cyan (`\033[34m`) |
| Text | - | `COLORSCHEME_FOREGROUND` | White (`\033[37m`) |

## Theme Support

The TODO list rendering automatically adapts to the active theme:

- **Dracula**: Purple/pink tones with vibrant colors
- **Gruvbox**: Warm, retro colors with lower contrast
- **Solarized**: Blue-tinted palette with balanced colors
- **Kitty Default**: Classic high contrast colors

Set theme via:
```bash
export CLAUDE_C_THEME="dracula"
# or
export CLAUDE_C_THEME="/path/to/theme.conf"
```

## Testing

Run the TODO list tests to see the rendering in action:

```bash
make test-todo
```

The test suite includes a visual rendering test that displays TODO lists with different status combinations.
