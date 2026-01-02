# File Search (Ctrl+F)

Interactive file finder for quickly inserting file paths into the input buffer.

## Usage

1. In **INSERT mode**, press `Ctrl+F` to open the file search popup
2. Start typing to filter files by name (case-insensitive substring matching)
3. Navigate with `j/k` or arrow keys
4. Press `Enter` to insert the selected file path at cursor position
5. Press `ESC` to cancel

## Features

- **Fuzzy matching**: Type part of a filename to filter
- **Fast tool detection**: Automatically uses the best available tool:
  - `fd` (preferred - fastest, respects .gitignore)
  - `rg --files` (ripgrep)
  - `find` (fallback)
- **Timeout protection**: 3-second timeout prevents hangs on large directories
- **File caching**: Files are cached on first open for instant filtering

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `Ctrl+F` | Open file search (INSERT mode) |
| `j` / `↓` | Select next result |
| `k` / `↑` | Select previous result |
| `Ctrl+D` / `PgDn` | Page down |
| `Ctrl+U` / `PgUp` | Page up |
| `Enter` | Insert selected path |
| `ESC` | Cancel search |
| `Backspace` | Delete last character from pattern |
| `Ctrl+L` | Clear search pattern |

## Configuration

The search timeout can be adjusted via the `FILE_SEARCH_DEFAULT_TIMEOUT_MS` constant in `src/file_search.h` (default: 3000ms).

## Technical Details

- Maximum 500 files cached per search session
- Hidden files are included (except `.git` directory)
- Paths are displayed relative to the working directory
- Long paths are truncated with `...` prefix to fit window

## Tool Priority

1. **fd**: Modern `find` replacement. Fastest, respects `.gitignore`
2. **rg --files**: Lists files ripgrep would search
3. **find**: POSIX standard fallback, always available

Install `fd` or `ripgrep` for best performance:
```bash
# macOS
brew install fd ripgrep

# Linux (Debian/Ubuntu)
apt install fd-find ripgrep
```
