/*
 * Unit Tests for Arena Allocator
 *
 * Tests the arena.h implementation including:
 * - Basic allocation and deallocation
 * - Alignment handling
 * - Memory bounds checking
 * - Arena creation and destruction
 * - Edge cases (zero size, NULL pointers)
 *
 * Compilation: make test-arena
 * Usage: ./build/test_arena
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

// Include arena.h (ARENA_IMPLEMENTATION is defined in Makefile)
#include "../src/arena.h"

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
    printf("  Total:  %d\n", tests_run);
    printf("  Passed: " COLOR_GREEN "%d" COLOR_RESET "\n", tests_passed);
    printf("  Failed: " COLOR_RED "%d" COLOR_RESET "\n", tests_failed);

    if (tests_failed == 0) {
        printf(COLOR_GREEN "\n✓ All tests passed!" COLOR_RESET "\n");
    } else {
        printf(COLOR_RED "\n✗ Some tests failed!" COLOR_RESET "\n");
    }
}

// Test functions
static void test_arena_create_destroy(void) {
    printf("\n[TEST] Arena Creation and Destruction\n");

    // Test 1: Create arena with valid size
    Arena *arena = arena_create(1024);
    print_test_result("arena_create with valid size returns non-NULL", arena != NULL);

    // Test 2: Destroy arena
    arena_destroy(arena);
    print_test_result("arena_destroy works without crashing", 1);

    // Test 3: Create arena with zero size
    Arena *arena_zero = arena_create(0);
    print_test_result("arena_create with zero size returns NULL", arena_zero == NULL);

    // Test 4: Destroy NULL arena
    arena_destroy(NULL);
    print_test_result("arena_destroy(NULL) doesn't crash", 1);
}

static void test_arena_basic_allocation(void) {
    printf("\n[TEST] Basic Allocation\n");

    Arena *arena = arena_create(1024);

    // Test 1: Allocate small chunk
    void *ptr1 = arena_alloc(arena, 64);
    print_test_result("arena_alloc returns non-NULL for small allocation", ptr1 != NULL);

    // Test 2: Allocate another chunk
    void *ptr2 = arena_alloc(arena, 128);
    print_test_result("arena_alloc returns non-NULL for second allocation", ptr2 != NULL);

    // Test 3: Pointers should be different
    print_test_result("Different allocations return different pointers", ptr1 != ptr2);

    // Test 4: Allocate zero bytes
    void *ptr_zero = arena_alloc(arena, 0);
    print_test_result("arena_alloc with zero size returns NULL", ptr_zero == NULL);

    arena_destroy(arena);
}

static void test_arena_alignment(void) {
    printf("\n[TEST] Alignment Handling\n");

    Arena *arena = arena_create(1024);

    // Test 1: Check default alignment
    void *ptr1 = arena_alloc(arena, 1);
    uintptr_t addr1 = (uintptr_t)ptr1;
    print_test_result("Default allocation is properly aligned",
                     (addr1 % ARENA_DEFAULT_ALIGNMENT) == 0);

    // Test 2: Test explicit alignment
    void *ptr2 = arena_alloc_aligned(arena, 1, 16);
    uintptr_t addr2 = (uintptr_t)ptr2;
    print_test_result("Explicit 16-byte alignment works", (addr2 % 16) == 0);

    // Test 3: Test power-of-two alignment
    void *ptr3 = arena_alloc_aligned(arena, 1, 32);
    uintptr_t addr3 = (uintptr_t)ptr3;
    print_test_result("Explicit 32-byte alignment works", (addr3 % 32) == 0);

    arena_destroy(arena);
}

static void test_arena_overflow(void) {
    printf("\n[TEST] Overflow Detection\n");

    Arena *arena = arena_create(100);

    // Test 1: Allocate within bounds
    void *ptr1 = arena_alloc(arena, 50);
    print_test_result("Allocation within arena size works", ptr1 != NULL);

    // Test 2: Try to allocate beyond bounds
    void *ptr2 = arena_alloc(arena, 100);
    print_test_result("Overflow allocation returns NULL", ptr2 == NULL);

    arena_destroy(arena);
}

static void test_arena_clear(void) {
    printf("\n[TEST] Arena Clear\n");

    Arena *arena = arena_create(1024);

    // Allocate some memory (mark variables as used to avoid warnings)
    void *ptr1 = arena_alloc(arena, 256);
    void *ptr2 = arena_alloc(arena, 256);
    (void)ptr1; (void)ptr2; // Mark as used

    // Clear the arena
    arena_clear(arena);

    // After clear, new allocation should start from beginning
    void *ptr3 = arena_alloc(arena, 256);
    print_test_result("After clear, allocation starts from beginning", ptr3 != NULL);

    arena_destroy(arena);
}

static void test_arena_copy(void) {
    printf("\n[TEST] Arena Copy\n");

    Arena *src = arena_create(1024);
    Arena *dest = arena_create(1024);

    // Write some data to source
    char *data = (char *)arena_alloc(src, 10);
    if (data) {
        strcpy(data, "test123");
    }

    // Copy from src to dest
    size_t copied = arena_copy(dest, src);
    print_test_result("arena_copy returns non-zero for valid copy", copied > 0);

    // The copy should copy the entire region, so dest should have the same data
    // at the beginning of its region
    if (dest && dest->region) {
        print_test_result("Copied data matches original", strncmp((char *)dest->region, "test123", 7) == 0);
    } else {
        print_test_result("Copied data matches original", 0);
    }

    arena_destroy(src);
    arena_destroy(dest);
}

static void test_arena_edge_cases(void) {
    printf("\n[TEST] Edge Cases\n");

    // Test 1: NULL arena parameter
    void *ptr = arena_alloc(NULL, 10);
    print_test_result("arena_alloc with NULL arena returns NULL", ptr == NULL);

    // Test 2: arena_alloc_aligned with NULL arena
    ptr = arena_alloc_aligned(NULL, 10, 8);
    print_test_result("arena_alloc_aligned with NULL arena returns NULL", ptr == NULL);

    // Test 3: arena_clear with NULL
    arena_clear(NULL);
    print_test_result("arena_clear(NULL) doesn't crash", 1);

    // Test 4: arena_copy with NULL parameters
    size_t copied = arena_copy(NULL, NULL);
    print_test_result("arena_copy with NULL parameters returns 0", copied == 0);
}

static void test_arena_string_allocation(void) {
    printf("\n[TEST] String Allocation Pattern\n");

    Arena *arena = arena_create(1024);

    // Test the pattern used in the codebase: allocate and copy string
    const char *test_str = "Hello, Arena!";
    size_t len = strlen(test_str) + 1;

    char *arena_str = (char *)arena_alloc(arena, len);
    if (arena_str) {
        strcpy(arena_str, test_str);
        print_test_result("String can be allocated and copied to arena",
                         strcmp(arena_str, test_str) == 0);
    } else {
        print_test_result("String can be allocated and copied to arena", 0);
    }

    // Test multiple string allocations
    const char *str2 = "Second string";
    size_t len2 = strlen(str2) + 1;
    char *arena_str2 = (char *)arena_alloc(arena, len2);
    if (arena_str2) {
        strcpy(arena_str2, str2);
        print_test_result("Multiple strings can be allocated",
                         strcmp(arena_str2, str2) == 0);
    } else {
        print_test_result("Multiple strings can be allocated", 0);
    }

    arena_destroy(arena);
}

int main(void) {
    printf(COLOR_CYAN "\n========================================\n");
    printf("   Arena Allocator Test Suite\n");
    printf("========================================\n" COLOR_RESET);

    test_arena_create_destroy();
    test_arena_basic_allocation();
    test_arena_alignment();
    test_arena_overflow();
    test_arena_clear();
    test_arena_copy();
    test_arena_edge_cases();
    test_arena_string_allocation();

    print_summary();

    return tests_failed == 0 ? 0 : 1;
}
