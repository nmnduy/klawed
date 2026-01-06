# Security Fixes TODO List

## Status
**Last Updated:** 2026-01-06
**Completed:**
- ✓ Fixed command injection in `write_file()` by replacing `system()` with `mkdir_p()`
- ✓ Fixed command injection in `save_binary_file()` by replacing `system()` with `mkdir_p()`
- ✓ Implemented secure memory wiping with `secure_free()` function
- ✓ Updated AWS credential handling to use `secure_free()`
- ✓ Added security hardening flags to Makefile (`-fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE -Wl,-pie` with Linux-only `-Wl,-z,relro -Wl,-z,now`)

## Critical Fixes (Immediate)

### 1. Fix Command Injection in `write_file()` - **COMPLETED**
**File:** `src/klawed.c`
**Status:** Fixed in commit [current]
**Fix:** Replaced `system("mkdir -p")` with `mkdir_p()` function using `mkdir()` system calls
**File:** `src/klawed.c`
**Issue:** `system()` call with user-controlled path in `mkdir -p` command
**Fix Options:**
- Option A: Replace with `mkdir()` system call (preferred)
- Option B: Use `execvp()` with proper argument handling
- Option C: Implement proper shell escaping for all metacharacters

**Implementation Plan (Option A):**
```c
// Replace system() call with mkdir() recursion
int create_directory_recursive(const char *path) {
    char *path_copy = strdup(path);
    if (!path_copy) return -1;
    
    char *p = path_copy;
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            if (strlen(path_copy) > 0) {
                mkdir(path_copy, 0755);  // Ignore errors (directory may exist)
            }
            *p = '/';
        }
        p++;
    }
    mkdir(path_copy, 0755);  // Create final directory
    free(path_copy);
    return 0;
}
```

### 2. Implement Secure Memory Wiping - **COMPLETED**
**Files:** Multiple files handling API keys and secrets
**Status:** Fixed in commit [current]
**Issue:** Sensitive data not securely wiped from memory
**Fix:** Use `explicit_bzero()` before `free()`

**Implementation:**
1. Created helper function `secure_free()` in `src/tool_utils.c`:
```c
void secure_free(void *ptr, size_t size) {
    if (!ptr) return;
    
    // Securely wipe memory before freeing
    #ifdef __APPLE__
    // On macOS, use memset_s if available, otherwise use volatile memset
    #ifdef memset_s
    memset_s(ptr, size, 0, size);
    #else
    // Use volatile pointer to prevent compiler optimization
    volatile char *vptr = (volatile char *)ptr;
    for (size_t i = 0; i < size; i++) {
        vptr[i] = 0;
    }
    #endif
    #else
    // On other systems, use explicit_bzero from libbsd
    explicit_bzero(ptr, size);
    #endif
    
    free(ptr);
}
```
```

2. Update all API key/secret freeing to use `secure_free()`
3. Update AWS credential handling in `aws_bedrock.c`
4. Update OpenAI/Anthropic provider credential handling

## Medium Priority Fixes

### 3. Add Security Hardening Flags - **COMPLETED**
**File:** `Makefile`
**Status:** Fixed in commit [current]
**Issue:** Missing modern security hardening flags
**Fix:** Added to CFLAGS and LDFLAGS with OS-specific handling

**Implementation:**
```makefile
# Added to CFLAGS
CFLAGS += -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE

# Added to LDFLAGS with macOS/Linux compatibility
LDFLAGS += -Wl,-pie
ifeq ($(UNAME_S),Linux)
    LDFLAGS += -Wl,-z,relro -Wl,-z,now
endif
```

### 4. Enhance Bash Tool Security
**File:** `src/klawed.c` (tool_bash function)
**Issue:** Potential shell injection despite escaping
**Fix Options:**
- Option A: Use `execvp()` with argument array
- Option B: Implement comprehensive shell escaping
- Option C: Add dangerous pattern detection

**Implementation Plan (Option B):**
```c
// Enhanced shell escaping function
char *escape_shell_argument(const char *arg) {
    // Escape all shell metacharacters: ' " ` $ \ ! * ? [ ] { } ( ) < > & | ; ~ # \n \t space
    // Or better: wrap in single quotes and escape single quotes
    size_t len = strlen(arg);
    size_t escaped_len = len + 2;  // For wrapping quotes
    for (size_t i = 0; i < len; i++) {
        if (arg[i] == '\'') escaped_len += 3;  // '\'' -> '\'\\\'''
    }
    
    char *escaped = malloc(escaped_len + 1);
    if (!escaped) return NULL;
    
    char *p = escaped;
    *p++ = '\'';
    for (size_t i = 0; i < len; i++) {
        if (arg[i] == '\'') {
            *p++ = '\'';
            *p++ = '\\';
            *p++ = '\'';
            *p++ = '\'';
        } else {
            *p++ = arg[i];
        }
    }
    *p++ = '\'';
    *p = '\0';
    
    return escaped;
}
```

## Low Priority Improvements

### 5. Comprehensive Input Validation
**Files:** All tool implementations
**Issue:** Inconsistent input validation
**Fix:** Create validation helper functions

**Implementation Plan:**
```c
// Validation helpers
int validate_file_path(const char *path) {
    // Check for path traversal attempts
    if (strstr(path, "..")) return 0;
    if (strstr(path, "//")) return 0;
    // Add more checks as needed
    return 1;
}

int validate_command(const char *cmd) {
    // Check for dangerous patterns
    const char *dangerous[] = {"&&", "||", ";", "`", "$(", ">", "<", "|", NULL};
    for (int i = 0; dangerous[i]; i++) {
        if (strstr(cmd, dangerous[i])) return 0;
    }
    return 1;
}
```

### 6. Audit All System/Popen Calls
**Files:** Multiple files
**Issue:** Potential command injection in other `system()`/`popen()` calls
**Fix:** Audit and secure all shell command executions

**Files to check:**
- `src/klawed.c` (multiple `system()` and `popen()` calls)
- `src/commands.c`
- `src/voice_input.c`
- `src/file_search.c`
- `src/tui.c`

## Testing Plan

### 1. Unit Tests for Security Fixes
- Test `create_directory_recursive()` function
- Test `secure_free()` function
- Test `escape_shell_argument()` function
- Test validation helper functions

### 2. Integration Tests
- Test file operations with malicious paths
- Test bash tool with injection attempts
- Test API key handling

### 3. Static Analysis
- Run `clang --analyze` after fixes
- Run with all warnings enabled
- Check for new vulnerabilities

## Timeline

### Week 1:
- Fix command injection in `write_file()`
- Implement secure memory wiping
- Update Makefile with hardening flags

### Week 2:
- Enhance bash tool security
- Implement input validation helpers
- Begin audit of other `system()`/`popen()` calls

### Week 3:
- Complete audit of shell command executions
- Write security unit tests
- Update documentation

### Week 4:
- Security review and testing
- Finalize fixes
- Update SECURITY_AUDIT.md with fixes applied

## Success Criteria

1. No command injection vulnerabilities
2. All sensitive data securely wiped from memory
3. Build includes modern security hardening
4. All shell commands properly escaped or avoided
5. Comprehensive input validation
6. Passing security tests