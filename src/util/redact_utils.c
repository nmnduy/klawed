/*
 * redact_utils.c - Secret redaction for tool output and logs
 *
 * Inspired by hermes-agent (NousResearch/hermes-agent) agent/redact.py
 */

#include "redact_utils.h"
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <pthread.h>

/* ============================================================================
 * Masking helper
 * ========================================================================== */

/*
 * Write a masked version of [start, start+len) into buf (which must be large
 * enough).  Returns the number of bytes written (excluding NUL).
 *
 * Short tokens (< 18 chars): "***"
 * Longer tokens: first 6 chars + "..." + last 4 chars
 */
static int mask_token(const char *start, size_t len, char *buf, size_t buf_size) {
    if (len < 18) {
        return snprintf(buf, buf_size, "***");
    }
    /* first 6 ... last 4 */
    char prefix[7], suffix[5];
    size_t pfx = len < 6 ? len : 6;
    size_t sfx = len < 4 ? len : 4;
    memcpy(prefix, start, pfx); prefix[pfx] = '\0';
    memcpy(suffix, start + len - sfx, sfx); suffix[sfx] = '\0';
    return snprintf(buf, buf_size, "%s...%s", prefix, suffix);
}

/* ============================================================================
 * Pattern definitions
 * ========================================================================== */

/*
 * Each entry in the prefix table is a POSIX ERE pattern.
 * We compile them all once into a single alternation.
 */
static const char *PREFIX_PATTERNS[] = {
    "sk-[A-Za-z0-9_-]{10,}",           /* OpenAI / OpenRouter / Anthropic sk-ant-* */
    "ghp_[A-Za-z0-9]{10,}",            /* GitHub PAT classic */
    "github_pat_[A-Za-z0-9_]{10,}",    /* GitHub PAT fine-grained */
    "xox[baprs]-[A-Za-z0-9-]{10,}",    /* Slack */
    "AIza[A-Za-z0-9_-]{30,}",          /* Google API key */
    "pplx-[A-Za-z0-9]{10,}",           /* Perplexity */
    "fal_[A-Za-z0-9_-]{10,}",          /* Fal.ai */
    "fc-[A-Za-z0-9]{10,}",             /* Firecrawl */
    "bb_live_[A-Za-z0-9_-]{10,}",      /* BrowserBase */
    "AKIA[A-Z0-9]{16}",                /* AWS Access Key ID */
    "sk_live_[A-Za-z0-9]{10,}",        /* Stripe live */
    "sk_test_[A-Za-z0-9]{10,}",        /* Stripe test */
    "rk_live_[A-Za-z0-9]{10,}",        /* Stripe restricted */
    "SG\\.[A-Za-z0-9_-]{10,}",         /* SendGrid */
    "hf_[A-Za-z0-9]{10,}",             /* HuggingFace */
    "r8_[A-Za-z0-9]{10,}",             /* Replicate */
    "npm_[A-Za-z0-9]{10,}",            /* npm */
    "pypi-[A-Za-z0-9_-]{10,}",         /* PyPI */
    "dop_v1_[A-Za-z0-9]{10,}",         /* DigitalOcean PAT */
    "doo_v1_[A-Za-z0-9]{10,}",         /* DigitalOcean OAuth */
    NULL
};

/*
 * ENV assignment:
 *   group 1 = variable name
 *   group 2 = optional quote char
 *   group 3 = value
 */
#define ENV_ASSIGN_PAT \
    "([A-Za-z_][A-Za-z0-9_]*" \
    "(API_?KEY|TOKEN|SECRET|PASSWORD|PASSWD|CREDENTIAL|AUTH)" \
    "[A-Za-z0-9_]*)[ \t]*=[ \t]*(['\"]?)([^ \t\n'\"]{1,})"

/*
 * JSON field:
 *   group 1 = quoted field name
 *   group 2 = value
 */
#define JSON_FIELD_PAT \
    "(\"(api_?[Kk]ey|token|secret|password|access_token|refresh_token|auth_token|bearer)\")" \
    "[ \t]*:[ \t]*\"([^\"]{1,})\""

