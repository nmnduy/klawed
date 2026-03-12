/*
 * test_redact.c - Comprehensive unit tests for redact_sensitive_text()
 *
 * Tests every pattern category in redact_utils.c:
 *   1. Known API key prefixes  (sk-, ghp_, AKIA, hf_, etc.)
 *   2. ENV variable assignments (SOME_API_KEY=value)
 *   3. JSON secret fields      ("token": "value")
 *   4. Authorization headers   (Authorization: Bearer <token>)
 *   5. Telegram bot tokens     (1234567890:AABBCCDDeeffgg...)
 *   6. DB connection strings   (postgres://user:pass@host)
 *   7. Private key PEM blocks  (-----BEGIN RSA PRIVATE KEY-----)
 *   8. Null / empty / no-match passthrough
 *   9. opt-out via KLAWED_REDACT_SECRETS=0
 *
 * Build: make test-redact
 * Run:   ./build/test_redact
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../src/util/redact_utils.h"

/* ============================================================================
 * Tiny test framework
 * ========================================================================== */

#define COLOR_RESET  "\033[0m"
#define COLOR_GREEN  "\033[32m"
#define COLOR_RED    "\033[31m"
#define COLOR_CYAN   "\033[36m"
#define COLOR_YELLOW "\033[33m"

static int g_run    = 0;
static int g_passed = 0;
static int g_failed = 0;

static void check(const char *name, int cond) {
    g_run++;
    if (cond) {
        g_passed++;
        printf(COLOR_GREEN "✓ PASS" COLOR_RESET " %s\n", name);
    } else {
        g_failed++;
        printf(COLOR_RED   "✗ FAIL" COLOR_RESET " %s\n", name);
    }
}

/* Returns 1 if result does NOT contain needle. */
static int not_contains(const char *haystack, const char *needle) {
    return strstr(haystack, needle) == NULL;
}

/* Returns 1 if result DOES contain needle. */
static int contains(const char *haystack, const char *needle) {
    return strstr(haystack, needle) != NULL;
}

/* ============================================================================
 * Helper: call redact and assert raw secret is gone, label is present
 * ========================================================================== */
static char *redact(const char *input) {
    char *r = redact_sensitive_text(input);
    /* If redaction failed (NULL) return a copy of input so tests still run */
    if (!r) return strdup(input);
    return r;
}

/* ============================================================================
 * 1. NULL / empty / innocuous passthrough
 * ========================================================================== */
static void test_null_and_empty(void) {
    printf(COLOR_CYAN "\n--- null / empty / passthrough ---\n" COLOR_RESET);

    char *r;

    /* NULL input → NULL (not a crash) */
    r = redact_sensitive_text(NULL);
    check("null input returns NULL", r == NULL);

    /* Empty string → NULL (no-op) */
    r = redact_sensitive_text("");
    check("empty string returns NULL", r == NULL);

    /* Plain text with no secrets → unchanged */
    r = redact("Hello, world! No secrets here.");
    check("plain text unchanged", strcmp(r, "Hello, world! No secrets here.") == 0);
    free(r);
}

/* ============================================================================
 * 2. Known API key prefix patterns
 * ========================================================================== */
