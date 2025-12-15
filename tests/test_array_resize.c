#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../src/array_resize.h"

// Test helper: count allocations (unused for now, reserved for future tests)
// static int allocation_count = 0;
// static void* test_malloc(size_t size) {
//     allocation_count++;
//     return malloc(size);
// }

static void test_overflow_detection(void) {
    printf("Testing overflow detection...\n");

    // Test multiplication overflow
    assert(check_mul_overflow(SIZE_MAX, 2) == 1);
    assert(check_mul_overflow(SIZE_MAX / 2 + 1, 2) == 1);
    assert(check_mul_overflow(SIZE_MAX / 2, 2) == 0);
    assert(check_mul_overflow(0, SIZE_MAX) == 0);
    assert(check_mul_overflow(1, SIZE_MAX) == 0);

    // Test addition overflow
    assert(check_add_overflow(SIZE_MAX, 1) == 1);
    assert(check_add_overflow(SIZE_MAX - 1, 2) == 1);
    assert(check_add_overflow(SIZE_MAX - 1, 1) == 0);
    assert(check_add_overflow(0, SIZE_MAX) == 0);

    // Test safe_mul
    size_t result;
    assert(safe_mul(SIZE_MAX, 2, &result) == -1);
    assert(safe_mul(100, 200, &result) == 0);
    assert(result == 20000);
    assert(safe_mul(0, SIZE_MAX, &result) == 0);
    assert(result == 0);

    // Test safe_add
    assert(safe_add(SIZE_MAX, 1, &result) == -1);
    assert(safe_add(100, 200, &result) == 0);
    assert(result == 300);
    assert(safe_add(SIZE_MAX - 1, 1, &result) == 0);
    assert(result == SIZE_MAX);

    printf("  ✓ Overflow detection works correctly\n");
}

static void test_calculate_capacity_double(void) {
    printf("Testing capacity calculation (GROWTH_DOUBLE)...\n");

    size_t new_cap;
    ArrayResizeConfig config = CONFIG_ARRAY;

    // Starting from 0, should use min_capacity
    assert(calculate_capacity(0, 5, &config, &new_cap) == 0);
    assert(new_cap >= 5 && new_cap >= config.min_capacity);
    printf("  0 -> %zu (needed 5, min %zu)\n", new_cap, config.min_capacity);

    // Doubling from 8
    assert(calculate_capacity(8, 10, &config, &new_cap) == 0);
    assert(new_cap >= 10);
    printf("  8 -> %zu (needed 10)\n", new_cap);

    // Doubling from 100
    assert(calculate_capacity(100, 150, &config, &new_cap) == 0);
    assert(new_cap >= 150);
    printf("  100 -> %zu (needed 150)\n", new_cap);

    // Already sufficient
    assert(calculate_capacity(100, 50, &config, &new_cap) == 0);
    assert(new_cap == 100);

    // Overflow case
    assert(calculate_capacity(SIZE_MAX / 2 + 1, SIZE_MAX / 2 + 2, &config, &new_cap) == -1);
    printf("  ✓ Overflow correctly rejected\n");

    printf("  ✓ GROWTH_DOUBLE strategy works correctly\n");
}

static void test_calculate_capacity_additive(void) {
    printf("Testing capacity calculation (GROWTH_ADDITIVE)...\n");

    size_t new_cap;
    ArrayResizeConfig config = CONFIG_LARGE_BUFFER;

    // Starting from 0
    assert(calculate_capacity(0, 1000, &config, &new_cap) == 0);
    assert(new_cap >= 1000);
    printf("  0 -> %zu (needed 1000)\n", new_cap);

    // Adding increments
    assert(calculate_capacity(10000, 20000, &config, &new_cap) == 0);
    assert(new_cap >= 20000);
    printf("  10000 -> %zu (needed 20000, increment %zu)\n",
           new_cap, config.growth_amount);

    printf("  ✓ GROWTH_ADDITIVE strategy works correctly\n");
}

