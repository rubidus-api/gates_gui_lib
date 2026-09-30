/* proportional text (0.2.0): the per-code-point contract, width and
 * offset helpers, the text box (caret, selection, pointer, scrolling,
 * composition, passwords, accessibility rects) on a proportional backend,
 * and fonts chosen per node with inheritance. */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/layout.h>
#include <gates/access.h>
#include <gates/draw.h>
#include "gates_test.h"
#include "text_prop_backend.h"

#include <string.h>

static const gates_text_backend_t *be = &tp_backend;

static void layout(gates_tree_t *t, gates_i32 w) {
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ w, 200 }, be));
}

/* -- the contract ---------------------------------------------------------------------- */

static void test_contract(void) {
    const gates_text_backend_t *bi = gates_text_backend_builtin();
    static const char *const samples[] = { "", "a", "mill", "\xed\x95\x9c\xea\xb8\x80 ok", "e\xcc\x81", "\xff" };
    for (unsigned k = 0; k < sizeof samples / sizeof samples[0]; k++) {
        gates_str_t s = { .ptr = (const gates_u8 *)samples[k], .size = strlen(samples[k]) };
        for (gates_font_t f = GATES_FONT_UI; f <= GATES_FONT_MONO; f++) {
            /* measure == sum of glyph advances, for both backends */
            gates_i32 sum_b = 0, sum_p = 0;
            for (gates_u32 i = 0; i < s.size;) {
                gates_u32 cp;
                i += gates_text_decode(s, i, &cp);
                sum_b += bi->glyph_advance(bi->ctx, f, cp);
                sum_p += be->glyph_advance(be->ctx, f, cp);
            }
            GT_ASSERT(bi->measure(bi->ctx, f, s).w == sum_b);
            GT_ASSERT(be->measure(be->ctx, f, s).w == sum_p);
            GT_ASSERT(gates_text_width(bi, f, s) == sum_b);
            GT_ASSERT(gates_text_width(be, f, s) == sum_p);
        }
    }
    /* builtin: the cell rule for both fonts (it stays the monospace reference) */
    GT_ASSERT(bi->glyph_advance(bi->ctx, GATES_FONT_UI, 'm') == 8);
    GT_ASSERT(bi->glyph_advance(bi->ctx, GATES_FONT_UI, 0xD55C) == 16);
    GT_ASSERT(gates_text_width(be, GATES_FONT_UI, GATES_STR("mil")) == 12 + 3 + 3);
    GT_ASSERT(gates_text_width(be, GATES_FONT_MONO, GATES_STR("mil")) == 24);
    GT_ASSERT(gates_text_width(nullptr, GATES_FONT_UI, GATES_STR("x")) == 0);
}

static void test_offset_at_x(void) {
    gates_str_t s = GATES_STR("mi\xed\x95\x9c" "a"); /* m(12) i(3) wide(16) a(7): edges 0 12 15 31 38 */
    GT_ASSERT(gates_text_offset_at_x(be, GATES_FONT_UI, s, -5) == 0);
    GT_ASSERT(gates_text_offset_at_x(be, GATES_FONT_UI, s, 5) == 0);   /* nearer 0 than 12 */
    GT_ASSERT(gates_text_offset_at_x(be, GATES_FONT_UI, s, 7) == 1);   /* nearer 12 */
    GT_ASSERT(gates_text_offset_at_x(be, GATES_FONT_UI, s, 14) == 2);  /* nearer 15 */
    GT_ASSERT(gates_text_offset_at_x(be, GATES_FONT_UI, s, 22) == 2);  /* inside the wide one, left half */
    GT_ASSERT(gates_text_offset_at_x(be, GATES_FONT_UI, s, 24) == 5);  /* right half */
    GT_ASSERT(gates_text_offset_at_x(be, GATES_FONT_UI, s, 999) == 6);
    GT_ASSERT(gates_text_offset_at_x(be, GATES_FONT_UI, GATES_STR(""), 3) == 0);
}

/* -- the text box on a proportional backend ---------------------------------------------- */

static gates_rect_t caret_of(gates_tree_t *t) {
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, gates_theme_light(), be));
    gates_draw_list_deinit(&dl);
    gates_rect_t r = {0};
    GT_ASSERT(gates_input_caret_rect(t, &r));
    return r;
}

