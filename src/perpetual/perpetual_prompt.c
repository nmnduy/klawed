#include "perpetual_prompt.h"

#include <bsd/stdlib.h>
#include <bsd/string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_BUF 4096

/* Append src into *buf (capacity *cap, current length *len).
 * Grows the buffer with reallocarray if needed.
 * Returns 0 on success, -1 on OOM. */
static int buf_append(char **buf, size_t *cap, size_t *len, const char *src)
{
    size_t src_len = strlen(src);
    size_t needed = *len + src_len + 1;

    if (needed > *cap) {
        size_t new_cap = *cap;
        while (new_cap < needed)
            new_cap *= 2;
        char *tmp = reallocarray(*buf, new_cap, 1);
        if (!tmp)
            return -1;
        *buf = tmp;
        *cap = new_cap;
    }

    strlcpy(*buf + *len, src, *cap - *len);
    *len += src_len;
    return 0;
}

/* Append a formatted string; returns 0 on success, -1 on OOM. */
static int buf_appendf(char **buf, size_t *cap, size_t *len,
                       const char *fmt, ...)
{
    char tmp[1024] = {0};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    return buf_append(buf, cap, len, tmp);
}

/* Phase 1+2 recon/extract instructions (only when log exists). */
static int append_recon_phases(char **buf, size_t *cap, size_t *len,
                                const char *log_path)
{
    int rc = 0;
    rc |= buf_append(buf, cap, len,
        "## Phase 1 — Recon (log exists, run these bash commands first)\n\n");
    rc |= buf_appendf(buf, cap, len,
        "1. Check log size:\n"
        "   bash: wc -l %s\n\n", log_path);
    rc |= buf_appendf(buf, cap, len,
        "2. List recent sessions:\n"
        "   bash: grep -n \"^## \\[\" %s | tail -20\n\n", log_path);
    rc |= buf_append(buf, cap, len,
        "3. Search for relevant prior work using keywords from the current\n"
        "   request:\n");
    rc |= buf_appendf(buf, cap, len,
        "   bash: grep -n \"<keywords>\" %s\n\n", log_path);
    rc |= buf_append(buf, cap, len,
        "## Phase 2 — Extract (pull only the relevant blocks)\n\n");
    rc |= buf_appendf(buf, cap, len,
        "For each relevant line range found above:\n"
        "   bash: sed -n '<start>,<end>p' %s\n\n"
        "Read only the blocks that are directly useful for the current\n"
        "request. Do NOT read the entire log.\n\n", log_path);
    return rc;
}

/* Phase 3 act instructions. */
static int append_act_phase(char **buf, size_t *cap, size_t *len)
{
    int rc = 0;
    rc |= buf_append(buf, cap, len,
        "## Phase 3 — Act\n\n"
        "Spawn one or more subagents to carry out the work. Include all\n"
        "relevant context you extracted from the log inside the subagent\n"
        "prompt so it does not need to re-read it.\n\n"
        "Preferred method — use the Subagent tool (supports multi-line prompts,\n"
        "automatic log file tracking, and CheckSubagentProgress polling):\n"
        "   Subagent(prompt=\"<enriched prompt with context>\")\n\n"
        "Alternative — exec klawed directly from bash:\n"
        "   bash: KLAWED_IS_SUBAGENT=1 klawed \"<enriched prompt with context>\"\n\n"
        "Use Sleep between CheckSubagentProgress polls to avoid busy-waiting.\n\n"
        "The subagent prompt MUST include:\n"
        "  - The concrete task to perform\n"
        "  - Any extracted context from prior sessions that is relevant\n"
        "  - Expected output / success criteria\n\n");
    return rc;
}

/* Phase 4 done/summary instructions. */
static int append_done_phase(char **buf, size_t *cap, size_t *len)
{
    return buf_append(buf, cap, len,
        "## Phase 4 — Done\n\n"
        "After all subagents complete (or if no work was needed), end your\n"
        "final response with EXACTLY this block — no extra blank lines\n"
        "inside it, no trailing spaces:\n\n"
        "PERPETUAL_SUMMARY:\n"
        "Request: <one-line description of what was requested>\n"
        "Summary: <what was actually done; one or two sentences>\n"
        "Files: <comma-separated list of changed files, or \"none\">\n"
        "Commit: <git commit hash if a commit was made, or \"none\">\n"
        "END_PERPETUAL_SUMMARY\n\n"
        "The perpetual mode runtime parses this block to update the log.\n"
        "If the block is missing or malformed the session will be recorded\n"
        "as failed, so emit it every time.\n");
}

char *perpetual_prompt_build(const char *log_path, long log_size_bytes)
{
    if (!log_path)
        return NULL;

    size_t cap = INITIAL_BUF;
    size_t len = 0;
    char *buf = reallocarray(NULL, cap, 1);
    if (!buf)
        return NULL;
    buf[0] = '\0';

    int rc = 0;

    rc |= buf_append(&buf, &cap, &len,
        "You are the perpetual-mode orchestrator for klawed.\n"
        "You have ONE tool: Bash. Use it to inspect the perpetual log,\n"
        "extract context, and spawn subagents that do the real work.\n"
        "Follow the phases below in order. Be concise — do not produce\n"
        "large blocks of explanation; act instead.\n\n");

    rc |= buf_appendf(&buf, &cap, &len,
        "Log file: %s\n"
        "Log size: %ld bytes\n\n",
        log_path, log_size_bytes);

    if (log_size_bytes > 0)
        rc |= append_recon_phases(&buf, &cap, &len, log_path);
    else
        rc |= buf_append(&buf, &cap, &len,
            "## Phase 1+2 — Recon/Extract (skipped — log is empty)\n\n"
            "No prior history exists yet. Proceed directly to Phase 3.\n\n");

    rc |= append_act_phase(&buf, &cap, &len);
    rc |= append_done_phase(&buf, &cap, &len);

    if (rc != 0) {
        free(buf);
        return NULL;
    }

    return buf;
}
