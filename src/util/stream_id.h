#ifndef STREAM_ID_H
#define STREAM_ID_H

#include <stddef.h>
#include <bsd/stdlib.h>

/**
 * Stream ID Utilities
 *
 * Generates random 12-hex-char stream identifiers for grouping
 * streaming delta chunks across provider responses.
 */

#define STREAM_ID_HEX_LEN  12
#define STREAM_ID_BUF_SIZE (STREAM_ID_HEX_LEN + 1)  /* 12 hex chars + NUL */

/**
 * Generate a random 12-hex-char stream identifier (48 bits of entropy).
 *
 * 12 hex chars gives ~16 million IDs for a 50% collision probability via
 * the birthday paradox — more than enough for stream grouping within a
 * session, and visually comparable to YouTube video IDs.
 *
 * Uses arc4random_buf() for cryptographically strong randomness.
 * The result is written into the provided buffer (which must be at
 * least STREAM_ID_BUF_SIZE bytes).
 *
 * @param out  Output buffer (must be at least STREAM_ID_BUF_SIZE bytes)
 */
static inline void generate_stream_id(char out[STREAM_ID_BUF_SIZE]) {
    static const char hex_chars[] = "0123456789abcdef";
    unsigned char random_bytes[6];

    arc4random_buf(random_bytes, sizeof(random_bytes));

    for (int i = 0; i < 6; i++) {
        out[i * 2]     = hex_chars[(random_bytes[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex_chars[random_bytes[i] & 0x0f];
    }

    out[STREAM_ID_HEX_LEN] = '\0';
}

#endif /* STREAM_ID_H */
