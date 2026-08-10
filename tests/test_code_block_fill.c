/*
 * test_code_block_fill.c — Verify code block background fill doesn't
 * produce extra blank lines.
 *
 * Renders simulated code blocks through the exact LinePrinter path
 * the TUI uses, then inspects the pad content to check for extra
 * blank lines caused by scrollok auto-wrap + explicit \n.
 *
 * Compile:
 *   cc -std=c11 -D_DARWIN_C_SOURCE -I/opt/homebrew/include -I. -DTEST_BUILD \
 *      -o build/test_code_block_fill tests/test_code_block_fill.c \
 *      build/line_printer.o build/markdown_render.o \
 *      -L/opt/homebrew/lib -lncurses -lcjson
 *
 * Run:
 *   ./build/test_code_block_fill
 */
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/line_printer.h"
#include "../src/tui.h"
#include "../src/markdown_render.h"

/*
 * Dump pad to a text file for visual inspection with `cat -vet`.
 * Non-printable chars become `.`; spaces show as `_` so blank lines
 * are clearly visible as runs of `_`.
 */
static void dump_pad_readable(WINDOW *pad, int nlines, int ncols,
                               const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        perror("fopen");
        return;
    }
    for (int y = 0; y < nlines; y++) {
        /* Find the rightmost written column to avoid printing
         * thousands of uninit pad cells. */
        int last_written = -1;
        for (int x = ncols - 1; x >= 0; x--) {
            chtype ch = mvwinch(pad, y, x);
            if (ch != (chtype)ERR) {
                last_written = x;
                break;
            }
        }
        if (last_written < 0) {
            /* Entirely uninitialised row — pad hasn't been written
             * this far. Stop dumping. */
            break;
        }
        fprintf(f, "%3d: ", y);
        for (int x = 0; x <= last_written; x++) {
            chtype ch = mvwinch(pad, y, x);
            if (ch == (chtype)ERR) {
                fputc('?', f);
            } else {
                char c = (char)(ch & A_CHARTEXT);
                if (c == ' ')
                    fputc('_', f);
                else if (c == '\0')
                    fputc('.', f);
                else if (c < 32 || c > 126)
                    fprintf(f, "[%02x]", (unsigned char)c);
                else
                    fputc(c, f);
            }
        }
        fputc('\n', f);
    }
    fclose(f);
}

/*
 * Run one sub-test: render `nlines` lines of code through LinePrinter,
 * then verify there are no extra blank lines between content lines.
 *
 * Returns 0 if content lines appear to be consecutive (no extra blanks
 * between them), 1 if issues found.
 */
