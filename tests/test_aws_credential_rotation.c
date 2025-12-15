/*
 * Test AWS credential rotation - updated for new flow
 *
 * Tests the new authentication flow where:
 * 1. bedrock_load_credentials() returns cached credentials or NULL
 * 2. bedrock_authenticate() triggers SSO login or custom auth
 * 3. Credentials are loaded after authentication
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

// Need to expose internals for testing
#define TEST_BUILD 1
#include "../src/aws_bedrock.h"
#include "../src/logger.h"

// Test framework
static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT_TRUE(cond, msg) do { \
    tests_run++; \
    if (cond) { \
        tests_passed++; \
        printf("  [PASS] %s\n", msg); \
    } else { \
        printf("  [FAIL] %s (at line %d)\n", msg, __LINE__); \
    } \
} while(0)

#define ASSERT_EQ(expected, actual, msg) do { \
    tests_run++; \
    if ((expected) == (actual)) { \
        tests_passed++; \
        printf("  [PASS] %s (expected=%d, actual=%d)\n", msg, (int)(expected), (int)(actual)); \
    } else { \
        printf("  [FAIL] %s (expected=%d, actual=%d, at line %d)\n", msg, (int)(expected), (int)(actual), __LINE__); \
    } \
} while(0)

#define ASSERT_STR_EQ(expected, actual, msg) do { \
    tests_run++; \
    if (strcmp((expected), (actual)) == 0) { \
        tests_passed++; \
        printf("  [PASS] %s\n", msg); \
    } else { \
        printf("  [FAIL] %s (expected='%s', actual='%s', at line %d)\n", msg, expected, actual, __LINE__); \
    } \
} while(0)

// ============================================================================
// Mock State
// ============================================================================

static int mock_auth_calls = 0;
static int mock_exec_calls = 0;
static int mock_credential_version = 0;  // Incremented to simulate credential changes

// ============================================================================
// Mock Functions
// ============================================================================

/**
 * Mock exec_command - simulates AWS CLI commands
 * Returns different credentials based on mock_credential_version
 */
static char* mock_exec_command(const char *cmd) {
    mock_exec_calls++;

    // aws sts get-caller-identity (validation)
    if (strstr(cmd, "aws sts get-caller-identity")) {
        // Always return valid after authentication
        if (mock_credential_version > 0) {
            return strdup("{\"UserId\": \"VALID123\", \"Account\": \"123456789\"}");
        } else {
            return strdup("ExpiredToken");
        }
    }

    // aws configure get sso_start_url
    if (strstr(cmd, "aws configure get sso_start_url")) {
        return strdup("https://test-sso.awsapps.com/start");
    }

    // aws configure export-credentials
    if (strstr(cmd, "export-credentials")) {
        // Return credentials based on current version
        if (mock_credential_version > 0) {
            char buffer[512];
            // Different access key for each version to simulate rotation
            snprintf(buffer, sizeof(buffer),
                    "export AWS_ACCESS_KEY_ID=AKIA_VERSION_%d\n"
                    "export AWS_SECRET_ACCESS_KEY=SECRET_VERSION_%d\n"
                    "export AWS_SESSION_TOKEN=TOKEN_VERSION_%d\n",
                    mock_credential_version, mock_credential_version, mock_credential_version);
            return strdup(buffer);
        } else {
            // No credentials yet
            return strdup("");
        }
    }

    return strdup("");
}

/**
 * Mock system - simulates authentication commands
 */
static int mock_system(const char *cmd) {
    if (strstr(cmd, "aws sso login") || strstr(cmd, "custom-auth")) {
        mock_auth_calls++;
        // Simulate credential update after auth
        mock_credential_version++;
        return 0;  // Success
    }
    return 1;  // Failure
}

/**
 * Mock system that fails authentication
 */
static int mock_system_fail(const char *cmd) {
    (void)cmd;
    mock_auth_calls++;
    return 1;  // Always fail
}

// ============================================================================
// Helper Functions
// ============================================================================

static void reset_mocks(void) {
    mock_auth_calls = 0;
    mock_exec_calls = 0;
    mock_credential_version = 0;
}

static void setup_test_env(void) {
    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
    unsetenv("AWS_SESSION_TOKEN");
    unsetenv("AWS_AUTH_COMMAND");
    setenv("AWS_PROFILE", "test-profile", 1);
    setenv("AWS_REGION", "us-west-2", 1);
}

static void cleanup_test_env(void) {
    unsetenv("AWS_PROFILE");
    unsetenv("AWS_REGION");
    unsetenv("AWS_AUTH_COMMAND");
}

// ============================================================================
// Test Cases
// ============================================================================

/**
 * Test 1: No cached credentials returns NULL
 */
