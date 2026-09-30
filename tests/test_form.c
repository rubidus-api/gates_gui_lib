/* forms - the FORM layout (label column, stacked rows, hidden rows) and
 * the form helper (fields, help, errors, ids, rollback). */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/form.h>
#include "gates_test.h"
#include <proven/heap.h>

#include <string.h>

static const gates_text_backend_t *be;
static const gates_theme_t *theme;
#define VW 480
#define VH 400

static void layout_w(gates_tree_t *t, gates_i32 w) {
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ w, VH }, be));
}

static gates_tree_t *new_tree(gates_allocator_t alloc) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){ .allocator = alloc }, &t));
    GT_ASSERT_OK(gates_layout_set(t, gates_tree_root(t), GATES_LAYOUT_KIND_COLUMN));
    return t;
}

static bool str_is(gates_str_t s, const char *lit) {
    size_t n = strlen(lit);
    return s.size == n && (n == 0 || memcmp(s.ptr, lit, n) == 0);
}

static gates_i32 advance(void) {
    return be->metrics(be->ctx, 0).advance;
}

/* -- the FORM layout on hand-built rows ---------------------------------------- */

typedef struct rows_t {
    gates_node_t form, row[3], label[3], editor[3];
} rows_t;

static void build_rows(gates_tree_t *t, rows_t *r) {
    static const char *const labels[3] = { "Name", "E-mail address", "Age" };
    GT_ASSERT_OK(gates_panel_create(t, gates_tree_root(t), &r->form));
    GT_ASSERT_OK(gates_layout_set(t, r->form, GATES_LAYOUT_KIND_FORM));
    GT_ASSERT_OK(gates_layout_set_gap(t, r->form, 6));
    for (int i = 0; i < 3; i++) {
        GT_ASSERT_OK(gates_panel_create(t, r->form, &r->row[i]));
        GT_ASSERT_OK(gates_label_create(t, r->row[i],
                                        (gates_str_t){ .ptr = (const gates_u8 *)labels[i],
                                                       .size = strlen(labels[i]) },
                                        &r->label[i]));
        GT_ASSERT_OK(gates_textbox_create(t, r->row[i], GATES_STR(""), 10, &r->editor[i]));
        GT_ASSERT_OK(gates_layout_set_child_align(t, r->editor[i], GATES_ALIGN_START_V));
    }
}

