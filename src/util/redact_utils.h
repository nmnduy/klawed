/*
 * redact_utils.h - Secret redaction for tool output and logs
 *
 * Applies regex-based pattern matching to mask API keys, tokens, and
 * credentials before they reach the LLM context, log files, or verbose output.
 *
 * Inspired by hermes-agent (NousResearch/hermes-agent) agent/redact.py
 *
 * Short tokens (< 18 chars) are fully masked to "***".
 * Longer tokens preserve the first 6 and last 4 characters for debuggability.
 *
 * Toggle: set KLAWED_REDACT_SECRETS=0|false|no|off to disable.
 * Default: enabled.
 */

#ifndef REDACT_UTILS_H
#define REDACT_UTILS_H

/**
 * Apply all secret-redaction patterns to a block of text.
 *
 * Returns a newly allocated string with secrets masked.
 * Safe to call on any text — non-matching content passes through unchanged.
 * Returns NULL on allocation failure (caller should fall back to original text).
 * Caller must free() the returned string.
 *
 * Patterns covered:
 *   - Known API key prefixes: sk-, ghp_, github_pat_, xoxb-, AIza, AKIA, etc.
 *   - ENV assignments: NAME=value where NAME matches *API_KEY|TOKEN|SECRET|PASSWORD*
 *   - JSON fields: "apiKey"/"token"/"secret"/"password"/"access_token"/etc.
 *   - Authorization headers: Authorization: Bearer <token>
 *   - Telegram bot tokens: bot<digits>:<token> or <digits>:<token>
 *   - Private key PEM blocks: -----BEGIN * PRIVATE KEY-----
 *   - Database connection string passwords: postgres://user:PASS@host
 *   - E.164 phone numbers: +1234567890
 *
 * Disabled when KLAWED_REDACT_SECRETS=0|false|no|off.
 */
char *redact_sensitive_text(const char *text);

#endif /* REDACT_UTILS_H */