static void test_no_cached_credentials_returns_null(void) {
    printf("\n[Test 1] No cached credentials returns NULL\n");
    reset_mocks();
    setup_test_env();

    aws_bedrock_set_exec_command_fn(mock_exec_command);
    aws_bedrock_set_system_fn(mock_system);

    // No cached credentials
    mock_credential_version = 0;

    // Load credentials (should return NULL - no auth triggered)
    AWSCredentials *creds = bedrock_load_credentials("test-profile", "us-west-2");

    ASSERT_TRUE(creds == NULL, "NULL returned when no cached credentials");
    ASSERT_EQ(0, mock_auth_calls, "No authentication triggered");

    cleanup_test_env();
}

/**
 * Test 2: Cached credentials are returned without validation
 */
static void test_cached_credentials_returned(void) {
    printf("\n[Test 2] Cached credentials returned without validation\n");
    reset_mocks();
    setup_test_env();

    aws_bedrock_set_exec_command_fn(mock_exec_command);
    aws_bedrock_set_system_fn(mock_system);

    // Set up cached credentials
    mock_credential_version = 1;

    // Load credentials (should return cached creds without validation)
    AWSCredentials *creds = bedrock_load_credentials("test-profile", "us-west-2");

    ASSERT_TRUE(creds != NULL, "Credentials returned from cache");
    ASSERT_EQ(0, mock_auth_calls, "No authentication triggered");
    ASSERT_TRUE(creds->access_key_id != NULL, "Access key is present");
    ASSERT_TRUE(strstr(creds->access_key_id, "VERSION_1") != NULL, "Access key is version 1");

    bedrock_creds_free(creds);
    cleanup_test_env();
}

/**
 * Test 3: bedrock_authenticate triggers SSO login
 */
static void test_authenticate_triggers_sso(void) {
    printf("\n[Test 3] bedrock_authenticate triggers SSO login\n");
    reset_mocks();
    setup_test_env();

    aws_bedrock_set_exec_command_fn(mock_exec_command);
    aws_bedrock_set_system_fn(mock_system);

    mock_credential_version = 0;

    // Authenticate
    int result = bedrock_authenticate("test-profile");

    ASSERT_EQ(0, result, "Authentication succeeded");
    ASSERT_EQ(1, mock_auth_calls, "SSO login was called");
    ASSERT_EQ(1, mock_credential_version, "Credential version incremented");

    cleanup_test_env();
}

/**
 * Test 4: Credentials available after authentication
 */
static void test_credentials_after_auth(void) {
    printf("\n[Test 4] Credentials available after authentication\n");
    reset_mocks();
    setup_test_env();

    aws_bedrock_set_exec_command_fn(mock_exec_command);
    aws_bedrock_set_system_fn(mock_system);

    // No cached credentials initially
    mock_credential_version = 0;
    AWSCredentials *creds1 = bedrock_load_credentials("test-profile", "us-west-2");
    ASSERT_TRUE(creds1 == NULL, "No credentials before auth");

    // Authenticate
    int auth_result = bedrock_authenticate("test-profile");
    ASSERT_EQ(0, auth_result, "Authentication succeeded");

    // Load credentials after auth
    AWSCredentials *creds2 = bedrock_load_credentials("test-profile", "us-west-2");
    ASSERT_TRUE(creds2 != NULL, "Credentials available after auth");
    ASSERT_TRUE(creds2->access_key_id != NULL, "Access key is present");
    ASSERT_TRUE(strstr(creds2->access_key_id, "VERSION_1") != NULL, "Got version 1 credentials");

    bedrock_creds_free(creds2);
    cleanup_test_env();
}

/**
 * Test 5: Credential rotation changes access keys
 */
static void test_credential_rotation(void) {
    printf("\n[Test 5] Credential rotation changes access keys\n");
    reset_mocks();
    setup_test_env();

    aws_bedrock_set_exec_command_fn(mock_exec_command);
    aws_bedrock_set_system_fn(mock_system);

    // Set up initial credentials
    mock_credential_version = 1;
    AWSCredentials *creds1 = bedrock_load_credentials("test-profile", "us-west-2");
    ASSERT_TRUE(creds1 != NULL, "First credentials loaded");
    const char *key1 = creds1->access_key_id;

    // Trigger rotation
    int auth_result = bedrock_authenticate("test-profile");
    ASSERT_EQ(0, auth_result, "Authentication succeeded");
    ASSERT_EQ(2, mock_credential_version, "Credential version incremented to 2");

    // Load new credentials
    AWSCredentials *creds2 = bedrock_load_credentials("test-profile", "us-west-2");
    ASSERT_TRUE(creds2 != NULL, "Second credentials loaded");
    ASSERT_TRUE(strcmp(key1, creds2->access_key_id) != 0, "Access keys are different after rotation");
    ASSERT_TRUE(strstr(creds2->access_key_id, "VERSION_2") != NULL, "Got version 2 credentials");

    bedrock_creds_free(creds1);
    bedrock_creds_free(creds2);
    cleanup_test_env();
}

