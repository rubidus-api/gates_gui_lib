/* UTF-8 text edit core. */
#include <gates/text_edit.h>
#include <proven/heap.h>
#include "gates_test.h"

#include <string.h>

#define HAN "한"      /* 3 bytes, 2 cells */
#define HANGUL "한글"  /* 6 bytes, 4 cells */

static bool text_is(const gates_text_edit_t *ed, const char *expect) {
    gates_str_t s = gates_text_edit_text(ed);
    gates_usize_t n = strlen(expect);
    return s.size == n && (n == 0 || memcmp(s.ptr, expect, n) == 0);
}

static gates_str_t lit(const char *s) {
    return (gates_str_t){ .ptr = (const proven_byte_t *)s, .size = strlen(s) };
}

static void test_init_and_set(void) {
    gates_text_edit_t ed;
    GT_ASSERT_OK(gates_text_edit_init(&ed, (gates_allocator_t){0}, GATES_STR("hello")));
    GT_ASSERT(text_is(&ed, "hello"));
    GT_ASSERT(gates_text_edit_caret(&ed) == 5);
    GT_ASSERT(!gates_text_edit_has_selection(&ed));

    GT_ASSERT_OK(gates_text_edit_set_text(&ed, lit(HANGUL)));
    GT_ASSERT(text_is(&ed, HANGUL));
    GT_ASSERT(gates_text_edit_caret(&ed) == 6); /* bytes, not cells */
    GT_ASSERT(gates_text_edit_cells_before(&ed, 6) == 4);

    GT_ASSERT_OK(gates_text_edit_set_text(&ed, (gates_str_t){0}));
    GT_ASSERT(text_is(&ed, ""));
    gates_text_edit_deinit(&ed);
}

static void test_insert_at_caret(void) {
    gates_text_edit_t ed;
    GT_ASSERT_OK(gates_text_edit_init(&ed, (gates_allocator_t){0}, GATES_STR("ac")));
    gates_text_edit_set_caret(&ed, 1, false);
    GT_ASSERT_OK(gates_text_edit_insert(&ed, GATES_STR("b")));
    GT_ASSERT(text_is(&ed, "abc"));
    GT_ASSERT(gates_text_edit_caret(&ed) == 2);

    /* Multi-byte insert moves the caret by bytes and keeps UTF-8 intact. */
    GT_ASSERT_OK(gates_text_edit_insert(&ed, lit(HAN)));
    GT_ASSERT(text_is(&ed, "ab" HAN "c"));
    GT_ASSERT(gates_text_edit_caret(&ed) == 5);
    GT_ASSERT(gates_text_edit_cells_before(&ed, gates_text_edit_caret(&ed)) == 4);

    /* Empty insert is a no-op that still collapses the selection. */
    gates_text_edit_select_all(&ed);
    GT_ASSERT_OK(gates_text_edit_insert(&ed, (gates_str_t){0}));
    GT_ASSERT(text_is(&ed, ""));
    gates_text_edit_deinit(&ed);
}

static void test_backspace_delete_codepoints(void) {
    gates_text_edit_t ed;
    GT_ASSERT_OK(gates_text_edit_init(&ed, (gates_allocator_t){0}, lit("a" HANGUL "b")));
    GT_ASSERT(gates_text_edit_caret(&ed) == 8); /* 1 + 6 + 1 bytes */

    GT_ASSERT_OK(gates_text_edit_backspace(&ed));      /* removes 'b' */
    GT_ASSERT(text_is(&ed, "a" HANGUL));
    GT_ASSERT_OK(gates_text_edit_backspace(&ed));      /* removes 글 whole */
    GT_ASSERT(text_is(&ed, "a" HAN));
    GT_ASSERT(gates_text_edit_caret(&ed) == 4);
    GT_ASSERT_OK(gates_text_edit_backspace(&ed));      /* removes 한 whole */
    GT_ASSERT(text_is(&ed, "a"));
    GT_ASSERT_OK(gates_text_edit_backspace(&ed));
    GT_ASSERT(text_is(&ed, ""));
    GT_ASSERT_OK(gates_text_edit_backspace(&ed));      /* at start: harmless */
    GT_ASSERT(text_is(&ed, ""));
    GT_ASSERT(gates_text_edit_caret(&ed) == 0);

    /* Forward delete, also codepoint-wise. */
    GT_ASSERT_OK(gates_text_edit_set_text(&ed, lit(HAN "z")));
    gates_text_edit_move(&ed, GATES_CARET_HOME, false);
    GT_ASSERT_OK(gates_text_edit_delete(&ed));
    GT_ASSERT(text_is(&ed, "z"));
    GT_ASSERT_OK(gates_text_edit_delete(&ed));
    GT_ASSERT(text_is(&ed, ""));
    GT_ASSERT_OK(gates_text_edit_delete(&ed));         /* at end: harmless */
    GT_ASSERT(text_is(&ed, ""));
    gates_text_edit_deinit(&ed);
}

