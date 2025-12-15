# Color Theme Support

The TUI uses **Kitty terminal's theme format** - a simple, dependency-free configuration format. Built-in themes are embedded in the binary for zero-dependency operation, and you can still use external .conf files for custom themes.

## Built-in Themes

Six themes are embedded in the binary (no external files needed):

- `tender` - Warm and soft, easy on the eyes (default)
- `kitty-default` - Classic high contrast black & white
- `dracula` - Dark purple with vibrant colors
- `gruvbox-dark` - Warm retro with low contrast
- `solarized-dark` - Blue-tinted, carefully balanced
- `black-metal` - Pure black with grayscale tones

## Configuration

**Using a built-in theme:**
```bash
export CLAUDE_C_THEME="dracula"
./claude-c "your prompt"
```

**Using an external theme file:**
```bash
export CLAUDE_C_THEME="/path/to/your/theme.conf"
./claude-c "your prompt"
```

**No theme specified:** Defaults to `tender` built-in theme

## Theme Format

Kitty's dead-simple key-value format - no parser library needed!

```conf
# Claude TUI Theme
background #282a36
foreground #f8f8f2
cursor #bbbbbb
selection_background #44475a

# 16 ANSI colors
color0 #000000
color1 #ff5555
color2 #50fa7b
color3 #f1fa8c
# ... color4-15

# TUI-specific (optional)
assistant_fg #8be9fd
user_fg #50fa7b
status_bg #44475a
error_fg #ff5555
```

## Why Kitty's format?

- ✅ Zero dependencies - no parser library needed
- ✅ Trivial to parse in C (~50 lines)
- ✅ 300+ themes available from kitty-themes
- ✅ Human-readable and editable
- ✅ Compatible with Kitty terminal themes
- ✅ Faster than structured formats (TOML/YAML)

## Using External Kitty Themes

Most Kitty themes work out of the box! Download from [kitty-themes](https://github.com/dexpota/kitty-themes):

```bash
# Download a theme
curl -o ~/nord.conf \
  https://raw.githubusercontent.com/dexpota/kitty-themes/master/themes/Nord.conf

# Use it
export CLAUDE_C_THEME=~/nord.conf
./claude-c "your prompt"
```

## Adding New Built-in Themes

To add a theme to the binary (see `src/builtin_themes.c`):

1. Download the .conf file
2. Convert to C string literal with escaped newlines
3. Add to the `built_in_themes[]` array
4. Rebuild with `make`

This way, your favorite theme is always available without carrying external files.