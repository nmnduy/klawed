/*
 * klawed_md.h - Read and parse KLAWED.md file
 *
 * Functions to read the KLAWED.md file from the working directory
 * and include it in the system prompt.
 */

#ifndef CONTEXT_KLAWED_MD_H
#define CONTEXT_KLAWED_MD_H

/**
 * Read KLAWED.md from the working directory if it exists.
 * Caller must free the returned string.
 *
 * Returns: Newly allocated string with file contents, or NULL if file doesn't exist
 */
char* read_klawed_md(const char *working_dir);

#endif /* CONTEXT_KLAWED_MD_H */