static void test_caret_movement(void) {
    gates_text_edit_t ed;
    GT_ASSERT_OK(gates_text_edit_init(&ed, (gates_allocator_t){0}, lit("a" HAN "b")));

    gates_text_edit_move(&ed, GATES_CARET_HOME, false);
    GT_ASSERT(gates_text_edit_caret(&ed) == 0);
    gates_text_edit_move(&ed, GATES_CARET_LEFT, false); /* clamps */
    GT_ASSERT(gates_text_edit_caret(&ed) == 0);

    gates_text_edit_move(&ed, GATES_CARET_RIGHT, false);
    GT_ASSERT(gates_text_edit_caret(&ed) == 1);
    gates_text_edit_move(&ed, GATES_CARET_RIGHT, false); /* over the whole 한 */
    GT_ASSERT(gates_text_edit_caret(&ed) == 4);
    gates_text_edit_move(&ed, GATES_CARET_END, false);
    GT_ASSERT(gates_text_edit_caret(&ed) == 5);
    gates_text_edit_move(&ed, GATES_CARET_RIGHT, false); /* clamps */
    GT_ASSERT(gates_text_edit_caret(&ed) == 5);

    /* set_caret snaps onto a codepoint boundary (byte 2 is mid-Hangul). */
    gates_text_edit_set_caret(&ed, 2, false);
    GT_ASSERT(gates_text_edit_caret(&ed) == 1);
    gates_text_edit_deinit(&ed);
}

static void test_selection(void) {
    gates_text_edit_t ed;
    GT_ASSERT_OK(gates_text_edit_init(&ed, (gates_allocator_t){0}, GATES_STR("abcdef")));

    /* Shift+Left twice selects "ef". */
    gates_text_edit_move(&ed, GATES_CARET_LEFT, true);
    gates_text_edit_move(&ed, GATES_CARET_LEFT, true);
    GT_ASSERT(gates_text_edit_has_selection(&ed));
    GT_ASSERT(gates_text_edit_sel_begin(&ed) == 4 && gates_text_edit_sel_end(&ed) == 6);

    /* Typing replaces the selection. */
    GT_ASSERT_OK(gates_text_edit_insert(&ed, GATES_STR("XY")));
    GT_ASSERT(text_is(&ed, "abcdXY"));
    GT_ASSERT(!gates_text_edit_has_selection(&ed));

    /* Backspace with a selection removes exactly the selection. */
    gates_text_edit_set_caret(&ed, 1, false);
    gates_text_edit_set_caret(&ed, 4, true);
    GT_ASSERT(gates_text_edit_sel_begin(&ed) == 1 && gates_text_edit_sel_end(&ed) == 4);
    GT_ASSERT_OK(gates_text_edit_backspace(&ed));
    GT_ASSERT(text_is(&ed, "aXY"));
    GT_ASSERT(gates_text_edit_caret(&ed) == 1);

    /* An unextended arrow collapses the selection to its near edge. */
    gates_text_edit_select_all(&ed);
    gates_text_edit_move(&ed, GATES_CARET_LEFT, false);
    GT_ASSERT(gates_text_edit_caret(&ed) == 0 && !gates_text_edit_has_selection(&ed));
    gates_text_edit_select_all(&ed);
    gates_text_edit_move(&ed, GATES_CARET_RIGHT, false);
    GT_ASSERT(gates_text_edit_caret(&ed) == 3 && !gates_text_edit_has_selection(&ed));

    /* select_all then delete empties the buffer. */
    gates_text_edit_select_all(&ed);
    GT_ASSERT_OK(gates_text_edit_delete(&ed));
    GT_ASSERT(text_is(&ed, ""));
    gates_text_edit_deinit(&ed);
}

