# TUI View Modes & Display Density — Design Document

## Status: IMPLEMENTED

## Problem

Long conversations become hard to navigate. Reasoning/thinking traces can span
hundreds of lines. Bash and tool outputs (especially `Read` on large files,
`Grep` with many results, `Bash` with verbose output) consume enormous screen
real estate. The user has no way to control how much of this is shown without
scrolling past it.

This feature adds:

1. **Thinking/reasoning output folded by default** — show a collapsed summary
   line; expand on demand.
2. **Tool output abbreviation by default** — show first N lines or N chars of
   Bash and other tool outputs, with a "truncated" indicator.
3. **Quick hotkeys** to toggle these settings on the fly.
4. **Command-mode commands** (`:set ...`) for explicit control.
5. **Persistence** — settings saved to `config.json` and restored on restart.

---

## Architecture Overview

### Current rendering pipeline (what we build on)

```
ConversationEntry[]  (prefix, text, color_pair, pad_start_line)
    │
    ├── redraw_conversation()         [tui_render.c] — full rebuild, iterates all entries
    │       └── render_entry_to_pad() [tui_render.c] — renders one entry to ncurses pad
    │               ├── user message styling (caret, padding)
    │               ├── assistant message styling (border/caret/robot/cat/bg)
    │               ├── error message styling (icon, bg, closing rule)
    │               └── tool/reasoning/other (prefix + dim text)
    │
    └── (tui_virtual_scroll.c has been deleted — was dead code)
```

Key insight: `render_entry_to_pad()` is the single chokepoint for all rendering.
It already classifies entries via:
- `tui_conversation_is_tool_message(prefix)`
- `tui_conversation_is_reasoning_message(prefix)`
- `tui_conversation_is_error_message(prefix)`

This is where folding and abbreviation logic hooks in.

### Config persistence (what we extend)

```
KlawedConfig (config.h)
    ├── input_box_style     ← existing pattern: enum + to_string/from_string + load/save
    ├── response_style      ← existing pattern
    ├── thinking_style      ← existing pattern
    └── (NEW) view_mode settings  ← we add fields here
```

The existing toggle pattern (e.g., `b` → input_box_style, `r` → response_style)
is: hotkey in `tui_modes.c` → mutate `tui->field` → save to config →
`redraw_conversation()` → refresh. We follow this exactly.

### Entry classification (already exists, we reuse)

| Category | Detection function | Example prefixes |
|---|---|---|
| Reasoning | `tui_conversation_is_reasoning_message()` | ``, `` |
| Tool | `tui_conversation_is_tool_message()` | `● Bash`, ` Read`, `✓ Bash` |
| Error | `tui_conversation_is_error_message()` | ``, `[Error]` |
| User | prefix == `tui_icon_user()` | `` |
| Assistant | prefix == `tui_icon_assistant()` | `` |

---

## Design: Display Density Settings

### New config fields in `KlawedConfig`

```c
// Display density — how much of each entry type to show
typedef enum {
    DENSITY_EXPANDED = 0,   // Show full content (current behavior)
    DENSITY_FOLDED,         // Show collapsed summary line only
    DENSITY_ABBREVIATED     // Show first N lines/chars, then truncate indicator
} DisplayDensity;

// Per-category density settings
typedef struct {
    DisplayDensity reasoning;    // Default: DENSITY_FOLDED
    DisplayDensity tool_output;  // Default: DENSITY_ABBREVIATED
    int abbrev_lines;            // Max lines to show when abbreviated (default: 8)
    int abbrev_chars;            // Max chars to show when abbreviated (default: 500)
} ViewModeConfig;
```

In `KlawedConfig`:
```c
ViewModeConfig view_mode;  // Display density settings
```

### New TUIState fields (mirror config, like input_box_style etc.)

```c
// In TUIState (tui.h)
DisplayDensity reasoning_density;
DisplayDensity tool_density;
int abbrev_lines;
int abbrev_chars;
```

### Config JSON representation

```json
{
    "view_mode": {
        "reasoning": "folded",
        "tool_output": "abbreviated",
        "abbrev_lines": 8,
        "abbrev_chars": 500
    }
}
```

String mappings:
- `DisplayDensity` → `"expanded"`, `"folded"`, `"abbreviated"`

---

## Rendering Behavior

### Reasoning entries (DENSITY_FOLDED by default)

**Folded state:**
```
  ◆ Thinking (47 lines) — press 'H' to expand
```
- Single line showing entry type, line count, and hint
- Dim color (`NCURSES_PAIR_TOOL_DIM`)
- Line count computed from the text (count `\n` + 1, or use
  `text_wrapped_height()` from virtual scroll code as reference)