static void click(gates_tree_t *t, gates_i32 x, gates_i32 y) {
    gates_pointer_event_t ev = { .action = GATES_POINTER_DOWN, .button = GATES_BUTTON_LEFT, .pos = { x, y },
                                 .buttons = GATES_BUTTON_LEFT, .primary = true };
    (void)gates_input_pointer(t, &ev);
    ev.action = GATES_POINTER_UP;
    ev.buttons = 0;
    (void)gates_input_pointer(t, &ev);
}

static void test_textbox(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t), box;
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_textbox_create(t, root, GATES_STR("mill"), 10, &box));
    layout(t, 300);
    gates_rect_t whole = gates_node_layout_rect(t, box);
    GT_ASSERT(whole.w == 300); /* the column stretches it */
    gates_tree_set_focus(t, box);
    /* caret at the end of "mill": 12 + 3 + 3 + 3 = 21 after the inner edge */
    GT_ASSERT_OK(gates_textbox_set_selection(t, box, 4, 4));
    gates_rect_t inner_probe = caret_of(t);
    GT_ASSERT_OK(gates_textbox_set_selection(t, box, 0, 0));
    gates_rect_t c0 = caret_of(t);
    GT_ASSERT(inner_probe.x - c0.x == 21);
    GT_ASSERT_OK(gates_textbox_set_selection(t, box, 1, 1));
    GT_ASSERT(caret_of(t).x - c0.x == 12);

    /* a click lands on the nearest boundary: 13 after the edge -> after "m" */
    click(t, c0.x + 13, c0.y + 4);
    gates_u32 a = 0, k = 0;
    GT_ASSERT_OK(gates_textbox_selection(t, box, &a, &k));
    GT_ASSERT(k == 1);
    click(t, c0.x + 19, c0.y + 4); /* between 18 (after "mil") and 21: nearer 18 */
    GT_ASSERT_OK(gates_textbox_selection(t, box, &a, &k));
    GT_ASSERT(k == 3);

    /* the selection rectangle spans the real glyph widths */
    GT_ASSERT_OK(gates_textbox_set_selection(t, box, 1, 3));
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, gates_theme_light(), be));
    bool found = false;
    for (gates_u32 i = 0; i < gates_draw_list_len(&dl); i++) {
        const gates_draw_cmd_t *cmd = gates_draw_list_at(&dl, i);
        gates_color_t sel = gates_theme_color(gates_theme_light(), GATES_COLOR_SELECTION_BG);
        if (cmd->kind == GATES_DRAW_RECT && cmd->color.r == sel.r && cmd->color.g == sel.g && cmd->color.b == sel.b) {
            GT_ASSERT(cmd->rect.x == c0.x + 12 && cmd->rect.w == 6);
            found = true;
        }
        if (cmd->kind == GATES_DRAW_TEXT) GT_ASSERT(cmd->rect.w == 21 && cmd->font == GATES_FONT_UI);
    }
    GT_ASSERT(found);
    gates_draw_list_deinit(&dl);

    /* accessibility rects agree with the drawing */
    gates_rect_t r;
    GT_ASSERT(gates_access_text_rect(t, box, 1, 3, &r));
    GT_ASSERT(r.x == c0.x + 12 && r.w == 6);
    GT_ASSERT(gates_access_text_offset_at(t, box, (gates_point_t){ c0.x + 13, c0.y }) == 1);

    /* a long text scrolls to keep the caret inside the box */
    GT_ASSERT_OK(gates_textbox_set_text(t, box, GATES_STR("mmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmm"))); /* 50 x 12 = 600 */
    GT_ASSERT_OK(gates_textbox_set_selection(t, box, 50, 50));
    gates_rect_t far = caret_of(t);
    gates_rect_t inner = gates_node_layout_rect(t, box);
    GT_ASSERT(far.x >= inner.x && far.x < inner.x + inner.w);
    gates_i32 right_edge = far.x;          /* scrolled so the caret sits at the right edge */
    GT_ASSERT(right_edge > c0.x + 200);
    /* a click while scrolled maps through the scroll offset: just left of the caret -> the end */
    click(t, right_edge - 2, c0.y + 4);
    GT_ASSERT_OK(gates_textbox_selection(t, box, &a, &k));
    GT_ASSERT(k == 50);
    /* moving left of the view scrolls just enough: the caret lands on the left edge */
    GT_ASSERT_OK(gates_textbox_set_selection(t, box, 10, 10));
    GT_ASSERT(caret_of(t).x == c0.x);
    GT_ASSERT_OK(gates_textbox_set_selection(t, box, 11, 11));
    GT_ASSERT(caret_of(t).x == c0.x + 12); /* no scroll while it stays inside */
    /* a caret past the right edge (text still beyond it) lands on the right edge */
    GT_ASSERT_OK(gates_textbox_set_selection(t, box, 0, 0));
    (void)caret_of(t);
    GT_ASSERT_OK(gates_textbox_set_selection(t, box, 30, 30)); /* x 360, beyond the box */
    GT_ASSERT(caret_of(t).x == right_edge);
    GT_ASSERT_OK(gates_textbox_set_selection(t, box, 0, 0));
    GT_ASSERT(caret_of(t).x == c0.x); /* back at the start: scrolled home */

    /* composition: the caret follows the preedit's own widths */
    GT_ASSERT_OK(gates_textbox_set_text(t, box, GATES_STR("ab")));
    GT_ASSERT_OK(gates_textbox_set_selection(t, box, 1, 1));
    (void)gates_input_preedit(t, GATES_STR("\xed\x95\x9c"), 3); /* wide, cursor after it */
    GT_ASSERT(caret_of(t).x - c0.x == 7 + 16);
    (void)gates_input_preedit_cancel(t);

    /* passwords: one star per code point, at the star's width */
    GT_ASSERT_OK(gates_textbox_set_password(t, box, true));
    GT_ASSERT_OK(gates_textbox_set_text(t, box, GATES_STR("mi")));
    GT_ASSERT_OK(gates_textbox_set_selection(t, box, 2, 2));
    GT_ASSERT(caret_of(t).x - c0.x == 2 * 7);
    gates_tree_destroy(t);
}

