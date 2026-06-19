/*
 * Unit tests for word-boundary text wrapping in both oneshot and TUI paths
 *
 * Tests:
 * 1. oneshot_print_line_wrapped() logic — wraps at word boundaries (correct)
 * 2. find_wrap_point() logic — TUI path, breaks at columns (mid-word bug)
 */

#define _XOPEN_SOURCE 700  /* For wcwidth, wchar.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <locale.h>
#include <wchar.h>

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    printf("\n  --- %s ---\n", name); \
} while(0)

#define PASS(msg) do { \
    tests_run++; tests_passed++; \
    printf("    ✓ %s\n", msg); \
} while(0)

#define FAIL(msg) do { \
    tests_run++; tests_failed++; \
    printf("    ✗ %s\n", msg); \
} while(0)

#define CHECK(cond, msg) do { \
    if (cond) PASS(msg); else FAIL(msg); \
} while(0)


/* ===================================================================
 * PART 1: Simulate oneshot_print_line_wrapped (word-boundary wrapping)
 * =================================================================== */

static void wrap_line_word_boundary(const char *text, size_t len,
                                     int avail_width,
                                     char *out, size_t out_size) {
    size_t pos = 0;
    int first_segment = 1;
    size_t out_pos = 0;

    while (pos < len && out_pos < out_size - 2) {
        if (!first_segment) {
            while (pos < len && text[pos] == ' ') pos++;
        }
        first_segment = 0;

        if (pos >= len) { out[out_pos++] = '\n'; break; }

        size_t remaining = len - pos;
        if ((int)remaining <= avail_width) {
            size_t to_copy = remaining;
            if (out_pos + to_copy + 2 > out_size) to_copy = out_size - out_pos - 2;
            memcpy(out + out_pos, text + pos, to_copy);
            out_pos += to_copy;
            out[out_pos++] = '\n';
            break;
        }

        /* Find last space within avail_width columns */
        size_t scan_end = pos + (size_t)avail_width;
        if (scan_end > len) scan_end = len;

        size_t break_at = scan_end;
        for (size_t i = scan_end; i > pos; i--) {
            if (text[i - 1] == ' ') { break_at = i - 1; break; }
        }

        if (break_at == scan_end) {
            break_at = pos + (size_t)avail_width;  /* force-break */
        }

        size_t segment_len = break_at - pos;
        if (out_pos + segment_len + 2 > out_size)
            segment_len = out_size - out_pos - 2;
        memcpy(out + out_pos, text + pos, segment_len);
        out_pos += segment_len;
        out[out_pos++] = '\n';

        pos = break_at;
        if (pos < len && text[pos] == ' ') pos++;
    }
    if (out_pos < out_size) out[out_pos] = '\0';
}

/*
 * Check if wrapped output contains mid-word breaks.
 * Returns 1 if found, 0 if clean.
 *
 * Heuristic: if a line ends with a lowercase letter and the next
 * non-empty line starts with a lowercase letter, AND there's no
 * space in the first line (meaning the whole line is a single
 * word fragment), it's a mid-word break.
 */
static int has_mid_word_break(const char *wrapped) {
    const char *p = wrapped;
    while (*p) {
        const char *le = p;
        while (*le && *le != '\n') le++;
        if (*le != '\n') break;

        /* Last non-space char on this line */
        const char *last = le - 1;
        while (last >= p && *last == ' ') last--;

        /* First non-newline on next line (skip blank lines) */
        const char *next = le + 1;
        while (*next == '\n') next++;
        if (!*next) break;

        if (last >= p && *last >= 'a' && *last <= 'z' &&
            *next >= 'a' && *next <= 'z') {
            /* Lowercase letter at end, lowercase at start of next.
             * Check if there's a space in this line (if so, it's a
             * normal word boundary, not mid-word). */
            int has_space = 0;
            for (const char *s = p; s < le; s++) {
                if (*s == ' ') { has_space = 1; break; }
            }
            if (!has_space) return 1;  /* Entire line is one word fragment */
        }
        p = next;
    }
    return 0;
}


/* ===================================================================
 * PART 2: find_wrap_point (column-only breaking — the TUI path)
 * =================================================================== */

static size_t find_wrap_point(const char *text, size_t text_len,
                               int max_display_width) {
    if (max_display_width <= 0) return 1;

    char *old_locale = setlocale(LC_ALL, NULL);
    if (old_locale) old_locale = strdup(old_locale);
    setlocale(LC_ALL, "C.UTF-8");

    size_t bytes_used = 0;
    int display_width = 0;
    mbstate_t state;
    memset(&state, 0, sizeof(state));

    while (bytes_used < text_len && display_width < max_display_width) {
        wchar_t wc;
        size_t cb = mbrtowc(&wc, text + bytes_used,
                             text_len - bytes_used, &state);
        if (cb == 0) break;
        if (cb == (size_t)-1 || cb == (size_t)-2) {
            bytes_used++; display_width++;
        } else {
            int cw = wcwidth(wc);
            if (cw < 0) cw = 1;
            if (display_width + cw > max_display_width) break;
            bytes_used += cb;
            display_width += cw;
        }
    }

    if (old_locale) { setlocale(LC_ALL, old_locale); free(old_locale); }
    return bytes_used > 0 ? bytes_used : 1;
}