**Expanded state:** current behavior — full reasoning text in dim color.

### Tool output entries (DENSITY_ABBREVIATED by default)

**Abbreviated state:**
```
  ● Bash
  $ make test
  Running tests...
  test_edit.c... OK
  test_todo.c... OK
  … (+127 more lines, 4.2k chars) — press 'H' to expand
```
- Show first `abbrev_lines` lines (default 8) OR first `abbrev_chars` chars
  (default 500), whichever is reached first
- Truncation indicator line in dim color showing how much was hidden
- The prefix/header is always shown (tool name + first line of command/input)

**Expanded state:** current behavior — full tool output.

### Error entries

Errors are always shown expanded — they're critical and usually short.
No density setting for errors.

### User & Assistant entries

Always shown in full — these are the core content. No density setting.

---

## Interaction Design

### Hotkeys (Normal mode)

| Key | Action | Status message |
|---|---|---|
| `H` | Cycle density of entry under cursor (or all reasoning if none) | `Reasoning: folded → expanded` |
| `Shift+H` (same as `H`) | (same) | |
| `Ctrl+H` | Cycle all reasoning entries density | `All reasoning: folded` |
| `T` | Cycle tool output density (all tool entries) | `Tool output: abbreviated → expanded` |

**`H` behavior in detail:**

The cursor in Normal mode is at `normal_cursor_line`. We find the entry at that
line (using the existing `pad_start_line` field on entries — linear scan or
binary search by line range). If the entry is reasoning or tool type, cycle its
density between folded → abbreviated → expanded. If the entry is user/assistant,
do nothing (or show a status message "Nothing to fold here").

Since density is a global setting (not per-entry), `H` cycles the *global*
density for the category of the entry under the cursor. This is simpler than
per-entry state and matches the existing toggle pattern (like `r` for response
style).

**Revised simpler approach:**

| Key | Action |
|---|---|
| `H` | Cycle reasoning density: folded → abbreviated → expanded → folded |
| `T` | Cycle tool output density: abbreviated → folded → expanded → abbreviated |

This is cleaner — two keys, two categories, three states each. No need for
cursor-position awareness.

### Command-mode commands (`:`)

```
:set reasoning folded       " Fold all reasoning traces
:set reasoning abbreviated  " Show first N lines of reasoning
:set reasoning expanded     " Show full reasoning
:set tool abbreviated       " Abbreviate tool outputs
:set tool expanded          " Show full tool outputs
:set tool folded            " Fold tool outputs (just header line)
:set abbrev_lines 12        " Change abbreviation line limit
:set abbrev_chars 800       " Change abbreviation char limit
```

These follow the existing `:wrap` / `:set wrap` pattern in `tui_modes.c`.

### Slash command (`/config`)

Extend the existing `/config` command:

```
/config view_reasoning folded
/config view_tool abbreviated
/config abbrev_lines 12
```

This follows the existing `config_command.c` pattern.

### Settings menu

Add two items to the settings menu (`settings_menu.c`):
- `Reasoning density` — cycles: folded → abbreviated → expanded
- `Tool density` — cycles: abbreviated → folded → expanded

---

## Implementation Plan

### Files to modify

| File | Changes |
|---|---|
| `src/tui.h` | Add `DisplayDensity` enum, `ViewModeConfig` to TUIState, density fields |
| `src/config.h` | Add `ViewModeConfig` to `KlawedConfig`, to_string/from_string decls |
| `src/config.c` | Add defaults, load/save, to_string/from_string for DisplayDensity |
| `src/tui_render.c` | Modify `render_entry_to_pad()` — add folding & abbreviation logic |
| `src/tui_modes.c` | Add `H` and `T` hotkey handlers, `:set` command handlers |
| `src/tui_core.c` | Load density settings from config in `tui_init()` |
| `src/tui_completion.c` | Add new `:set` commands to completion list |
| `src/config_command.c` | Add `view_reasoning`, `view_tool`, `abbrev_lines`, `abbrev_chars` settings |
| `src/settings_menu.c` | Add reasoning density and tool density menu items |
| `docs/keyboard-shortcuts.md` | Document `H` and `T` hotkeys, `:set` commands |

### Core rendering changes in `render_entry_to_pad()`

The function currently has this structure:
```c
if (is_user_message) { ... }
else if (is_assistant_message) { ... }
else if (is_error_message) { ... }
else {
    // tool, reasoning, other — write prefix + text
}
```

We add density logic in the `else` branch (tool/reasoning/other):

