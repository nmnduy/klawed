/**
 * test_paste_placeholder.c
 *
 * Unit tests for paste placeholder resolution bugs:
 *
 * Bug 1 (second paste overwrites first): When a second paste starts before
 *   the first paste's placeholder has been resolved, the second paste
 *   overwrites paste_content, leaving the first paste's placeholder text
 *   as literal text in the visible buffer.
 *
 * Bug 2 (history stores placeholder): Input history would store the
 *   placeholder text ("[N characters pasted]") instead of the actual
 *   pasted content.
 *
 * These tests replicate and validate the fix from commit 1340cfd.
 *
 * NOTE: This is a standalone test that duplicates the core paste
 * placeholder logic inline to avoid linking against ncurses & co.
 * The logic here mirrors src/tui_paste.c:paste_expand_previous()
 * and src/tui_paste.c:tui_paste_finalize().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Test colors */
#define GREEN "\033[32m"
#define RED   "\033[31m"
#define YELLOW "\033[33m"
#define CYAN  "\033[36m"
#define RESET "\033[0m"

#define PLACEHOLDER_THRESHOLD 200

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do {                                     \
        if (cond) {                                                     \
            printf(GREEN "  ✓" RESET " %s\n", msg);                     \
            tests_passed++;                                             \
        } else {                                                        \
            printf(RED "  ✗" RESET " %s  [line %d]\n", msg, __LINE__);  \
            tests_failed++;                                             \
        }                                                               \
    } while (0)


/* ================================================================
 * Minimal input buffer struct — mirrors TUIInputBuffer fields
 * relevant to paste handling.
 * ================================================================ */
typedef struct {
    char  *buffer;
    size_t capacity;
    int    length;
    int    cursor;

    /* Paste tracking */
    int    paste_mode;
    size_t paste_capacity;
    char  *paste_content;       /* actual content kept separate */
    size_t paste_content_len;   /* length of actual content */
    int    paste_start_pos;     /* position in buffer where placeholder sits */
    int    paste_placeholder_len; /* length of placeholder string in buffer */
} TestInputBuf;


/* ================================================================
 * Heap helpers — we don't link against array_resize.c so we use
 * plain realloc.
 * ================================================================ */
static int test_reserve(void **ptr, size_t *cap, size_t needed)
{
    if (needed <= *cap) return 0;
    void *p = realloc(*ptr, needed);
    if (!p) return -1;
    *ptr = p;
    *cap = needed;
    return 0;
}

/* ================================================================
 * Replica of tui_paste_finalize() — inserts paste content OR
 * placeholder into the visible buffer.
 * ================================================================ */
static void finalize_paste(TestInputBuf *input)
{
    if (!input || !input->paste_content || input->paste_content_len == 0)
        return;

    int insert_pos = input->paste_start_pos;
    if (insert_pos < 0) insert_pos = 0;
    if (insert_pos > input->length) insert_pos = input->length;

    /* ----- Small pastes: insert directly -------------------- */
    if (input->paste_content_len < PLACEHOLDER_THRESHOLD) {
        if (input->length + (int)input->paste_content_len >= (int)input->capacity - 1)
            return;  /* not enough room */

        int paste_len = (int)input->paste_content_len;
        memmove(&input->buffer[insert_pos + paste_len],
                &input->buffer[insert_pos],
                (size_t)(input->length - insert_pos + 1));
        memcpy(&input->buffer[insert_pos],
               input->paste_content, input->paste_content_len);

        input->length += paste_len;
        input->cursor = insert_pos + paste_len;
        input->paste_placeholder_len = 0;
        return;
    }

    /* ----- Large pastes: insert placeholder ----------------- */
    char placeholder[128];
    int ph_len = snprintf(placeholder, sizeof(placeholder),
                          "[%zu characters pasted]",
                          input->paste_content_len);
    if (ph_len >= (int)sizeof(placeholder))
        ph_len = (int)sizeof(placeholder) - 1;

    if (input->length + ph_len >= (int)input->capacity - 1) return;

    memmove(&input->buffer[insert_pos + ph_len],
            &input->buffer[insert_pos],
            (size_t)(input->length - insert_pos + 1));
    memcpy(&input->buffer[insert_pos], placeholder, (size_t)ph_len);

    input->length += ph_len;
    input->cursor = insert_pos + ph_len;
    input->paste_placeholder_len = ph_len;
}

