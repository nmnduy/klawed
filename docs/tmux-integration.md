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

Klawed sends two escape sequences when the AI is working or changing state:

| Escape Sequence | What It Sets | Requires |
|---|---|---|
| `OSC 0` (`\033]0;...\007`) | Terminal window title | Standard terminal support |
| `OSC 2` (`\033]2;...\007`) | Tmux pane title (2.6+) | `allow-rename on` + `pane-border-status` |

Klawed does **not** set tmux window names (`OSC k`). Since klawed runs in individual panes, pane-level titles are the right granularity — each pane gets its own title without competing for the window name.

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

Since klawed only sets pane titles (not window names), multiple klawed instances in different panes won't fight over the window name — each pane independently shows its own status.

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
