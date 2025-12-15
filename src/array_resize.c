#include "array_resize.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

// Default configurations
const ArrayResizeConfig CONFIG_ARRAY = {
    .min_capacity = 8,
    .max_capacity = 0,  // No limit (SIZE_MAX)
    .strategy = GROWTH_DOUBLE,
    .growth_amount = 0
};

const ArrayResizeConfig CONFIG_BUFFER = {
    .min_capacity = 256,
    .max_capacity = 0,  // No limit
    .strategy = GROWTH_HYBRID,
    .growth_amount = 4096  // Add at least 4KB when growing
};

const ArrayResizeConfig CONFIG_LARGE_BUFFER = {
    .min_capacity = 4096,
    .max_capacity = 0,  // No limit
    .strategy = GROWTH_ADDITIVE,
    .growth_amount = 65536  // Add 64KB chunks
};

int calculate_capacity(size_t current_capacity,
                      size_t needed_capacity,
                      const ArrayResizeConfig *config,
                      size_t *new_capacity) {
    if (!new_capacity) {
        LOG_ERROR("[array_resize] new_capacity pointer is NULL");
        return -1;
    }

    // Use default config if none provided
    if (!config) {
        config = &CONFIG_ARRAY;
    }

    // Validate needed_capacity
    if (needed_capacity == 0) {
        LOG_ERROR("[array_resize] needed_capacity is 0");
        return -1;
    }

    // Check max_capacity limit (0 means no limit)
    size_t max_cap = (config->max_capacity == 0) ? SIZE_MAX : config->max_capacity;
    if (needed_capacity > max_cap) {
        LOG_ERROR("[array_resize] needed_capacity %zu exceeds max_capacity %zu",
                 needed_capacity, max_cap);
        return -1;
    }

    // If current capacity already sufficient, no change needed
    if (current_capacity >= needed_capacity) {
        *new_capacity = current_capacity;
        return 0;
    }

    size_t calculated_capacity = 0;

    switch (config->strategy) {
        case GROWTH_DOUBLE: {
            // Double the capacity (or start with min_capacity)
            if (current_capacity == 0) {
                calculated_capacity = (config->min_capacity > 0) ? config->min_capacity : 8;
            } else {
                // Check for overflow before doubling
                if (current_capacity > SIZE_MAX / 2) {
                    LOG_ERROR("[array_resize] capacity doubling would overflow: %zu",
                             current_capacity);
                    return -1;
                }
                calculated_capacity = current_capacity * 2;
            }

            // Keep doubling until we meet needed_capacity
            while (calculated_capacity < needed_capacity) {
                if (calculated_capacity > SIZE_MAX / 2) {
                    LOG_ERROR("[array_resize] capacity growth would overflow");
                    return -1;
                }
                calculated_capacity *= 2;
            }
            break;
        }

        case GROWTH_ADDITIVE: {
            // Add fixed amount
            size_t increment = config->growth_amount;
            if (increment == 0) increment = 4096;  // Default

            calculated_capacity = current_capacity;
            if (calculated_capacity == 0) {
                calculated_capacity = (config->min_capacity > 0) ?
                                     config->min_capacity : increment;
            }

            // Keep adding until we meet needed_capacity
            while (calculated_capacity < needed_capacity) {
                if (check_add_overflow(calculated_capacity, increment)) {
                    LOG_ERROR("[array_resize] additive growth would overflow");
                    return -1;
                }
                calculated_capacity += increment;
            }
            break;
        }

        case GROWTH_HYBRID: {
            // Try doubling first
            if (current_capacity == 0) {
                calculated_capacity = (config->min_capacity > 0) ? config->min_capacity : 256;
            } else if (current_capacity <= SIZE_MAX / 2) {
                calculated_capacity = current_capacity * 2;
            } else {
                // Can't double, try adding
                size_t increment = config->growth_amount;
                if (increment == 0) increment = 4096;

                if (check_add_overflow(current_capacity, increment)) {
                    LOG_ERROR("[array_resize] hybrid growth would overflow");
                    return -1;
                }
                calculated_capacity = current_capacity + increment;
            }

            // Ensure we have enough
            if (calculated_capacity < needed_capacity) {
                calculated_capacity = needed_capacity;
            }

            // Also ensure minimum growth amount
            size_t increment = config->growth_amount;
            if (increment > 0 && calculated_capacity < current_capacity + increment) {
                if (check_add_overflow(current_capacity, increment)) {
                    // Just use calculated_capacity
                } else {
                    size_t with_increment = current_capacity + increment;
                    if (with_increment > calculated_capacity) {
                        calculated_capacity = with_increment;
                    }
                }
            }
            break;
        }

        default:
            LOG_ERROR("[array_resize] unknown growth strategy: %d", config->strategy);
            return -1;
    }

    // Clamp to max_capacity
    if (calculated_capacity > max_cap) {
        if (needed_capacity > max_cap) {
            LOG_ERROR("[array_resize] needed_capacity exceeds max_capacity");
            return -1;
        }
        calculated_capacity = max_cap;
    }

    // Ensure minimum capacity
    if (config->min_capacity > 0 && calculated_capacity < config->min_capacity) {
        calculated_capacity = config->min_capacity;
    }

    *new_capacity = calculated_capacity;
    return 0;
}

