/* T018: IME composition routing - preedit, commit, cancel, failure contract
 * (docs/tests/cases/T018-ime-compose.md, plan-0006). Platform-free: these are
 * the core entry points the Win32 IMM32 adapter calls. */
#include <gates/ui.h>
#include <gates/widget.h>
#include "gates_test.h"
#include <proven/heap.h>

#include <string.h>

#define HAN "\xED\x95\x9C"   /* U+D55C "han", 2 cells */
#define GUL "\xEA\xB8\x80"   /* U+AE00 "geul" */
#define G_J "\xE3\x84\xB1"   /* U+3131 compatibility jamo "g" */

static const gates_text_backend_t *be;
static const gates_theme_t *theme;
static gates_text_metrics_t M;

/* Heap allocator with a switch that makes every allocation fail. */
typedef struct fail_alloc_t {
    gates_allocator_t inner;
    bool fail;
} fail_alloc_t;

static proven_result_mem_mut_t fa_alloc(void *ctx, proven_size_t size, proven_size_t align) {
    fail_alloc_t *f = ctx;
    if (f->fail) {
        return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    }
    return f->inner.alloc_fn(f->inner.ctx, size, align);
}

static proven_result_mem_mut_t fa_realloc(void *ctx, void *old_ptr, proven_size_t old_size,
                                          proven_size_t new_size, proven_size_t align) {
    fail_alloc_t *f = ctx;
    if (f->fail) {
        return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    }
    return f->inner.realloc_fn(f->inner.ctx, old_ptr, old_size, new_size, align);
}

static void fa_free(void *ctx, void *ptr) {
    fail_alloc_t *f = ctx;
    f->inner.free_fn(f->inner.ctx, ptr);
}

static gates_tree_t *make_ui_alloc(gates_allocator_t alloc, gates_node_t *tb1,
                                   gates_node_t *tb2) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){ .allocator = alloc }, &t));
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_textbox_create(t, root, GATES_STR("abc"), 20, tb1));
    if (tb2 != nullptr) {
        GT_ASSERT_OK(gates_textbox_create(t, root, GATES_STR("xyz"), 20, tb2));
    }
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 400, 200 }, be));
    return t;
}

static gates_tree_t *make_ui(gates_node_t *tb1, gates_node_t *tb2) {
    return make_ui_alloc((gates_allocator_t){0}, tb1, tb2);
}

static bool str_is(gates_str_t s, const char *expect) {
    gates_usize_t n = strlen(expect);
    return s.size == n && (n == 0 || memcmp(s.ptr, expect, n) == 0);
}

static bool tb_is(const gates_tree_t *t, gates_node_t tb, const char *expect) {
    return str_is(gates_textbox_text(t, tb), expect);
}

static gates_str_t lit(const char *s) {
    return (gates_str_t){ .ptr = (const gates_u8 *)s, .size = strlen(s) };
}

static void paint(gates_tree_t *t, gates_draw_list_t *dl) {
    gates_draw_list_reset(dl);
    GT_ASSERT_OK(gates_paint_tree(t, dl, theme, be));
}

static const gates_draw_cmd_t *find_text(const gates_draw_list_t *dl, const char *needle) {
    for (gates_u32 i = 0; i < gates_draw_list_len(dl); i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(dl, i);
        if (c->kind == GATES_DRAW_TEXT &&
            str_is((gates_str_t){ .ptr = dl->text + c->text_offset, .size = c->text_len }, needle)) {
            return c;
        }
    }
    return nullptr;
}

/* The IME adapter's only preconditions: a focused, enabled textbox. */
static void test_no_target_is_ignored(void) {
    gates_node_t tb = GATES_NODE_NULL;
    gates_tree_t *t = make_ui(&tb, nullptr);
    GT_ASSERT(gates_input_preedit(t, lit(HAN), 3) == GATES_INPUT_IGNORED);
    GT_ASSERT(gates_input_commit(t, lit(HAN)) == GATES_INPUT_IGNORED);
    GT_ASSERT(gates_input_preedit_cancel(t) == GATES_INPUT_IGNORED);
    GT_ASSERT(gates_input_char(t, 'q') == GATES_INPUT_IGNORED);
    GT_ASSERT(!gates_input_composing(t));
    gates_rect_t r;
    GT_ASSERT(!gates_input_caret_rect(t, &r));
    GT_ASSERT(tb_is(t, tb, "abc"));

    gates_tree_set_focus(t, tb);
    GT_ASSERT_OK(gates_widget_set_disabled(t, tb, true));
    GT_ASSERT(gates_input_preedit(t, lit(HAN), 3) == GATES_INPUT_IGNORED);
    GT_ASSERT(gates_input_commit(t, lit(HAN)) == GATES_INPUT_IGNORED);
    GT_ASSERT(tb_is(t, tb, "abc"));
    gates_tree_destroy(t);
}