static void test_prefix_patterns(void) {
    printf(COLOR_CYAN "\n--- API key prefix patterns ---\n" COLOR_RESET);

    char *r;

    /* OpenAI key (long enough to get first6...last4) */
    r = redact("key=sk-abcdefghijklmnopqrstuvwxyz1234");
    check("openai sk- key: raw secret gone",  not_contains(r, "sk-abcdefghijklmnopqrstuvwxyz1234"));
    check("openai sk- key: prefix visible",    contains(r, "sk-abc"));
    free(r);

    /* Short key (<18 chars) → *** */
    r = redact("sk-shortkey123");
    check("short sk- (<18): replaced with ***", contains(r, "***"));
    check("short sk- (<18): raw secret gone",   not_contains(r, "sk-shortkey123"));
    free(r);

    /* Anthropic-style key */
    r = redact("Authorization: sk-ant-api03-LONGKEYVALUE0123456789ABCDEF");
    check("anthropic sk-ant-: raw gone", not_contains(r, "sk-ant-api03-LONGKEYVALUE0123456789ABCDEF"));
    free(r);

    /* GitHub classic PAT */
    r = redact("token=ghp_abcdefghijklmnopqrstuvwxyz01234567");
    check("github ghp_: raw secret gone", not_contains(r, "ghp_abcdefghijklmnopqrstuvwxyz01234567"));
    free(r);

    /* GitHub fine-grained PAT */
    r = redact("github_pat_11ABCDEF0123456789_abcdefgh_ijklmnopqrstuvwxyz01234567890ABCDEF");
    check("github_pat_: raw secret gone",
          not_contains(r, "github_pat_11ABCDEF0123456789_abcdefgh_ijklmnopqrstuvwxyz01234567890ABCDEF"));
    free(r);

    /* AWS Access Key ID */
    r = redact("access_key=AKIAIOSFODNN7EXAMPLE");
    check("AWS AKIA: raw secret gone", not_contains(r, "AKIAIOSFODNN7EXAMPLE"));
    free(r);

    /* Stripe live */
    r = redact("EXAMPLE_STRIPE_LIVE_KEY");
    check("stripe sk_live_: raw secret gone", not_contains(r, "EXAMPLE_STRIPE_LIVE_KEY"));
    free(r);

    /* Stripe test */
    r = redact("EXAMPLE_STRIPE_TEST_KEY");
    check("stripe sk_test_: raw secret gone", not_contains(r, "EXAMPLE_STRIPE_TEST_KEY"));
    free(r);

    /* HuggingFace */
    r = redact("hf_abcdefghijklmnopqrstuvwxyz012345");
    check("huggingface hf_: raw secret gone", not_contains(r, "hf_abcdefghijklmnopqrstuvwxyz012345"));
    free(r);

    /* Replicate */
    r = redact("r8_abcdefghijklmnopqrstuvwxyz012345");
    check("replicate r8_: raw secret gone", not_contains(r, "r8_abcdefghijklmnopqrstuvwxyz012345"));
    free(r);

    /* npm token */
    r = redact("npm_abcdefghijklmnopqrstuvwxyz01234");
    check("npm npm_: raw secret gone", not_contains(r, "npm_abcdefghijklmnopqrstuvwxyz01234"));
    free(r);

    /* Slack */
    r = redact("EXAMPLE_SLACK_TOKEN");
    check("slack xoxb-: raw secret gone", not_contains(r, "EXAMPLE_SLACK_TOKEN"));
    free(r);

    /* DigitalOcean */
    r = redact("dop_v1_abcdefghijklmnopqrstuvwxyz0123");
    check("digitalocean dop_v1_: raw secret gone", not_contains(r, "dop_v1_abcdefghijklmnopqrstuvwxyz0123"));
    free(r);

    /* Mask format: long tokens show first6...last4 */
    r = redact("sk-abcdefghijklmnopqrstuvwxyz1234");
    /* first 6 of the token starting at "sk-abc" */
    check("mask format: prefix visible",  contains(r, "sk-abc"));
    /* last 4 chars of the token */
    check("mask format: suffix visible",  contains(r, "1234"));
    check("mask format: ellipsis present", contains(r, "..."));
    free(r);
}

/* ============================================================================
 * 3. ENV variable assignments
 * ========================================================================== */
static void test_env_patterns(void) {
    printf(COLOR_CYAN "\n--- ENV assignment patterns ---\n" COLOR_RESET);

    char *r;

    /* Unquoted */
    r = redact("OPENAI_API_KEY=supersecretvalue123");
    check("env unquoted: var name kept",    contains(r, "OPENAI_API_KEY"));
    check("env unquoted: raw value gone",   not_contains(r, "supersecretvalue123"));
    free(r);

    /* Single-quoted */
    r = redact("SOME_TOKEN='mysecrettoken999'");
    check("env single-quoted: var name kept",  contains(r, "SOME_TOKEN"));
    check("env single-quoted: raw value gone", not_contains(r, "mysecrettoken999"));
    free(r);

    /* Double-quoted */
    r = redact("DB_PASSWORD=\"p@ssw0rd!\"");
    check("env double-quoted: var name kept",  contains(r, "DB_PASSWORD"));
    check("env double-quoted: raw value gone", not_contains(r, "p@ssw0rd!"));
    free(r);

    /* export prefix in shell script */
    r = redact("export API_SECRET=topsecret1234abcd");
    check("env export: raw value gone", not_contains(r, "topsecret1234abcd"));
    free(r);

    /* Unrelated var — should NOT be redacted */
    r = redact("HOME=/home/user");
    check("env non-secret var: unchanged", strcmp(r, "HOME=/home/user") == 0);
    free(r);
}