/*
 * Authorization header:
 *   group 1 = "Authorization: Bearer "
 *   group 2 = token
 */
#define AUTH_HEADER_PAT \
    "(Authorization:[ \t]*Bearer[ \t]+)([^ \t\n]{8,})"

/*
 * Telegram bot token:
 *   group 1 = optional "bot" prefix
 *   group 2 = digit string (chat id)
 *   group 3 = token portion
 */
#define TELEGRAM_PAT \
    "(bot)?([0-9]{8,}):([A-Za-z0-9_-]{30,})"

/*
 * Database connection string password:
 *   group 1 = scheme://user:
 *   group 2 = password
 *   group 3 = @
 */
#define DB_CONNSTR_PAT \
    "((postgres(ql)?|mysql|mongodb(\\+srv)?|redis|amqp)://[^:/@]+:)([^@/ \t\n]{1,})(@)"

/* ============================================================================
 * Compiled regex state (lazy initialised, thread-safe via pthread_once)
 * ========================================================================== */

typedef struct {
    regex_t prefix_re;
    regex_t env_re;
    regex_t json_re;
    regex_t auth_re;
    regex_t telegram_re;
    regex_t privkey_re;
    regex_t db_re;
    int ok;
} RedactPatterns;

static RedactPatterns g_pat;
static pthread_once_t g_pat_once = PTHREAD_ONCE_INIT;

static void build_prefix_pattern(char *buf, size_t buf_size) {
    size_t pos = 0;
    /* word-boundary negative look-around is not POSIX, so we rely on the
     * alternation order and word-char check in the replacement loop instead.
     * We wrap the whole alternation in a capture group. */
    pos += (size_t)snprintf(buf + pos, buf_size - pos, "(");
    for (int i = 0; PREFIX_PATTERNS[i] != NULL; i++) {
        if (i > 0)
            pos += (size_t)snprintf(buf + pos, buf_size - pos, "|");
        pos += (size_t)snprintf(buf + pos, buf_size - pos, "%s", PREFIX_PATTERNS[i]);
    }
    snprintf(buf + pos, buf_size - pos, ")");
}

static void init_patterns(void) {
    char prefix_pat[2048];
    build_prefix_pattern(prefix_pat, sizeof(prefix_pat));

    int ok = 1;
    ok &= (regcomp(&g_pat.prefix_re,  prefix_pat,    REG_EXTENDED) == 0);
    ok &= (regcomp(&g_pat.env_re,     ENV_ASSIGN_PAT, REG_EXTENDED | REG_ICASE) == 0);
    ok &= (regcomp(&g_pat.json_re,    JSON_FIELD_PAT, REG_EXTENDED | REG_ICASE) == 0);
    ok &= (regcomp(&g_pat.auth_re,    AUTH_HEADER_PAT, REG_EXTENDED | REG_ICASE) == 0);
    ok &= (regcomp(&g_pat.telegram_re, TELEGRAM_PAT,  REG_EXTENDED) == 0);
    ok &= (regcomp(&g_pat.privkey_re,
                   "-----BEGIN [A-Z ]*PRIVATE KEY-----",
                   REG_EXTENDED) == 0);
    ok &= (regcomp(&g_pat.db_re,      DB_CONNSTR_PAT, REG_EXTENDED | REG_ICASE) == 0);
    g_pat.ok = ok;
}

/* ============================================================================
 * Generic substitution helper
 *
 * Scan `src` for matches of `re`.  For each match call `replace_fn` which
 * appends the replacement to `*out`/`*out_len`/`*out_cap`.
 * Unmatchted spans are copied verbatim.
 * ========================================================================== */

typedef struct {
    char   *buf;
    size_t  len;
    size_t  cap;
} Buf;

static int buf_append(Buf *b, const char *data, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t new_cap = (b->cap + n + 1) * 2;
        char *nb = realloc(b->buf, new_cap);
        if (!nb) return -1;
        b->buf = nb;
        b->cap = new_cap;
    }
    memcpy(b->buf + b->len, data, n);
    b->len += n;
    b->buf[b->len] = '\0';
    return 0;
}

