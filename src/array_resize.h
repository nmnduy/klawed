#ifndef SAFE_MEMORY_H
#define SAFE_MEMORY_H

#include <stddef.h>
#include <stdint.h>

/**
 * array_resize.h - Array and buffer resizing utilities
 *
 * This module provides overflow-safe array and buffer operations with:
 * - Integer overflow checking
 * - Standardized growth strategies
 * - Consistent error handling
 * - NULL pointer safety
 *
 * All functions return 0 on success, -1 on failure.
 * On failure, the original pointer is NOT freed (caller must handle cleanup).
 */

// Growth strategies for different use cases
typedef enum {
    GROWTH_DOUBLE,      // Double capacity (good for arrays)
    GROWTH_ADDITIVE,    // Add fixed amount (good for large buffers)
    GROWTH_HYBRID       // Double or add minimum, whichever is larger
} GrowthStrategy;

// Configuration for resize operations
typedef struct {
    size_t min_capacity;     // Minimum capacity (0 = no minimum)
    size_t max_capacity;     // Maximum capacity (0 = SIZE_MAX)
    GrowthStrategy strategy; // Growth strategy to use
    size_t growth_amount;    // For GROWTH_ADDITIVE: amount to add
} ArrayResizeConfig;

// Default configurations for common use cases
extern const ArrayResizeConfig CONFIG_ARRAY;         // For pointer arrays
extern const ArrayResizeConfig CONFIG_BUFFER;        // For string buffers
extern const ArrayResizeConfig CONFIG_LARGE_BUFFER;  // For large data buffers

/**
 * Overflow-safe arithmetic operations
 */

// Check if a * b would overflow size_t
// Returns: 1 if overflow would occur, 0 if safe
static inline int check_mul_overflow(size_t a, size_t b) {
    if (a == 0 || b == 0) return 0;
    return a > SIZE_MAX / b;
}

// Check if a + b would overflow size_t
// Returns: 1 if overflow would occur, 0 if safe
static inline int check_add_overflow(size_t a, size_t b) {
    return a > SIZE_MAX - b;
}

// Safe multiplication with overflow check
// Returns: 0 on success, -1 on overflow
static inline int safe_mul(size_t a, size_t b, size_t *result) {
    if (check_mul_overflow(a, b)) return -1;
    *result = a * b;
    return 0;
}

// Safe addition with overflow check
// Returns: 0 on success, -1 on overflow
static inline int safe_add(size_t a, size_t b, size_t *result) {
    if (check_add_overflow(a, b)) return -1;
    *result = a + b;
    return 0;
}

/**
 * Calculate next capacity using specified growth strategy
 *
 * @param current_capacity Current capacity
 * @param needed_capacity Minimum capacity required
 * @param config Configuration (can be NULL for defaults)
 * @param new_capacity Output: calculated new capacity
 * @return 0 on success, -1 on overflow or invalid input
 */
int calculate_capacity(size_t current_capacity,
                      size_t needed_capacity,
                      const ArrayResizeConfig *config,
                      size_t *new_capacity);

/**
 * Ensure an array has capacity for at least N elements
 *
 * This function handles:
 * - Integer overflow in capacity calculation
 * - Integer overflow in size calculation (capacity * element_size)
 * - Growth strategy application
 * - NULL pointer safety
 *
 * @param ptr Pointer to array pointer (will be updated on success)
 * @param current_capacity Pointer to current capacity (will be updated)
 * @param needed_capacity Minimum capacity needed
 * @param element_size Size of each element (e.g., sizeof(Type))
 * @param config Configuration (NULL = use CONFIG_ARRAY)
 * @return 0 on success, -1 on failure (original pointer unchanged)
 *
 * Example:
 *   int *array = NULL;
 *   size_t capacity = 0;
 *   if (array_ensure_capacity((void**)&array, &capacity, 10, sizeof(int), NULL) == 0) {
 *       // array now has capacity for at least 10 ints
 *   }
 */
int array_ensure_capacity(void **ptr,
                         size_t *current_capacity,
                         size_t needed_capacity,
                         size_t element_size,
                         const ArrayResizeConfig *config);

/**
 * Append data to a growing buffer
 *
 * This function handles:
 * - Integer overflow in size calculations
 * - Automatic capacity management
 * - NULL terminator space (for strings)
 * - Exponential growth for efficiency
 *
 * @param buffer Pointer to buffer pointer (will be updated on resize)
 * @param current_capacity Pointer to current capacity (will be updated)
 * @param current_size Current used size (not including null terminator)
 * @param data Data to append (can be NULL if just reserving space)
 * @param data_size Size of data to append
 * @param null_terminate If 1, ensure space for '\0' after data
 * @param config Configuration (NULL = use CONFIG_BUFFER)
 * @return 0 on success, -1 on failure
 *
 * Example:
 *   char *buf = NULL;
 *   size_t capacity = 0, size = 0;
 *   if (buffer_append((void**)&buf, &capacity, size, "hello", 5, 1, NULL) == 0) {
 *       size += 5;
 *       buf[size] = '\0';
 *   }
 */
int buffer_append(void **buffer,
                 size_t *current_capacity,
                 size_t current_size,
                 const void *data,
                 size_t data_size,
                 int null_terminate,
                 const ArrayResizeConfig *config);

/**
 * Reserve specific buffer capacity
 *
 * This is a lower-level function for cases where you need exact control.
 * Most code should use array_ensure_capacity() or buffer_append() instead.
 *
 * @param ptr Pointer to buffer pointer (will be updated on success)
 * @param current_capacity Pointer to current capacity (will be updated)
 * @param new_capacity New capacity to allocate
 * @return 0 on success, -1 on failure (original pointer unchanged)
 */
int buffer_reserve(void **ptr,
                  size_t *current_capacity,
                  size_t new_capacity);

/**
 * Helper macros for common patterns
 */

// Ensure array has capacity for at least N elements
#define ARRAY_ENSURE_CAPACITY(ptr, capacity, needed, type) \
    array_ensure_capacity((void**)(ptr), (capacity), (needed), sizeof(type), NULL)

// Append data to a string buffer (with null terminator)
#define BUFFER_APPEND_STRING(buf, capacity, size, data, len) \
    buffer_append((void**)(buf), (capacity), (size), (data), (len), 1, NULL)

// Append data to a binary buffer (no null terminator)
#define BUFFER_APPEND_BINARY(buf, capacity, size, data, len) \
    buffer_append((void**)(buf), (capacity), (size), (data), (len), 0, NULL)

#endif // ARRAY_RESIZE_H