static void test_form_layout(void) {
    gates_tree_t *t = new_tree((gates_allocator_t){0});
    rows_t r;
    build_rows(t, &r);
    layout_w(t, VW);
    gates_i32 label_w = gates_node_preferred_size(t, r.label[1]).w; /* the widest */
    gates_rect_t fr = gates_node_layout_rect(t, r.form);
    for (int i = 0; i < 3; i++) {
        gates_rect_t lr = gates_node_layout_rect(t, r.label[i]);
        gates_rect_t er = gates_node_layout_rect(t, r.editor[i]);
        GT_ASSERT(lr.x == fr.x);
        GT_ASSERT(er.x == fr.x + label_w + GATES_FORM_COLUMN_GAP); /* one label column */
        GT_ASSERT(er.w == gates_node_preferred_size(t, r.editor[i]).w);
        GT_ASSERT(lr.y >= er.y && lr.y + lr.h <= er.y + er.h); /* label beside its editor */
    }
    /* Rows stack with the gap and do not overlap. */
    gates_rect_t r0 = gates_node_layout_rect(t, r.row[0]);
    gates_rect_t r1 = gates_node_layout_rect(t, r.row[1]);
    GT_ASSERT(r1.y == r0.y + r0.h + 6);
    GT_ASSERT(gates_layout_validate(t, gates_tree_root(t)));
    gates_size_t pref = gates_node_preferred_size(t, r.form);
    GT_ASSERT(pref.w == label_w + GATES_FORM_COLUMN_GAP +
                            gates_node_preferred_size(t, r.editor[0]).w);
    GT_ASSERT(pref.h == 3 * gates_node_preferred_size(t, r.editor[0]).h + 2 * 6);

    /* Narrower than the label column plus 12 cells: labels go above editors. */
    gates_i32 threshold = label_w + GATES_FORM_COLUMN_GAP + GATES_FORM_MIN_EDITOR_CELLS * advance();
    layout_w(t, threshold);
    GT_ASSERT(gates_node_layout_rect(t, r.editor[0]).x > fr.x);         /* still beside */
    layout_w(t, threshold - 1);
    for (int i = 0; i < 3; i++) {
        gates_rect_t lr = gates_node_layout_rect(t, r.label[i]);
        gates_rect_t er = gates_node_layout_rect(t, r.editor[i]);
        GT_ASSERT(er.x == lr.x);
        GT_ASSERT(er.y == lr.y + lr.h + GATES_FORM_STACK_GAP);
    }
    r0 = gates_node_layout_rect(t, r.row[0]);
    r1 = gates_node_layout_rect(t, r.row[1]);
    GT_ASSERT(r1.y == r0.y + r0.h + 6);
    GT_ASSERT(gates_layout_validate(t, gates_tree_root(t)));
    /* Stacked, the form is as tall as its stacked rows (0.8.0): what follows
     * it starts below the last row, not over it. */
    gates_i32 lh = gates_node_preferred_size(t, r.label[0]).h, eh = gates_node_preferred_size(t, r.editor[0]).h;
    gates_i32 stacked_h = 3 * (lh + GATES_FORM_STACK_GAP + eh) + 2 * 6;
    GT_ASSERT(gates_node_preferred_size(t, r.form).h == stacked_h);
    GT_ASSERT(gates_node_layout_rect(t, r.form).h == stacked_h);
    gates_rect_t last = gates_node_layout_rect(t, r.editor[2]);
    GT_ASSERT(last.y + last.h == gates_node_layout_rect(t, r.form).y + stacked_h);
    gates_node_t after;
    GT_ASSERT_OK(gates_label_create(t, gates_tree_root(t), GATES_STR("after"), &after));
    layout_w(t, threshold - 1);
    GT_ASSERT(gates_node_layout_rect(t, after).y >= last.y + last.h);
    layout_w(t, threshold - 5); /* stacked at both widths: no second pass needed, same height */
    GT_ASSERT(gates_node_layout_rect(t, r.form).h == stacked_h);
    layout_w(t, VW); /* wide again: side by side, the side-by-side height */
    GT_ASSERT(gates_node_layout_rect(t, r.form).h == pref.h);
    GT_ASSERT(gates_node_layout_rect(t, after).y < last.y);
    GT_ASSERT_OK(gates_node_destroy(t, after));
    (void)gates_tree_flush_destroys(t);

    /* A hidden row takes no space; the next row moves up. */
    layout_w(t, VW);
    gates_rect_t row1_was = gates_node_layout_rect(t, r.row[1]);
    GT_ASSERT_OK(gates_node_set_hidden(t, r.row[1], true));
    layout_w(t, VW);
    GT_ASSERT(gates_node_layout_rect(t, r.row[2]).y == row1_was.y);
    GT_ASSERT(gates_node_preferred_size(t, r.form).h == pref.h - row1_was.h - 6);
    /* ...and the label column no longer counts its label. */
    GT_ASSERT(gates_node_layout_rect(t, r.editor[0]).x ==
              fr.x + gates_node_preferred_size(t, r.label[0]).w + GATES_FORM_COLUMN_GAP);
    gates_tree_destroy(t);
}

/* -- the form helper -------------------------------------------------------------- */

static const gates_option_t langs[] = {
    { .id = 1, .label = GATES_STR_INIT("English") },
    { .id = 2, .label = GATES_STR_INIT("Korean") },
};

enum { F_NAME = 1, F_MAIL, F_LANG, F_THEME, F_NEWS };

typedef struct form_app_t {
    gates_tree_t *t;
    gates_node_t form, name, mail, lang, theme, news;
} form_app_t;