/**
 * Test 6: Credential validation function works independently
 */
static void test_credential_validation(void) {
    printf("\n[Test 6] Credential validation works independently\n");
    reset_mocks();
    setup_test_env();

    aws_bedrock_set_exec_command_fn(mock_exec_command);
    aws_bedrock_set_system_fn(mock_system);

    // Set up valid credentials
    mock_credential_version = 1;
    AWSCredentials *creds = bedrock_load_credentials("test-profile", "us-west-2");
    ASSERT_TRUE(creds != NULL, "Credentials loaded");

    // Validate credentials
    int valid = bedrock_validate_credentials(creds, "test-profile");
    ASSERT_EQ(1, valid, "Credentials are valid");

    bedrock_creds_free(creds);
    cleanup_test_env();
}

/**
 * Test 7: Custom authentication command
 */
static void test_custom_auth_command(void) {
    printf("\n[Test 7] Custom authentication command\n");
    reset_mocks();
    setup_test_env();

    setenv("AWS_AUTH_COMMAND", "custom-auth --profile test", 1);

    aws_bedrock_set_exec_command_fn(mock_exec_command);
    aws_bedrock_set_system_fn(mock_system);

    // Authenticate using custom command
    int result = bedrock_authenticate("test-profile");

    ASSERT_EQ(0, result, "Custom authentication succeeded");
    ASSERT_EQ(1, mock_auth_calls, "Custom auth command called");
    ASSERT_EQ(1, mock_credential_version, "Credential version incremented");

    cleanup_test_env();
}

/**
 * Test 8: Authentication failure handling
 */
static void test_authentication_failure(void) {
    printf("\n[Test 8] Authentication failure handling\n");
    reset_mocks();
    setup_test_env();

    aws_bedrock_set_exec_command_fn(mock_exec_command);
    aws_bedrock_set_system_fn(mock_system_fail);

    // Try to authenticate (should fail)
    int result = bedrock_authenticate("test-profile");

    ASSERT_EQ(-1, result, "Authentication returns error on failure");
    ASSERT_EQ(1, mock_auth_calls, "Authentication was attempted");

    cleanup_test_env();
}

/**
 * Test 9: Multiple rotation cycles
 */
static void test_multiple_rotation_cycles(void) {
    printf("\n[Test 9] Multiple rotation cycles\n");
    reset_mocks();
    setup_test_env();

    aws_bedrock_set_exec_command_fn(mock_exec_command);
    aws_bedrock_set_system_fn(mock_system);

    char *keys[3] = {NULL, NULL, NULL};

    // Perform 3 rotation cycles
    for (int i = 0; i < 3; i++) {
        // Authenticate
        int auth_result = bedrock_authenticate("test-profile");
        ASSERT_EQ(0, auth_result, "Authentication succeeded in cycle");

        // Load credentials
        AWSCredentials *creds = bedrock_load_credentials("test-profile", "us-west-2");
        ASSERT_TRUE(creds != NULL, "Credentials loaded in cycle");
        ASSERT_TRUE(creds->access_key_id != NULL, "Access key present in cycle");

        // Save key for comparison
        keys[i] = strdup(creds->access_key_id);

        bedrock_creds_free(creds);
    }

    // Verify all keys are different
    ASSERT_TRUE(strcmp(keys[0], keys[1]) != 0, "Keys differ between cycle 1 and 2");
    ASSERT_TRUE(strcmp(keys[1], keys[2]) != 0, "Keys differ between cycle 2 and 3");
    ASSERT_TRUE(strcmp(keys[0], keys[2]) != 0, "Keys differ between cycle 1 and 3");

    // Verify version numbers increased
    ASSERT_TRUE(strstr(keys[0], "VERSION_1") != NULL, "First key is version 1");
    ASSERT_TRUE(strstr(keys[1], "VERSION_2") != NULL, "Second key is version 2");
    ASSERT_TRUE(strstr(keys[2], "VERSION_3") != NULL, "Third key is version 3");

    for (int i = 0; i < 3; i++) {
        free(keys[i]);
    }

    cleanup_test_env();
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    printf("=== AWS Credential Rotation Tests (New Flow) ===\n");
    printf("Testing: load returns cached or NULL, auth must be called explicitly\n");

    // Initialize logger
    log_init();

    // Run test suite
    test_no_cached_credentials_returns_null();
    test_cached_credentials_returned();
    test_authenticate_triggers_sso();
    test_credentials_after_auth();
    test_credential_rotation();
    test_credential_validation();
    test_custom_auth_command();
    test_authentication_failure();
    test_multiple_rotation_cycles();

    // Print summary
    printf("\n=== Test Summary ===\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_run - tests_passed);

    if (tests_run == tests_passed) {
        printf("\n✓ All AWS credential rotation tests passed!\n");
        return 0;
    } else {
        printf("\n✗ Some tests failed\n");
        return 1;
    }
}
