/*
 * base64.h - Base64 encoding and decoding utilities
 */

#ifndef BASE64_H
#define BASE64_H

#include <stddef.h>

// Base64 encode binary data
// Parameters:
//   data - pointer to binary data to encode
//   input_length - length of input data in bytes
//   output_length - pointer to store encoded data length (excluding null terminator)
// Returns: allocated string with base64 encoded data, or NULL on failure
// Caller must free the returned string
char *base64_encode(const unsigned char *data, size_t input_length, size_t *output_length);

// Base64 decode string
// Parameters:
//   data - base64 encoded string
//   input_length - length of input string
//   output_length - pointer to store decoded data length
// Returns: allocated buffer with decoded binary data, or NULL on failure
// Caller must free the returned buffer
unsigned char *base64_decode(const char *data, size_t input_length, size_t *output_length);

#endif // BASE64_H