static void test_preedit_then_commit(void) {
    gates_node_t tb = GATES_NODE_NULL;
    gates_tree_t *t = make_ui(&tb, nullptr);
    gates_tree_set_focus(t, tb);
    gates_text_edit_set_caret(gates_textbox_edit(t, tb), 3, false);

    /* g -> geu -> geul: each update replaces the preedit; nothing committed yet. */
    GT_ASSERT(gates_input_preedit(t, lit(G_J), 3) == GATES_INPUT_CONSUMED);
    GT_ASSERT(gates_input_composing(t));
    GT_ASSERT(gates_input_preedit(t, lit(GUL), 3) == GATES_INPUT_CONSUMED);
    GT_ASSERT(tb_is(t, tb, "abc"));
    GT_ASSERT(str_is(gates_text_edit_preedit(gates_textbox_edit(t, tb)), GUL));
    GT_ASSERT((gates_tree_dirty(t) & GATES_TREE_DIRTY_PAINT) != 0);

    /* The result string is the only commit: once, and the preedit goes away. */
    GT_ASSERT(gates_input_commit(t, lit(GUL)) == GATES_INPUT_CONSUMED);
    GT_ASSERT(tb_is(t, tb, "abc" GUL));
    GT_ASSERT(!gates_input_composing(t));
    GT_ASSERT(gates_text_edit_caret(gates_textbox_edit(t, tb)) == 6);

    /* Result for one syllable plus the start of the next, as IMEs deliver it. */
    GT_ASSERT(gates_input_preedit(t, lit(HAN), 3) == GATES_INPUT_CONSUMED);
    GT_ASSERT(gates_input_commit(t, lit(HAN)) == GATES_INPUT_CONSUMED);
    GT_ASSERT(gates_input_preedit(t, lit(G_J), 3) == GATES_INPUT_CONSUMED);
    GT_ASSERT(tb_is(t, tb, "abc" GUL HAN));
    GT_ASSERT(str_is(gates_text_edit_preedit(gates_textbox_edit(t, tb)), G_J));

    /* An empty composition (last jamo erased) clears without committing. */
    GT_ASSERT(gates_input_preedit(t, (gates_str_t){0}, 0) == GATES_INPUT_CONSUMED);
    GT_ASSERT(!gates_input_composing(t));
    GT_ASSERT(tb_is(t, tb, "abc" GUL HAN));

    /* A commit with no preedit (IME sends the result directly) still inserts once. */
    GT_ASSERT(gates_input_commit(t, lit("d")) == GATES_INPUT_CONSUMED);
    GT_ASSERT(tb_is(t, tb, "abc" GUL HAN "d"));
    gates_tree_destroy(t);
}

static void test_cancel_never_commits(void) {
    gates_node_t tb = GATES_NODE_NULL;
    gates_tree_t *t = make_ui(&tb, nullptr);
    gates_tree_set_focus(t, tb);
    gates_text_edit_t *ed = gates_textbox_edit(t, tb);
    gates_text_edit_set_caret(ed, 1, false);
    gates_text_edit_set_caret(ed, 3, true); /* select "bc" */

    GT_ASSERT(gates_input_preedit(t, lit(HAN), 3) == GATES_INPUT_CONSUMED);
    GT_ASSERT(gates_input_preedit_cancel(t) == GATES_INPUT_CONSUMED);
    GT_ASSERT(!gates_input_composing(t));
    GT_ASSERT(tb_is(t, tb, "abc"));
    GT_ASSERT(gates_text_edit_sel_begin(ed) == 1 && gates_text_edit_sel_end(ed) == 3);
    GT_ASSERT(gates_input_preedit_cancel(t) == GATES_INPUT_IGNORED); /* nothing open */

    /* An empty result ends the composition like a cancel: selection kept. */
    GT_ASSERT(gates_input_preedit(t, lit(HAN), 3) == GATES_INPUT_CONSUMED);
    GT_ASSERT(gates_input_commit(t, (gates_str_t){0}) == GATES_INPUT_CONSUMED);
    GT_ASSERT(!gates_input_composing(t));
    GT_ASSERT(tb_is(t, tb, "abc"));
    GT_ASSERT(gates_text_edit_sel_begin(ed) == 1 && gates_text_edit_sel_end(ed) == 3);
    gates_tree_destroy(t);
}