static gates_err_t build_form(form_app_t *a) {
    gates_err_t err = gates_form_create(a->t, gates_tree_root(a->t), &a->form);
    gates_field_desc_t d = { .label = GATES_STR("Name"), .required = true,
                             .text = GATES_STR("Kim"), .max_bytes = 20 };
    if (gates_is_ok(err)) err = gates_form_add_text(a->t, a->form, F_NAME, &d, &a->name);
    d = (gates_field_desc_t){ .label = GATES_STR("E-mail"), .help = GATES_STR("used for receipts") };
    if (gates_is_ok(err)) err = gates_form_add_text(a->t, a->form, F_MAIL, &d, &a->mail);
    d = (gates_field_desc_t){ .label = GATES_STR("Language"), .options = langs, .option_count = 2,
                              .selected_id = 2 };
    if (gates_is_ok(err)) err = gates_form_add_choice(a->t, a->form, F_LANG, &d, &a->lang);
    if (gates_is_ok(err)) err = gates_form_add_radio(a->t, a->form, F_THEME, &d, &a->theme);
    d = (gates_field_desc_t){ .label = GATES_STR("News"), .text = GATES_STR("send me news"),
                              .checked = true };
    if (gates_is_ok(err)) err = gates_form_add_checkbox(a->t, a->form, F_NEWS, &d, &a->news);
    return err;
}

/* The label and help/error lines of a field, found through the row. */
static gates_node_t row_label(const gates_tree_t *t, gates_node_t form, gates_u32 id) {
    return gates_node_first_child(t, gates_form_row(t, form, id));
}

static gates_node_t cell_line(const gates_tree_t *t, gates_node_t form, gates_u32 id, int k) {
    gates_node_t n = gates_node_first_child(t, gates_node_last_child(t, gates_form_row(t, form, id)));
    for (int i = 0; i < k; i++) n = gates_node_next_sibling(t, n);
    return n;
}

static bool drawn_text_color(const gates_draw_list_t *dl, gates_rect_t r, gates_color_token_t tok) {
    gates_color_t want = gates_theme_color(theme, tok);
    for (gates_u32 i = 0; i < gates_draw_list_len(dl); i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(dl, i);
        if (c->kind == GATES_DRAW_TEXT && c->rect.x == r.x && c->rect.y == r.y &&
            c->color.r == want.r && c->color.g == want.g && c->color.b == want.b) {
            return true;
        }
    }
    return false;
}

