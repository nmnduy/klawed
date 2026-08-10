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

### Marks (Vim-style)

Marks let you bookmark positions in the conversation and quickly jump back to them — similar to vim's `m{a-z}` / `'{a-z}`.

- `m{a-z}` — **Set a mark** at the current scroll position. Press `m` followed by a letter `a`-`z`.  
  Example: `ma` sets mark 'a' at the current line.
- `'{a-z}` — **Jump to a mark**. Press `'` followed by the mark's letter.  
  Example: `'a` scrolls the conversation to mark 'a'.
- **Status bar indicator**: Active marks appear in the status bar as `◸ a b c` (only visible in Normal mode).
- **Status feedback**: Setting or jumping to a mark shows a confirmation (e.g., `Mark set: a` or `Jump to mark: a`). If you try to jump to an unset mark, it shows `Mark 'x' not set`.

**Practical use cases:**
1. **Compare two sections**: Scroll to the top of a function, press `ma`, scroll to another area, press `mb`, then toggle between them with `'a` and `'b`.
2. **Mark an interesting response**: While reviewing a long conversation, mark positions you want to revisit later.
3. **Anchoring**: Set mark 'a' at the beginning of a large assistant response, then use `'a` to return after scrolling through tool outputs.
4. **Reference points**: Use marks like breadcrumbs — `ma` at a code diff, `mb` at an error message, `mc` at a TODO update — and jump between them.

**Notes:**
- Marks are **per-session** — they're only valid for the current TUI session and reset when you restart.
- You can set all 26 marks (a-z) independently.
- Mark names are case-sensitive: `ma` and `mA` are different (but `mA` is not a standard vim behavior; klawed only supports `a-z`).
- Pressing an invalid character after `m` or `'` (anything outside `a-z`) cancels the operation and shows an error.

### Search
- `n` - Jump to next search result
- `N` - Jump to previous search result

### UI Customization
- `b` - Toggle input box style (cycles: bland → background → border → horizontal → bland)
- `r` - Toggle response style (cycles: border → caret → robot → border)
- `t` - Toggle thinking style (cycles: wave → pacman → wave)
- `w` - Toggle word wrap on/off (when off, use `h`/`l` to scroll horizontally)
- `h` - Scroll left (horizontal scroll when wrap is disabled)
- `l` - Scroll right (horizontal scroll when wrap is disabled)
- `0` - Reset horizontal scroll to beginning of line

## Insert Mode Shortcuts

### Text Editing
- `Enter` - Send message and execute
- `Ctrl+J` - Insert newline (multiline input)
- `Backspace` - Delete character before cursor
- `Delete` - Delete character at cursor
- `Ctrl+W` - Delete word before cursor
- `Ctrl+U` - Delete from cursor to beginning of line
- `Ctrl+K` - Delete from cursor to end of line (opens command palette when input is empty)

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
- `Ctrl+K` - Open command palette (when input is empty, like Cmd+K in web UIs); otherwise, delete from cursor to end of line
- `Tab` - Command/path autocomplete (context-dependent)

## Command Mode

### Entering Command Mode
From Normal mode, press `:` to enter command mode. The command prompt will appear at the bottom showing `:`.

### Available Commands
- `:q` or `:quit` - Quit klawed
- `:w` or `:write` - Write/save (not yet implemented)
- `:wq` - Write and quit
- `:noh` or `:nohlsearch` - Clear search highlighting
- `:wrap` or `:set wrap` - Enable word wrap
- `:nowrap` or `:set nowrap` - Disable word wrap (horizontal scroll mode)
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

## Command Palette (Ctrl+K)

Press `Ctrl+K` when the input is empty to open the command palette, similar to Cmd+K in most web UIs. This lets you quickly find and execute slash commands without memorizing them.

### Using the palette
- **Type to filter** — the palette filters commands by name and description as you type
- `↑` / `↓` — Navigate through matching commands
- `Enter` — Select the highlighted command (inserts `/command ` into the input buffer)
- `ESC` or `Ctrl+C` — Cancel and close the palette, returning to INSERT mode

### Mouse

- **Scroll wheel** — scrolls the conversation in any mode: wheel up = older
  messages, wheel down = newer messages.
- Wheel scrolling requires terminal mouse reporting, which klawed enables
  automatically at startup (`mousemask`). Both modern ncurses (6.x, SGR
  protocol) and Apple's system ncurses (5.4, legacy X10 protocol) are
  supported; the Apple X10 decode quirk (both wheel directions reported with
  the button-5 bit) is handled internally.
- **Inside tmux**: tmux only forwards mouse events to a pane when its `mouse`
  option is on. If the wheel does nothing, run `tmux set -g mouse on` (or add
  `set -g mouse on` to `~/.tmux.conf` to make it permanent). Klawed shows a
  hint about this at startup when it detects tmux with mouse mode off.

### Tips
- Press `Ctrl+K` on empty input to browse all available commands
- Start typing to quickly narrow down the list (e.g., type "clea" to find "/clear")
- After selecting, the command is inserted with the `/` prefix ready for you to add arguments or press Enter to run

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

6. **Use Marks for Navigation**:
   - `ma` to mark a position, `'a` to return — works across entire conversation
   - Set marks on key reference points (configs, error outputs, code blocks)
   - Combine with `{`/`}` (paragraph jumps) for fast navigation: mark paragraphs as you go
   - Active marks are shown in the status bar as `◸ a b c` — glance to see which marks are set

7. **Input Box Styles** (toggle with `b` in Normal mode):
   - **Bland** (default): Minimal style with just '>>>' caret, no borders or padding
   - **Background**: Left border line + colored background with padding
   - **Border**: Full box border around input, no background color
   - **Horizontal**: Only top and bottom borders, no left/right borders, with padding
