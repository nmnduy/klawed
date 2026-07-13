# Tmux Integration

Klawed sets your tmux pane titles so you can see what it's doing at a glance — spinner animation, model name, and working directory — even when Klawed is in another pane.

## Quick Setup

Add these to your `~/.tmux.conf`:

```tmux
# Allow programs like klawed to rename panes
set -g allow-rename on

# Don't let tmux auto-rename over our custom titles
set -g automatic-rename off

# Show pane titles in the pane border bar (required to see titles)
set -g pane-border-status top
set -g pane-border-format ' #{pane_title} '
```

Then reload: `tmux source-file ~/.tmux.conf`

## What Klawed Sets

Klawed sends three escape sequences when the AI is working or changing state:

| Escape Sequence | What It Sets | Requires |
|---|---|---|
| `OSC 0` (`\033]0;...\007`) | Terminal window title | Standard terminal support |
| `OSC 2` (`\033]2;...\007`) | Tmux pane title (2.6+) | `allow-rename on` + `pane-border-status` |
| `OSC k` (`\033k...\033\\`) | Tmux window name | `allow-rename on` + `automatic-rename off` |

**Window name vs pane title:** The window name appears in the tmux status bar as the tab label for the window. The pane title appears in each pane's border bar. Both show a spinner when klawed is working.

### Title Formats

**Terminal window title** (medium-length, includes model + working directory):
```
◉ klawed(gpt-4o) · my-project    ← while AI is working
klawed(gpt-4o) · my-project      ← idle
```

**Pane title** (compact, just the essentials):
```
◉ my-project    ← while AI is working
klawed my-project  ← idle
```

**Tmux window name** (same format as terminal window title, shown in status bar tab):
```
◉ klawed(gpt-4o) · my-project    ← any klawed working in this window
klawed(gpt-4o) · my-project      ← all klaweds idle
```

The spinner character changes based on what Klawed is doing (thinking, calling tools, reasoning, etc.).

## Multi-Pane Workflows

With `pane-border-status top`, each pane shows its own title. This works perfectly when you have:

```
┌─ ◉ my-project ─────────────────────────────┐
│ klawed working on your code...              │
├─────────────────────────────────────────────┤
│ $ zsh                                       │
│ Running tests, checking logs, etc.          │
└─────────────────────────────────────────────┘
```

### Window Title Coordination

When multiple klawed instances run in different panes of the same tmux window, the window title (shown in the tmux status bar tab) coordinates naturally:

- **Working klaweds** update the window title with a spinner at ~60fps
- **Idle klaweds** don't touch the window title (they only update titles while actively working)
- As long as **any** klawed in the window is working, the window tab shows the spinner
- When the last klawed stops, the spinner is cleared from the window tab

This means you can glance at the tmux window tab to see if anything is happening across all your klawed panes.

## Troubleshooting

### Pane titles not showing

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

3. Verify `pane-border-status` is set:
   ```bash
   tmux show-option -g pane-border-status
   # Should print: pane-border-status top (or bottom)
   ```

4. Ensure your tmux version is 2.6 or later:
   ```bash
   tmux -V
   ```

### Spinner not appearing in title (ncurses terminal apps)

Klawed uses `dprintf()` to write escape sequences directly to the file descriptor, bypassing ncurses' internal buffering. This ensures titles work even when ncurses controls the terminal display. If titles still don't update, check that your terminal emulator supports OSC escape sequences (all modern terminals do).
