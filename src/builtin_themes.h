#ifndef BUILTIN_THEMES_H
#define BUILTIN_THEMES_H

#include <stddef.h>

// Structure representing a built-in theme
typedef struct {
    const char *name;      // Theme name (e.g., "dracula")
    const char *content;   // Raw .conf file content
} BuiltInTheme;

// Array of built-in themes
extern const BuiltInTheme built_in_themes[];
// Number of built-in themes
extern const size_t built_in_themes_count;

// Return the content of a built-in theme matching the given filepath.
// Extracts the base filename (without path and .conf extension) and
// compares to built_in_themes[].name.
// Returns NULL if no built-in theme matches.
const char *get_builtin_theme_content(const char *filepath);

#endif // BUILTIN_THEMES_H
