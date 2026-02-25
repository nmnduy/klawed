/*
 * base64.c - Base64 encoding and decoding implementation
 */

#include <stdlib.h>
#include <string.h>
#include "base64.h"

// Base64 encoding table
static const char base64_table[] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
    'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
    'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', '0', '1', '2', '3',
    '4', '5', '6', '7', '8', '9', '+', '/'
};

// Base64 decoding table
static const char base64_decode_table[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 62,  0,  0,  0, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61,  0,  0,  0,  0,  0,  0,
    0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,  0,  0,  0,  0,  0,
    0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,  0,  0,  0,  0,  0
};

char *base64_encode(const unsigned char *data, size_t input_length, size_t *output_length) {
    if (!data || !output_length) {
        return NULL;
    }

    // Calculate output length: 4 * ceil(input_length / 3)
    size_t encoded_length = 4 * ((input_length + 2) / 3);

    // Allocate output buffer with space for null terminator
    char *encoded_data = malloc(encoded_length + 1);
    if (!encoded_data) {
        return NULL;
    }

    size_t i = 0;
    size_t j = 0;

    // Process input in chunks of 3 bytes
    for (i = 0; i < input_length; i += 3) {
        unsigned char octet_a = i < input_length ? data[i] : 0;
        unsigned char octet_b = i + 1 < input_length ? data[i + 1] : 0;
        unsigned char octet_c = i + 2 < input_length ? data[i + 2] : 0;

        unsigned int triple = ((unsigned int)octet_a << 16) + ((unsigned int)octet_b << 8) + octet_c;

        encoded_data[j++] = base64_table[(triple >> 18) & 0x3F];
        encoded_data[j++] = base64_table[(triple >> 12) & 0x3F];
        encoded_data[j++] = base64_table[(triple >> 6) & 0x3F];
        encoded_data[j++] = base64_table[triple & 0x3F];
    }

    // Add padding if needed
    for (size_t k = 0; k < (3 - (input_length % 3)) % 3; k++) {
        encoded_data[encoded_length - 1 - k] = '=';
    }

    encoded_data[encoded_length] = '\0';
    *output_length = encoded_length;

    return encoded_data;
}

unsigned char *base64_decode(const char *data, size_t input_length, size_t *output_length) {
    if (!data || !output_length) {
        return NULL;
    }

    // Skip padding characters at the end
    while (input_length > 0 && data[input_length - 1] == '=') {
        input_length--;
    }

    // Calculate output length: floor(input_length * 3 / 4)
    size_t decoded_length = (input_length * 3) / 4;

    // Allocate output buffer
    unsigned char *decoded_data = malloc(decoded_length + 1);
    if (!decoded_data) {
        return NULL;
    }

    size_t i = 0;
    size_t j = 0;

    // Process input in chunks of 4 characters
    for (i = 0; i < input_length; i += 4) {
        // Get 4 base64 characters
        unsigned char char_a = (unsigned char)(i < input_length ? data[i] : 0);
        unsigned char char_b = (unsigned char)(i + 1 < input_length ? data[i + 1] : 0);
        unsigned char char_c = (unsigned char)(i + 2 < input_length ? data[i + 2] : 0);
        unsigned char char_d = (unsigned char)(i + 3 < input_length ? data[i + 3] : 0);

        // Convert to 6-bit values
        unsigned char sextet_a = (unsigned char)(char_a < 128 ? base64_decode_table[(unsigned char)char_a] : 0);
        unsigned char sextet_b = (unsigned char)(char_b < 128 ? base64_decode_table[(unsigned char)char_b] : 0);
        unsigned char sextet_c = (unsigned char)(char_c < 128 ? base64_decode_table[(unsigned char)char_c] : 0);
        unsigned char sextet_d = (unsigned char)(char_d < 128 ? base64_decode_table[(unsigned char)char_d] : 0);

        // Combine into 24-bit value
        unsigned int triple = ((unsigned int)sextet_a << 18) + ((unsigned int)sextet_b << 12) + ((unsigned int)sextet_c << 6) + sextet_d;

        // Extract 3 bytes
        if (j < decoded_length) decoded_data[j++] = (unsigned char)((triple >> 16) & 0xFF);
        if (j < decoded_length) decoded_data[j++] = (unsigned char)((triple >> 8) & 0xFF);
        if (j < decoded_length) decoded_data[j++] = (unsigned char)(triple & 0xFF);
    }

    // Null terminate for safety (though this is binary data)
    decoded_data[decoded_length] = '\0';
    *output_length = decoded_length;

    return decoded_data;
}