/* ================================================================
 * Replica of paste_expand_previous() — replaces a placeholder
 * in the visible buffer with the actual paste content.
 * This is THE fix from commit 1340cfd.
 * ================================================================ */
static void expand_previous(TestInputBuf *input)
{
    if (!input || !input->paste_content || input->paste_content_len == 0 ||
        input->paste_placeholder_len == 0)
        return;

    int insert_pos = input->paste_start_pos;
    if (insert_pos < 0) insert_pos = 0;
    if (insert_pos > input->length) insert_pos = input->length;

    int paste_len      = (int)input->paste_content_len;
    int placeholder_len = input->paste_placeholder_len;
    int size_change    = paste_len - placeholder_len;

    /* Grow buffer if needed */
    if (input->length + size_change >= (int)input->capacity - 1) {
        size_t needed = (size_t)(input->length + size_change + 4096);
        if (test_reserve((void**)&input->buffer, &input->capacity, needed) != 0)
            return;
    }

    int after_pos = insert_pos + placeholder_len;
    int after_len = input->length - after_pos;

    memmove(&input->buffer[insert_pos + paste_len],
            &input->buffer[after_pos],
            (size_t)(after_len + 1));

    memcpy(&input->buffer[insert_pos],
           input->paste_content, input->paste_content_len);

    input->length += size_change;
    input->cursor = insert_pos + paste_len;

    /* Mark paste as resolved */
    input->paste_placeholder_len = 0;
    input->paste_content_len     = 0;
}


/* ================================================================
 * Helper: create an empty input buffer
 * ================================================================ */
static TestInputBuf *make_buf(size_t init_cap)
{
    TestInputBuf *b = calloc(1, sizeof(*b));
    b->buffer   = malloc(init_cap);
    b->capacity = init_cap;
    b->buffer[0] = '\0';
    return b;
}

static void free_buf(TestInputBuf *b)
{
    if (!b) return;
    free(b->buffer);
    free(b->paste_content);
    free(b);
}


/* ================================================================
 * Helper: simulate starting a paste with content.
 * If content is >= threshold, a placeholder is inserted.
 * ================================================================ */
static void start_paste(TestInputBuf *b, const char *content)
{
    /* --- First, expand any unresolved previous paste (THE FIX) --- */
    expand_previous(b);

    b->paste_mode         = 1;
    b->paste_start_pos    = b->cursor;
    b->paste_content_len  = strlen(content);

    if (!b->paste_content || b->paste_capacity < b->paste_content_len + 1) {
        b->paste_capacity = b->paste_content_len + 4096;
        b->paste_content  = realloc(b->paste_content, b->paste_capacity);
    }
    if (b->paste_content)
        memcpy(b->paste_content, content, b->paste_content_len + 1);
}

/* ================================================================
 * Helper: end paste mode (finalize placeholder or direct insert)
 * ================================================================ */
static void end_paste(TestInputBuf *b)
{
    b->paste_mode = 0;
    finalize_paste(b);
}


/* ================================================================
 * Helper: reconstruct the full buffer content (placeholder replaced
 * with actual paste content) — mirrors tui_get_input_buffer().
 * Returns a static buffer; NOT thread-safe.
 * ================================================================ */