/*
 * replace_fn signature:
 *   src          - original text (full string)
 *   m            - array of regmatch_t from the match
 *   ngroups      - number of groups including group 0
 *   out          - buffer to append the replacement to
 * Returns 0 on success, -1 on failure.
 */
typedef int (*replace_fn_t)(const char *src, regmatch_t *m, size_t ngroups, Buf *out);

static int apply_pattern(const char *src, regex_t *re, size_t ngroups,
                         replace_fn_t fn, Buf *out) {
    regmatch_t *m = calloc(ngroups, sizeof(regmatch_t));
    if (!m) return -1;

    const char *cursor = src;
    int rc;

    while (*cursor) {
        rc = regexec(re, cursor, ngroups, m, 0);
        if (rc == REG_NOMATCH) {
            /* Copy rest verbatim */
            if (buf_append(out, cursor, strlen(cursor)) < 0) { free(m); return -1; }
            break;
        }
        /* Copy pre-match text */
        if (m[0].rm_so > 0) {
            if (buf_append(out, cursor, (size_t)m[0].rm_so) < 0) { free(m); return -1; }
        }
        /* Apply replacement */
        if (fn(cursor, m, ngroups, out) < 0) { free(m); return -1; }
        /* Advance past match */
        if (m[0].rm_eo == m[0].rm_so) {
            /* Zero-length match guard */
            if (buf_append(out, cursor, 1) < 0) { free(m); return -1; }
            cursor += 1;
        } else {
            cursor += m[0].rm_eo;
        }
    }

    free(m);
    return 0;
}

/* ============================================================================
 * Per-pattern replace functions
 * ========================================================================== */

/* Helper: check if the char before match is a word char (to avoid mid-word hits) */
static int is_word_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '-';
}

/* 1. Known prefix tokens — group 1 is the token */
static int replace_prefix(const char *src, regmatch_t *m, size_t ngroups, Buf *out) {
    (void)ngroups;
    /* Reject if immediately preceded by a word char */
    if (m[0].rm_so > 0 && is_word_char(src[m[0].rm_so - 1])) {
        return buf_append(out, src + m[0].rm_so,
                          (size_t)(m[0].rm_eo - m[0].rm_so));
    }
    /* Reject if immediately followed by a word char */
    if (src[m[0].rm_eo] != '\0' && is_word_char(src[m[0].rm_eo])) {
        return buf_append(out, src + m[0].rm_so,
                          (size_t)(m[0].rm_eo - m[0].rm_so));
    }
    /* group 1 = full token */
    const char *tok = src + m[1].rm_so;
    size_t toklen   = (size_t)(m[1].rm_eo - m[1].rm_so);
    char masked[64];
    mask_token(tok, toklen, masked, sizeof(masked));
    return buf_append(out, masked, strlen(masked));
}

/* 2. ENV assignments — NAME=value, keep name and optional quotes, mask value */
static int replace_env(const char *src, regmatch_t *m, size_t ngroups, Buf *out) {
    (void)ngroups;
    /* group 1 = full var name, group 3 = quote char, group 4 = value */
    /* We reconstruct: name + "=" + quote + masked + quote */

    /* full match is m[0]; groups: 1=name, 2=suffix-keyword (inside name), 3=quote, 4=value */
    const char *name  = src + m[1].rm_so;
    size_t name_len   = (size_t)(m[1].rm_eo - m[1].rm_so);
    const char *quote = src + m[3].rm_so;
    size_t quote_len  = (size_t)(m[3].rm_eo - m[3].rm_so);
    const char *val   = src + m[4].rm_so;
    size_t val_len    = (size_t)(m[4].rm_eo - m[4].rm_so);

    char masked[64];
    mask_token(val, val_len, masked, sizeof(masked));

    if (buf_append(out, name, name_len) < 0) return -1;
    if (buf_append(out, "=", 1) < 0) return -1;
    if (quote_len > 0 && buf_append(out, quote, quote_len) < 0) return -1;
    if (buf_append(out, masked, strlen(masked)) < 0) return -1;
    if (quote_len > 0 && buf_append(out, quote, quote_len) < 0) return -1;
    return 0;
}

