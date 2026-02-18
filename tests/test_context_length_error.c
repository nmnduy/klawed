/*
 * Unit Tests for Context Length Error Detection
 *
 * Tests the is_context_length_error() function to ensure it correctly
 * identifies various context length exceeded messages from different
 * API providers, with case-insensitive matching.
 *
 * Compilation: make test-context-length-error
 * Usage: ./build/test_context_length_error
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/retry_logic.h"

// Test framework colors
#define COLOR_RESET "\033[0m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_CYAN "\033[36m"

// Test counters
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// Test utilities
static void assert_true(const char *test_name, int condition, const char *message) {
    tests_run++;
    if (condition) {
        tests_passed++;
        printf("%s✓%s %s\n", COLOR_GREEN, COLOR_RESET, test_name);
    } else {
        tests_failed++;
        printf("%s✗%s %s: %s\n", COLOR_RED, COLOR_RESET, test_name, message);
    }
}

static void assert_context_error(const char *test_name, const char *error_msg,
                                  const char *error_type, int expected) {
    int result = is_context_length_error(error_msg, error_type);
    char msg[256];
    snprintf(msg, sizeof(msg), "expected %s but got %s",
             expected ? "context error" : "no error",
             result ? "context error" : "no error");
    assert_true(test_name, result == expected, msg);
}

// Test 1: OpenAI-style "maximum context length" errors
static void test_openai_maximum_context_length(void) {
    printf("\n%s=== Test 1: OpenAI Maximum Context Length ===%s\n", COLOR_CYAN, COLOR_RESET);

    assert_context_error("OpenAI: maximum context length",
                         "This model's maximum context length is 8192 tokens",
                         "invalid_request_error", 1);

    assert_context_error("OpenAI: exceeds maximum context length",
                         "The message exceeds the maximum context length of 4096 tokens",
                         "invalid_request_error", 1);
}

// Test 2: Case-insensitive matching for "Context length exceeded" user-friendly message
static void test_user_friendly_message_case_insensitive(void) {
    printf("\n%s=== Test 2: User-Friendly Message (Case-Insensitive) ===%s\n", COLOR_CYAN, COLOR_RESET);

    // The exact user-friendly message from get_context_length_error_message()
    const char *user_friendly = "Context length exceeded. The conversation has grown too large for the model's memory. "
                                 "Try starting a new conversation or reduce the amount of code/files being discussed.";

    assert_context_error("User-friendly: Capital C in 'Context'",
                         user_friendly, NULL, 1);

    assert_context_error("User-friendly: lowercase 'context'",
                         "context length exceeded. conversation too large.", NULL, 1);

    assert_context_error("User-friendly: mixed case 'CONTEXT LENGTH EXCEEDED'",
                         "CONTEXT LENGTH EXCEEDED", NULL, 1);
}

// Test 3: "context length" + "tokens" combination
static void test_context_length_and_tokens(void) {
    printf("\n%s=== Test 3: Context Length + Tokens Pattern ===%s\n", COLOR_CYAN, COLOR_RESET);

    assert_context_error("context length + tokens",
                         "The context length is too large. Total tokens: 10000",
                         NULL, 1);

    assert_context_error("CONTEXT LENGTH + TOKENS (uppercase)",
                         "CONTEXT LENGTH EXCEEDED. TOO MANY TOKENS",
                         NULL, 1);

    // Should NOT match if "tokens" is missing
    assert_context_error("context length without tokens - should NOT match",
                         "The context length is too large", NULL, 0);
}

// Test 4: "too many tokens" pattern
static void test_too_many_tokens(void) {
    printf("\n%s=== Test 4: Too Many Tokens Pattern ===%s\n", COLOR_CYAN, COLOR_RESET);

    assert_context_error("too many tokens (lowercase)",
                         "Error: too many tokens in request", NULL, 1);

    assert_context_error("TOO MANY TOKENS (uppercase)",
                         "Error: TOO MANY TOKENS in request", NULL, 1);

    assert_context_error("Too Many Tokens (mixed case)",
                         "Error: Too Many Tokens in request", NULL, 1);
}

// Test 5: OpenAI "exceeded model token limit" pattern
static void test_exceeded_model_token_limit(void) {
    printf("\n%s=== Test 5: Exceeded Model Token Limit ===%s\n", COLOR_CYAN, COLOR_RESET);

    assert_context_error("exceeded model token limit",
                         "Request exceeded model token limit of 4096 tokens", NULL, 1);

    assert_context_error("EXCEEDED MODEL TOKEN LIMIT",
                         "Request EXCEEDED MODEL TOKEN LIMIT", NULL, 1);
}

// Test 6: Generic "token limit" pattern
static void test_token_limit(void) {
    printf("\n%s=== Test 6: Token Limit Pattern ===%s\n", COLOR_CYAN, COLOR_RESET);

    assert_context_error("token limit exceeded",
                         "Error: token limit exceeded", NULL, 1);

    assert_context_error("TOKEN LIMIT",
                         "Request hit TOKEN LIMIT", NULL, 1);

    assert_context_error("Token Limit (mixed case)",
                         "Error: Token Limit exceeded", NULL, 1);
}

// Test 7: LiteLLM/Bedrock ContextWindowExceededError
static void test_context_window_exceeded_error(void) {
    printf("\n%s=== Test 7: ContextWindowExceededError (LiteLLM/Bedrock) ===%s\n", COLOR_CYAN, COLOR_RESET);

    assert_context_error("ContextWindowExceededError (camelCase)",
                         "Error: ContextWindowExceededError - window too large", NULL, 1);

    assert_context_error("contextwindowexceedederror (lowercase)",
                         "Error: contextwindowexceedederror - window too large", NULL, 1);

    assert_context_error("CONTEXTWINDOWEXCEEDEDERROR (uppercase)",
                         "Error: CONTEXTWINDOWEXCEEDEDERROR", NULL, 1);
}

// Test 8: "Context Window Error" pattern
static void test_context_window_error(void) {
    printf("\n%s=== Test 8: Context Window Error Pattern ===%s\n", COLOR_CYAN, COLOR_RESET);

    assert_context_error("Context Window Error (mixed case)",
                         "Error: Context Window Error", NULL, 1);

    assert_context_error("context window error (lowercase)",
                         "Error: context window error", NULL, 1);

    assert_context_error("CONTEXT WINDOW ERROR (uppercase)",
                         "Error: CONTEXT WINDOW ERROR", NULL, 1);
}

// Test 9: "Input is too long" pattern
static void test_input_is_too_long(void) {
    printf("\n%s=== Test 9: Input Is Too Long Pattern ===%s\n", COLOR_CYAN, COLOR_RESET);

    assert_context_error("Input is too long",
                         "Error: Input is too long", NULL, 1);

    assert_context_error("INPUT IS TOO LONG",
                         "Error: INPUT IS TOO LONG", NULL, 1);

    assert_context_error("input is too long",
                         "Error: input is too long", NULL, 1);
}

// Test 10: invalid_request_error + tokens combination
static void test_invalid_request_error_with_tokens(void) {
    printf("\n%s=== Test 10: Invalid Request Error + Tokens ===%s\n", COLOR_CYAN, COLOR_RESET);

    assert_context_error("invalid_request_error with 'tokens' in message",
                         "Request contains too many tokens",
                         "invalid_request_error", 1);

    assert_context_error("INVALID_REQUEST_ERROR (case insensitive) with 'tokens'",
                         "Request contains too many tokens",
                         "INVALID_REQUEST_ERROR", 1);

    // Should match because message contains "too many tokens" regardless of error_type
    assert_context_error("any error type with 'too many tokens' - should match",
                         "Request contains too many tokens",
                         "server_error", 1);

    // Should NOT match if "tokens" is missing
    assert_context_error("invalid_request_error without 'tokens' - should NOT match",
                         "Some other invalid request",
                         "invalid_request_error", 0);
}

// Test 11: Non-context-length errors
static void test_non_context_length_errors(void) {
    printf("\n%s=== Test 11: Non-Context-Length Errors ===%s\n", COLOR_CYAN, COLOR_RESET);

    assert_context_error("rate limit error - should NOT match",
                         "Rate limit exceeded", NULL, 0);

    assert_context_error("authentication error - should NOT match",
                         "Invalid API key", NULL, 0);

    assert_context_error("server error - should NOT match",
                         "Internal server error", NULL, 0);

    assert_context_error("timeout error - should NOT match",
                         "Request timeout", NULL, 0);

    assert_context_error("empty string - should NOT match",
                         "", NULL, 0);
}

// Test 12: NULL handling
static void test_null_handling(void) {
    printf("\n%s=== Test 12: NULL Handling ===%s\n", COLOR_CYAN, COLOR_RESET);

    assert_context_error("NULL error_message should return 0",
                         NULL, NULL, 0);

    assert_context_error("NULL error_message with valid error_type should return 0",
                         NULL, "invalid_request_error", 0);
}

// Test 13: Edge cases and partial matches
static void test_edge_cases(void) {
    printf("\n%s=== Test 13: Edge Cases and Partial Matches ===%s\n", COLOR_CYAN, COLOR_RESET);

    // Should NOT match partial strings that look similar
    assert_context_error("'context' alone - should NOT match",
                         "This is the context of the discussion", NULL, 0);

    assert_context_error("'length' alone - should NOT match",
                         "The length of the message", NULL, 0);

    assert_context_error("'tokens' alone with context but not 'context length' - should NOT match",
                         "The request has many tokens in this context", NULL, 0);

    // Full phrases should match
    assert_context_error("Full phrase 'context length exceeded'",
                         "Error: context length exceeded", NULL, 1);
}

// Test 14: Real-world error messages from various providers
static void test_real_world_messages(void) {
    printf("\n%s=== Test 14: Real-World Provider Messages ===%s\n", COLOR_CYAN, COLOR_RESET);

    // OpenAI
    assert_context_error("OpenAI: actual error message",
                         "This model's maximum context length is 16385 tokens. "
                         "However, you requested 20000 tokens (15000 in the messages, 5000 in the completion). "
                         "Please reduce the length of the messages or completion.",
                         "invalid_request_error", 1);

    // Anthropic (simulated)
    assert_context_error("Anthropic-style: context window exceeded",
                         "Context window exceeded: input is too long for model", NULL, 1);

    // AWS Bedrock (simulated)
    assert_context_error("Bedrock-style: input is too long",
                         "ValidationException: Input is too long", NULL, 1);

    // Azure OpenAI (simulated)
    assert_context_error("Azure OpenAI-style: token limit",
                         "Request exceeded token limit for deployment", NULL, 1);
}

// Main test runner
int main(void) {
    printf("\n%s╔══════════════════════════════════════════════════════╗%s\n",
           COLOR_CYAN, COLOR_RESET);
    printf("%s║  Context Length Error Detection - Unit Test Suite   ║%s\n",
           COLOR_CYAN, COLOR_RESET);
    printf("%s╚══════════════════════════════════════════════════════╝%s\n",
           COLOR_CYAN, COLOR_RESET);

    test_openai_maximum_context_length();
    test_user_friendly_message_case_insensitive();
    test_context_length_and_tokens();
    test_too_many_tokens();
    test_exceeded_model_token_limit();
    test_token_limit();
    test_context_window_exceeded_error();
    test_context_window_error();
    test_input_is_too_long();
    test_invalid_request_error_with_tokens();
    test_non_context_length_errors();
    test_null_handling();
    test_edge_cases();
    test_real_world_messages();

    // Print summary
    printf("\n%s═══════════════════════════════════════════════════════%s\n",
           COLOR_CYAN, COLOR_RESET);
    printf("Tests run:    %d\n", tests_run);
    printf("%sPassed:       %d%s\n", COLOR_GREEN, tests_passed, COLOR_RESET);
    if (tests_failed > 0) {
        printf("%sFailed:       %d%s\n", COLOR_RED, tests_failed, COLOR_RESET);
    }
    printf("%s═══════════════════════════════════════════════════════%s\n",
           COLOR_CYAN, COLOR_RESET);

    return (tests_failed == 0) ? 0 : 1;
}
