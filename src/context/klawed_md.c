/*
 * klawed_md.c - Read and parse KLAWED.md file
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <limits.h>

#include "klawed_md.h"

/**
 * Read KLAWED.md from the working directory if it exists.
 * Reads the entire file into memory.
 */
char* read_klawed_md(const char *working_dir) {
    if (!working_dir) {
        return NULL;
    }

    char klawed_md_path[PATH_MAX];
    snprintf(klawed_md_path, sizeof(klawed_md_path), "%s/KLAWED.md", working_dir);

    // Check if file exists
    struct stat st;
    if (stat(klawed_md_path, &st) != 0) {
        return NULL; // File doesn't exist
    }

    // Read the file
    FILE *f = fopen(klawed_md_path, "r");
    if (!f) {
        return NULL;
    }

    // Allocate buffer based on file size
    size_t file_size = (size_t)st.st_size;
    char *content = malloc(file_size + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }

    size_t read_size = fread(content, 1, file_size, f);
    fclose(f);

    if (read_size != file_size) {
        free(content);
        return NULL;
    }

    content[file_size] = '\0';
    return content;
}