static const char *get_reconstructed(TestInputBuf *b)
{
    if (!b->paste_content || b->paste_content_len == 0 ||
        b->paste_placeholder_len == 0)
        return b->buffer;

    static char *recon = NULL;
    static size_t recon_cap = 0;

    size_t before_len = (size_t)b->paste_start_pos;
    size_t after_start = (size_t)(b->paste_start_pos + b->paste_placeholder_len);
    size_t after_len = 0;
    if (after_start <= (size_t)b->length)
        after_len = (size_t)b->length - after_start;

    size_t total = before_len + b->paste_content_len + after_len;
    if (total + 1 > recon_cap) {
        recon_cap = total + 1024;
        char *np = realloc(recon, recon_cap);
        if (!np) return b->buffer;
        recon = np;
    }

    char *d = recon;
    if (before_len > 0) {
        memcpy(d, b->buffer, before_len);
        d += before_len;
    }
    if (b->paste_content_len > 0) {
        memcpy(d, b->paste_content, b->paste_content_len);
        d += b->paste_content_len;
    }
    if (after_len > 0) {
        memcpy(d, &b->buffer[after_start], after_len);
        d += after_len;
    }
    *d = '\0';
    return recon;
}


/* ================================================================
 * Helper: simulate typing a string into the buffer at cursor
 * ================================================================ */
static void type_text(TestInputBuf *b, const char *text)
{
    size_t len = strlen(text);
    if ((size_t)b->length + len >= b->capacity - 1) {
        size_t needed = b->capacity + len + 1024;
        test_reserve((void**)&b->buffer, &b->capacity, needed);
    }
    memmove(&b->buffer[(size_t)b->cursor + len],
            &b->buffer[(size_t)b->cursor],
            (size_t)(b->length - b->cursor + 1));
    memcpy(&b->buffer[(size_t)b->cursor], text, len);
    b->length += (int)len;
    b->cursor += (int)len;
}


/* ================================================================
 * TEST: Single paste — placeholder is resolved after paste ends
 * ================================================================ */
static int test_single_paste_resolves_placeholder(void)
{
    printf("\n" YELLOW "Single paste — placeholder resolved after end" RESET "\n");

    TestInputBuf *b = make_buf(512);
    /* Type some prefix text */
    type_text(b, "prefix: ");

    /* Paste large content (≥ threshold) */
    char big[512];
    memset(big, 'A', 300);
    big[300] = '\0';

    start_paste(b, big);
    end_paste(b);

    /* After end, buffer should have placeholder */
    int found_placeholder = (strstr(b->buffer, "characters pasted") != NULL);
    TEST_ASSERT(found_placeholder, "Placeholder present in buffer after paste ends");

    /* Reconstructed buffer should have the actual content */
    const char *recon = get_reconstructed(b);
    TEST_ASSERT(recon != NULL, "Reconstruction returns non-NULL");

    int has_actual = (strstr(recon, big) != NULL);
    TEST_ASSERT(has_actual, "Reconstructed buffer contains actual pasted content");

    int no_placeholder = (strstr(recon, "characters pasted") == NULL);
    TEST_ASSERT(no_placeholder, "Reconstructed buffer has no placeholder text");

    /* Full reconstruction should be prefix: + content */
    TEST_ASSERT(strncmp(recon, "prefix: ", 8) == 0,
                "Reconstructed buffer starts with prefix text");

    free_buf(b);
    return 1;
}


/* ================================================================
 * TEST: Bug 1 — second paste overwrites first paste's content,
 * leaving the first paste's placeholder text in the buffer.
 *
 * Scenario:
 *   1. Paste large content A (placeholder "[300 chars pasted]" inserted)
 *   2. Start paste B without resolving A (placeholder A still in buffer)
 *   3. End paste B (placeholder B replaces placeholder A? or just appends?)
 *   BEFORE FIX: paste_content was overwritten, placeholder A was left
 *               as literal text. Reconstruction would use paste_content B
 *               to substitute for placeholder A's position, leaving
 *               mismatched content in the buffer.
 * ================================================================ */
