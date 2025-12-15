/**
 * test_stubs.h - Declarations for test stub functions
 */

#ifndef TEST_STUBS_H
#define TEST_STUBS_H

// Stubs for tool functions needed by patch_parser
char* read_file(const char *path);
int write_file(const char *path, const char *content);
char* resolve_path(const char *path, const char *working_dir);

#endif // TEST_STUBS_H
