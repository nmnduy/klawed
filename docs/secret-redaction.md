# Secret Redaction — Hiding Secrets from the LLM

Inspired by [hermes-agent](https://github.com/NousResearch/hermes-agent)'s `agent/redact.py`.

## Problem

The LLM can run arbitrary shell commands. Commands like `env`, `printenv`, `cat .env`, or
`cat ~/.aws/credentials` will dump secrets into tool output, which then go straight into
the LLM context window — and from there into logs, trajectories, and any model that sees
the conversation.

## Solution

Intercept tool output at the boundary — before it's returned to the LLM — and apply regex
redaction to mask known secret patterns.

In klawed, the right place is in `tool_bash.c`, right before `cJSON_AddStringToObject(result, "output", ...)`.
An equivalent pass should also run on file-read output (`tool_filesystem.c`) for any file
that might contain credentials.

---

## What to Redact

### 1. Known API key prefixes

Match the prefix + contiguous token characters. Mask the full value for short tokens
(`< 18 chars → "***"`), or preserve first-6/last-4 for longer ones (`"sk-pro...l012"`).

| Pattern | Service |
|---------|---------|
| `sk-[A-Za-z0-9_-]{10,}` | OpenAI / OpenRouter / Anthropic (`sk-ant-*`) |
| `ghp_[A-Za-z0-9]{10,}` | GitHub PAT (classic) |
| `github_pat_[A-Za-z0-9_]{10,}` | GitHub PAT (fine-grained) |
| `xox[baprs]-[A-Za-z0-9-]{10,}` | Slack tokens |
| `AIza[A-Za-z0-9_-]{30,}` | Google API keys |
| `pplx-[A-Za-z0-9]{10,}` | Perplexity |
| `fal_[A-Za-z0-9_-]{10,}` | Fal.ai |
| `fc-[A-Za-z0-9]{10,}` | Firecrawl |
| `AKIA[A-Z0-9]{16}` | AWS Access Key ID |
| `sk_live_[A-Za-z0-9]{10,}` | Stripe secret (live) |
| `sk_test_[A-Za-z0-9]{10,}` | Stripe secret (test) |
| `SG\.[A-Za-z0-9_-]{10,}` | SendGrid |
| `hf_[A-Za-z0-9]{10,}` | HuggingFace |
| `r8_[A-Za-z0-9]{10,}` | Replicate |
| `npm_[A-Za-z0-9]{10,}` | npm access token |
| `pypi-[A-Za-z0-9_-]{10,}` | PyPI token |

Word-boundary anchors on both sides (`(?<![A-Za-z0-9_-])` … `(?![A-Za-z0-9_-])`) prevent
false positives inside larger identifiers.

### 2. ENV assignments

```
([A-Z_]*(?:API_?KEY|TOKEN|SECRET|PASSWORD|PASSWD|CREDENTIAL|AUTH)[A-Z_]*)\s*=\s*(['"]?)(\S+)\2
```

Catches `OPENAI_API_KEY=sk-…`, `MY_SECRET_TOKEN="supersecretvalue"`, etc.  
Preserves the variable name and quotes; masks only the value.  
Does **not** touch `HOME=/home/user`, `PATH=…`, etc.

### 3. JSON fields

```
("(?:api_?[Kk]ey|token|secret|password|access_token|refresh_token|auth_token|bearer)")\s*:\s*"([^"]+)"
```

Catches `{"apiKey": "sk-…"}`, `{"access_token": "eyJ…"}`, etc.

### 4. Authorization headers

```
(Authorization:\s*Bearer\s+)(\S+)
```

Case-insensitive. Keeps the `Authorization: Bearer ` prefix, masks the token.

### 5. Telegram bot tokens

```
(bot)?(\d{8,}):([-A-Za-z0-9_]{30,})
```

Replaces with `<digits>:***`.

### 6. Private key PEM blocks

```
-----BEGIN[A-Z ]*PRIVATE KEY-----[\s\S]*?-----END[A-Z ]*PRIVATE KEY-----
```

Replaces the entire block with `[REDACTED PRIVATE KEY]`.

### 7. Database connection string passwords

```
((?:postgres(?:ql)?|mysql|mongodb(?:\+srv)?|redis|amqp)://[^:]+:)([^@]+)(@)
```

Replaces the password portion with `***`, keeps protocol/user/host intact.

### 8. E.164 phone numbers

```
(\+[1-9]\d{6,14})(?![A-Za-z0-9])
```

Partial mask: `+1234****5678`. Negative lookahead prevents matching hex strings.

---

## Masking Logic

```c
// Short tokens are fully hidden; longer tokens keep prefix+suffix for debuggability.
// if len < 18:  "***"
// else:         first 6 chars + "..." + last 4 chars
```

---

## Integration Points in klawed

### Primary: `src/tools/tool_bash.c`

After `execute_command_with_timeout()` returns, before building the result JSON:

```c
// Redact secrets from command output (catches env/printenv/cat .env leaking keys)
char *redacted_output = redact_sensitive_text(clean_output ? clean_output : (output ? output : ""));
cJSON_AddStringToObject(result, "output", redacted_output ? redacted_output : "");
free(redacted_output);
```

### Secondary: `src/tools/tool_filesystem.c` (Read tool)

Apply the same pass to file content before returning — catches `.env`, `credentials`,
`config.yaml` containing inline secrets, etc.

### Logging: `src/logger.c`

Wrap the final formatted log string through `redact_sensitive_text()` in the log output
function so secrets never appear in `~/.klawed/logs/` or stderr even in verbose mode.

---

## Toggle

Controlled by `KLAWED_REDACT_SECRETS` env var (check `"0"`, `"false"`, `"no"`, `"off"`
→ skip redaction). Default: **on**.

---

## Implementation: `src/util/redact_utils.c` + `src/util/redact_utils.h`

Suggested module layout:

```c
// redact_utils.h

/**
 * Apply all secret-redaction patterns to a block of text.
 * Returns a newly allocated string with secrets masked.
 * Safe to call on any text — non-matching content passes through unchanged.
 * Returns NULL on allocation failure (caller should use original text).
 * Caller must free() the returned string.
 *
 * Disabled when KLAWED_REDACT_SECRETS=0|false|no|off.
 */
char *redact_sensitive_text(const char *text);
```

Use compiled `regex.h` (`regcomp` / `regexec`) patterns — compile once at program start
(or on first call with a `static int initialized` guard) to avoid recompiling on every
tool call.

---

## What to NOT Redact

- Short numeric strings that collide with phone patterns inside code (use the negative
  lookahead `(?![A-Za-z0-9])` carefully)
- Normal environment variables: `HOME`, `PATH`, `SHELL`, `USER`, `TERM`, `LANG`, etc.
- URLs without embedded credentials: `https://api.openai.com/v1/…`

Run the hermes-agent test suite cases (`tests/agent/test_redact.py`) mentally against
your implementation as a sanity check — it has good coverage of passthrough cases.
