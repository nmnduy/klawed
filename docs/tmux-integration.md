# Tmux Integration

Klawed sets your tmux window and pane titles so you can see what it's doing at a glance — spinner animation, model name, and working directory — even when Klawed is in another pane.

## Quick Setup

Add these to your `~/.tmux.conf`:

```tmux
# Allow programs like klawed to rename windows and panes
set -g allow-rename on

# Don't let tmux auto-rename over our custom titles
set -g automatic-rename off

# Optional: show pane titles in the pane border bar
# This is especially useful with multi-pane workflows
set -g pane-border-status top
set -g pane-border-format ' #{pane_title} '
```

Then reload: `tmux source-file ~/.tmux.conf`

## What Klawed Sets

Klawed sends three escape sequences when the AI is working or changing state:

| Escape Sequence | What It Sets | Requires |
|---|---|---|
| `OSC 0` (`\033]0;...\007`) | Terminal window title (icon name + title) | Standard terminal support |
| `OSC k` (`\033k...\033\`) | Tmux window name | `allow-rename on` |
| `OSC 2` (`\033]2;...\007`) | Tmux pane title (2.6+) | `allow-rename on` + `pane-border-status` |

### Title Formats

**Window title** (medium-length, includes model + working directory):
```
◉ klawed(gpt-4o) · my-project    ← while AI is working
klawed(gpt-4o) · my-project      ← idle
```

**Pane title** (compact, just the essentials):
```
◉ my-project    ← while AI is working
klawed my-project  ← idle
```

The spinner character changes based on what Klawed is doing (thinking, calling tools, reasoning, etc.).

## Multi-Pane Workflows

With `pane-border-status top`, each pane shows its own title. This is useful when you have:

```
┌─ ◉ my-project ─────────────────────────────┐
│ klawed working on your code...              │
└─────────────────────────────────────────────┘
┌─ $ zsh ────────────────────────────────────┐
│ Running tests, checking logs, etc.          │
└─────────────────────────────────────────────┘
```

The klawed pane shows `◉ my-project` while it's working, so you always know it's busy.

## Troubleshooting

### Window titles not updating

1. Verify `allow-rename` is on:
   ```bash
   tmux show-option -g allow-rename
   # Should print: allow-rename on
   ```

2. Verify `automatic-rename` is off:
   ```bash
   tmux show-option -g automatic-rename
   # Should print: automatic-rename off
   ```

3. If using per-window settings, check the window options too:
   ```bash
   tmux show-option -w allow-rename
   tmux show-option -w automatic-rename
   ```

### Pane titles not showing

1. Make sure `pane-border-status` is set:
   ```bash
   tmux show-option -g pane-border-status
   # Should print: pane-border-status top (or bottom)
   ```

2. Ensure your tmux version is 2.6 or later:
   ```bash
   tmux -V
   ```

### Spinner not appearing in title (ncurses terminal apps)

Klawed uses `dprintf()` to write escape sequences directly to the file descriptor, bypassing ncurses' internal buffering. This ensures titles work even when ncurses controls the terminal display. If titles still don't update, check that your terminal emulator supports OSC escape sequences (all modern terminals do).