static void test_cells_and_hit_mapping(void) {
    gates_text_edit_t ed;
    GT_ASSERT_OK(gates_text_edit_init(&ed, (gates_allocator_t){0}, lit("a" HANGUL "b")));
    /* bytes: a(1) 한(3) 글(3) b(1); cells: a=1, 한=2, 글=2, b=1 */
    GT_ASSERT(gates_text_edit_cells_before(&ed, 0) == 0);
    GT_ASSERT(gates_text_edit_cells_before(&ed, 1) == 1);
    GT_ASSERT(gates_text_edit_cells_before(&ed, 4) == 3);
    GT_ASSERT(gates_text_edit_cells_before(&ed, 8) == 6);

    /* Clicking a cell maps back to a codepoint boundary; the far half of a
     * wide glyph rounds on to the following boundary (cells: a=0, 한=1..2,
     * 글=3..4, b=5). */
    GT_ASSERT(gates_text_edit_offset_at_cell(&ed, 0) == 0);
    GT_ASSERT(gates_text_edit_offset_at_cell(&ed, 1) == 1); /* left half of 한 */
    GT_ASSERT(gates_text_edit_offset_at_cell(&ed, 2) == 4); /* right half of 한 */
    GT_ASSERT(gates_text_edit_offset_at_cell(&ed, 3) == 4); /* left half of 글 */
    GT_ASSERT(gates_text_edit_offset_at_cell(&ed, 4) == 7); /* right half of 글 */
    GT_ASSERT(gates_text_edit_offset_at_cell(&ed, 5) == 7); /* before b */
    GT_ASSERT(gates_text_edit_offset_at_cell(&ed, 99) == 8); /* past the end */
    gates_text_edit_deinit(&ed);
}

static void test_preedit_does_not_touch_buffer(void) {
    gates_text_edit_t ed;
    GT_ASSERT_OK(gates_text_edit_init(&ed, (gates_allocator_t){0}, GATES_STR("ab")));
    GT_ASSERT(gates_text_edit_preedit(&ed).size == 0);

    GT_ASSERT_OK(gates_text_edit_set_preedit(&ed, lit(HAN)));
    GT_ASSERT(gates_text_edit_preedit(&ed).size == 3);
    GT_ASSERT(text_is(&ed, "ab"));                 /* committed text untouched */
    GT_ASSERT(ed.preedit_at == gates_text_edit_caret(&ed));

    GT_ASSERT_OK(gates_text_edit_set_preedit(&ed, lit(HANGUL))); /* replace */
    GT_ASSERT(gates_text_edit_preedit(&ed).size == 6);
    gates_text_edit_clear_preedit(&ed);
    GT_ASSERT(gates_text_edit_preedit(&ed).size == 0);
    GT_ASSERT(text_is(&ed, "ab"));

    /* Commit is an ordinary insert once the IME finishes. */
    GT_ASSERT_OK(gates_text_edit_insert(&ed, lit(HANGUL)));
    GT_ASSERT(text_is(&ed, "ab" HANGUL));
    gates_text_edit_deinit(&ed);
}

static void test_growth_and_bad_args(void) {
    gates_text_edit_t ed;
    GT_ASSERT_OK(gates_text_edit_init(&ed, (gates_allocator_t){0}, (gates_str_t){0}));
    for (int i = 0; i < 500; i++) {
        GT_ASSERT_OK(gates_text_edit_insert(&ed, lit(HAN)));
    }
    GT_ASSERT(gates_text_edit_text(&ed).size == 1500);
    GT_ASSERT(gates_text_edit_cells_before(&ed, 1500) == 1000);
    /* Deleting back down stays consistent. */
    for (int i = 0; i < 500; i++) {
        GT_ASSERT_OK(gates_text_edit_backspace(&ed));
    }
    GT_ASSERT(gates_text_edit_text(&ed).size == 0);
    gates_text_edit_deinit(&ed);

    GT_ASSERT_ERR(gates_text_edit_init(nullptr, (gates_allocator_t){0}, (gates_str_t){0}));
    GT_ASSERT_ERR(gates_text_edit_insert(nullptr, GATES_STR("x")));
}

int main(void) {
    test_init_and_set();
    test_insert_at_caret();
    test_backspace_delete_codepoints();
    test_caret_movement();
    test_selection();
    test_cells_and_hit_mapping();
    test_preedit_does_not_touch_buffer();
    test_growth_and_bad_args();
    return gt_report("test_text_edit");
}