/* ============================================================================
 * 4. JSON secret fields
 * ========================================================================== */
static void test_json_patterns(void) {
    printf(COLOR_CYAN "\n--- JSON field patterns ---\n" COLOR_RESET);

    char *r;

    /* api_key */
    r = redact("{\"api_key\": \"supersecretapikey123\"}");
    check("json api_key: field name kept",   contains(r, "api_key"));
    check("json api_key: raw value gone",    not_contains(r, "supersecretapikey123"));
    free(r);

    /* token */
    r = redact("{\"token\": \"mytoken1234567890abcdef\"}");
    check("json token: field name kept",  contains(r, "token"));
    check("json token: raw value gone",   not_contains(r, "mytoken1234567890abcdef"));
    free(r);

    /* password */
    r = redact("{\"password\": \"hunter2supersecret\"}");
    check("json password: raw value gone", not_contains(r, "hunter2supersecret"));
    free(r);

    /* access_token */
    r = redact("{\"access_token\": \"ya29.c.abcdefghij-LONGTOKEN\"}");
    check("json access_token: raw value gone", not_contains(r, "ya29.c.abcdefghij-LONGTOKEN"));
    free(r);

    /* Non-secret field — should NOT be redacted */
    r = redact("{\"username\": \"alice\"}");
    check("json non-secret field: unchanged", contains(r, "alice"));
    free(r);
}

/* ============================================================================
 * 5. Authorization headers
 * ========================================================================== */
static void test_auth_header_patterns(void) {
    printf(COLOR_CYAN "\n--- Authorization header patterns ---\n" COLOR_RESET);

    char *r;

    /* Bearer token (long) */
    r = redact("Authorization: Bearer eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.longpayload");
    check("auth bearer: prefix kept",    contains(r, "Authorization: Bearer"));
    check("auth bearer: raw token gone", not_contains(r, "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.longpayload"));
    free(r);

    /* Case-insensitive */
    r = redact("authorization: bearer ABCDEFGHIJKLMNOPQRSTUVWXYZ012345");
    check("auth bearer case-insensitive: raw token gone",
          not_contains(r, "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345"));
    free(r);

    /* Short token (<8 chars) should NOT be redacted by auth pattern */
    r = redact("Authorization: Bearer short");
    check("auth bearer short token: not redacted", contains(r, "short"));
    free(r);
}

/* ============================================================================
 * 6. Telegram bot tokens
 * ========================================================================== */
static void test_telegram_patterns(void) {
    printf(COLOR_CYAN "\n--- Telegram bot token patterns ---\n" COLOR_RESET);

    char *r;

    /* Standard bot token */
    r = redact("bot123456789:AABBCCDDeeffgghhiijjkkllmmnnooppqq");
    check("telegram bot: chat id kept",   contains(r, "123456789"));
    check("telegram bot: token gone",     not_contains(r, "AABBCCDDeeffgghhiijjkkllmmnnooppqq"));
    check("telegram bot: replaced w ***", contains(r, ":***"));
    free(r);

    /* Without "bot" prefix (bare numeric:token) */
    r = redact("1234567890:ZZYYXXWWVVUUTTSSRRQQPPOONNMMLLKKjj");
    check("telegram bare: token gone",   not_contains(r, "ZZYYXXWWVVUUTTSSRRQQPPOONNMMLLKKjj"));
    free(r);
}

/* ============================================================================
 * 7. Database connection string passwords
 * ========================================================================== */
