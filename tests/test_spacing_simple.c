// Simple test to check spacing in non-TUI mode
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main() {
    printf("Testing spacing behavior...\n");

    // Simulate what happens in the CLI
    printf("[Assistant] I'll help you test the spacing\n");

    // This is what was causing the extra newline
    // printf("\n");  // REMOVED THIS LINE

    printf("[Tool: Bash] echo 'test'\n");

    // Simulate spinner
    printf("\r\033[K✓ Tool execution completed successfully\n");

    printf("[Assistant] Done!\n");

    return 0;
}