static void test_form_helper(void) {
    form_app_t a = { .t = new_tree((gates_allocator_t){0}) };
    gates_tree_t *t = a.t;
    GT_ASSERT_OK(build_form(&a));
    layout_w(t, VW);
    GT_ASSERT(gates_node_kind(t, a.form) == GATES_NODE_FORM);
    GT_ASSERT(gates_form_field_count(t, a.form) == 5);

    /* Editors, labels, required marker, help line. */
    GT_ASSERT(gates_node_kind(t, a.name) == GATES_NODE_TEXTBOX);
    GT_ASSERT(str_is(gates_textbox_text(t, a.name), "Kim"));
    GT_ASSERT(gates_textbox_max_bytes(t, a.name) == 20);
    GT_ASSERT(str_is(gates_widget_text(t, row_label(t, a.form, F_NAME)), "Name *"));
    GT_ASSERT(str_is(gates_widget_text(t, row_label(t, a.form, F_MAIL)), "E-mail"));
    GT_ASSERT(str_is(gates_widget_text(t, cell_line(t, a.form, F_MAIL, 1)), "used for receipts"));
    GT_ASSERT(gates_options_selected(t, a.lang) == 2 && gates_node_kind(t, a.lang) == GATES_NODE_CHOICE);
    GT_ASSERT(gates_node_kind(t, a.theme) == GATES_NODE_RADIO);
    GT_ASSERT(gates_checkbox_checked(t, a.news));
    GT_ASSERT(str_is(gates_widget_text(t, a.news), "send me news"));

    /* Lookup by id and back. */
    GT_ASSERT(gates_node_eq(gates_form_editor(t, a.form, F_MAIL), a.mail));
    GT_ASSERT(gates_form_field_of(t, a.form, a.lang) == F_LANG);
    GT_ASSERT(gates_form_field_of(t, a.form, a.form) == 0);
    GT_ASSERT(gates_node_eq(gates_form_editor(t, a.form, 99), GATES_NODE_NULL));

    /* All editors sit in one column beside the labels. */
    gates_i32 ex = gates_node_layout_rect(t, a.name).x;
    GT_ASSERT(gates_node_layout_rect(t, a.mail).x == ex && gates_node_layout_rect(t, a.lang).x == ex);
    GT_ASSERT(ex > gates_node_layout_rect(t, row_label(t, a.form, F_MAIL)).x);
    GT_ASSERT(gates_layout_validate(t, gates_tree_root(t)));
    /* A radio group's label sits by its first option, not its middle. */
    gates_i32 dy = gates_node_layout_rect(t, row_label(t, a.form, F_THEME)).y -
                   gates_node_layout_rect(t, a.theme).y;
    GT_ASSERT(dy >= 0 && dy <= 4);

    /* Errors: a line under the editor in the error colour; a text editor turns invalid. */
    gates_node_t mail_err = cell_line(t, a.form, F_MAIL, 2);
    GT_ASSERT(gates_node_hidden(t, mail_err));
    gates_i32 mail_row_h = gates_node_layout_rect(t, gates_form_row(t, a.form, F_MAIL)).h;
    GT_ASSERT_OK(gates_form_set_error(t, a.form, F_MAIL, GATES_STR("not an address")));
    GT_ASSERT(!gates_node_hidden(t, mail_err));
    GT_ASSERT(str_is(gates_widget_text(t, mail_err), "not an address"));
    GT_ASSERT(gates_textbox_invalid(t, a.mail));
    layout_w(t, VW);
    GT_ASSERT(gates_node_layout_rect(t, gates_form_row(t, a.form, F_MAIL)).h > mail_row_h);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    GT_ASSERT(drawn_text_color(&dl, gates_node_layout_rect(t, mail_err), GATES_COLOR_ERROR));
    gates_draw_list_deinit(&dl);
    GT_ASSERT_OK(gates_form_set_error(t, a.form, F_MAIL, GATES_STR("")));
    GT_ASSERT(gates_node_hidden(t, mail_err) && !gates_textbox_invalid(t, a.mail));
    GT_ASSERT_OK(gates_form_set_error(t, a.form, F_NEWS, GATES_STR("required")));
    GT_ASSERT(!gates_node_hidden(t, cell_line(t, a.form, F_NEWS, 1)));
    GT_ASSERT(gates_form_set_error(t, a.form, 99, GATES_STR("x")) == PROVEN_ERR_INVALID_ARG);

    /* Ids: nonzero and unique. */
    gates_field_desc_t d = { .label = GATES_STR("again") };
    gates_node_t e = GATES_NODE_NULL;
    gates_u32 live = gates_tree_live_count(t);
    GT_ASSERT(gates_form_add_text(t, a.form, F_NAME, &d, &e) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_form_add_text(t, a.form, 0, &d, &e) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_form_add_text(t, a.name, 7, &d, &e) == PROVEN_ERR_INVALID_ARG); /* not a form */
    /* Text longer than its maximum: refused, nothing left behind. */
    d = (gates_field_desc_t){ .label = GATES_STR("short"), .text = GATES_STR("abcdef"), .max_bytes = 3 };
    GT_ASSERT(gates_form_add_text(t, a.form, 7, &d, &e) == PROVEN_ERR_OUT_OF_BOUNDS);
    GT_ASSERT_OK(gates_tree_flush_destroys(t));
    GT_ASSERT(gates_tree_live_count(t) == live);
    GT_ASSERT(gates_form_field_count(t, a.form) == 5);

    /* Read-only: a read-only box, other editors disabled. */
    d = (gates_field_desc_t){ .label = GATES_STR("Id"), .text = GATES_STR("42"), .read_only = true };
    gates_node_t id_box, id_lang;
    GT_ASSERT_OK(gates_form_add_text(t, a.form, 8, &d, &id_box));
    GT_ASSERT(gates_textbox_read_only(t, id_box));
    d = (gates_field_desc_t){ .label = GATES_STR("Fixed"), .options = langs, .option_count = 2,
                              .read_only = true };
    GT_ASSERT_OK(gates_form_add_choice(t, a.form, 9, &d, &id_lang));
    GT_ASSERT(gates_widget_disabled(t, id_lang));

    /* A hidden row: its editor leaves the Tab order. */
    GT_ASSERT_OK(gates_form_set_row_hidden(t, a.form, F_MAIL, true));
    GT_ASSERT(gates_node_hidden(t, gates_form_row(t, a.form, F_MAIL)));
    gates_tree_set_focus(t, a.name);
    GT_ASSERT(gates_tree_focus_next(t, false));
    GT_ASSERT(gates_node_eq(gates_tree_focus(t), a.lang));
    GT_ASSERT_OK(gates_form_set_row_hidden(t, a.form, F_MAIL, false));
    GT_ASSERT(gates_form_set_row_hidden(t, a.form, 99, true) == PROVEN_ERR_INVALID_ARG);

    /* A destroyed row frees its id. */
    GT_ASSERT_OK(gates_node_destroy(t, gates_form_row(t, a.form, 8)));
    GT_ASSERT(gates_node_eq(gates_form_editor(t, a.form, 8), GATES_NODE_NULL));
    d = (gates_field_desc_t){ .label = GATES_STR("Id again") };
    GT_ASSERT_OK(gates_form_add_text(t, a.form, 8, &d, nullptr));
    GT_ASSERT_OK(gates_tree_flush_destroys(t));
    gates_tree_destroy(t);
}

