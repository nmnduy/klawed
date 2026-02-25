/*
 * Unit Tests for Base64 Encoding/Decoding
 *
 * Tests the Base64 implementation including:
 * - Basic encoding/decoding
 * - Empty input handling
 * - Padding scenarios
 * - Invalid input handling
 * - Round-trip encoding/decoding
 * - Binary data handling
 *
 * Compilation: make test-base64
 * Usage: ./test_base64
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Include base64 header
#include "../src/base64.h"

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
static void print_test_result(const char *test_name, int passed) {
    tests_run++;
    if (passed) {
        tests_passed++;
        printf(COLOR_GREEN "✓ PASS" COLOR_RESET " %s\n", test_name);
    } else {
        tests_failed++;
        printf(COLOR_RED "✗ FAIL" COLOR_RESET " %s\n", test_name);
    }
}

static void print_summary(void) {
    printf("\n" COLOR_CYAN "Test Summary:" COLOR_RESET "\n");
    printf("Tests run: %d\n", tests_run);
    printf(COLOR_GREEN "Tests passed: %d\n" COLOR_RESET, tests_passed);
    if (tests_failed > 0) {
        printf(COLOR_RED "Tests failed: %d\n" COLOR_RESET, tests_failed);
    } else {
        printf(COLOR_GREEN "All tests passed!\n" COLOR_RESET);
    }
}

// Test cases

static void test_base64_encode_empty(void) {
    const char *test_name = "test_base64_encode_empty";

    size_t output_length;
    char *encoded = base64_encode((const unsigned char *)"", 0, &output_length);

    int passed = (encoded != NULL && output_length == 0 && strcmp(encoded, "") == 0);

    if (encoded) free(encoded);
    print_test_result(test_name, passed);
}

static void test_base64_decode_empty(void) {
    const char *test_name = "test_base64_decode_empty";

    size_t output_length;
    unsigned char *decoded = base64_decode("", 0, &output_length);

    int passed = (decoded != NULL && output_length == 0);

    if (decoded) free(decoded);
    print_test_result(test_name, passed);
}

static void test_base64_encode_basic(void) {
    const char *test_name = "test_base64_encode_basic";

    const char *input = "Hello, World!";
    const char *expected = "SGVsbG8sIFdvcmxkIQ==";

    size_t output_length;
    char *encoded = base64_encode((const unsigned char *)input, strlen(input), &output_length);

    int passed = (encoded != NULL && strcmp(encoded, expected) == 0);

    if (encoded) free(encoded);
    print_test_result(test_name, passed);
}

static void test_base64_decode_basic(void) {
    const char *test_name = "test_base64_decode_basic";

    const char *input = "SGVsbG8sIFdvcmxkIQ==";
    const char *expected = "Hello, World!";

    size_t output_length;
    unsigned char *decoded = base64_decode(input, strlen(input), &output_length);

    int passed = (decoded != NULL &&
                  output_length == strlen(expected) &&
                  memcmp(decoded, expected, output_length) == 0);

    if (decoded) free(decoded);
    print_test_result(test_name, passed);
}

static void test_base64_encode_no_padding(void) {
    const char *test_name = "test_base64_encode_no_padding";

    const char *input = "Man";
    const char *expected = "TWFu";

    size_t output_length;
    char *encoded = base64_encode((const unsigned char *)input, strlen(input), &output_length);

    int passed = (encoded != NULL && strcmp(encoded, expected) == 0);

    if (encoded) free(encoded);
    print_test_result(test_name, passed);
}

static void test_base64_decode_no_padding(void) {
    const char *test_name = "test_base64_decode_no_padding";

    const char *input = "TWFu";
    const char *expected = "Man";

    size_t output_length;
    unsigned char *decoded = base64_decode(input, strlen(input), &output_length);

    int passed = (decoded != NULL &&
                  output_length == strlen(expected) &&
                  memcmp(decoded, expected, output_length) == 0);

    if (decoded) free(decoded);
    print_test_result(test_name, passed);
}

static void test_base64_encode_one_padding(void) {
    const char *test_name = "test_base64_encode_one_padding";

    const char *input = "Ma";
    const char *expected = "TWE=";

    size_t output_length;
    char *encoded = base64_encode((const unsigned char *)input, strlen(input), &output_length);

    int passed = (encoded != NULL && strcmp(encoded, expected) == 0);

    if (encoded) free(encoded);
    print_test_result(test_name, passed);
}

static void test_base64_decode_one_padding(void) {
    const char *test_name = "test_base64_decode_one_padding";

    const char *input = "TWE=";
    const char *expected = "Ma";

    size_t output_length;
    unsigned char *decoded = base64_decode(input, strlen(input), &output_length);

    int passed = (decoded != NULL &&
                  output_length == strlen(expected) &&
                  memcmp(decoded, expected, output_length) == 0);

    if (decoded) free(decoded);
    print_test_result(test_name, passed);
}

static void test_base64_encode_two_padding(void) {
    const char *test_name = "test_base64_encode_two_padding";

    const char *input = "M";
    const char *expected = "TQ==";

    size_t output_length;
    char *encoded = base64_encode((const unsigned char *)input, strlen(input), &output_length);

    int passed = (encoded != NULL && strcmp(encoded, expected) == 0);

    if (encoded) free(encoded);
    print_test_result(test_name, passed);
}

static void test_base64_decode_two_padding(void) {
    const char *test_name = "test_base64_decode_two_padding";

    const char *input = "TQ==";
    const char *expected = "M";

    size_t output_length;
    unsigned char *decoded = base64_decode(input, strlen(input), &output_length);

    int passed = (decoded != NULL &&
                  output_length == strlen(expected) &&
                  memcmp(decoded, expected, output_length) == 0);

    if (decoded) free(decoded);
    print_test_result(test_name, passed);
}

static void test_base64_roundtrip(void) {
    const char *test_name = "test_base64_roundtrip";

    const char *original = "Test roundtrip with various characters: !@#$%^&*()_+-=[]{}|;:,.<>?/`~";

    size_t encoded_length;
    char *encoded = base64_encode((const unsigned char *)original, strlen(original), &encoded_length);

    if (!encoded) {
        print_test_result(test_name, 0);
        return;
    }

    size_t decoded_length;
    unsigned char *decoded = base64_decode(encoded, encoded_length, &decoded_length);

    int passed = (decoded != NULL &&
                  decoded_length == strlen(original) &&
                  memcmp(decoded, original, decoded_length) == 0);

    free(encoded);
    if (decoded) free(decoded);
    print_test_result(test_name, passed);
}

static void test_base64_binary_data(void) {
    const char *test_name = "test_base64_binary_data";

    // Create binary data with various byte values
    unsigned char binary_data[256];
    for (int i = 0; i < 256; i++) {
        binary_data[i] = (unsigned char)i;
    }

    size_t encoded_length;
    char *encoded = base64_encode(binary_data, sizeof(binary_data), &encoded_length);

    if (!encoded) {
        print_test_result(test_name, 0);
        return;
    }

    size_t decoded_length;
    unsigned char *decoded = base64_decode(encoded, encoded_length, &decoded_length);

    int passed = (decoded != NULL &&
                  decoded_length == sizeof(binary_data) &&
                  memcmp(decoded, binary_data, decoded_length) == 0);

    free(encoded);
    if (decoded) free(decoded);
    print_test_result(test_name, passed);
}

static void test_base64_null_parameters(void) {
    const char *test_name = "test_base64_null_parameters";

    // Test with NULL data
    size_t output_length;
    char *encoded = base64_encode(NULL, 10, &output_length);
    int passed1 = (encoded == NULL);

    // Test with NULL output_length
    char *encoded2 = base64_encode((const unsigned char *)"test", 4, NULL);
    int passed2 = (encoded2 == NULL);

    // Test decode with NULL data
    unsigned char *decoded = base64_decode(NULL, 10, &output_length);
    int passed3 = (decoded == NULL);

    // Test decode with NULL output_length
    unsigned char *decoded2 = base64_decode("test", 4, NULL);
    int passed4 = (decoded2 == NULL);

    int passed = passed1 && passed2 && passed3 && passed4;

    if (encoded) free(encoded);
    if (encoded2) free(encoded2);
    if (decoded) free(decoded);
    if (decoded2) free(decoded2);

    print_test_result(test_name, passed);
}

static void test_base64_invalid_characters(void) {
    const char *test_name = "test_base64_invalid_characters";

    // Test with invalid base64 characters
    const char *invalid_base64 = "Test!@#$%^&*()";

    size_t output_length;
    unsigned char *decoded = base64_decode(invalid_base64, strlen(invalid_base64), &output_length);

    // The current implementation doesn't validate input characters,
    // so this should still "decode" but the result will be garbage
    // We just test that it doesn't crash
    int passed = (decoded != NULL);

    if (decoded) free(decoded);
    print_test_result(test_name, passed);
}

static void test_base64_length_calculation(void) {
    const char *test_name = "test_base64_length_calculation";

    // Test various input lengths to verify length calculations
    struct {
        size_t input_len;
        size_t expected_encoded_len;
        size_t expected_decoded_len;
    } test_cases[] = {
        {0, 0, 0},
        {1, 4, 1},
        {2, 4, 2},
        {3, 4, 3},
        {4, 8, 4},
        {5, 8, 5},
        {6, 8, 6},
        {7, 12, 7},
        {8, 12, 8},
        {9, 12, 9},
        {10, 16, 10}
    };

    int all_passed = 1;

    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        // Create dummy data
        unsigned char *data = malloc(test_cases[i].input_len);
        if (data && test_cases[i].input_len > 0) {
            memset(data, 'A', test_cases[i].input_len);
        }

        // Test encoding
        size_t encoded_len;
        char *encoded = base64_encode(data, test_cases[i].input_len, &encoded_len);

        if (encoded) {
            if (encoded_len != test_cases[i].expected_encoded_len) {
                all_passed = 0;
            }

            // Test decoding
            size_t decoded_len;
            unsigned char *decoded = base64_decode(encoded, encoded_len, &decoded_len);

            if (decoded) {
                if (decoded_len != test_cases[i].expected_decoded_len) {
                    all_passed = 0;
                }
                free(decoded);
            } else {
                all_passed = 0;
            }

            free(encoded);
        } else {
            all_passed = 0;
        }

        if (data) free(data);
    }

    print_test_result(test_name, all_passed);
}

// Main test runner
int main(void) {
    printf(COLOR_CYAN "Running Base64 Unit Tests\n" COLOR_RESET);
    printf("===========================\n\n");

    // Run all test cases
    test_base64_encode_empty();
    test_base64_decode_empty();
    test_base64_encode_basic();
    test_base64_decode_basic();
    test_base64_encode_no_padding();
    test_base64_decode_no_padding();
    test_base64_encode_one_padding();
    test_base64_decode_one_padding();
    test_base64_encode_two_padding();
    test_base64_decode_two_padding();
    test_base64_roundtrip();
    test_base64_binary_data();
    test_base64_null_parameters();
    test_base64_invalid_characters();
    test_base64_length_calculation();

    print_summary();

    return tests_failed > 0 ? 1 : 0;
}
