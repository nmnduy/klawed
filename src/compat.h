/*
 * compat.h - Compatibility functions for non-BSD systems
 *
 * Provides fallback implementations of BSD functions like strlcpy/strlcat
 * for systems that don't have them in their standard library.
 */

#ifndef COMPAT_H
#define COMPAT_H

#include <string.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdint.h>

/* Handle strlcpy/strlcat availability */
#ifdef HAVE_STRLCPY
/* strlcpy detected by configure - we have libbsd */
/* On Linux with libbsd, we need bsd/string.h */
/* On BSD systems (including macOS), strlcpy is in string.h */
#if defined(__linux__)
#include <bsd/string.h>
#endif
/* On other systems (macOS, *BSD), strlcpy is already in string.h */
#else
/* strlcpy not detected by configure */
/* It might still be available as a macro (e.g., on macOS) */
/* We'll use it if available, otherwise define our own */
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
/* On these systems, strlcpy/strlcat are in string.h */
/* Don't define our own - use the system one */
#else
/* On other systems without HAVE_STRLCPY, define our own implementations */

/*
 * strlcpy - Copy string with size limitation
 * 
 * Copy src to string dst of size siz. At most siz-1 characters
 * will be copied. Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
static inline size_t strlcpy(char *dst, const char *src, size_t siz) {
    char *d = dst;
    const char *s = src;
    size_t n = siz;
    
    /* Copy as many bytes as will fit */
    if (n != 0) {
        while (--n != 0) {
            if ((*d++ = *s++) == '\0')
                break;
        }
    }
    
    /* Not enough room in dst, add NUL and traverse rest of src */
    if (n == 0) {
        if (siz != 0)
            *d = '\0';      /* NUL-terminate dst */
        while (*s++)
            ;
    }
    
    return (s - src - 1);   /* count does not include NUL */
}

/*
 * strlcat - Concatenate string with size limitation
 *
 * Appends src to string dst of size siz (unlike strncat, siz is the
 * full size of dst, not space left). At most siz-1 characters
 * will be copied. Always NUL terminates (unless siz <= strlen(dst)).
 * Returns strlen(src) + MIN(siz, strlen(initial dst)).
 * If retval >= siz, truncation occurred.
 */
static inline size_t strlcat(char *dst, const char *src, size_t siz) {
    char *d = dst;
    const char *s = src;
    size_t n = siz;
    size_t dlen;
    
    /* Find the end of dst and adjust bytes left but don't go past end */
    while (n-- != 0 && *d != '\0')
        d++;
    dlen = d - dst;
    n = siz - dlen;
    
    if (n == 0)
        return(dlen + strlen(s));
    
    while (*s != '\0') {
        if (n != 1) {
            *d++ = *s;
            n--;
        }
        s++;
    }
    *d = '\0';
    
    return(dlen + (s - src));   /* count does not include NUL */
}

#endif /* __APPLE__ etc */
#endif /* HAVE_STRLCPY */

/* reallocarray is another useful function from libbsd */
/* Provide a fallback implementation if not available */
#ifndef HAVE_REALLOCARRAY
/* Simple fallback implementation of reallocarray */
static inline void *reallocarray(void *ptr, size_t nmemb, size_t size) {
    /* Check for overflow in multiplication */
    if (nmemb > 0 && size > SIZE_MAX / nmemb) {
        return NULL;
    }
    return realloc(ptr, nmemb * size);
}
#endif

#endif /* COMPAT_H */