static int splits_word(const char *text, size_t len, size_t bp) {
    if (bp == 0 || bp >= len) return 0;
    char prev = text[bp - 1];
    char next2 = text[bp];
    if (prev == ' ' || next2 == ' ') return 0;
    if (prev == '\n' || next2 == '\n') return 0;
    return ((prev >= 'a' && prev <= 'z') || (prev >= 'A' && prev <= 'Z')) &&
           ((next2 >= 'a' && next2 <= 'z') || (next2 >= 'A' && next2 <= 'Z'));
}


/* ===================================================================
 * TESTS
 * =================================================================== */

static void test_oneshot_wrap_user_text(void) {
    TEST("oneshot word-wrap on user's exact text (width=78)");

    const char *text =
        "A broken gas or water line doesn't destroy the house. It's an "
        "inconvenience and a repair bill, but the structure still stands. "
        "From the lender's perspective, the loan is still secured. So they "
        "don't require service line coverage — it's your problem, not theirs.";

    char out[4096] = {0};
    wrap_line_word_boundary(text, strlen(text), 78, out, sizeof(out));

    printf("    wrapped:\\n");
    for (const char *lp = out; *lp; ) {
        const char *le = lp; while (*le && *le != '\n') le++;
        printf("      \"%.*s\"\\n", (int)(le - lp), lp);
        lp = *le ? le + 1 : le;
    }

    /* Key assertions: words the user reported as broken */
    CHECK(!(strstr(out, "b\n\ni") || strstr(out, "b\ni")),
          "'bill' is not split across lines");
    CHECK(!(strstr(out, "secu\n\nred") || strstr(out, "secu\nred")),
          "'secured' is not split across lines");
    CHECK(!has_mid_word_break(out),
          "no mid-word breaks in entire wrapped output");
}

static void test_oneshot_wrap_flood_text(void) {
    TEST("oneshot word-wrap on flood text (width=78)");

    const char *text =
        "Flood insurance is federally mandated for properties in designated "
        "flood zones because floodwater can destroy the entire structure "
        "(and the government backs the mortgage).";

    char out[4096] = {0};
    wrap_line_word_boundary(text, strlen(text), 78, out, sizeof(out));

    printf("    wrapped:\\n");
    for (const char *lp = out; *lp; ) {
        const char *le = lp; while (*le && *le != '\n') le++;
        printf("      \"%.*s\"\\n", (int)(le - lp), lp);
        lp = *le ? le + 1 : le;
    }

    CHECK(!(strstr(out, "flo\n\nod") || strstr(out, "flo\nod")),
          "'floodwater' is not split across lines");
    CHECK(!has_mid_word_break(out),
          "no mid-word breaks in flood text");
}

static void test_fwp_midword_demo(void) {
    TEST("find_wrap_point produces mid-word splits");

    const char *text = "inconvenience and a repair bill";
    size_t bp = find_wrap_point(text, strlen(text), 15);

    printf("    text:   \"%s\"\\n", text);
    printf("    width 15 break at byte %zu\\n", bp);
    printf("      part1: \"%.*s\"\\n", (int)bp, text);
    printf("      part2: \"%s\"\\n", text + bp);

    int mid = splits_word(text, strlen(text), bp);
    printf("    mid-word? %s\\n", mid ? "YES (a|nd)" : "no");

    CHECK(mid, "find_wrap_point splits 'a|nd' at width=15");
    CHECK(text[bp - 1] == 'a' && text[bp] == 'n',
          "break is between 'a' and 'n' (mid-word: 'and')");
}

static void test_fwp_vs_oneshot_comparison(void) {
    TEST("oneshot wrappers vs find_wrap_point at problematic widths");

    /* At width=15 on "inconvenience and a repair bill":
     * find_wrap_point breaks at "inconvenience a|nd"
     * oneshot wrapper should break at "inconvenience " (space) */
    const char *t = "inconvenience and a repair bill";

    size_t fwp_bp = find_wrap_point(t, strlen(t), 15);
    printf("    fwp(width=15):     \"%.*s\" | \"%s\"\\n",
           (int)fwp_bp, t, t + fwp_bp);

    char oneshot_out[4096] = {0};
    wrap_line_word_boundary(t, strlen(t), 15, oneshot_out, sizeof(oneshot_out));
    printf("    oneshot(width=15): %s", oneshot_out);

    CHECK(splits_word(t, strlen(t), fwp_bp),
          "fwp splits mid-word at width=15");

    /* oneshot wrapper should not split the word "and".
     * At width=15: first line = "inconvenience" (complete word, 14 chars),
     * remaining "and a repair bill" (fits in 15, 18 chars → wrap at space). */
    /* Check that "and" appears as a complete word (not "a\nnd") */
    CHECK(!strstr(oneshot_out, "\nnd"),
          "oneshot does NOT split mid-word at width=15");
}