/* 3. JSON fields — keep "fieldname": ", mask value, re-add closing " */
static int replace_json(const char *src, regmatch_t *m, size_t ngroups, Buf *out) {
    (void)ngroups;
    /* groups: 1=quoted field name (e.g. "apiKey"), 2=bare name, 3=value */
    const char *field = src + m[1].rm_so;
    size_t field_len  = (size_t)(m[1].rm_eo - m[1].rm_so);
    const char *val   = src + m[3].rm_so;
    size_t val_len    = (size_t)(m[3].rm_eo - m[3].rm_so);

    char masked[64];
    mask_token(val, val_len, masked, sizeof(masked));

    if (buf_append(out, field, field_len) < 0) return -1;
    if (buf_append(out, ": \"", 3) < 0) return -1;
    if (buf_append(out, masked, strlen(masked)) < 0) return -1;
    if (buf_append(out, "\"", 1) < 0) return -1;
    return 0;
}

/* 4. Authorization: Bearer <token> — keep prefix, mask token */
static int replace_auth(const char *src, regmatch_t *m, size_t ngroups, Buf *out) {
    (void)ngroups;
    /* group 1 = "Authorization: Bearer ", group 2 = token */
    const char *prefix = src + m[1].rm_so;
    size_t prefix_len  = (size_t)(m[1].rm_eo - m[1].rm_so);
    const char *tok    = src + m[2].rm_so;
    size_t tok_len     = (size_t)(m[2].rm_eo - m[2].rm_so);

    char masked[64];
    mask_token(tok, tok_len, masked, sizeof(masked));

    if (buf_append(out, prefix, prefix_len) < 0) return -1;
    return buf_append(out, masked, strlen(masked));
}

/* 5. Telegram tokens — keep bot prefix and digits, mask token part */
static int replace_telegram(const char *src, regmatch_t *m, size_t ngroups, Buf *out) {
    (void)ngroups;
    /* group 1 = "bot" (may be empty), group 2 = digits, group 3 = token */
    if (m[1].rm_so >= 0 && m[1].rm_eo > m[1].rm_so) {
        if (buf_append(out, src + m[1].rm_so,
                       (size_t)(m[1].rm_eo - m[1].rm_so)) < 0) return -1;
    }
    /* digits */
    if (buf_append(out, src + m[2].rm_so,
                   (size_t)(m[2].rm_eo - m[2].rm_so)) < 0) return -1;
    if (buf_append(out, ":***", 4) < 0) return -1;
    return 0;
}

/* 7. DB connection string passwords — keep scheme://user:, mask password, keep @ */
static int replace_db(const char *src, regmatch_t *m, size_t ngroups, Buf *out) {
    (void)ngroups;
    /* group 1 = "scheme://user:", group 5 = password, group 6 = "@" */
    const char *prefix = src + m[1].rm_so;
    size_t prefix_len  = (size_t)(m[1].rm_eo - m[1].rm_so);

    if (buf_append(out, prefix, prefix_len) < 0) return -1;
    if (buf_append(out, "***", 3) < 0) return -1;
    if (buf_append(out, "@", 1) < 0) return -1;
    return 0;
}

/* ============================================================================
 * Private key block: we need a two-pass approach since POSIX regex is not
 * DOTALL.  We replace BEGIN...END blocks by scanning the string manually.
 * ========================================================================== */

