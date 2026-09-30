/* proportional text (0.2.0): the per-code-point contract, width and
 * offset helpers, the text box (caret, selection, pointer, scrolling,
 * composition, passwords, accessibility rects) on a proportional backend,
 * and fonts chosen per node with inheritance. */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/layout.h>
#include <gates/access.h>
#include <gates/draw.h>
#include <gates/render.h>
#include <gates/view.h>
#include <gates/editor.h>
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

/* -- sizes (0.10.0) ------------------------------------------------------------------------ */

static void test_font_values(void) {
    GT_ASSERT(gates_font_sized(GATES_FONT_UI, 100) == GATES_FONT_UI);
    GT_ASSERT(gates_font_sized(GATES_FONT_MONO, 100) == GATES_FONT_MONO);
    gates_font_t m150 = gates_font_sized(GATES_FONT_MONO, 150);
    GT_ASSERT(m150 != GATES_FONT_MONO && gates_font_face(m150) == GATES_FONT_MONO && gates_font_percent(m150) == 150);
    GT_ASSERT(gates_font_percent(GATES_FONT_UI) == 100 && gates_font_percent(GATES_FONT_MONO) == 100);
    gates_font_t m85 = gates_font_sized(m150, 85); /* a sized font's face, resized */
    GT_ASSERT(gates_font_face(m85) == GATES_FONT_MONO && gates_font_percent(m85) == 85);
    GT_ASSERT(gates_font_sized(m150, 100) == GATES_FONT_MONO);
    GT_ASSERT(gates_font_scale(gates_font_sized(GATES_FONT_UI, 85), 8) == 7);   /* 6.8 */
    GT_ASSERT(gates_font_scale(gates_font_sized(GATES_FONT_UI, 85), 16) == 14); /* 13.6 */
    GT_ASSERT(gates_font_scale(gates_font_sized(GATES_FONT_UI, 50), 7) == 4);   /* 3.5 rounds up */
    GT_ASSERT(gates_font_scale(gates_font_sized(GATES_FONT_UI, 50), 5) == 3);   /* 2.5 rounds up */
    GT_ASSERT(gates_font_scale(GATES_FONT_UI, 13) == 13);
    GT_ASSERT(gates_font_scale(gates_font_sized(GATES_FONT_UI, 400), 16) == 64);

    /* The contract holds at every size, on both backends. */
    const gates_text_backend_t *bi = gates_text_backend_builtin();
    static const unsigned pcts[] = { 50, 85, 150, 400 };
    gates_str_t sample = GATES_STR("mil \xed\x95\x9c\xea\xb8\x80 e\xcc\x81");
    for (unsigned k = 0; k < 4; k++) {
        for (gates_font_t face = GATES_FONT_UI; face <= GATES_FONT_MONO; face++) {
            gates_font_t f = gates_font_sized(face, pcts[k]);
            gates_i32 sum_b = 0, sum_p = 0;
            for (gates_u32 i = 0; i < sample.size;) {
                gates_u32 cp;
                i += gates_text_decode(sample, i, &cp);
                sum_b += bi->glyph_advance(bi->ctx, f, cp);
                sum_p += be->glyph_advance(be->ctx, f, cp);
            }
            GT_ASSERT(bi->measure(bi->ctx, f, sample).w == sum_b && gates_text_width(bi, f, sample) == sum_b);
            GT_ASSERT(be->measure(be->ctx, f, sample).w == sum_p);
            GT_ASSERT(bi->measure(bi->ctx, f, sample).h == bi->metrics(bi->ctx, f).line_height);
        }
    }
    /* builtin: the cell scales. */
    gates_font_t u150 = gates_font_sized(GATES_FONT_UI, 150), u50 = gates_font_sized(GATES_FONT_UI, 50);
    gates_text_metrics_t m = bi->metrics(bi->ctx, u150);
    GT_ASSERT(m.advance == 12 && m.line_height == 24 && m.ascent == 18 && m.descent == 6);
    m = bi->metrics(bi->ctx, u50);
    GT_ASSERT(m.advance == 4 && m.line_height == 8 && m.ascent == 6 && m.descent == 2);
    GT_ASSERT(bi->glyph_advance(bi->ctx, u150, 'm') == 12 && bi->glyph_advance(bi->ctx, u150, 0xD55C) == 24);
    GT_ASSERT(bi->measure(bi->ctx, u150, GATES_STR("ab")).w == 24);
    m = bi->metrics(bi->ctx, GATES_FONT_UI);
    GT_ASSERT(m.advance == 8 && m.line_height == 16 && m.ascent == 12 && m.descent == 4);
    /* Below the range a backend still never answers 0 wide or tall. */
    gates_font_t tiny = gates_font_sized(GATES_FONT_UI, 5);
    GT_ASSERT(bi->glyph_advance(bi->ctx, tiny, 'a') == 1 && bi->metrics(bi->ctx, tiny).line_height == 1);
}