static void test_calculate_capacity_hybrid(void) {
    printf("Testing capacity calculation (GROWTH_HYBRID)...\n");

    size_t new_cap;
    ArrayResizeConfig config = CONFIG_BUFFER;

    // Small buffer: should double
    assert(calculate_capacity(256, 300, &config, &new_cap) == 0);
    assert(new_cap >= 300);
    printf("  256 -> %zu (needed 300)\n", new_cap);

    // Large buffer: should add increment
    size_t large = SIZE_MAX / 4;
    assert(calculate_capacity(large, large + 1, &config, &new_cap) == 0);
    assert(new_cap >= large + 1);
    printf("  %zu -> %zu (needed %zu)\n", large, new_cap, large + 1);

    printf("  ✓ GROWTH_HYBRID strategy works correctly\n");
}

static void test_calculate_capacity_max_limit(void) {
    printf("Testing capacity calculation with max limit...\n");

    size_t new_cap;
    ArrayResizeConfig config = {
        .min_capacity = 8,
        .max_capacity = 1000,
        .strategy = GROWTH_DOUBLE,
        .growth_amount = 0
    };

    // Should succeed under limit
    assert(calculate_capacity(100, 500, &config, &new_cap) == 0);
    assert(new_cap >= 500 && new_cap <= 1000);
    printf("  100 -> %zu (max 1000)\n", new_cap);

    // Should fail over limit
    assert(calculate_capacity(100, 1001, &config, &new_cap) == -1);
    printf("  ✓ Max capacity limit enforced\n");

    printf("  ✓ Max capacity works correctly\n");
}

static void test_resize_array_basic(void) {
    printf("Testing array_ensure_capacity (basic)...\n");

    int *array = NULL;
    size_t capacity = 0;

    // Initial allocation
    assert(array_ensure_capacity((void**)&array, &capacity, 10, sizeof(int), NULL) == 0);
    assert(array != NULL);
    assert(capacity >= 10);
    printf("  Initial: capacity = %zu\n", capacity);

    // Fill with test data
    for (size_t i = 0; i < 10; i++) {
        array[i] = (int)i * 10;
    }

    // Grow the array
    size_t old_capacity = capacity;
    assert(array_ensure_capacity((void**)&array, &capacity, 50, sizeof(int), NULL) == 0);
    assert(capacity >= 50);
    assert(capacity > old_capacity);
    printf("  Grown: capacity %zu -> %zu\n", old_capacity, capacity);

    // Verify data preserved
    for (int i = 0; i < 10; i++) {
        assert(array[i] == i * 10);
    }

    // Fill more data
    for (size_t i = 10; i < 50; i++) {
        array[i] = (int)i * 10;
    }

    // Request capacity we already have (should be no-op)
    old_capacity = capacity;
    assert(array_ensure_capacity((void**)&array, &capacity, 25, sizeof(int), NULL) == 0);
    assert(capacity == old_capacity);

    free(array);
    printf("  ✓ Basic array resize works correctly\n");
}

static void test_resize_array_overflow(void) {
    printf("Testing array_ensure_capacity (overflow protection)...\n");

    void *array = NULL;
    size_t capacity = 0;

    // Try to allocate array that would overflow in size calculation
    // SIZE_MAX / sizeof(int) is the maximum number of ints we can allocate
    size_t max_count = SIZE_MAX / sizeof(int);

    // This should fail due to overflow
    assert(array_ensure_capacity(&array, &capacity, max_count + 1, sizeof(int), NULL) == -1);
    assert(array == NULL);
    assert(capacity == 0);

    printf("  ✓ Overflow in array allocation correctly rejected\n");

    // Test overflow in capacity doubling
    capacity = SIZE_MAX / 2 + 1;
    // The doubling would overflow
    assert(array_ensure_capacity(&array, &capacity, SIZE_MAX / 2 + 2, sizeof(char), NULL) == -1);

    printf("  ✓ Overflow protection works correctly\n");
}

static void test_resize_array_struct(void) {
    printf("Testing array_ensure_capacity with structs...\n");

    typedef struct {
        int id;
        char name[32];
        double value;
    } TestStruct;

    TestStruct *items = NULL;
    size_t capacity = 0;

    // Allocate array of structs
    assert(array_ensure_capacity((void**)&items, &capacity, 5, sizeof(TestStruct), NULL) == 0);
    assert(items != NULL);
    assert(capacity >= 5);

    // Initialize structs
    for (size_t i = 0; i < 5; i++) {
        items[i].id = (int)i;
        snprintf(items[i].name, sizeof(items[i].name), "Item %zu", i);
        items[i].value = (double)i * 1.5;
    }

    // Grow array
    assert(array_ensure_capacity((void**)&items, &capacity, 20, sizeof(TestStruct), NULL) == 0);
    assert(capacity >= 20);

    // Verify data preserved
    for (int i = 0; i < 5; i++) {
        assert(items[i].id == i);
        // Use epsilon comparison for floating point
        double expected = (double)i * 1.5;
        assert(items[i].value >= expected - 0.0001 && items[i].value <= expected + 0.0001);
    }

    free(items);
    printf("  ✓ Struct array resize works correctly\n");
}