static void test_commit_replaces_selection(void) {
    gates_node_t tb = GATES_NODE_NULL;
    gates_tree_t *t = make_ui(&tb, nullptr);
    gates_tree_set_focus(t, tb);
    gates_text_edit_t *ed = gates_textbox_edit(t, tb);
    gates_text_edit_set_caret(ed, 3, false);
    gates_text_edit_set_caret(ed, 1, true); /* select "bc", caret at the left end */

    /* The selection stays until the commit; the preedit shows after it. */
    GT_ASSERT(gates_input_preedit(t, lit(HAN), 3) == GATES_INPUT_CONSUMED);
    GT_ASSERT(ed->preedit_at == 3);
    GT_ASSERT(gates_text_edit_has_selection(ed));
    GT_ASSERT(gates_input_commit(t, lit(HAN)) == GATES_INPUT_CONSUMED);
    GT_ASSERT(tb_is(t, tb, "a" HAN));
    GT_ASSERT(!gates_text_edit_has_selection(ed));
    gates_tree_destroy(t);
}

static void test_focus_change_drops_preedit(void) {
    gates_node_t tb1 = GATES_NODE_NULL, tb2 = GATES_NODE_NULL;
    gates_tree_t *t = make_ui(&tb1, &tb2);
    gates_tree_set_focus(t, tb1);
    GT_ASSERT(gates_input_preedit(t, lit(HAN), 3) == GATES_INPUT_CONSUMED);

    gates_tree_set_focus(t, tb2);
    GT_ASSERT(!gates_input_composing(t));
    GT_ASSERT(gates_text_edit_preedit(gates_textbox_edit(t, tb1)).size == 0);
    GT_ASSERT(tb_is(t, tb1, "abc"));

    /* A late result after the switch goes to the new focus only, once. */
    GT_ASSERT(gates_input_commit(t, lit(HAN)) == GATES_INPUT_CONSUMED);
    GT_ASSERT(tb_is(t, tb1, "abc"));
    GT_ASSERT(tb_is(t, tb2, "xyz" HAN) || tb_is(t, tb2, HAN "xyz"));

    /* Destroying the composing textbox leaves no composition behind; focus
     * moves on to the next control (plan-0009), which holds no preedit. */
    GT_ASSERT(gates_input_preedit(t, lit(G_J), 3) == GATES_INPUT_CONSUMED);
    GT_ASSERT_OK(gates_node_destroy(t, tb2));
    GT_ASSERT_OK(gates_tree_flush_destroys(t));
    GT_ASSERT(!gates_input_composing(t));
    GT_ASSERT(gates_node_eq(gates_tree_focus(t), tb1));
    GT_ASSERT(gates_text_edit_preedit(gates_textbox_edit(t, tb1)).size == 0);
    gates_tree_destroy(t);
}

static void test_set_text_busy_while_composing(void) {
    gates_node_t tb = GATES_NODE_NULL;
    gates_tree_t *t = make_ui(&tb, nullptr);

    GT_ASSERT_OK(gates_textbox_set_text(t, tb, GATES_STR("hello")));
    GT_ASSERT(tb_is(t, tb, "hello"));
    GT_ASSERT(gates_text_edit_caret(gates_textbox_edit(t, tb)) == 5);

    /* The generic text setter edits a textbox's real text, not a hidden label. */
    GT_ASSERT_OK(gates_widget_set_text(t, tb, GATES_STR("world")));
    GT_ASSERT(tb_is(t, tb, "world"));

    gates_tree_set_focus(t, tb);
    GT_ASSERT(gates_input_preedit(t, lit(HAN), 3) == GATES_INPUT_CONSUMED);
    GT_ASSERT(gates_textbox_set_text(t, tb, GATES_STR("zz")) == PROVEN_ERR_BUSY);
    GT_ASSERT(gates_widget_set_text(t, tb, GATES_STR("zz")) == PROVEN_ERR_BUSY);
    GT_ASSERT(tb_is(t, tb, "world"));
    GT_ASSERT(gates_input_composing(t));

    GT_ASSERT(gates_input_preedit_cancel(t) == GATES_INPUT_CONSUMED);
    GT_ASSERT_OK(gates_textbox_set_text(t, tb, GATES_STR("zz")));
    GT_ASSERT(tb_is(t, tb, "zz"));

    GT_ASSERT(gates_textbox_set_text(t, gates_tree_root(t), GATES_STR("x")) ==
              PROVEN_ERR_INVALID_ARG);
    gates_tree_destroy(t);
}