/* builtin draws a sized glyph as the base glyph scaled, pixel for pixel. */
static void test_builtin_sized_draw(void) {
    const gates_text_backend_t *bi = gates_text_backend_builtin();
    static gates_u32 one[48 * 48], two[48 * 48];
    gates_color_t ink = GATES_RGB(255, 255, 255);
    for (int i = 0; i < 48 * 48; i++) one[i] = two[i] = 0;
    gates_pixels_t p1 = { .ptr = one, .w = 48, .h = 48, .stride_bytes = 48 * 4 };
    gates_pixels_t p2 = { .ptr = two, .w = 48, .h = 48, .stride_bytes = 48 * 4 };
    gates_rect_t all = { 0, 0, 48, 48 };
    bi->draw(bi->ctx, p1, all, all, GATES_FONT_UI, GATES_STR("A\xed\x95\x9c"), ink);
    bi->draw(bi->ctx, p2, all, all, gates_font_sized(GATES_FONT_UI, 200), GATES_STR("A"), ink);
    int lit = 0, same = 1;
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 16; x++) {
            bool a = one[(y / 2) * 48 + x / 2] != 0, b = two[y * 48 + x] != 0;
            if (a != b) same = 0;
            lit += b;
        }
    }
    GT_ASSERT(same && lit > 0);
    GT_ASSERT(two[32 * 48 + 2] == 0 && two[2 * 48 + 16] == 0); /* nothing past the 16 x 32 cell */
    /* The replacement box spans the sized cells: outline on rows 1 and h - 2. */
    for (int i = 0; i < 48 * 48; i++) two[i] = 0;
    bi->draw(bi->ctx, p2, all, all, gates_font_sized(GATES_FONT_UI, 150), GATES_STR("\xed\x95\x9c"), ink);
    GT_ASSERT(two[1 * 48 + 5] != 0 && two[22 * 48 + 5] != 0 && two[10 * 48 + 1] != 0 && two[10 * 48 + 22] != 0);
    GT_ASSERT(two[10 * 48 + 5] == 0 && two[23 * 48 + 5] == 0 && two[10 * 48 + 24] == 0);
    /* The pen moves by sized advances: the second glyph starts at 12. */
    for (int i = 0; i < 48 * 48; i++) one[i] = two[i] = 0;
    bi->draw(bi->ctx, p1, all, all, gates_font_sized(GATES_FONT_UI, 150), GATES_STR("\x7f"), ink); /* a box */
    bi->draw(bi->ctx, p2, all, all, gates_font_sized(GATES_FONT_UI, 150), GATES_STR(" \x7f"), ink);
    int shifted = 1;
    for (int y = 0; y < 24; y++) {
        for (int x = 0; x < 12; x++) {
            if ((one[y * 48 + x] != 0) != (two[y * 48 + x + 12] != 0)) shifted = 0;
        }
    }
    GT_ASSERT(shifted);
}

static gates_u64 fm_count(void *u) { (void)u; return 100; }
static gates_item_id_t fm_id_at(void *u, gates_u64 row) { (void)u; return row < 100 ? row + 1 : 0; }
static bool fm_index_of(void *u, gates_item_id_t id, gates_u64 *row) {
    (void)u;
    if (id == 0 || id > 100) return false;
    *row = id - 1;
    return true;
}
static gates_err_t fm_cell(void *u, gates_item_id_t id, gates_column_id_t col, gates_cell_t *out) {
    (void)u; (void)id; (void)col;
    out->text = GATES_STR("row");
    return GATES_OK;
}