static int redact_private_keys(const char *src, Buf *out) {
    const char *begin_marker = "-----BEGIN";
    const char *end_marker   = "-----END";
    const char *private_kw   = "PRIVATE KEY-----";

    const char *cursor = src;
    while (*cursor) {
        const char *begin = strstr(cursor, begin_marker);
        if (!begin) {
            return buf_append(out, cursor, strlen(cursor));
        }
        /* Check that "PRIVATE KEY-----" appears somewhere after BEGIN */
        const char *pk = strstr(begin, private_kw);
        if (!pk) {
            /* Not a private key block — copy up to and including begin_marker */
            size_t copy_len = (size_t)(begin - cursor) + strlen(begin_marker);
            if (buf_append(out, cursor, copy_len) < 0) return -1;
            cursor = begin + strlen(begin_marker);
            continue;
        }
        /* Find matching END block */
        const char *end = strstr(pk, end_marker);
        if (!end) {
            /* No closing marker — copy rest verbatim */
            return buf_append(out, cursor, strlen(cursor));
        }
        /* Advance end past "-----END...-----" (find closing -----)*/
        const char *end_close = strstr(end, "-----");
        if (end_close) {
            end_close += 5; /* past the five dashes */
        } else {
            end_close = end + strlen(end_marker);
        }
        /* Copy pre-block text */
        if (buf_append(out, cursor, (size_t)(begin - cursor)) < 0) return -1;
        /* Emit replacement */
        if (buf_append(out, "[REDACTED PRIVATE KEY]", 22) < 0) return -1;
        cursor = end_close;
    }
    return 0;
}

/* ============================================================================
 * Public API
 * ========================================================================== */

char *redact_sensitive_text(const char *text) {
    if (!text || !*text) return NULL;

    /* Check opt-out env var */
    const char *opt = getenv("KLAWED_REDACT_SECRETS");
    if (opt) {
        if (strcasecmp(opt, "0") == 0 || strcasecmp(opt, "false") == 0 ||
            strcasecmp(opt, "no") == 0 || strcasecmp(opt, "off") == 0) {
            char *copy = strdup(text);
            return copy;
        }
    }

    /* Ensure patterns are compiled */
    pthread_once(&g_pat_once, init_patterns);
    if (!g_pat.ok) {
        /* Compilation failed; return copy unmodified */
        return strdup(text);
    }

    /* We run each pass on the output of the previous pass.
     * We ping-pong between two Buf objects to avoid copying. */

    Buf a = {0}, b = {0};
    /* Initial capacity: 1.25x input length */
    size_t init_cap = strlen(text) * 5 / 4 + 256;
    a.buf = malloc(init_cap); if (!a.buf) return NULL;
    a.cap = init_cap;
    a.len = 0;

#define PASS(re_ptr, ngroups, fn) \
    do { \
        b.len = 0; \
        if (!b.buf) { b.buf = malloc(a.cap + 256); b.cap = a.cap + 256; } \
        if (!b.buf) goto fail; \
        if (apply_pattern(a.buf, (re_ptr), (ngroups), (fn), &b) < 0) goto fail; \
        /* swap a <-> b */ \
        { char *tmp = a.buf; size_t tl = a.len, tc = a.cap; \
          a.buf = b.buf; a.len = b.len; a.cap = b.cap; \
          b.buf = tmp;   b.len = tl;   b.cap = tc; } \
    } while(0)

    /* Seed a with the input text */
    if (buf_append(&a, text, strlen(text)) < 0) goto fail;

    /* Pass 1: known API key prefixes */
    PASS(&g_pat.prefix_re, 2, replace_prefix);

    /* Pass 2: ENV assignments */
    PASS(&g_pat.env_re, 5, replace_env);

    /* Pass 3: JSON fields */
    PASS(&g_pat.json_re, 4, replace_json);

    /* Pass 4: Authorization headers */
    PASS(&g_pat.auth_re, 3, replace_auth);

    /* Pass 5: Telegram bot tokens */
    PASS(&g_pat.telegram_re, 4, replace_telegram);

    /* Pass 6: DB connection string passwords */
    PASS(&g_pat.db_re, 7, replace_db);

    /* Pass 7: Private key blocks (multi-line, manual scan) */
    b.len = 0;
    if (!b.buf) { b.buf = malloc(a.cap + 256); b.cap = a.cap + 256; }
    if (!b.buf) goto fail;
    if (redact_private_keys(a.buf, &b) < 0) goto fail;
    /* swap */
    { char *tmp = a.buf; a.buf = b.buf; a.len = b.len; a.cap = b.cap;
      b.buf = tmp; }

#undef PASS

    free(b.buf);
    return a.buf;   /* caller must free */

fail:
    free(a.buf);
    free(b.buf);
    return NULL;
}
