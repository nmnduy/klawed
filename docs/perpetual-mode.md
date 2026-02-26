# Perpetual Mode

Perpetual mode is a stateless-but-persistent execution mode for klawed, inspired
by the [Recursive Language Model (RLM) paper](https://arxiv.org/abs/2512.24601v1).

Every session starts fresh — no in-memory conversation history is carried over.
Instead, a growing `perpetual.md` log file acts as external memory. The root LLM
uses tools to search and extract relevant context from this file, spawns subagents
to do the actual work, and appends a structured summary back to the log when done.

## The Key Idea

The LLM never reads the entire log raw. It *programs its way* through it:

```
bash: grep -n "auth module" .klawed/perpetual.md
bash: sed -n '42,67p' .klawed/perpetual.md   # read only the matching block
```

This means a 50,000-line log costs the same to query as a 500-line one.

## How It Works

Each request follows four phases:

**Phase 1 — Recon** (if log has history):
- Check log size and structure
- Grep for recent sessions and keyword matches to the current request

**Phase 2 — Extract**:
- Read only the relevant line ranges from the log
- Build a picture of prior work relevant to this task

**Phase 3 — Act**:
- Spawn one or more subagents with context-enriched prompts
- Monitor subagent progress, wait, collect results

**Phase 4 — Done**:
- Emit a `PERPETUAL_SUMMARY:` block in the final response
- The runtime parses this and appends a structured entry to `perpetual.md`

## Root LLM Tool Set

The root LLM in perpetual mode has exactly these tools:

| Tool | Purpose |
|------|---------|
| `Bash` | Recon the log (grep, sed, wc), exec child processes |
| `Subagent` | Spawn a full klawed agent to do the actual work |
| `CheckSubagentProgress` | Poll a running subagent's log |
| `InterruptSubagent` | Stop a stuck subagent |
| `Sleep` | Wait between progress checks |

All other tools (Read, Write, Edit, Glob, Grep, TodoWrite, Memory*, etc.) are
disabled. The root LLM is an orchestrator, not a worker — subagents do the work.

## perpetual.md Format

The log file is append-only markdown. Each entry:

```markdown
## [2026-02-26 14:03] Session abc123
**Request:** refactor the auth module
**Summary:** extracted 3 functions into auth_utils.c, updated all call sites
**Files:** src/auth.c, src/auth_utils.c
**Commit:** a3f1c2d

---

```

Fields:
- **Request** — the user's original query (truncated to 200 chars)
- **Summary** — what was actually done (one or two sentences)
- **Files** — comma-separated list of changed files (omitted if none)
- **Commit** — git commit hash (omitted if no commit was made)

## Usage

### Enable via environment variable

```bash
KLAWED_PERPETUAL=1 klawed "add rate limiting to the API"
```

### Enable via CLI flag

```bash
klawed --perpetual "add rate limiting to the API"
```

### Custom log file path

```bash
KLAWED_PERPETUAL_FILE=/path/to/my-project.md klawed --perpetual "..."
```

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `KLAWED_PERPETUAL` | — | Set to `1`, `true`, or `yes` to enable |
| `KLAWED_PERPETUAL_FILE` | `.klawed/perpetual.md` | Path to the log file |

## Implementation

```
src/perpetual/
  perpetual_mode.c    — top-level entry point, API loop, summary parser
  perpetual_mode.h
  perpetual_log.c     — append-only log file I/O
  perpetual_log.h
  perpetual_prompt.c  — system prompt builder
  perpetual_prompt.h
```

The `PERPETUAL_SUMMARY:` block that the LLM must emit:

```
PERPETUAL_SUMMARY:
Request: <one-line description>
Summary: <what was done>
Files: <comma-separated list, or "none">
Commit: <hash, or "none">
END_PERPETUAL_SUMMARY
```

If this block is missing from the final response, the session is still recorded
with the original query as request and a generic "no summary provided" message.

## Comparison to Normal Modes

| | Interactive | Oneshot | Perpetual |
|---|---|---|---|
| Memory | In-session only | In-session only | Persistent log file |
| Context across sessions | ❌ | ❌ | ✅ via perpetual.md |
| Start fresh each request | — | ✅ | ✅ |
| Root LLM role | Worker | Worker | Orchestrator |
| Subagents | Optional | Optional | Primary workers |
| Good for | Exploration | Scripting | Long-running projects |