static void test_sized_editor_and_form(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t), ed, form, row, lbl, box;
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_editor_create(t, root, &(gates_editor_desc_t){0}, &ed));
    GT_ASSERT_OK(gates_editor_set_text(t, ed, GATES_STR("a\nb")));
    GT_ASSERT_OK(gates_node_set_font_size(t, ed, 150));
    /* A form at 200 %: its editors need 12 of the size's average widths (14 here). */
    GT_ASSERT_OK(gates_panel_create(t, root, &form));
    GT_ASSERT_OK(gates_layout_set(t, form, GATES_LAYOUT_KIND_FORM));
    GT_ASSERT_OK(gates_panel_create(t, form, &row));
    GT_ASSERT_OK(gates_label_create(t, row, GATES_STR("ab"), &lbl));
    GT_ASSERT_OK(gates_textbox_create(t, row, GATES_STR(""), 2, &box));
    GT_ASSERT_OK(gates_node_set_font_size(t, form, 200));
    layout(t, 150); /* < 28 + 8 + 12 * 14 = 204: stacked; at the base size (120) it would not be */
    gates_rect_t lr = gates_node_layout_rect(t, lbl), br = gates_node_layout_rect(t, box);
    GT_ASSERT(br.y >= lr.y + lr.h && br.x == lr.x);
    layout(t, 260);
    lr = gates_node_layout_rect(t, lbl);
    br = gates_node_layout_rect(t, box);
    GT_ASSERT(br.y == lr.y || br.x > lr.x);
    /* The editor's lines are the sized line apart. */
    gates_rect_t r0 = {0}, r2 = {0};
    GT_ASSERT(gates_access_text_rects(t, ed, 0, 1, &r0, 1) == 1);
    GT_ASSERT(gates_access_text_rects(t, ed, 2, 3, &r2, 1) == 1);
    GT_ASSERT(r2.y - r0.y == 24 && r0.w == 11);
    gates_tree_destroy(t);
}