int array_ensure_capacity(void **ptr,
                     size_t *current_capacity,
                     size_t needed_capacity,
                     size_t element_size,
                     const ArrayResizeConfig *config) {
    if (!ptr) {
        LOG_ERROR("[array_resize] ptr is NULL");
        return -1;
    }
    if (!current_capacity) {
        LOG_ERROR("[array_resize] current_capacity pointer is NULL");
        return -1;
    }
    if (element_size == 0) {
        LOG_ERROR("[array_resize] element_size is 0");
        return -1;
    }

    // Check if resize is needed
    if (*current_capacity >= needed_capacity) {
        return 0;  // Already have enough capacity
    }

    // Calculate new capacity
    size_t new_capacity = 0;
    if (calculate_capacity(*current_capacity, needed_capacity, config, &new_capacity) != 0) {
        return -1;
    }

    // Check for overflow in size calculation (new_capacity * element_size)
    size_t new_size = 0;
    if (safe_mul(new_capacity, element_size, &new_size) != 0) {
        LOG_ERROR("[array_resize] size calculation overflow: %zu * %zu",
                 new_capacity, element_size);
        return -1;
    }

    // Perform the realloc
    void *new_ptr = realloc(*ptr, new_size);
    if (!new_ptr) {
        LOG_ERROR("[array_resize] realloc failed for %zu bytes", new_size);
        return -1;
    }

    // Success: update pointers
    *ptr = new_ptr;
    *current_capacity = new_capacity;

    LOG_DEBUG("[array_resize] resized array: capacity %zu -> %zu (%zu bytes)",
             *current_capacity - new_capacity + *current_capacity,
             new_capacity, new_size);

    return 0;
}

int buffer_append(void **buffer,
                      size_t *current_capacity,
                      size_t current_size,
                      const void *data,
                      size_t data_size,
                      int null_terminate,
                      const ArrayResizeConfig *config) {
    if (!buffer) {
        LOG_ERROR("[array_resize] buffer pointer is NULL");
        return -1;
    }
    if (!current_capacity) {
        LOG_ERROR("[array_resize] current_capacity pointer is NULL");
        return -1;
    }

    // Calculate space needed
    size_t space_needed = data_size;
    if (null_terminate) {
        if (check_add_overflow(space_needed, 1)) {
            LOG_ERROR("[array_resize] space calculation overflow");
            return -1;
        }
        space_needed += 1;  // For '\0'
    }

    // Calculate total size needed
    size_t needed_size = 0;
    if (safe_add(current_size, space_needed, &needed_size) != 0) {
        LOG_ERROR("[array_resize] total size calculation overflow: %zu + %zu",
                 current_size, space_needed);
        return -1;
    }

    // Resize if needed
    if (*current_capacity < needed_size) {
        size_t new_capacity = 0;
        if (calculate_capacity(*current_capacity, needed_size, config, &new_capacity) != 0) {
            return -1;
        }

        void *new_buffer = realloc(*buffer, new_capacity);
        if (!new_buffer) {
            LOG_ERROR("[array_resize] realloc failed for %zu bytes", new_capacity);
            return -1;
        }

        *buffer = new_buffer;
        *current_capacity = new_capacity;

        LOG_DEBUG("[array_resize] expanded buffer: capacity %zu -> %zu",
                 *current_capacity - new_capacity + *current_capacity,
                 new_capacity);
    }

    // Append data if provided
    if (data && data_size > 0) {
        memcpy((char*)*buffer + current_size, data, data_size);
    }

    return 0;
}

int buffer_reserve(void **ptr,
                      size_t *current_capacity,
                      size_t new_capacity) {
    if (!ptr) {
        LOG_ERROR("[array_resize] ptr is NULL");
        return -1;
    }
    if (!current_capacity) {
        LOG_ERROR("[array_resize] current_capacity pointer is NULL");
        return -1;
    }
    if (new_capacity == 0) {
        LOG_ERROR("[array_resize] new_capacity is 0");
        return -1;
    }

    // Skip if already at desired capacity
    if (*current_capacity == new_capacity) {
        return 0;
    }

    void *new_ptr = realloc(*ptr, new_capacity);
    if (!new_ptr) {
        LOG_ERROR("[array_resize] realloc failed for %zu bytes", new_capacity);
        return -1;
    }

    *ptr = new_ptr;
    *current_capacity = new_capacity;

    LOG_DEBUG("[array_resize] resized buffer: %zu -> %zu bytes",
             *current_capacity - new_capacity + *current_capacity,
             new_capacity);

    return 0;
}