```c
// After determining is_tool_message and is_reasoning_message:
DisplayDensity density = DENSITY_EXPANDED;
if (is_reasoning_message) {
    density = tui->reasoning_density;
} else if (is_tool_message) {
    density = tui->tool_density;
}

if (density == DENSITY_FOLDED && text && text[0] != '\0') {
    // Render: prefix + "(N lines) — press H to expand"
    render_folded_summary(tui, prefix, text, mapped_pair);
    goto skip_newline;
} else if (density == DENSITY_ABBREVIATED && text && text[0] != '\0') {
    // Render: prefix + first N lines/chars + truncation indicator
    render_abbreviated_text(tui, prefix, text, mapped_pair,
                            tui->abbrev_lines, tui->abbrev_chars);
    goto skip_newline;
}
// else: fall through to existing full-text rendering
```

### New helper functions (in `tui_render.c`)

```c
// Count lines in text (count '\n' + 1)
static int count_text_lines(const char *text);

// Render a folded summary line: "prefix (N lines) — press H to expand"
static void render_folded_summary(TUIState *tui, const char *prefix,
                                   const char *text, int mapped_pair);

// Render abbreviated text: first N lines or N chars, then truncation indicator
static void render_abbreviated_text(TUIState *tui, const char *prefix,
                                     const char *text, int mapped_pair,
                                     int max_lines, int max_chars);
```

### `render_abbreviated_text()` algorithm

```
1. Write prefix (tool name, same as current code)
2. Iterate through text line by line:
   a. For each line, check if adding it would exceed max_lines OR max_chars
   b. If within limits: write the line
   c. If exceeded: stop, write truncation indicator
3. Truncation indicator:
   "  … (+{remaining_lines} more lines, {remaining_chars} chars) — press H to expand"
   In dim color (NCURSES_PAIR_TOOL_DIM)
4. If text is shorter than limits: write full text (no indicator)
```

Edge cases:
- Text with no newlines but very long: truncate by chars, add indicator
- Text shorter than limits: render normally (no change from current behavior)
- Empty text: skip (existing behavior already handles this)

### Config defaults

```c
// In config_init_defaults()
config->view_mode.reasoning = DENSITY_FOLDED;
config->view_mode.tool_output = DENSITY_ABBREVIATED;
config->view_mode.abbrev_lines = 8;
config->view_mode.abbrev_chars = 500;
```

### Hotkey handler in `tui_modes.c`

Following the exact pattern of the existing `r` (response style) handler:

```c
case 'H':  // Cycle reasoning density
    if (tui->reasoning_density == DENSITY_FOLDED) {
        tui->reasoning_density = DENSITY_ABBREVIATED;
        tui_update_status(tui, "Reasoning: abbreviated");
    } else if (tui->reasoning_density == DENSITY_ABBREVIATED) {
        tui->reasoning_density = DENSITY_EXPANDED;
        tui_update_status(tui, "Reasoning: expanded");
    } else {
        tui->reasoning_density = DENSITY_FOLDED;
        tui_update_status(tui, "Reasoning: folded");
    }
    // Save to config
    { KlawedConfig cfg; ... config_save(&cfg); }
    // Re-render conversation
    redraw_conversation(tui);
    refresh_conversation_viewport(tui);
    render_status_window(tui);
    input_redraw(tui, prompt);
    break;

case 'T':  // Cycle tool output density
    // Same pattern, cycles tui->tool_density
    // Default order: abbreviated → folded → expanded → abbreviated
    break;
```

### `:set` command handler in `tui_modes.c`

Extend the existing command parsing (after `:wrap` / `:nowrap`):

```c
} else if (strncmp(cmd, "set reasoning ", 14) == 0) {
    const char *value = cmd + 14;
    if (strcmp(value, "folded") == 0) {
        tui->reasoning_density = DENSITY_FOLDED;
        tui_update_status(tui, "Reasoning: folded");
        save_and_redraw();
    } else if (strcmp(value, "abbreviated") == 0) {
        tui->reasoning_density = DENSITY_ABBREVIATED;
        tui_update_status(tui, "Reasoning: abbreviated");
        save_and_redraw();
    } else if (strcmp(value, "expanded") == 0) {
        tui->reasoning_density = DENSITY_EXPANDED;
        tui_update_status(tui, "Reasoning: expanded");
        save_and_redraw();
    } else {
        tui_update_status(tui, "Usage: :set reasoning folded|abbreviated|expanded");
    }
} else if (strncmp(cmd, "set tool ", 9) == 0) {
    // Similar for tool density
} else if (strncmp(cmd, "set abbrev_lines ", 17) == 0) {
    int n = atoi(cmd + 17);
    if (n > 0 && n <= 1000) {
        tui->abbrev_lines = n;
        // save + redraw
    }
} else if (strncmp(cmd, "set abbrev_chars ", 17) == 0) {
    int n = atoi(cmd + 17);
    if (n > 0 && n <= 100000) {
        tui->abbrev_chars = n;
        // save + redraw
    }
}
```