static void test_font_sizes(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t), panel, a, b, c, box, radio, view;
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_label_create(t, root, GATES_STR("mill"), &a));
    GT_ASSERT_OK(gates_panel_create(t, root, &panel));
    GT_ASSERT_OK(gates_layout_set(t, panel, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_label_create(t, panel, GATES_STR("mill"), &b));
    GT_ASSERT_OK(gates_label_create(t, panel, GATES_STR("mill"), &c));
    GT_ASSERT_OK(gates_textbox_create(t, panel, GATES_STR("mill"), 4, &box));
    gates_option_t opts[2] = { { .id = 1, .label = GATES_STR("one") }, { .id = 2, .label = GATES_STR("two") } };
    GT_ASSERT_OK(gates_radio_create(t, panel, opts, 2, 1, &radio));
    GT_ASSERT_OK(gates_view_create(t, panel, &(gates_view_desc_t){0}, &view));
    gates_rows_model_t model = { .count = fm_count, .id_at = fm_id_at, .index_of = fm_index_of, .cell = fm_cell };
    GT_ASSERT_OK(gates_view_set_model(t, view, &model));
    GT_ASSERT_OK(gates_layout_set_child_grow(t, view, 1));
    GT_ASSERT_OK(gates_layout_set_child_grow(t, panel, 1));
    /* Defaults and range. */
    GT_ASSERT(gates_node_font_size(t, b) == 100);
    GT_ASSERT(gates_node_font(t, b) == GATES_FONT_UI);
    GT_ASSERT(gates_node_set_font_size(t, panel, 49) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_node_set_font_size(t, panel, 401) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_node_set_font_size(nullptr, panel, 100) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_node_set_font_size(t, GATES_NODE_NULL, 100) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_node_font_size(nullptr, b) == 100 && gates_node_font_size(t, GATES_NODE_NULL) == 100);
    GT_ASSERT_OK(gates_node_set_font_size(t, a, GATES_FONT_SIZE_MIN));
    GT_ASSERT_OK(gates_node_set_font_size(t, a, GATES_FONT_SIZE_MAX));
    GT_ASSERT_OK(gates_node_set_font_size(t, a, 0));
    layout(t, 300);
    gates_size_t base_b = gates_node_preferred_size(t, b), base_box = gates_node_preferred_size(t, box);
    gates_size_t base_radio = gates_node_preferred_size(t, radio);
    gates_access_info_t vi = {0};
    GT_ASSERT_OK(gates_access_info(t, view, 1, &vi));
    gates_i32 base_row_h = vi.bounds.h;
    GT_ASSERT(base_row_h >= 16);
    GT_ASSERT(base_b.w == 21 && base_b.h == 16);
    /* A heading panel: everything under it at 150 %, inherited; the face separately. */
    gates_tree_clear_dirty(t, GATES_TREE_DIRTY_LAYOUT);
    GT_ASSERT_OK(gates_node_set_font_size(t, panel, GATES_FONT_SIZE_HEADING));
    GT_ASSERT((gates_tree_dirty(t) & GATES_TREE_DIRTY_LAYOUT) != 0);
    gates_tree_clear_dirty(t, GATES_TREE_DIRTY_LAYOUT);
    GT_ASSERT_OK(gates_node_set_font_size(t, panel, GATES_FONT_SIZE_HEADING)); /* the same: nothing */
    GT_ASSERT((gates_tree_dirty(t) & GATES_TREE_DIRTY_LAYOUT) == 0);
    GT_ASSERT(gates_node_font_size(t, b) == 150 && gates_node_font_size(t, a) == 100);
    GT_ASSERT(gates_node_font(t, b) == gates_font_sized(GATES_FONT_UI, 150));
    GT_ASSERT_OK(gates_node_set_font(t, c, GATES_FONT_MONO));
    GT_ASSERT(gates_node_font(t, c) == gates_font_sized(GATES_FONT_MONO, 150));
    GT_ASSERT_OK(gates_node_set_font_size(t, c, GATES_FONT_SIZE_SMALL)); /* its own wins */
    GT_ASSERT(gates_node_font(t, c) == gates_font_sized(GATES_FONT_MONO, 85));
    GT_ASSERT_OK(gates_node_set_font_size(t, c, 0));                     /* back to the panel's */
    GT_ASSERT(gates_node_font_size(t, c) == 150);
    layout(t, 300);
    /* Measured at the size: 12 + 3 + 3 + 3 = 21 at 100 %, each advance scaled at 150 %. */
    GT_ASSERT(gates_node_preferred_size(t, b).w == 18 + 5 + 5 + 5 && gates_node_preferred_size(t, b).h == 24);
    GT_ASSERT(gates_node_preferred_size(t, a).w == 21);
    GT_ASSERT(gates_node_preferred_size(t, box).h == base_box.h + 8);
    GT_ASSERT(gates_node_preferred_size(t, box).w > base_box.w); /* cols in the size's average width */
    GT_ASSERT(gates_node_preferred_size(t, radio).h > base_radio.h);
    /* Radio rows as assistive technology sees them match the painted rows. */
    gates_access_info_t info = {0};
    GT_ASSERT_OK(gates_access_info(t, radio, 2, &info));
    gates_rect_t row2 = info.bounds;
    GT_ASSERT_OK(gates_access_info(t, radio, 1, &info));
    GT_ASSERT(row2.y - info.bounds.y == info.bounds.h && 2 * info.bounds.h == gates_node_preferred_size(t, radio).h);
    /* A press on the bottom of the second radio row picks it (rows as painted). */
    gates_pointer_event_t pe = { .action = GATES_POINTER_DOWN, .button = GATES_BUTTON_LEFT,
                                 .pos = { row2.x + 4, row2.y + row2.h - 1 } };
    (void)gates_input_pointer(t, &pe);
    pe.action = GATES_POINTER_UP;
    (void)gates_input_pointer(t, &pe);
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(gates_options_selected(t, radio) == 2);
    /* A text box's character rects are the sized line's height. */
    gates_rect_t cr = {0};
    GT_ASSERT(gates_access_text_rects(t, box, 0, 1, &cr, 1) == 1 && cr.h == 24);
    /* A view's rows grow with the line. */
    GT_ASSERT_OK(gates_access_info(t, view, 1, &info));
    GT_ASSERT(info.bounds.h > base_row_h && info.bounds.h > 24);
    /* Paint hands the sized font to the backend. */
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, gates_theme_light(), be));
    int sized = 0, plain = 0, mono = 0;
    for (gates_u32 i = 0; i < gates_draw_list_len(&dl); i++) {
        const gates_draw_cmd_t *cmd = gates_draw_list_at(&dl, i);
        if (cmd->kind != GATES_DRAW_TEXT) continue;
        if (cmd->font == gates_font_sized(GATES_FONT_UI, 150)) sized++;
        else if (cmd->font == gates_font_sized(GATES_FONT_MONO, 150)) mono++;
        else if (cmd->font == GATES_FONT_UI) plain++;
    }
    GT_ASSERT(sized >= 3 && mono == 1 && plain == 1);
    gates_draw_list_deinit(&dl);
    /* A node made in a destroyed node's slot starts at the parent's size. */
    gates_node_t d = GATES_NODE_NULL, e = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_label_create(t, root, GATES_STR("x"), &d));
    GT_ASSERT_OK(gates_node_set_font_size(t, d, 200));
    GT_ASSERT_OK(gates_node_destroy(t, d));
    GT_ASSERT_OK(gates_tree_flush_destroys(t));
    GT_ASSERT_OK(gates_label_create(t, root, GATES_STR("x"), &e));
    GT_ASSERT(e.index == d.index && gates_node_font_size(t, e) == 100);
    gates_tree_destroy(t);
}

int main(void) {
    test_contract();
    test_offset_at_x();
    test_textbox();
    test_fonts();
    test_font_values();
    test_builtin_sized_draw();
    test_font_sizes();
    test_sized_editor_and_form();
    return gt_report("test_text_prop");
}