static int test_code_block(const char *name, const char **lines, int nlines,
                           int pad_width, int pad_height) {
    WINDOW *pad = newpad(pad_height, pad_width);
    if (!pad) {
        fprintf(stderr, "newpad failed\n");
        return 1;
    }
    scrollok(pad, TRUE);

    int code_pair = NCURSES_PAIR_CODE_BLOCK;
    int border_pair = NCURSES_PAIR_TOOL_DIM;
    const char *border_str = "│ ";

    LinePrinter lp;
    lp_init(&lp, NULL, pad, border_str, border_pair, code_pair, pad_width);

    for (int i = 0; i < nlines; i++) {
        lp_border(&lp);
        if (has_colors())
            wattron(pad, COLOR_PAIR((unsigned)code_pair));
        if (lines[i] && strlen(lines[i]) > 0)
            waddstr(pad, lines[i]);
        if (has_colors())
            wattroff(pad, COLOR_PAIR((unsigned)code_pair));
        lp_newline(&lp);
    }

    /* Find the last row that has been written to. */
    int last_row = -1;
    for (int y = pad_height - 1; y >= 0; y--) {
        chtype ch = mvwinch(pad, y, 0);
        if (ch != (chtype)ERR) {
            last_row = y;
            break;
        }
    }

    int issues = 0;

    /* Check: the first `nlines` rows should have content (non-space
     * after the border).  All should be consecutive — no blank line
     * between them. */
    int prev_content = -2;
    for (int y = 0; y <= last_row; y++) {
        int has_content = 0;
        for (int x = 2; x < pad_width; x++) {
            chtype ch = mvwinch(pad, y, x);
            if (ch == (chtype)ERR) break;
            char c = (char)(ch & A_CHARTEXT);
            if (c != ' ' && c != '\0') {
                has_content = 1;
                break;
            }
        }
        if (has_content) {
            if (prev_content >= 0 && y > prev_content + 1) {
                printf("  ISSUE: blank line between row %d and %d\n",
                       prev_content, y);
                issues++;
            }
            prev_content = y;
        }
    }

    int content_rows = (prev_content >= 0) ? (prev_content + 1) : 0;
    int expected = nlines;  /* We rendered N lines, all should appear */
    if (content_rows != expected) {
        printf("  NOTE: content on %d rows, expected ~%d (content on "
               "row 0 is row index 0)\n", content_rows, expected);
        /* Not a hard failure — depends on whether empty code lines
         * render as visibly blank. */
    }

    printf("  %s: %d content rows, %d gap issues\n",
           issues == 0 ? "OK" : "FAIL", content_rows, issues);

    /* Write a visual dump */
    char path[256];
    snprintf(path, sizeof(path), "/tmp/klawed_test_%s.txt", name);
    dump_pad_readable(pad, pad_height, pad_width, path);
    printf("  Visual dump: %s\n", path);

    delwin(pad);
    return issues;
}

int main(void) {
    FILE *devnull = fopen("/dev/null", "w");
    SCREEN *scr = newterm(NULL, devnull, stdin);
    if (!scr) {
        fprintf(stderr, "SKIP: cannot create ncurses screen (headless)\n");
        fclose(devnull);
        return 0;
    }
    set_term(scr);
    noecho();

    const int PW = 60, PH = 20;
    int total_issues = 0;

    /* Test A: 3 short code lines, no empty lines */
    {
        const char *lines[] = { "def foo():", "    x = 1", "    return x" };
        printf("\n=== Test A: 3 short code lines ===\n");
        total_issues += test_code_block("short3", lines, 3, PW, PH);
    }

    /* Test B: code lines with empty lines interleaved */
    {
        const char *lines[] = { "def bar():", "", "    pass", "", "    return 0" };
        printf("\n=== Test B: code with internal blank lines ===\n");
        total_issues += test_code_block("blanks", lines, 5, PW, PH);
    }

    /* Test C: line that fills content width exactly, then short line */
    {
        char long_line[59];
        memset(long_line, 'x', 58);   /* pad_width(60) - border(2) = 58 */
        long_line[58] = '\0';
        const char *lines[] = { long_line, "y = 42" };
        printf("\n=== Test C: full-width line + short line ===\n");
        total_issues += test_code_block("fullwidth", lines, 2, PW, PH);
    }

    /* Test D: single-line code block (edge case) */
    {
        const char *lines[] = { "pass" };
        printf("\n=== Test D: single-line code block ===\n");
        total_issues += test_code_block("single", lines, 1, PW, PH);
    }

    /* Test E: wider pad (80 cols — more realistic) */
    {
        const int pw = 80, ph = 10;
        const char *lines[] = { "import os", "x = list(range(100))",
                                "print(x[:5])" };
        printf("\n=== Test E: 80-col pad ===\n");
        total_issues += test_code_block("wide", lines, 3, pw, ph);
    }

    printf("\n===========================\n");
    printf("Total gap issues: %d\n", total_issues);
    printf("Overall: %s\n", total_issues == 0 ? "PASS — no extra blank lines"
                                            : "FAIL — blank line gaps found");
    printf("\nVisual dumps in /tmp/klawed_test_*.txt\n");
    printf("Inspect with: for f in /tmp/klawed_test_*.txt; do\n");
    printf("  echo \"=== $f ===\"; cat \"$f\"; done\n");

    endwin();
    fclose(devnull);
    return total_issues == 0 ? 0 : 1;
}
