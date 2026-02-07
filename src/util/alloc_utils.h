/*
 * alloc_utils.h - Portable allocation utilities
 *
 * Provides reallocarray() for systems that don't have it (e.g., macOS).
 * reallocarray is a BSD extension that checks for overflow in the size calculation.
 *
 * Usage: Include this header in any file that uses reallocarray.
 *        On systems with native reallocarray, it will use the native version.
 *        On macOS and other systems without it, a fallback is provided.
 */

#ifndef ALLOC_UTILS_H
#define ALLOC_UTILS_H

#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

/*
 * reallocarray is available on:
 * - BSD systems (FreeBSD, OpenBSD, NetBSD, DragonFly BSD)
 * - Linux with glibc 2.26+
 * - NOT available on macOS
 *
 * We provide a fallback implementation for macOS.
 */
#if defined(__APPLE__) && defined(__MACH__)

/* macOS doesn't have reallocarray - provide our own inline implementation */
static inline void *klawed_reallocarray(void *ptr, size_t nmemb, size_t size) {
    /* Check for overflow: if nmemb > SIZE_MAX / size, multiplication would overflow */
    if (nmemb > 0 && size > SIZE_MAX / nmemb) {
        errno = ENOMEM;
        return NULL;
    }
    return realloc(ptr, nmemb * size);
}

/* Define reallocarray to use our implementation */
#define reallocarray klawed_reallocarray

#endif /* __APPLE__ && __MACH__ */

#endif /* ALLOC_UTILS_H */