static int test_multiple_paste_preserves_first_content(void)
{
    printf("\n" YELLOW "Bug 1 — Multiple pastes: first content preserved" RESET "\n");

    TestInputBuf *b = make_buf(1024);
    type_text(b, "start ");

    /* --- First paste (large) --- */
    char first[300];
    memset(first, 'X', 299);
    first[299] = '\0';

    start_paste(b, first);
    end_paste(b);
    int cursor_after_first = b->cursor;

    /* Verify placeholder exists */
    int has_ph1 = (strstr(b->buffer, "characters pasted") != NULL);
    TEST_ASSERT(has_ph1, "First paste placeholder in buffer");

    /* Reconstruct: should have first content */
    const char *recon1 = get_reconstructed(b);
    TEST_ASSERT(recon1 != NULL, "First reconstruction non-NULL");
    int first_ok = (strstr(recon1, first) != NULL) &&
                   (strstr(recon1, "characters pasted") == NULL);
    TEST_ASSERT(first_ok, "First reconstruction has actual content, no placeholder");

    /* --- Second paste WITHOUT resolving the first --- */
    char second[250];
    memset(second, 'Y', 249);
    second[249] = '\0';

    /*
     * THIS is the key: start_paste now calls expand_previous() first,
     * which was added by the fix. Without it, the second paste would
     * overwrite paste_content and the first placeholder would remain
     * as literal text.
     */
    start_paste(b, second);

    /*
     * WITHOUT THE FIX: b->paste_content_len would be 249 (overwrites
     * first paste). b->paste_placeholder_len would still be from first
     * paste (e.g. 28 for "[299 characters pasted]"). This mismatch
     * means reconstruction would produce garbage.
     *
     * WITH THE FIX: expand_previous() resolved the first paste before
     * the second started, so paste_placeholder_len == 0 at this point.
     */
    TEST_ASSERT(b->paste_placeholder_len == 0,
                "After fix: first paste placeholder is resolved before second starts");

    end_paste(b);

    /* Now the second paste's placeholder is in the buffer */
    int has_ph2 = (strstr(b->buffer, "characters pasted") != NULL);
    TEST_ASSERT(has_ph2, "Second paste placeholder in buffer");

    /* Reconstruct: should have BOTH first AND second actual content */
    const char *recon2 = get_reconstructed(b);
    TEST_ASSERT(recon2 != NULL, "Second reconstruction non-NULL");

    int has_first  = (strstr(recon2, first) != NULL);
    int has_second = (strstr(recon2, second) != NULL);
    TEST_ASSERT(has_first,  "First paste content present in reconstruction");
    TEST_ASSERT(has_second, "Second paste content present in reconstruction");

    int no_placeholder = (strstr(recon2, "characters pasted") == NULL);
    TEST_ASSERT(no_placeholder, "No placeholder text in reconstruction");

    /* Order check: first content should come before second */
    const char *pos_first  = strstr(recon2, first);
    const char *pos_second = strstr(recon2, second);
    TEST_ASSERT(pos_first != NULL && pos_second != NULL,
                "Both paste contents found in reconstruction");
    if (pos_first && pos_second) {
        TEST_ASSERT(pos_first < pos_second,
                    "First paste content appears before second in reconstruction");
    }

    (void)cursor_after_first;
    free_buf(b);
    return 1;
}


/* ================================================================
 * TEST: Bug 2 — Input history stores placeholder text instead of
 * actual content.
 *
 * Scenario: Paste large content, then submit (Enter). Without the
 * fix, the placeholder text would be what gets stored in history
 * and displayed in the conversation window.
 * ================================================================ */