static void test_append_buffer_basic(void) {
    printf("Testing buffer_append (basic)...\n");

    char *buffer = NULL;
    size_t capacity = 0;
    size_t size = 0;

    // Append first string
    const char *str1 = "Hello";
    assert(buffer_append((void**)&buffer, &capacity, size, str1, strlen(str1), 1, NULL) == 0);
    assert(buffer != NULL);
    assert(capacity > strlen(str1));
    size += strlen(str1);
    buffer[size] = '\0';
    assert(strcmp(buffer, "Hello") == 0);
    printf("  After first append: '%s' (capacity %zu)\n", buffer, capacity);

    // Append second string
    const char *str2 = " World";
    assert(buffer_append((void**)&buffer, &capacity, size, str2, strlen(str2), 1, NULL) == 0);
    size += strlen(str2);
    buffer[size] = '\0';
    assert(strcmp(buffer, "Hello World") == 0);
    printf("  After second append: '%s' (capacity %zu)\n", buffer, capacity);

    // Append many small strings (test growth)
    size_t old_capacity = capacity;
    for (int i = 0; i < 100; i++) {
        char temp[16];
        snprintf(temp, sizeof(temp), ":%d", i);
        assert(buffer_append((void**)&buffer, &capacity, size, temp, strlen(temp), 1, NULL) == 0);
        size += strlen(temp);
    }
    buffer[size] = '\0';
    assert(capacity > old_capacity);
    printf("  After 100 appends: size %zu, capacity %zu\n", size, capacity);

    free(buffer);
    printf("  ✓ Buffer append works correctly\n");
}

static void test_append_buffer_overflow(void) {
    printf("Testing buffer_append (overflow protection)...\n");

    char *buffer = NULL;
    size_t capacity = 0;
    size_t size = SIZE_MAX - 10;  // Very large current size

    // Try to append data that would overflow
    char data[20] = "test";
    assert(buffer_append((void**)&buffer, &capacity, size, data, 20, 1, NULL) == -1);

    printf("  ✓ Overflow protection works correctly\n");
}

static void test_append_buffer_binary(void) {
    printf("Testing buffer_append (binary data)...\n");

    unsigned char *buffer = NULL;
    size_t capacity = 0;
    size_t size = 0;

    // Append binary data (including nulls)
    unsigned char data1[] = {0x01, 0x02, 0x00, 0x03, 0x04};
    assert(buffer_append((void**)&buffer, &capacity, size, data1, sizeof(data1), 0, NULL) == 0);
    size += sizeof(data1);

    unsigned char data2[] = {0xFF, 0xFE, 0x00, 0xFD};
    assert(buffer_append((void**)&buffer, &capacity, size, data2, sizeof(data2), 0, NULL) == 0);
    size += sizeof(data2);

    // Verify binary data preserved
    assert(memcmp(buffer, data1, sizeof(data1)) == 0);
    assert(memcmp(buffer + sizeof(data1), data2, sizeof(data2)) == 0);

    free(buffer);
    printf("  ✓ Binary buffer append works correctly\n");
}

static void test_macros(void) {
    printf("Testing helper macros...\n");

    // Test ENSURE_ARRAY_CAPACITY
    int *array = NULL;
    size_t capacity = 0;
    assert(ARRAY_ENSURE_CAPACITY(&array, &capacity, 10, int) == 0);
    assert(array != NULL);
    assert(capacity >= 10);
    free(array);

    // Test APPEND_STRING_BUFFER
    char *buf = NULL;
    capacity = 0;
    size_t size = 0;
    assert(BUFFER_APPEND_STRING(&buf, &capacity, size, "test", 4) == 0);
    size += 4;
    buf[size] = '\0';
    assert(strcmp(buf, "test") == 0);
    free(buf);

    // Test APPEND_BINARY_BUFFER
    unsigned char *bin = NULL;
    capacity = 0;
    size = 0;
    unsigned char data[] = {0x01, 0x00, 0x02};
    assert(BUFFER_APPEND_BINARY(&bin, &capacity, size, data, 3) == 0);
    size += 3;
    assert(memcmp(bin, data, 3) == 0);
    free(bin);

    printf("  ✓ Helper macros work correctly\n");
}

