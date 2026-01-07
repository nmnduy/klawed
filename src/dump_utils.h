#ifndef DUMP_UTILS_H
#define DUMP_UTILS_H

#include <stdio.h>

// Dump assistant response content to the provided FILE*.
// Returns 1 if any content was printed, 0 otherwise.
int dump_response_content(const char *response_json, FILE *out);

// Dump a single API call in JSON format
// Returns 1 if successful, 0 otherwise
int dump_api_call_json(
    const char *timestamp,
    const char *request_json,
    const char *response_json,
    const char *model,
    const char *status,
    const char *error_msg,
    int call_num,
    FILE *out
);

// Dump a single API call in Markdown format
// Returns 1 if successful, 0 otherwise
int dump_api_call_markdown(
    const char *timestamp,
    const char *request_json,
    const char *response_json,
    const char *model,
    const char *status,
    const char *error_msg,
    int call_num,
    FILE *out
);

#endif // DUMP_UTILS_H