static void test_db_connstr_patterns(void) {
    printf(COLOR_CYAN "\n--- DB connection string patterns ---\n" COLOR_RESET);

    char *r;

    /* PostgreSQL */
    r = redact("postgres://admin:p@ssw0rd_secret@db.example.com:5432/mydb");
    check("pg connstr: scheme/user kept",  contains(r, "postgres://admin:"));
    check("pg connstr: password gone",     not_contains(r, "p@ssw0rd_secret"));
    check("pg connstr: @ kept",            contains(r, "@db.example.com"));
    free(r);

    /* MySQL */
    r = redact("mysql://root:secretpassword@localhost/app");
    check("mysql connstr: password gone", not_contains(r, "secretpassword"));
    free(r);

    /* MongoDB */
    r = redact("mongodb://user:hunter2@cluster0.mongodb.net/db");
    check("mongodb connstr: password gone", not_contains(r, "hunter2"));
    free(r);

    /* Redis (note: empty username "redis://:pass@" doesn't match; username required) */
    r = redact("redis://default:myredispassword@127.0.0.1:6379/0");
    check("redis connstr: password gone", not_contains(r, "myredispassword"));
    free(r);

    /* Connection string without password — should be unchanged */
    r = redact("postgres://localhost/mydb");
    check("pg no-password: unchanged", contains(r, "postgres://localhost/mydb"));
    free(r);
}

/* ============================================================================
 * 8. Private key PEM blocks (including SSH)
 * ========================================================================== */
static void test_private_key_blocks(void) {
    printf(COLOR_CYAN "\n--- Private key PEM block patterns ---\n" COLOR_RESET);

    char *r;

    /* RSA private key */
    const char *rsa_key =
        "-----BEGIN RSA PRIVATE KEY-----\n"
        "MIIEowIBAAKCAQEA2a2rwplBQLzHPZe5TNJNHQ2MRKRrDJHxMJIhSJ0aBUJQiERK\n"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
        "-----END RSA PRIVATE KEY-----\n";
    r = redact(rsa_key);
    check("rsa key: BEGIN marker gone",  not_contains(r, "-----BEGIN RSA PRIVATE KEY-----"));
    check("rsa key: key data gone",      not_contains(r, "MIIEowIBAAKCAQEA"));
    check("rsa key: replaced correctly", contains(r, "[REDACTED PRIVATE KEY]"));
    free(r);

    /* OpenSSH private key */
    const char *openssh_key =
        "-----BEGIN OPENSSH PRIVATE KEY-----\n"
        "b3BlbnNzaC1rZXktdjEAAAAABG5vbmUAAAAEbm9uZQAAAAAAAAABAAAAaAAAABNlY2RzYS\n"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
        "-----END OPENSSH PRIVATE KEY-----\n";
    r = redact(openssh_key);
    check("openssh key: BEGIN marker gone",  not_contains(r, "-----BEGIN OPENSSH PRIVATE KEY-----"));
    check("openssh key: key data gone",      not_contains(r, "b3BlbnNzaC1rZXktdjE"));
    check("openssh key: replaced correctly", contains(r, "[REDACTED PRIVATE KEY]"));
    free(r);

    /* EC private key */
    const char *ec_key =
        "-----BEGIN EC PRIVATE KEY-----\n"
        "MHQCAQEEIOaLsWmMbscjuFNERUhj9hYzRKJXDieeXP4E3OtFBFkeoAoGCCqGSM49\n"
        "-----END EC PRIVATE KEY-----\n";
    r = redact(ec_key);
    check("ec key: BEGIN marker gone",  not_contains(r, "-----BEGIN EC PRIVATE KEY-----"));
    check("ec key: replaced correctly", contains(r, "[REDACTED PRIVATE KEY]"));
    free(r);

    /* PKCS#8 unencrypted */
    const char *pkcs8_key =
        "-----BEGIN PRIVATE KEY-----\n"
        "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQC7ZkP18sZVBMgn\n"
        "-----END PRIVATE KEY-----\n";
    r = redact(pkcs8_key);
    check("pkcs8 key: BEGIN marker gone",  not_contains(r, "-----BEGIN PRIVATE KEY-----"));
    check("pkcs8 key: replaced correctly", contains(r, "[REDACTED PRIVATE KEY]"));
    free(r);

    /* Encrypted private key */
    const char *enc_key =
        "-----BEGIN ENCRYPTED PRIVATE KEY-----\n"
        "MIIFHDBOBgkqhkiG9w0BBQ0wQTApBgkqhkiG9w0BBQwwHAIIFv5AAAAAAAACAAAI\n"
        "-----END ENCRYPTED PRIVATE KEY-----\n";
    r = redact(enc_key);
    check("encrypted key: replaced correctly", contains(r, "[REDACTED PRIVATE KEY]"));
    free(r);

    /* Text surrounding a key — non-key text preserved */
    const char *surrounding =
        "Here is the key:\n"
        "-----BEGIN RSA PRIVATE KEY-----\n"
        "AABBCCDD1122334455667788\n"
        "-----END RSA PRIVATE KEY-----\n"
        "End of key block.\n";
    r = redact(surrounding);
    check("key in context: surrounding text kept", contains(r, "Here is the key:"));
    check("key in context: trailing text kept",    contains(r, "End of key block."));
    check("key in context: key data gone",         not_contains(r, "AABBCCDD1122334455667788"));
    free(r);

    /* BEGIN without PRIVATE KEY — should NOT be redacted */
    const char *cert =
        "-----BEGIN CERTIFICATE-----\n"
        "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA\n"
        "-----END CERTIFICATE-----\n";
    r = redact(cert);
    check("certificate block: NOT redacted", contains(r, "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA"));
    free(r);
}

