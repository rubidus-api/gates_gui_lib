/* T015: textbox widget - focus, caret placement, typing, painting
 * (docs/tests/cases/T015-textbox.md). */
#include <gates/ui.h>
#include <gates/widget.h>
#include "gates_test.h"

#include <string.h>

#define COLS 10
#define HAN "한"

static const gates_text_backend_t *be;
static const gates_theme_t *theme;
static gates_text_metrics_t M;

static gates_tree_t *make_ui(gates_node_t *out_tb, gates_node_t *out_btn) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_textbox_create(t, root, GATES_STR("abc"), COLS, out_tb));
    if (out_btn != nullptr) {
        GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("B"), nullptr, nullptr, out_btn));
    }
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 300, 120 }, be));
    return t;
}

static bool tb_text_is(const gates_tree_t *t, gates_node_t tb, const char *expect) {
    gates_str_t s = gates_textbox_text(t, tb);
    gates_usize_t n = strlen(expect);
    return s.size == n && (n == 0 || memcmp(s.ptr, expect, n) == 0);
}

static void press(gates_tree_t *t, gates_i32 x, gates_i32 y) {
    gates_pointer_event_t e = { .action = GATES_POINTER_DOWN, .button = GATES_BUTTON_LEFT,
                                .pos = { x, y } };
    (void)gates_input_pointer(t, &e);
}

static void move_to(gates_tree_t *t, gates_i32 x, gates_i32 y) {
    gates_pointer_event_t e = { .action = GATES_POINTER_MOVE, .pos = { x, y } };
    (void)gates_input_pointer(t, &e);
}

static void release(gates_tree_t *t, gates_i32 x, gates_i32 y) {
    gates_pointer_event_t e = { .action = GATES_POINTER_UP, .button = GATES_BUTTON_LEFT,
                                .pos = { x, y } };
    (void)gates_input_pointer(t, &e);
}

static bool key(gates_tree_t *t, gates_key_t k, bool shift, bool ctrl) {
    gates_key_event_t e = { .key = k, .down = true, .shift = shift, .ctrl = ctrl };
    return gates_input_key(t, &e);
}

static void type_str(gates_tree_t *t, const char *ascii) {
    for (const char *p = ascii; *p; p++) {
        GT_ASSERT(gates_input_char(t, (gates_u32)(gates_u8)*p));
    }
}

static int count_kind(const gates_draw_list_t *dl, gates_draw_kind_t kind) {
    int n = 0;
    for (gates_u32 i = 0; i < gates_draw_list_len(dl); i++) {
        if (gates_draw_list_at(dl, i)->kind == kind) n++;
    }
    return n;
}

