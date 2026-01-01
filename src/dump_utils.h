#ifndef DUMP_UTILS_H
#define DUMP_UTILS_H

#include <stdio.h>

// Dump assistant response content to the provided FILE*.
// Returns 1 if any content was printed, 0 otherwise.
int dump_response_content(const char *response_json, FILE *out);

#endif // DUMP_UTILS_H
