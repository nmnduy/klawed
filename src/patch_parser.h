#ifndef PATCH_PARSER_H
#define PATCH_PARSER_H

#include <cjson/cJSON.h>
#include "claude_internal.h"

// Represents a single file operation in a patch
typedef struct {
    char *file_path;      // File path to edit
    char *old_content;    // Content to replace (between @@ markers)
    char *new_content;    // Replacement content
} PatchOperation;

// Represents a parsed patch
typedef struct {
    PatchOperation *operations;
    int operation_count;
    int is_valid;
    char *error_message;
} ParsedPatch;

// Check if content appears to be in the "Begin Patch/End Patch" format
int is_patch_format(const char *content);

// Parse the patch format and extract operations
ParsedPatch* parse_patch_format(const char *content);

// Free a ParsedPatch structure
void free_parsed_patch(ParsedPatch *patch);

// Apply a parsed patch to the filesystem
// Returns a cJSON object with success/error status
cJSON* apply_patch(ParsedPatch *patch, ConversationState *state);

#endif // PATCH_PARSER_H