static void test_allocation_failure_is_reported(void) {
    fail_alloc_t fa = { .inner = proven_heap_allocator() };
    gates_allocator_t alloc = { .ctx = &fa, .alloc_fn = fa_alloc, .realloc_fn = fa_realloc,
                                .free_fn = fa_free };
    gates_node_t tb = GATES_NODE_NULL;
    gates_tree_t *t = make_ui_alloc(alloc, &tb, nullptr);
    gates_tree_set_focus(t, tb);
    gates_text_edit_t *ed = gates_textbox_edit(t, tb);
    gates_text_edit_set_caret(ed, 3, false);

    /* 40 bytes: larger than any buffer the textbox holds so far. */
    const char *big = "0123456789012345678901234567890123456789";

    /* First preedit needs its buffer: failure leaves no composition. */
    fa.fail = true;
    GT_ASSERT(gates_input_preedit(t, lit(G_J), 3) == GATES_INPUT_FAILED);
    GT_ASSERT(!gates_input_composing(t));
    fa.fail = false;

    /* A growing preedit that fails keeps the previous preedit. */
    GT_ASSERT(gates_input_preedit(t, lit(G_J), 3) == GATES_INPUT_CONSUMED);
    fa.fail = true;
    GT_ASSERT(gates_input_preedit(t, lit(big), 40) == GATES_INPUT_FAILED);
    GT_ASSERT(str_is(gates_text_edit_preedit(ed), G_J));

    /* A failed commit leaves text and caret as they were and is not "consumed". */
    GT_ASSERT(gates_input_commit(t, lit(big)) == GATES_INPUT_FAILED);
    GT_ASSERT(tb_is(t, tb, "abc"));
    GT_ASSERT(gates_text_edit_caret(ed) == 3);

    /* Same contract for plain characters once the buffer is full. */
    fa.fail = false;
    GT_ASSERT(gates_input_preedit_cancel(t) == GATES_INPUT_CONSUMED);
    GT_ASSERT(gates_input_commit(t, lit("0123456789012345678901234567")) ==
              GATES_INPUT_CONSUMED); /* 31 bytes of 32 */
    GT_ASSERT(gates_input_char(t, 'x') == GATES_INPUT_CONSUMED); /* 32 of 32 */
    fa.fail = true;
    GT_ASSERT(gates_input_char(t, 'y') == GATES_INPUT_FAILED);
    GT_ASSERT(gates_textbox_text(t, tb).size == 32);
    GT_ASSERT(gates_textbox_set_text(t, tb, lit(big)) == PROVEN_ERR_NOMEM);
    GT_ASSERT(gates_textbox_text(t, tb).size == 32);
    fa.fail = false;
    gates_tree_destroy(t);
}