/* -- fonts per node --------------------------------------------------------------------- */

static void test_fonts(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t), panel, a, b, c;
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_label_create(t, root, GATES_STR("mill"), &a));
    GT_ASSERT_OK(gates_panel_create(t, root, &panel));
    GT_ASSERT_OK(gates_layout_set(t, panel, GATES_LAYOUT_KIND_ROW));
    GT_ASSERT_OK(gates_label_create(t, panel, GATES_STR("mill"), &b));
    GT_ASSERT_OK(gates_label_create(t, panel, GATES_STR("mill"), &c));
    GT_ASSERT(gates_node_font(t, a) == GATES_FONT_UI); /* the default */
    GT_ASSERT_OK(gates_node_set_font(t, panel, GATES_FONT_MONO));
    GT_ASSERT(gates_node_font(t, b) == GATES_FONT_MONO); /* inherited */
    GT_ASSERT_OK(gates_node_set_font(t, c, GATES_FONT_UI));
    GT_ASSERT(gates_node_font(t, c) == GATES_FONT_UI);   /* its own wins */
    GT_ASSERT(gates_node_set_font(t, a, 7) == PROVEN_ERR_INVALID_ARG);
    layout(t, 300);
    GT_ASSERT(gates_node_preferred_size(t, a).w == 21);
    GT_ASSERT(gates_node_preferred_size(t, b).w == 32);
    GT_ASSERT(gates_node_preferred_size(t, c).w == 21);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, gates_theme_light(), be));
    int mono = 0, ui = 0;
    for (gates_u32 i = 0; i < gates_draw_list_len(&dl); i++) {
        const gates_draw_cmd_t *cmd = gates_draw_list_at(&dl, i);
        if (cmd->kind != GATES_DRAW_TEXT) continue;
        if (cmd->font == GATES_FONT_MONO) mono++;
        else ui++;
    }
    GT_ASSERT(mono == 1 && ui == 2);
    gates_draw_list_deinit(&dl);
    /* back to inheriting */
    GT_ASSERT_OK(gates_node_set_font(t, c, GATES_FONT_INHERIT));
    GT_ASSERT(gates_node_font(t, c) == GATES_FONT_MONO);
    gates_tree_destroy(t);
}

int main(void) {
    test_contract();
    test_offset_at_x();
    test_textbox();
    test_fonts();
    return gt_report("test_text_prop");
}