static bool color_eq(gates_color_t a, gates_color_t b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static void test_creation_and_intrinsic_size(void) {
    gates_node_t tb = GATES_NODE_NULL;
    gates_tree_t *t = make_ui(&tb, nullptr);

    GT_ASSERT(gates_node_kind(t, tb) == GATES_NODE_TEXTBOX);
    GT_ASSERT(tb_text_is(t, tb, "abc"));
    GT_ASSERT(gates_textbox_edit(t, tb) != nullptr);

    /* cols * advance plus padding and border on both sides. */
    gates_size_t pref = gates_node_preferred_size(t, tb);
    GT_ASSERT(pref.w == COLS * M.advance + 2 * (4 + 1));
    GT_ASSERT(pref.h == M.line_height + 2 * (3 + 1));

    /* Non-textbox nodes return nothing from textbox accessors. */
    GT_ASSERT(gates_textbox_edit(t, gates_tree_root(t)) == nullptr);
    GT_ASSERT(gates_textbox_text(t, gates_tree_root(t)).size == 0);

    gates_tree_destroy(t);
}

static void test_click_focuses_and_places_caret(void) {
    gates_node_t tb = GATES_NODE_NULL, btn = GATES_NODE_NULL;
    gates_tree_t *t = make_ui(&tb, &btn);
    gates_rect_t r = gates_node_layout_rect(t, tb);
    gates_i32 inner_x = r.x + 4 + 1;
    gates_i32 mid_y = r.y + r.h / 2;

    GT_ASSERT(gates_node_is_null(gates_tree_focus(t)));

    /* Click on the second cell: caret lands between 'a' and 'b'. */
    press(t, inner_x + M.advance + 1, mid_y);
    release(t, inner_x + M.advance + 1, mid_y);
    GT_ASSERT(gates_node_eq(gates_tree_focus(t), tb));
    GT_ASSERT(gates_text_edit_caret(gates_textbox_edit(t, tb)) == 1);

    /* Click far right: caret goes to the end. */
    press(t, r.x + r.w - 2, mid_y);
    release(t, r.x + r.w - 2, mid_y);
    GT_ASSERT(gates_text_edit_caret(gates_textbox_edit(t, tb)) == 3);

    /* Clicking a button moves focus to it (plan-0009; DECISIONS 2026-09-25). */
    gates_rect_t br = gates_node_layout_rect(t, btn);
    press(t, br.x + 2, br.y + 2);
    release(t, br.x + 2, br.y + 2);
    GT_ASSERT(gates_node_eq(gates_tree_focus(t), btn));

    gates_tree_destroy(t);
}

static void test_drag_selects(void) {
    gates_node_t tb = GATES_NODE_NULL;
    gates_tree_t *t = make_ui(&tb, nullptr);
    gates_rect_t r = gates_node_layout_rect(t, tb);
    gates_i32 inner_x = r.x + 5;
    gates_i32 mid_y = r.y + r.h / 2;
    gates_text_edit_t *ed = gates_textbox_edit(t, tb);

    press(t, inner_x, mid_y);                       /* caret at 0 */
    move_to(t, inner_x + 2 * M.advance + 1, mid_y); /* extend over "ab" */
    GT_ASSERT(gates_text_edit_has_selection(ed));
    GT_ASSERT(gates_text_edit_sel_begin(ed) == 0 && gates_text_edit_sel_end(ed) == 2);
    release(t, inner_x + 2 * M.advance + 1, mid_y);

    /* Typing replaces the selection. */
    type_str(t, "Z");
    GT_ASSERT(tb_text_is(t, tb, "Zc"));

    gates_tree_destroy(t);
}

static void press_n(gates_tree_t *t, gates_i32 x, gates_i32 y, gates_u32 clicks, bool shift) {
    gates_pointer_event_t e = { .action = GATES_POINTER_DOWN, .button = GATES_BUTTON_LEFT,
                                .pos = { x, y }, .clicks = clicks, .shift = shift };
    (void)gates_input_pointer(t, &e);
    release(t, x, y);
}

/* 0.8.0: Shift+press extends, a double press selects a word, a triple press all. */
static void test_click_units(void) {
    gates_node_t tb = GATES_NODE_NULL;
    gates_tree_t *t = make_ui(&tb, nullptr);
    GT_ASSERT_OK(gates_textbox_set_text(t, tb, GATES_STR("ab cd.e")));
    gates_rect_t r = gates_node_layout_rect(t, tb);
    gates_i32 x0 = r.x + 5, y = r.y + r.h / 2;
    gates_text_edit_t *ed = gates_textbox_edit(t, tb);
    press_n(t, x0 + 3 * M.advance + 2, y, 2, false); /* on 'c' */
    GT_ASSERT(gates_text_edit_sel_begin(ed) == 3 && gates_text_edit_sel_end(ed) == 5);
    press_n(t, x0 + M.advance + 1, y, 1, true); /* Shift: from the anchor to 1 */
    GT_ASSERT(gates_text_edit_sel_begin(ed) == 1 && gates_text_edit_sel_end(ed) == 3);
    press_n(t, x0 + 5 * M.advance + 2, y, 2, false); /* on '.': punctuation is its own run */
    GT_ASSERT(gates_text_edit_sel_begin(ed) == 5 && gates_text_edit_sel_end(ed) == 6);
    press_n(t, x0 + 2 * M.advance + 2, y, 2, false); /* on the blank */
    GT_ASSERT(gates_text_edit_sel_begin(ed) == 2 && gates_text_edit_sel_end(ed) == 3);
    press_n(t, x0 + 20 * M.advance, y, 2, false); /* past the end: the last run */
    GT_ASSERT(gates_text_edit_sel_begin(ed) == 6 && gates_text_edit_sel_end(ed) == 7);
    press_n(t, x0 + M.advance, y, 3, false);
    GT_ASSERT(gates_text_edit_sel_begin(ed) == 0 && gates_text_edit_sel_end(ed) == 7);
    press_n(t, x0 + 20 * M.advance, y, 1, false); /* a plain press collapses */
    GT_ASSERT(!gates_text_edit_has_selection(ed) && gates_text_edit_caret(ed) == 7);
    GT_ASSERT_OK(gates_textbox_set_text(t, tb, GATES_STR("x \ty")));
    press_n(t, x0 + M.advance + 2, y, 2, false); /* a tab is a blank too */
    GT_ASSERT(gates_text_edit_sel_begin(ed) == 1 && gates_text_edit_sel_end(ed) == 3);
    GT_ASSERT_OK(gates_textbox_set_password(t, tb, true));
    GT_ASSERT_OK(gates_textbox_set_text(t, tb, GATES_STR("ab cd")));
    press_n(t, x0 + M.advance, y, 2, false); /* a password box has no words to show: all */
    GT_ASSERT(gates_text_edit_sel_begin(ed) == 0 && gates_text_edit_sel_end(ed) == 5);
    GT_ASSERT_OK(gates_textbox_set_password(t, tb, false));
    GT_ASSERT_OK(gates_textbox_set_text(t, tb, GATES_STR("")));
    press_n(t, x0, y, 2, false); /* empty: nothing to select, no crash */
    GT_ASSERT(!gates_text_edit_has_selection(ed));
    gates_tree_destroy(t);
}

static void test_typing_and_editing_keys(void) {
    gates_node_t tb = GATES_NODE_NULL;
    gates_tree_t *t = make_ui(&tb, nullptr);
    gates_text_edit_t *ed = gates_textbox_edit(t, tb);
    gates_tree_set_focus(t, tb);

    GT_ASSERT(key(t, GATES_KEY_END, false, false));
    type_str(t, "de");
    GT_ASSERT(tb_text_is(t, tb, "abcde"));

    GT_ASSERT(key(t, GATES_KEY_BACKSPACE, false, false));
    GT_ASSERT(tb_text_is(t, tb, "abcd"));
    GT_ASSERT(key(t, GATES_KEY_HOME, false, false));
    GT_ASSERT(key(t, GATES_KEY_DELETE, false, false));
    GT_ASSERT(tb_text_is(t, tb, "bcd"));

    /* Shift+Right selects; Ctrl+A selects everything. */
    GT_ASSERT(key(t, GATES_KEY_RIGHT, true, false));
    GT_ASSERT(gates_text_edit_sel_begin(ed) == 0 && gates_text_edit_sel_end(ed) == 1);
    GT_ASSERT(key(t, GATES_KEY_A, false, true));
    GT_ASSERT(gates_text_edit_sel_end(ed) == 3);

    /* Non-ASCII input arrives as a codepoint and encodes to UTF-8. */
    GT_ASSERT(gates_input_char(t, 0xD55C)); /* 한 */
    GT_ASSERT(tb_text_is(t, tb, HAN));

    /* Control characters and unmapped keys are not consumed. */
    GT_ASSERT(!gates_input_char(t, 0x0A));
    GT_ASSERT(!key(t, GATES_KEY_ENTER, false, false));
    GT_ASSERT(!key(t, GATES_KEY_TAB, false, false));

    gates_tree_destroy(t);
}

static void test_unfocused_and_disabled_ignore_keys(void) {
    gates_node_t tb = GATES_NODE_NULL;
    gates_tree_t *t = make_ui(&tb, nullptr);

    /* No focus: keys and characters go nowhere. */
    GT_ASSERT(!key(t, GATES_KEY_BACKSPACE, false, false));
    GT_ASSERT(!gates_input_char(t, 'x'));
    GT_ASSERT(tb_text_is(t, tb, "abc"));

    /* Disabled textbox: focusable by API but inert to input and to clicks. */
    gates_tree_set_focus(t, tb);
    GT_ASSERT_OK(gates_widget_set_disabled(t, tb, true));
    GT_ASSERT(!gates_input_char(t, 'x'));
    GT_ASSERT(!key(t, GATES_KEY_DELETE, false, false));
    GT_ASSERT(tb_text_is(t, tb, "abc"));

    gates_tree_set_focus(t, GATES_NODE_NULL);
    gates_rect_t r = gates_node_layout_rect(t, tb);
    press(t, r.x + 6, r.y + r.h / 2);
    GT_ASSERT(gates_node_is_null(gates_tree_focus(t))); /* disabled: no focus */

    gates_tree_destroy(t);
}

static void test_paint_chrome(void) {
    gates_node_t tb = GATES_NODE_NULL;
    gates_tree_t *t = make_ui(&tb, nullptr);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));

    /* Unfocused: bg + border(CONTROL_BORDER) + clipped text, no caret. */
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    GT_ASSERT(count_kind(&dl, GATES_DRAW_CLIP_PUSH) == 1);
    GT_ASSERT(gates_draw_list_balanced(&dl));
    const gates_draw_cmd_t *border = gates_draw_list_at(&dl, 2);
    GT_ASSERT(border->kind == GATES_DRAW_BORDER);
    GT_ASSERT(color_eq(border->color, gates_theme_color(theme, GATES_COLOR_CONTROL_BORDER)));
    int rects_unfocused = count_kind(&dl, GATES_DRAW_RECT);

    /* Focused: border switches to the focus-ring token and a caret appears. */
    gates_tree_set_focus(t, tb);
    gates_draw_list_reset(&dl);
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    border = gates_draw_list_at(&dl, 2);
    GT_ASSERT(color_eq(border->color, gates_theme_color(theme, GATES_COLOR_FOCUS_RING)));
    GT_ASSERT(count_kind(&dl, GATES_DRAW_RECT) == rects_unfocused + 1); /* caret */
    const gates_draw_cmd_t *caret = gates_draw_list_at(&dl, gates_draw_list_len(&dl) - 2);
    GT_ASSERT(caret->kind == GATES_DRAW_RECT && caret->rect.w == 1);
    GT_ASSERT(caret->rect.h == M.line_height);

    /* Selection adds a highlight rect behind the text. */
    gates_text_edit_select_all(gates_textbox_edit(t, tb));
    gates_draw_list_reset(&dl);
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    bool found_selection = false;
    for (gates_u32 i = 0; i < gates_draw_list_len(&dl); i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(&dl, i);
        if (c->kind == GATES_DRAW_RECT &&
            color_eq(c->color, gates_theme_color(theme, GATES_COLOR_SELECTION_BG)) &&
            c->rect.w == 3 * M.advance) {
            found_selection = true;
        }
    }
    GT_ASSERT(found_selection);

    gates_draw_list_deinit(&dl);
    gates_tree_destroy(t);
}

