# Color Theme Support

The TUI uses **Kitty terminal's theme format**, but themes are **embedded in the binary** so Klawed ships with zero runtime theme dependencies. External `.conf` files still work for overrides, but the preferred path is to embed themes directly in `src/builtin_themes.c`.

## Built-in Themes (shipped in the binary)

- `tender` *(default)* — Warm and soft
- `kitty-default` — Classic high contrast black & white
- `dracula` — Dark purple with vibrant colors
- `gruvbox-dark` — Warm retro with low contrast
- `solarized-dark` — Blue-tinted, carefully balanced
- `black-metal` — Pure black with grayscale tones

## Theme Explorer (Interactive Preview)

The easiest way to preview themes is with the built-in theme explorer:

1. Start klawed in interactive mode: `./klawed`
2. Type `/themes` and press Enter
3. Use `j`/`k` or arrow keys to browse themes
4. Press `Enter` to select a theme, or `q`/`ESC` to cancel
5. Follow the displayed instructions to apply the theme

The theme explorer shows a live preview of each theme with sample text, including:
- Foreground text color
- User, Assistant, Status, Error, and Tool colors
- Diff add/remove colors
- Full 16-color palette preview

## Choosing a Theme

**Use a built-in theme (recommended):**
```bash
export KLAWED_THEME="dracula"
./klawed "your prompt"
```

**Use an external theme file (legacy/override):**
```bash
export KLAWED_THEME="/path/to/theme.conf"
./klawed "your prompt"
```

**No theme specified:** Defaults to the built-in `tender` theme.

## Theme Format (Kitty-compatible)

```conf
# Klawed TUI Theme
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

## Adding New Built-in Themes (preferred)

1. Obtain a Kitty theme `.conf` file (local or URL).
2. Generate a C snippet using the helper script:
   ```bash
   ./scripts/embed_kitty_theme.sh "my-theme" /path/or/url/to/theme.conf > /tmp/my-theme-snippet.c
   ```
3. Paste the snippet into the `built_in_themes` array in `src/builtin_themes.c`.
4. Keep the name short (≤63 chars) and unique.
5. Rebuild: `make` (or `gmake` on macOS if needed).

> Tip: External themes are still supported via `KLAWED_THEME=/path/to/theme.conf`, but embedding keeps the binary self-contained.

## Why Kitty's format?

- ✅ No extra parser dependencies; trivial to parse in C
- ✅ Hundreds of community themes available
- ✅ Human-readable and editable
- ✅ Compatible with Kitty terminal themes

## Notes for tmux and limited-color terminals

- If `tmux` lacks truecolor, we fall back to the 256-color palette when `can_change_color()` is false but `COLORS >= 256`.
- Ensure tmux advertises truecolor: `set -g default-terminal "tmux-256color"` and `set -ga terminal-overrides ',*:Tc'`.
- Set your shell env before launching tmux (or inside): `export TERM=tmux-256color` (or `xterm-256color`) and `export COLORTERM=truecolor`.
- Kitty themes still apply in fallback modes, but colors are approximated to the nearest 256-color entry.