/* -- allocation failure at every step of building a row ---------------------------- */

typedef struct count_alloc_t {
    gates_allocator_t inner;
    int left;                    /* allocations that still succeed; < 0 = unlimited */
} count_alloc_t;

static proven_result_mem_mut_t ca_alloc(void *ctx, proven_size_t size, proven_size_t align) {
    count_alloc_t *c = ctx;
    if (c->left == 0) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    if (c->left > 0) c->left--;
    return c->inner.alloc_fn(c->inner.ctx, size, align);
}

static proven_result_mem_mut_t ca_realloc(void *ctx, void *p, proven_size_t os, proven_size_t ns,
                                          proven_size_t align) {
    count_alloc_t *c = ctx;
    if (c->left == 0) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    if (c->left > 0) c->left--;
    return c->inner.realloc_fn(c->inner.ctx, p, os, ns, align);
}

static void ca_free(void *ctx, void *p) {
    count_alloc_t *c = ctx;
    c->inner.free_fn(c->inner.ctx, p);
}

static void test_rollback(void) {
    count_alloc_t ca = { .inner = proven_heap_allocator(), .left = -1 };
    gates_allocator_t alloc = { .ctx = &ca, .alloc_fn = ca_alloc, .realloc_fn = ca_realloc,
                                .free_fn = ca_free };
    form_app_t a = { .t = new_tree(alloc) };
    GT_ASSERT_OK(build_form(&a));
    gates_tree_t *t = a.t;
    GT_ASSERT_OK(gates_tree_flush_destroys(t));
    gates_u32 live = gates_tree_live_count(t);
    bool done = false;
    int failures = 0;
    for (int k = 0; k < 64 && !done; k++) {
        gates_field_desc_t d = { .label = GATES_STR("Phone"), .required = true,
                                 .help = GATES_STR("digits only"), .text = GATES_STR("010") };
        ca.left = k;
        gates_node_t e = GATES_NODE_NULL;
        gates_err_t err = gates_form_add_text(t, a.form, 20 + (gates_u32)k, &d, &e);
        ca.left = -1;
        GT_ASSERT_OK(gates_tree_flush_destroys(t));
        if (gates_is_ok(err)) {
            done = true;
            GT_ASSERT(gates_form_field_count(t, a.form) == 6);
            GT_ASSERT(gates_tree_live_count(t) > live);
        } else {
            failures++;
            GT_ASSERT(err == PROVEN_ERR_NOMEM);
            GT_ASSERT(gates_form_field_count(t, a.form) == 5);   /* no half-built row */
            GT_ASSERT(gates_tree_live_count(t) == live);
            GT_ASSERT(gates_node_child_count(t, a.form) == 5);
        }
    }
    GT_ASSERT(done && failures > 3);
    gates_tree_destroy(t);
}

int main(void) {
    be = gates_text_backend_builtin();
    theme = gates_theme_light();
    test_form_layout();
    test_form_helper();
    test_rollback();
    return gt_report("test_form");
}