static void test_long_text_scrolls_into_view(void) {
    gates_node_t tb = GATES_NODE_NULL;
    gates_tree_t *t = make_ui(&tb, nullptr);
    gates_tree_set_focus(t, tb);
    /* Type past the visible width; the caret must stay inside the box. */
    type_str(t, "0123456789ABCDEFGHIJ");
    GT_ASSERT(gates_textbox_text(t, tb).size == 23);

    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));

    gates_rect_t r = gates_node_layout_rect(t, tb);
    const gates_draw_cmd_t *caret = gates_draw_list_at(&dl, gates_draw_list_len(&dl) - 2);
    GT_ASSERT(caret->kind == GATES_DRAW_RECT && caret->rect.w == 1);
    GT_ASSERT(caret->rect.x > r.x && caret->rect.x < r.x + r.w);

    /* Home scrolls the view back to the start. */
    GT_ASSERT(key(t, GATES_KEY_HOME, false, false));
    gates_draw_list_reset(&dl);
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    caret = gates_draw_list_at(&dl, gates_draw_list_len(&dl) - 2);
    GT_ASSERT(caret->rect.x == r.x + 5);

    gates_draw_list_deinit(&dl);
    gates_tree_destroy(t);
}

int main(void) {
    be = gates_text_backend_builtin();
    theme = gates_theme_light();
    M = be->metrics(be->ctx, 0);
    test_creation_and_intrinsic_size();
    test_click_focuses_and_places_caret();
    test_drag_selects();
    test_click_units();
    test_typing_and_editing_keys();
    test_unfocused_and_disabled_ignore_keys();
    test_paint_chrome();
    test_long_text_scrolls_into_view();
    return gt_report("test_textbox");
}