static void test_edge_cases(void) {
    printf("Testing edge cases...\n");

    // NULL pointer checks
    size_t capacity = 0;
    assert(array_ensure_capacity(NULL, &capacity, 10, sizeof(int), NULL) == -1);

    void *ptr = NULL;
    assert(array_ensure_capacity(&ptr, NULL, 10, sizeof(int), NULL) == -1);

    assert(buffer_append(NULL, &capacity, 0, "test", 4, 1, NULL) == -1);
    assert(buffer_append(&ptr, NULL, 0, "test", 4, 1, NULL) == -1);

    // Zero element size
    assert(array_ensure_capacity(&ptr, &capacity, 10, 0, NULL) == -1);

    // Zero capacity request (should fail)
    size_t new_cap;
    assert(calculate_capacity(100, 0, NULL, &new_cap) == -1);

    printf("  ✓ Edge cases handled correctly\n");
}

static void test_real_world_pattern_array(void) {
    printf("Testing real-world pattern: dynamic array...\n");

    // Simulate ConversationEntry array pattern from tui.c
    typedef struct {
        char *prefix;
        char *text;
    } Entry;

    Entry *entries = NULL;
    size_t capacity = 0;
    size_t count = 0;

    // Add entries one by one
    for (int i = 0; i < 100; i++) {
        // Check if resize needed
        if (count >= capacity) {
            size_t needed = count + 1;
            if (array_ensure_capacity((void**)&entries, &capacity, needed, sizeof(Entry), NULL) != 0) {
                fprintf(stderr, "Failed to resize array\n");
                break;
            }
        }

        // Add entry
        entries[count].prefix = strdup("PREFIX");
        entries[count].text = malloc(64);
        snprintf(entries[count].text, 64, "Entry %d", i);
        count++;
    }

    assert(count == 100);
    assert(capacity >= 100);
    printf("  Added 100 entries, capacity = %zu\n", capacity);

    // Cleanup
    for (size_t i = 0; i < count; i++) {
        free(entries[i].prefix);
        free(entries[i].text);
    }
    free(entries);

    printf("  ✓ Real-world array pattern works correctly\n");
}

static void test_real_world_pattern_buffer(void) {
    printf("Testing real-world pattern: accumulating output...\n");

    // Simulate bash output accumulation pattern from claude.c
    char *output = NULL;
    size_t capacity = 0;
    size_t total_size = 0;

    // Simulate reading chunks
    const char *chunks[] = {
        "Line 1\n",
        "Line 2\n",
        "A longer line with more text\n",
        "Short\n",
        "Another long line with even more text to test growth\n"
    };

    for (size_t i = 0; i < sizeof(chunks) / sizeof(chunks[0]); i++) {
        size_t len = strlen(chunks[i]);

        // Append chunk
        if (buffer_append((void**)&output, &capacity, total_size, chunks[i], len, 1, NULL) != 0) {
            fprintf(stderr, "Failed to append chunk %zu\n", i);
            break;
        }

        total_size += len;
        output[total_size] = '\0';
    }

    assert(output != NULL);
    assert(strlen(output) == total_size);
    printf("  Accumulated %zu bytes in buffer (capacity %zu)\n", total_size, capacity);

    free(output);
    printf("  ✓ Real-world buffer pattern works correctly\n");
}

int main(void) {
    printf("\n=== Testing Array Resize Module ===\n\n");

    test_overflow_detection();
    test_calculate_capacity_double();
    test_calculate_capacity_additive();
    test_calculate_capacity_hybrid();
    test_calculate_capacity_max_limit();
    test_resize_array_basic();
    test_resize_array_overflow();
    test_resize_array_struct();
    test_append_buffer_basic();
    test_append_buffer_overflow();
    test_append_buffer_binary();
    test_macros();
    test_edge_cases();
    test_real_world_pattern_array();
    test_real_world_pattern_buffer();

    printf("\n=== All Array Resize Tests Passed ===\n\n");
    return 0;
}
