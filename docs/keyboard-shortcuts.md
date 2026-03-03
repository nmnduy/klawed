# TUI Keyboard Shortcuts

## Overview
Klawed's TUI (Terminal User Interface) provides a vim-inspired modal interface with keyboard shortcuts for efficient navigation and interaction.

## TUI Modes

The TUI operates in different modes, similar to vim:

- **Normal Mode**: Default mode for viewing and navigating the conversation
- **Insert Mode**: Text input mode for sending messages  
- **Command Mode**: Execute vim-style commands (prefix with `:`)
- **Search Mode**: Search through conversation (prefix with `/` or `?`)
- **File Search Mode**: Fuzzy file finder (Ctrl+F)
- **History Search Mode**: Search command history (Ctrl+R)

## Normal Mode Shortcuts

### Mode Switching
- `i` - Enter Insert mode (start typing a message)
- `:` - Enter Command mode
- `/` - Enter Search mode (forward search)
- `?` - Enter Search mode (backward search)

### Navigation
- `j` or `↓` - Scroll down
- `k` or `↑` - Scroll up
- `d` or `Ctrl+D` - Scroll down half page
- `u` or `Ctrl+U` - Scroll up half page
- `f` or `Ctrl+F` - Scroll down full page
- `b` or `Ctrl+B` - Scroll up full page
- `g` or `Home` - Jump to top
- `G` or `End` - Jump to bottom
- `{` - Jump to previous paragraph (empty line)
- `}` - Jump to next paragraph (empty line)

### Search
- `n` - Jump to next search result
- `N` - Jump to previous search result

### UI Customization
- `b` - Toggle input box style (cycles: bland → background → border → horizontal → bland)
- `r` - Toggle response style (cycles: border → caret → border)
- `t` - Toggle thinking style (cycles: wave → pacman → wave)

## Insert Mode Shortcuts

### Text Editing
- `Enter` - Send message and execute
- `Ctrl+J` - Insert newline (multiline input)
- `Backspace` - Delete character before cursor
- `Delete` - Delete character at cursor
- `Ctrl+W` - Delete word before cursor
- `Ctrl+U` - Delete from cursor to beginning of line
- `Ctrl+K` - Delete from cursor to end of line

### Cursor Movement
- `←`/`→` - Move cursor left/right
- `Ctrl+A` or `Home` - Move to beginning of line
- `Ctrl+E` or `End` - Move to end of line
- `Alt+B` - Move back one word
- `Alt+F` - Move forward one word

### Special Functions
- `ESC` - Return to Normal mode (without sending)
- `Ctrl+L` - Clear screen/redraw
- `Ctrl+F` - Open file search popup
- `Ctrl+R` - Open history search popup
- `Tab` - Command/path autocomplete (context-dependent)

## Command Mode

### Entering Command Mode
From Normal mode, press `:` to enter command mode. The command prompt will appear at the bottom showing `:`.

### Available Commands
- `:q` or `:quit` - Quit klawed
- `:w` or `:write` - Write/save (not yet implemented)
- `:wq` - Write and quit
- `:noh` or `:nohlsearch` - Clear search highlighting
- `:!<cmd>` - Execute shell command (e.g., `:!ls -la`)
- `:re !<cmd>` - Replace input buffer with shell command output

### Command Mode Shortcuts
- `Tab` - **Autocomplete command** - Complete partial commands
  - Single match: Completes immediately
  - Multiple matches: Shows common prefix and available options
  - No match: Beeps
- `Enter` - Execute the command
- `ESC` - Cancel and return to Normal mode
- `Backspace` - Delete last character (exits command mode if only `:` remains)
- `Ctrl+L` - Clear command buffer (reset to just `:`)

### Command Autocomplete Examples

**Example 1: Single match**
```
:q<Tab>     → :quit
```

**Example 2: Multiple matches with common prefix**
```
:n<Tab>     → :no          (common prefix of 'noh' and 'nohlsearch')
:no<Tab>    → Shows: "Available: noh, nohlsearch"
```

**Example 3: Ambiguous prefix**
```
:w<Tab>     → Shows: "Available: w, write, wq"
```

## Search Mode

- Type search pattern and press `Enter` to search
- `ESC` - Cancel search and return to Normal mode
- Use `n`/`N` in Normal mode to jump between results

## File Search (Ctrl+F)

Fuzzy file finder popup:
- Type to filter files
- `↑`/`↓` or `Ctrl+P`/`Ctrl+N` - Navigate results
- `Enter` - Insert selected file path
- `ESC` - Cancel and close

## History Search (Ctrl+R)

Search through command history:
- Type to filter history
- `↑`/`↓` - Navigate results
- `Enter` - Insert selected command
- `ESC` - Cancel and close

## Slash Commands

From Insert mode, type commands starting with `/`:

- `/help` - Show help
- `/clear` - Clear conversation history
- `/exit` or `/quit` - Exit klawed
- `/add-dir <path>` - Add directory to working directories
- `/voice` - Record voice input and transcribe
- `/themes` - Browse color themes
- `/vim` - Open vim editor
- `/dump [file]` - Dump conversation to file

## Tips

1. **Command vs Slash Commands**: 
   - `:` commands (vim-style) are for TUI control
   - `/` commands are for klawed-specific operations

2. **Tab Completion**:
   - Works in Insert mode for `/` commands
   - Works in Command mode for `:` commands
   - Works for file paths in relevant contexts

3. **Multiline Input**:
   - Use `Ctrl+J` in Insert mode to add newlines
   - Input area will auto-expand

4. **Quick Exit**:
   - Normal mode: `:q<Tab><Enter>` or just `:q<Enter>`
   - Insert mode: `ESC` then `:q<Enter>`

5. **Search and Navigate**:
   - `/pattern<Enter>` to search
   - `n` to jump to next occurrence
   - `:noh<Tab><Enter>` to clear highlights

6. **Input Box Styles** (toggle with `b` in Normal mode):
   - **Bland** (default): Minimal style with just '>>>' caret, no borders or padding
   - **Background**: Left border line + colored background with padding
   - **Border**: Full box border around input, no background color
   - **Horizontal**: Only top and bottom borders, no left/right borders, with padding
