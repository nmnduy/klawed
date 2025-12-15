/*
 * Text wrapping has been DISABLED in the TUI
 *
 * As of this version, conversational messages are no longer wrapped by the TUI.
 * Instead, text wraps naturally at the terminal width, just like standard
 * terminal output (e.g., from `cat` or `echo`).
 *
 * This test file is kept for historical reference but all tests are now no-ops.
 * The custom text wrapping functions (wrap_text, find_wrap_position) have been
 * removed from tui.c.
 *
 * Rationale for removal:
 * - Simpler code with fewer edge cases and bugs
 * - Natural terminal wrapping behavior that users expect
 * - Better performance (no string processing overhead)
 * - Consistent with standard UNIX terminal behavior
 */

#include <stdio.h>

int main(void) {
    printf("========================================\n");
    printf("Text Wrapping Tests - DISABLED\n");
    printf("========================================\n");
    printf("\n");
    printf("Text wrapping has been removed from the TUI.\n");
    printf("Messages now wrap naturally at terminal width.\n");
    printf("\n");
    printf("This matches standard terminal behavior where\n");
    printf("long lines wrap at the edge of the terminal,\n");
    printf("just like output from cat, echo, or any other\n");
    printf("standard UNIX command.\n");
    printf("\n");
    printf("All previous text wrapping tests are now obsolete.\n");
    printf("========================================\n");

    return 0;
}
