# Perpetual Mode — Implementation Plan

## Overview

Perpetual mode is a stateless-but-persistent execution mode for klawed. Every user
request starts with a fresh LLM context. The LLM has exactly **one tool: Bash**.
It uses Bash to search and extract context from a growing `perpetual.md` log file,
then spawns subagents (via `klawed --perpetual` child invocations) to do the actual
work, then appends a summary of what happened back to the log.

The key insight (from the RLM paper): the LLM never reads the whole file raw. It
*programs its way* through it — grep for relevant sessions, read only matching line
ranges — so a 50,000-line log costs the same as a 500-line one.

## File Structure

```
src/perpetual/
  perpetual_mode.c      — top-level entry point, called from main()
  perpetual_mode.h
  perpetual_log.c       — append-only log file I/O
  perpetual_log.h
  perpetual_prompt.c    — system prompt builder
  perpetual_prompt.h
docs/perpetual-mode.md  — user-facing documentation
```

## perpetual.md Log Format

Append-only markdown. Each session is a fenced block, grep-friendly:

```
## [2026-02-26 14:03] Session abc123
**Request:** refactor the auth module
**Summary:** extracted 3 functions into auth_utils.c, updated call sites
**Files:** src/auth.c, src/auth_utils.c
**Commit:** a3f1c2d

---
```

## Root LLM System Prompt (perpetual_prompt.c)

The root LLM is told to follow a 3-phase loop using only Bash:

```
Phase 1 — Recon:
  bash: wc -l .klawed/perpetual.md
  bash: grep -n "^## \[" .klawed/perpetual.md | tail -20
  bash: grep -n "<keywords from user query>" .klawed/perpetual.md

Phase 2 — Extract:
  bash: sed -n '<start>,<end>p' .klawed/perpetual.md   (only relevant blocks)

Phase 3 — Act:
  spawn subagent(s) with context-enriched prompt via:
  bash: klawed --is-subagent "<prompt with extracted context>"

Phase 4 — Log:
  bash: cat >> .klawed/perpetual.md << 'EOF'
  ## [<timestamp>] Session <id>
  **Request:** ...
  **Summary:** ...
  **Files:** ...
  EOF
```

Note: subagent spawning uses a direct `klawed` exec (with `KLAWED_IS_SUBAGENT=1`)
so the child gets full tool access. The root session only ever uses Bash.

## Tool Configuration

In perpetual mode, the root LLM gets: **Bash, Subagent, CheckSubagentProgress, InterruptSubagent, Sleep**.

Rationale:
- **Bash** — recon, grep, sed on the log; exec child processes
- **Subagent / CheckSubagentProgress / InterruptSubagent** — spawn and manage full action-taking subagents
- **Sleep** — wait between polling subagent progress

All other tools (Read, Write, Edit, MultiEdit, Glob, Grep, UploadImage, TodoWrite, MemoryStore, MemoryRecall, MemorySearch) are disabled. The root LLM uses Bash for any direct file access it needs.

Set in `perpetual_mode.c` before the API call:
```c
setenv("KLAWED_DISABLE_TOOLS",
       "Read,Write,Edit,MultiEdit,Glob,Grep,UploadImage,TodoWrite,"
       "MemoryStore,MemoryRecall,MemorySearch", 1);
```

**TODO**: Update `perpetual_mode.c` to allow Subagent, CheckSubagentProgress, InterruptSubagent, and Sleep in addition to Bash. The current `KLAWED_DISABLE_TOOLS` string disables too many tools.

## Integration into main()

In `src/klawed.c` `main()`, after existing mode checks:

```c
// Check for perpetual mode
const char *perpetual_env = getenv("KLAWED_PERPETUAL");
if (perpetual_env && (strcmp(perpetual_env, "1") == 0 ||
                      strcasecmp(perpetual_env, "true") == 0 ||
                      strcasecmp(perpetual_env, "yes") == 0)) {
    exit_code = perpetual_mode_run(&state, single_command);
    goto cleanup;
}
```

Also support `--perpetual` CLI flag (parsed in the existing argv loop).

## New Source Files

### src/perpetual/perpetual_log.h / perpetual_log.c

```c
// Resolve log path: KLAWED_PERPETUAL_FILE env or .klawed/perpetual.md
char *perpetual_log_get_path(const char *data_dir);

// Append a completed turn to the log
int perpetual_log_append(const char *log_path,
                          const char *session_id,
                          const char *request,
                          const char *summary,
                          const char *files_touched,
                          const char *commit_hash);  // may be NULL

// Return file size in bytes (0 if not exists)
long perpetual_log_size(const char *log_path);
```

### src/perpetual/perpetual_prompt.h / perpetual_prompt.c

```c
// Build the system prompt string for perpetual mode root LLM.
// Caller must free the returned string.
char *perpetual_prompt_build(const char *log_path, long log_size_bytes);
```

### src/perpetual/perpetual_mode.h / perpetual_mode.c

```c
// Top-level entry point. Called from main() when KLAWED_PERPETUAL=1.
// query is the user's request (from CLI arg or stdin).
int perpetual_mode_run(ConversationState *state, const char *query);
```

Internal flow:
1. Resolve log path via `perpetual_log_get_path()`
2. Set `KLAWED_DISABLE_TOOLS` env to disable all tools except Bash
3. Build system prompt via `perpetual_prompt_build()`
4. Inject system prompt into `state`
5. Add user message
6. Run `call_api_with_retries()` loop (root LLM runs recon + spawn via Bash)
7. When LLM response contains no more tool calls, session is done
8. Parse the final assistant text for a `PERPETUAL_SUMMARY:` block
9. Call `perpetual_log_append()` to write the turn to the log
10. Return

## Environment Variables

| Var | Default | Description |
|-----|---------|-------------|
| `KLAWED_PERPETUAL` | — | Set to `1`/`true`/`yes` to enable perpetual mode |
| `KLAWED_PERPETUAL_FILE` | `.klawed/perpetual.md` | Path to the log file |

## Subtasks (for subagent delegation)

- [ ] **Task A**: `src/perpetual/perpetual_log.c/.h` — log file I/O
- [ ] **Task B**: `src/perpetual/perpetual_prompt.c/.h` — system prompt builder
- [ ] **Task C**: `src/perpetual/perpetual_mode.c/.h` — main entry point + integration
- [ ] **Task D**: `main()` integration in `src/klawed.c` + `Makefile` update
- [ ] **Task E**: `docs/perpetual-mode.md` — user documentation
- [ ] **Task F**: Build verification (`make` passes, no warnings)

## Build

Makefile addition:
```makefile
PERPETUAL_SRCS = src/perpetual/perpetual_log.c \
                 src/perpetual/perpetual_prompt.c \
                 src/perpetual/perpetual_mode.c
```
These are added to the main `SRCS` list alongside the other source files.

## Status

- [x] Plan written
- [ ] Task A: perpetual_log.c/.h ✅ done
- [ ] Task B: perpetual_prompt.c/.h ✅ done
- [ ] Task C: perpetual_mode.c/.h (in progress)
- [ ] Task D: main() + Makefile (in progress)
- [ ] Task E: docs
- [ ] Task F: build check
- [ ] **TODO**: Fix perpetual_mode.c KLAWED_DISABLE_TOOLS — allow Subagent, CheckSubagentProgress, InterruptSubagent, Sleep (not just Bash)