static int test_history_gets_actual_content_not_placeholder(void)
{
    printf("\n" YELLOW "Bug 2 — History gets actual content, not placeholder" RESET "\n");

    TestInputBuf *b = make_buf(1024);
    type_text(b, "edit: ");

    /* Paste large content */
    char big[500];
    memset(big, 'Z', 499);
    big[499] = '\0';

    start_paste(b, big);
    end_paste(b);

    /* This simulates what happens when user presses Enter:
     * tui_get_input_buffer() is called to get the submitted text */
    const char *submitted = get_reconstructed(b);

    TEST_ASSERT(submitted != NULL, "Submitted text is non-NULL");

    /* Submitted text should be prefix + actual big content */
    int starts_with_prefix = (strncmp(submitted, "edit: ", 6) == 0);
    TEST_ASSERT(starts_with_prefix, "Submitted text starts with prefix");

    int contains_actual = (strstr(submitted, big) != NULL);
    TEST_ASSERT(contains_actual, "Submitted text contains actual pasted content");

    int no_placeholder = (strstr(submitted, "characters pasted") == NULL);
    TEST_ASSERT(no_placeholder, "Submitted text has NO placeholder (Bug 2 fixed)");

    /* Length check: submitted text should be roughly prefix + content */
    size_t expected_len = 6 + 499;  /* "edit: " + 499 'Z' chars */
    size_t actual_len = strlen(submitted);
    TEST_ASSERT(actual_len == expected_len,
                "Submitted text length matches prefix + content length");

    free_buf(b);
    return 1;
}


/* ================================================================
 * TEST: Small paste (< threshold) — no placeholder used, direct
 * insertion should work without any issues.
 * ================================================================ */
static int test_small_paste_direct_insertion(void)
{
    printf("\n" YELLOW "Small paste — direct insertion (no placeholder)" RESET "\n");

    TestInputBuf *b = make_buf(512);
    type_text(b, "msg: ");

    const char *small = "Hello World";  /* < threshold */
    start_paste(b, small);
    end_paste(b);

    /* Small paste should be inserted directly, no placeholder */
    int has_placeholder = (strstr(b->buffer, "characters pasted") != NULL);
    TEST_ASSERT(!has_placeholder, "No placeholder for small paste");

    int has_content = (strstr(b->buffer, "Hello World") != NULL);
    TEST_ASSERT(has_content, "Small paste content directly visible in buffer");

    /* No reconstruction needed, but should still work */
    const char *recon = get_reconstructed(b);
    TEST_ASSERT(recon != NULL, "Reconstruction works for small paste");
    TEST_ASSERT(strcmp(recon, "msg: Hello World") == 0,
                "Full buffer content is correct");

    free_buf(b);
    return 1;
}


/* ================================================================
 * TEST: Expand previous paste expands placeholder to full content
 * in the visible buffer itself.
 * ================================================================ */
static int test_expand_previous_replaces_placeholder_in_buffer(void)
{
    printf("\n" YELLOW "Expand previous — placeholder replaced in buffer" RESET "\n");

    TestInputBuf *b = make_buf(512);
    type_text(b, "[");

    /* First paste (large) */
    char first[300];
    memset(first, 'M', 299);
    first[299] = '\0';

    start_paste(b, first);
    end_paste(b);
    int ph_len_before = b->paste_placeholder_len;

    TEST_ASSERT(ph_len_before > 0, "Placeholder length > 0 after first paste");
    TEST_ASSERT(ph_len_before < 299, "Placeholder is much shorter than actual content");

    /* Now call expand_previous directly */
    expand_previous(b);

    /* After expansion, placeholder should be gone and actual content in buffer */
    int has_placeholder = (strstr(b->buffer, "characters pasted") != NULL);
    TEST_ASSERT(!has_placeholder, "After expand: no placeholder text in buffer");

    int has_content = (strstr(b->buffer, first) != NULL);
    TEST_ASSERT(has_content, "After expand: actual content in buffer");

    TEST_ASSERT(b->paste_placeholder_len == 0,
                "After expand: placeholder_len reset to 0");
    TEST_ASSERT(b->paste_content_len == 0,
                "After expand: paste_content_len reset to 0");

    free_buf(b);
    return 1;
}


/* ================================================================
 * TEST: Three consecutive pastes — edge case of the bug fix
 * ================================================================ */