---

## Streaming Considerations

During streaming, `tui_update_last_conversation_line()` and
`tui_update_conversation_entry()` update entry text in real-time. The density
settings must work during streaming:

- **Reasoning streaming**: If density is `FOLDED`, the folded summary should
  update its line count as text streams in. The summary line shows "(N lines)"
  which updates. The full text is not rendered to the pad — only the summary.
- **Tool output streaming**: If density is `ABBREVIATED`, show the first N
  lines as they arrive. Once the limit is hit, show the truncation indicator.
  The indicator's "(+M more lines)" count updates as more text arrives.

Implementation: The streaming update functions call
`render_entry_to_pad()` (or update the pad directly). The density check happens
inside `render_entry_to_pad()`, so streaming naturally respects density —
each re-render applies the current density setting.

**Important:** The full text is always stored in `entry->text` — density only
affects *rendering*, not *storage*. Toggling density to "expanded" immediately
shows full content because `redraw_conversation()` re-renders from
`entry->text`.

---

## Visual Design

### Folded reasoning

```
  ◆ Thinking (47 lines) ───────────── press H to expand
```

- `◆` in reasoning color, rest in `NCURSES_PAIR_TOOL_DIM`
- Thin rule fills to a max of 40 chars (matching the error closing rule style)
- `press H to expand` at the end

### Abbreviated tool output

```
  ● Bash
  $ make test
  CC build/klawed.o
  Running tests...
  test_edit.c... OK
  test_todo.c... OK
  … +127 more lines (4.2k chars) ─── press H to expand
```

- Tool name + first lines in normal tool colors
- Truncation line in `NCURSES_PAIR_TOOL_DIM` with thin rule (same style as
  folded and error closing rules)

### Abbreviated reasoning (if user sets reasoning to abbreviated)

```
  ◆ Thinking
  I need to check the file structure first. Let me look at...
  … +340 more chars ─── press H to expand
```

---

## Edge Cases & Considerations

1. **Search highlighting**: When searching (`/pattern`), folded/abbreviated
   entries should auto-expand during search to show matches, then return to
   their density state. Alternatively, search only searches visible text and
   shows a "match in folded entry — press H to expand" hint. **Recommendation:**
   Auto-expand during active search, re-fold when search is cleared (`:noh`).

2. **Session restore**: When loading a session (`tui_populate_from_conversation`),
   density settings apply immediately to all loaded entries. No special
   handling needed — `redraw_conversation()` handles it.

3. **Very short outputs**: If a tool output is 3 lines and `abbrev_lines` is 8,
   no truncation happens — render normally. The density setting is a
   ceiling, not a fixed size.

4. **Nested folding**: Not needed for v1. Each entry is independently rendered
   with the global density setting for its category.

5. **Per-entry override**: Considered but rejected for v1. Global per-category
   density is simpler, matches existing patterns, and covers 95% of use cases.
   Could be added later if needed (e.g., `H` on cursor entry overrides global).

6. **Dead virtual scroll code**: `tui_virtual_scroll.c` and `.h` were deleted
   as part of this feature. They were not compiled (not in Makefile) and
   referenced struct fields that didn't exist in `ConversationEntry`.

7. **Terminal resize**: `redraw_conversation()` is called on resize, which
   re-applies density. No special handling needed.

---

## Summary

| Feature | Hotkey | Command | Config key | Default |
|---|---|---|---|---|
| Reasoning density | `H` | `:set reasoning folded\|abbreviated\|expanded` | `view_mode.reasoning` | `folded` |
| Tool output density | `T` | `:set tool abbreviated\|expanded\|folded` | `view_mode.tool_output` | `abbreviated` |
| Abbrev line limit | — | `:set abbrev_lines N` | `view_mode.abbrev_lines` | `8` |
| Abbrev char limit | — | `:set abbrev_chars N` | `view_mode.abbrev_chars` | `500` |

**Implementation:** ~600 lines of new code, all following existing patterns.
The core change is ~120 lines in `render_entry_to_pad()` plus helper functions
(`count_text_lines`, `render_folded_summary`, `render_abbreviated_text`,
`render_dim_rule`). The rest is config plumbing and UI handlers that mirror
existing toggle implementations exactly. The dead `tui_virtual_scroll.c/.h`
files were deleted as part of this change.