/* ============================================================================
 * 9. Multiple secrets in one string
 * ========================================================================== */
static void test_multiple_secrets(void) {
    printf(COLOR_CYAN "\n--- multiple secrets in one string ---\n" COLOR_RESET);

    char *r;

    const char *multi =
        "export OPENAI_API_KEY=sk-abcdefghijklmnopqrstuvwxyz1234\n"
        "Authorization: Bearer eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.payload\n"
        "postgres://admin:p@ssw0rd_secret@db.example.com/app\n";

    r = redact(multi);
    check("multi: openai key gone",       not_contains(r, "sk-abcdefghijklmnopqrstuvwxyz1234"));
    check("multi: bearer token gone",     not_contains(r, "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.payload"));
    check("multi: pg password gone",      not_contains(r, "p@ssw0rd_secret"));
    check("multi: non-secret text kept",  contains(r, "OPENAI_API_KEY"));
    check("multi: bearer prefix kept",    contains(r, "Authorization: Bearer"));
    free(r);
}

/* ============================================================================
 * 10. Opt-out via KLAWED_REDACT_SECRETS=0
 * ========================================================================== */
static void test_opt_out(void) {
    printf(COLOR_CYAN "\n--- opt-out via KLAWED_REDACT_SECRETS=0 ---\n" COLOR_RESET);

    setenv("KLAWED_REDACT_SECRETS", "0", 1);

    char *r = redact("OPENAI_API_KEY=sk-abcdefghijklmnopqrstuvwxyz1234");
    check("opt-out=0: raw value preserved", contains(r, "sk-abcdefghijklmnopqrstuvwxyz1234"));
    free(r);

    setenv("KLAWED_REDACT_SECRETS", "false", 1);
    r = redact("OPENAI_API_KEY=sk-abcdefghijklmnopqrstuvwxyz1234");
    check("opt-out=false: raw value preserved", contains(r, "sk-abcdefghijklmnopqrstuvwxyz1234"));
    free(r);

    unsetenv("KLAWED_REDACT_SECRETS");

    /* After unsetting, redaction should be active again */
    r = redact("OPENAI_API_KEY=sk-abcdefghijklmnopqrstuvwxyz1234");
    check("after unsetenv: redaction active again",
          not_contains(r, "sk-abcdefghijklmnopqrstuvwxyz1234"));
    free(r);
}

/* ============================================================================
 * main
 * ========================================================================== */
int main(void) {
    printf(COLOR_CYAN "=== test_redact: redact_sensitive_text() unit tests ===\n" COLOR_RESET);

    test_null_and_empty();
    test_prefix_patterns();
    test_env_patterns();
    test_json_patterns();
    test_auth_header_patterns();
    test_telegram_patterns();
    test_db_connstr_patterns();
    test_private_key_blocks();
    test_multiple_secrets();
    test_opt_out();

    printf(COLOR_CYAN "\n=== Summary ===\n" COLOR_RESET);
    printf("Tests run:    %d\n", g_run);
    printf(COLOR_GREEN "Tests passed: %d\n" COLOR_RESET, g_passed);
    if (g_failed > 0) {
        printf(COLOR_RED "Tests failed: %d\n" COLOR_RESET, g_failed);
        return 1;
    }
    printf(COLOR_GREEN "All tests passed!\n" COLOR_RESET);
    return 0;
}