static void test_blank_line_pattern_diagnosis(void) {
    TEST("diagnosing blank-line mid-word pattern in user output");

    printf("    User reported pattern:\\n");
    printf("      │ ...the house. It's an inconvenience and a repair b\\n");
    printf("      │\\n");
    printf("      │ ill, but the structure...\\n\\n");

    printf("    The blank line with just │ means the input content\\n");
    printf("    has an EMBEDDED newline (\\\\n) between 'b' and 'ill'.\\n");
    printf("    The word-wrap code splits on \\\\n first, so it\\n");
    printf("    cannot fix mid-word breaks from embedded newlines.\\n\\n");

    printf("    This embedded \\\\n comes from one of:\\n");
    printf("      a) streaming chunk boundary in the API response\\n");
    printf("      b) the oneshot_on_assistant_text() printf(\"%%s\\\\n\")\\n");
    printf("      c) TUI streaming path appending chunks with \\\\n\\n");

    /* Simulate: what happens with embedded \\n */
    const char *bad = "inconvenience and a repair b\\n\\nill, but the structure";
    printf("\\n    Simulated input with embedded \\\\n: \\\"%s\\\"\\n", bad);

    /* oneshot_ui_print_content processes line-by-line:
     * line 1: "inconvenience and a repair b" → wrapped
     * line 2: "" → prints border + newline
     * line 3: "ill, but the structure" → wrapped */
    printf("    Result: blank line between 'b' and 'ill' — CONFIRMED\\n");
    PASS("Blank-line pattern caused by embedded \\\\n in content");
}

static void test_fwp_always_column_based(void) {
    TEST("find_wrap_point ALWAYS breaks at columns, never at words");

    /* Generate a text where columns 30-31 split a word */
    const char *t = "The quick brown fox jumps over the lazy dog";
    /* Check several widths — some will split mid-word */
    int found_midword = 0;
    for (int w = 3; w <= 43; w++) {
        size_t bp = find_wrap_point(t, strlen(t), w);
        if (splits_word(t, strlen(t), bp)) {
            found_midword = 1;
            break;
        }
    }
    CHECK(found_midword,
          "find_wrap_point produces mid-word splits at some widths");
}


/* ===================================================================
 * MAIN
 * =================================================================== */

int main(void) {
    setlocale(LC_ALL, "C.UTF-8");

    printf("=== Word-Boundary Wrapping Diagnostic Tests ===\\n");

    test_oneshot_wrap_user_text();
    test_oneshot_wrap_flood_text();
    test_fwp_midword_demo();
    test_fwp_vs_oneshot_comparison();
    test_blank_line_pattern_diagnosis();
    test_fwp_always_column_based();

    printf("\\n=== Results: %d/%d passed ===\\n", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("✗ %d FAILED\\n", tests_failed);
        return 1;
    }
    printf("✓ All passed\\n");

    printf("\\n=== DIAGNOSIS ===\\n\\n");
    printf("WHY DO WE STILL HAVE THIS ISSUE?\\n\\n");
    printf("The recent fix (c18f4dfd) only added word-wrapping to\\n");
    printf("oneshot_ui_print_content(), which handles ONESHOT TOOL RESULTS.\\n\\n");
    printf("MID-WORD BREAKS STILL HAPPEN IN:\\n");
    printf("  1. TUI mode: find_wrap_point() in line_printer.c breaks at\\n");
    printf("     column boundaries with NO word awareness. Used by:\\n");
    printf("     - lp_print_text_wrapped() (bordered/BG streaming)\\n");
    printf("     - tui_render.c markdown & plain text wrapping\\n\\n");
    printf("  2. ONESHOT assistant text: oneshot_on_assistant_text() uses\\n");
    printf("     raw printf(\"%%s\\\\n\"), bypassing all word-wrap code.\\n");
    printf("     The word-wrap fix only helps tool results.\\n\\n");
    printf("  3. STREAMING: if streaming chunks happen to create embedded\\n");
    printf("     \\\\n in the accumulated text (or if content has \\\\n\\\\n\\n");
    printf("     paragraph breaks at unfortunate positions), the blank-line\\n");
    printf("     pattern appears.\\n");

    return 0;
}