static int test_three_consecutive_pastes(void)
{
    printf("\n" YELLOW "Three consecutive pastes — all content preserved" RESET "\n");

    TestInputBuf *b = make_buf(2048);

    char a[300]; memset(a, 'A', 299); a[299] = '\0';
    char b2[300]; memset(b2, 'B', 299); b2[299] = '\0';
    char c[300]; memset(c, 'C', 299); c[299] = '\0';

    start_paste(b, a); end_paste(b);
    start_paste(b, b2); end_paste(b);
    start_paste(b, c); end_paste(b);

    const char *recon = get_reconstructed(b);
    TEST_ASSERT(recon != NULL, "Reconstruction non-NULL for 3 pastes");

    int has_a = (strstr(recon, a) != NULL);
    int has_b = (strstr(recon, b2) != NULL);
    int has_c = (strstr(recon, c) != NULL);
    TEST_ASSERT(has_a, "First paste content preserved over 3 pastes");
    TEST_ASSERT(has_b, "Second paste content preserved");
    TEST_ASSERT(has_c, "Third paste content preserved");

    int no_ph = (strstr(recon, "characters pasted") == NULL);
    TEST_ASSERT(no_ph, "No placeholder text in reconstruction");

    /* Order: A, B, C */
    const char *pa = strstr(recon, a);
    const char *pb = strstr(recon, b2);
    const char *pc = strstr(recon, c);
    if (pa && pb && pc) {
        TEST_ASSERT(pa < pb, "Paste A before B");
        TEST_ASSERT(pb < pc, "Paste B before C");
    }

    free_buf(b);
    return 1;
}


/* ================================================================
 * TEST: Paste with trailing text (user types after the placeholder)
 * ================================================================ */
static int test_paste_with_trailing_text(void)
{
    printf("\n" YELLOW "Paste with trailing text — reconstruction preserves order" RESET "\n");

    TestInputBuf *b = make_buf(1024);
    type_text(b, "before ");

    /* Paste */
    char big[300];
    memset(big, 'P', 299);
    big[299] = '\0';

    start_paste(b, big);
    end_paste(b);

    /* Type after paste (cursor was after placeholder) */
    type_text(b, " after");

    /* Reconstruct: should be "before " + content + " after" */
    const char *recon = get_reconstructed(b);
    TEST_ASSERT(recon != NULL, "Reconstruction non-NULL with trailing text");

    int has_prefix = (strncmp(recon, "before ", 7) == 0);
    TEST_ASSERT(has_prefix, "Prefix text preserved");

    int has_suffix = (strstr(recon, " after") != NULL);
    TEST_ASSERT(has_suffix, "Suffix after paste preserved");

    int has_content = (strstr(recon, big) != NULL);
    TEST_ASSERT(has_content, "Paste content in reconstruction");

    int no_ph = (strstr(recon, "characters pasted") == NULL);
    TEST_ASSERT(no_ph, "No placeholder in reconstruction");

    /* Verify the order: prefix → content → suffix */
    const char *p_prefix = recon;
    const char *p_content = strstr(recon, big);
    const char *p_suffix = strstr(recon, " after");
    TEST_ASSERT(p_prefix < p_content, "Prefix before content");
    TEST_ASSERT(p_content < p_suffix, "Content before suffix");

    free_buf(b);
    return 1;
}


/* ================================================================
 * Main
 * ================================================================ */
int main(void)
{
    printf(CYAN "\n========================================\n" RESET);
    printf(CYAN "  Paste Placeholder Unit Tests\n" RESET);
    printf(CYAN "========================================\n" RESET);

    test_single_paste_resolves_placeholder();
    test_multiple_paste_preserves_first_content();
    test_history_gets_actual_content_not_placeholder();
    test_small_paste_direct_insertion();
    test_expand_previous_replaces_placeholder_in_buffer();
    test_three_consecutive_pastes();
    test_paste_with_trailing_text();

    printf(YELLOW "\n========================================\n" RESET);
    printf("Tests passed: " GREEN "%d" RESET "\n", tests_passed);
    printf("Tests failed: " RED "%d" RESET "\n", tests_failed);
    printf(YELLOW "========================================\n" RESET);

    return (tests_failed == 0) ? 0 : 1;
}