static void test_paint_shows_preedit_and_caret_rect(void) {
    gates_node_t tb = GATES_NODE_NULL;
    gates_tree_t *t = make_ui(&tb, nullptr);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));

    gates_tree_set_focus(t, tb);
    gates_text_edit_set_caret(gates_textbox_edit(t, tb), 1, false); /* a|bc */
    paint(t, &dl);
    gates_rect_t before;
    GT_ASSERT(gates_input_caret_rect(t, &before));

    GT_ASSERT(gates_input_preedit(t, lit(HAN), 3) == GATES_INPUT_CONSUMED);
    paint(t, &dl);

    /* Committed text splits around the preedit; the preedit sits 1 cell in. */
    const gates_draw_cmd_t *pre = find_text(&dl, HAN);
    const gates_draw_cmd_t *left = find_text(&dl, "a");
    const gates_draw_cmd_t *right = find_text(&dl, "bc");
    GT_ASSERT(pre != nullptr && left != nullptr && right != nullptr);
    if (pre != nullptr && left != nullptr && right != nullptr) {
        GT_ASSERT(pre->rect.x == left->rect.x + M.advance);
        GT_ASSERT(pre->rect.w == 2 * M.advance);
        GT_ASSERT(right->rect.x == pre->rect.x + 2 * M.advance);

        /* Underline: a rect exactly under the preedit cells. */
        bool underline = false;
        for (gates_u32 i = 0; i < gates_draw_list_len(&dl); i++) {
            const gates_draw_cmd_t *c = gates_draw_list_at(&dl, i);
            if (c->kind == GATES_DRAW_RECT && c->rect.x == pre->rect.x &&
                c->rect.w == 2 * M.advance && c->rect.h == 1 &&
                c->rect.y == pre->rect.y + M.line_height - 1) {
                underline = true;
            }
        }
        GT_ASSERT(underline);

        /* The IME cursor is at the end of the preedit (byte 3 = after U+D55C). */
        gates_rect_t cr;
        GT_ASSERT(gates_input_caret_rect(t, &cr));
        GT_ASSERT(cr.x == pre->rect.x + 2 * M.advance);
        GT_ASSERT(cr.x == before.x + 2 * M.advance);
        GT_ASSERT(cr.h == M.line_height);

        /* Cursor at the preedit start (some IMEs report 0). */
        GT_ASSERT(gates_input_preedit(t, lit(HAN), 0) == GATES_INPUT_CONSUMED);
        paint(t, &dl);
        GT_ASSERT(gates_input_caret_rect(t, &cr));
        GT_ASSERT(cr.x == before.x);
        /* A cursor inside a codepoint snaps back to its start. */
        GT_ASSERT(gates_input_preedit(t, lit(HAN), 2) == GATES_INPUT_CONSUMED);
        paint(t, &dl);
        GT_ASSERT(gates_input_caret_rect(t, &cr));
        GT_ASSERT(cr.x == before.x);
    }

    /* No preedit: a single committed run again. */
    GT_ASSERT(gates_input_preedit_cancel(t) == GATES_INPUT_CONSUMED);
    paint(t, &dl);
    GT_ASSERT(find_text(&dl, "abc") != nullptr);
    GT_ASSERT(find_text(&dl, HAN) == nullptr);

    /* Unfocused: no caret rect. */
    gates_tree_set_focus(t, GATES_NODE_NULL);
    paint(t, &dl);
    gates_rect_t r;
    GT_ASSERT(!gates_input_caret_rect(t, &r));

    gates_draw_list_deinit(&dl);
    gates_tree_destroy(t);
}

static void test_preedit_stays_in_view(void) {
    gates_node_t tb = GATES_NODE_NULL;
    gates_tree_t *t = make_ui(&tb, nullptr);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    gates_tree_set_focus(t, tb);
    /* Fill beyond the 20-cell box, caret at the end. */
    GT_ASSERT_OK(gates_textbox_set_text(t, tb, GATES_STR("abcdefghijklmnopqrstuvwxyz")));
    paint(t, &dl);
    GT_ASSERT(gates_input_preedit(t, lit(HAN HAN), 6) == GATES_INPUT_CONSUMED);
    paint(t, &dl);

    const gates_draw_cmd_t *border = nullptr;
    for (gates_u32 i = 0; i < gates_draw_list_len(&dl); i++) {
        if (gates_draw_list_at(&dl, i)->kind == GATES_DRAW_BORDER) {
            border = gates_draw_list_at(&dl, i);
        }
    }
    gates_rect_t cr;
    GT_ASSERT(border != nullptr && gates_input_caret_rect(t, &cr));
    if (border != nullptr) {
        gates_rect_t box = border->rect;
        GT_ASSERT(cr.x > box.x && cr.x < box.x + box.w);
    }
    gates_draw_list_deinit(&dl);
    gates_tree_destroy(t);
}

int main(void) {
    be = gates_text_backend_builtin();
    theme = gates_theme_light();
    M = be->metrics(be->ctx, 0);
    test_no_target_is_ignored();
    test_preedit_then_commit();
    test_cancel_never_commits();
    test_commit_replaces_selection();
    test_focus_change_drops_preedit();
    test_set_text_busy_while_composing();
    test_allocation_failure_is_reported();
    test_paint_shows_preedit_and_caret_rect();
    test_preedit_stays_in_view();
    return gt_report("test_ime_compose");
}
