/* T053: data controls (plan-0021 stage 1) - cell kinds (check, progress, icon
 * and text), custom cell painting, in-place editing through the model's
 * set_cell (F2, double click, Enter, Escape, focus leaving), check toggles,
 * CELL_EDITED, refusals, and edits across scrolling and model changes. */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/view.h>
#include <gates/image.h>
#include <gates/access.h>
#include <gates/overlay.h>
#include <gates/state.h>
#include <gates/command.h>
#include <gates/propgrid.h>
#include <gates/inputs.h>
#include <gates/timer.h>
#include <gates/clipboard.h>
#include "gates_test.h"
#include <proven/heap.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const gates_text_backend_t *be;
static const gates_theme_t *theme;
#define VW 640
#define VH 300

enum { C_NAME = 1, C_DONE, C_PROG, C_FILE, C_NOTE, C_OWN };

/* -- the model: rows 1..n, each with a name, a done mark, progress, an icon, a file -- */

typedef struct row_t {
    char name[32];
    bool done;
    gates_u32 pm;
    char file[32];
} row_t;

typedef struct model_t {
    row_t rows[64];
    gates_item_id_t ids[64];
    gates_u64 n;
    gates_image_id_t icon;
    int sets;
    gates_err_t refuse;          /* set_cell answers this when not OK */
    gates_item_id_t last_id;
    gates_column_id_t last_col;
    char last_text[32];
    bool last_checked;
    bool remove_on_set;          /* set_cell also removes the last row (the model may change) */
} model_t;

static row_t *row_of(model_t *m, gates_item_id_t id) {
    for (gates_u64 i = 0; i < m->n; i++) {
        if (m->ids[i] == id) return &m->rows[i];
    }
    return nullptr;
}

static gates_u64 m_count(void *u) { return ((model_t *)u)->n; }
static gates_item_id_t m_id_at(void *u, gates_u64 row) {
    model_t *m = u;
    return row < m->n ? m->ids[row] : 0;
}
static bool m_index_of(void *u, gates_item_id_t id, gates_u64 *row) {
    model_t *m = u;
    for (gates_u64 i = 0; i < m->n; i++) {
        if (m->ids[i] == id) { *row = i; return true; }
    }
    return false;
}

static gates_err_t m_cell(void *u, gates_item_id_t id, gates_column_id_t col, gates_cell_t *out) {
    model_t *m = u;
    row_t *r = row_of(m, id);
    if (r == nullptr) return PROVEN_ERR_INVALID_ARG;
    switch (col) {
    case C_NAME: out->text = (gates_str_t){ (const gates_u8 *)r->name, strlen(r->name) }; break;
    case C_DONE:
        out->checked = r->done;
        if (r->done) out->text = GATES_STR("yes");
        break;
    case C_PROG: out->permille = r->pm; out->text = GATES_STR("pct"); break;
    case C_FILE:
        out->icon = m->icon;
        out->text = (gates_str_t){ (const gates_u8 *)r->file, strlen(r->file) };
        break;
    default: out->text = GATES_STR("note"); break;
    }
    return GATES_OK;
}

static gates_err_t m_set_cell(void *u, gates_item_id_t id, gates_column_id_t col, const gates_cell_t *v) {
    model_t *m = u;
    m->sets++;
    m->last_id = id;
    m->last_col = col;
    m->last_checked = v->checked;
    size_t n = v->text.size < sizeof m->last_text - 1 ? v->text.size : sizeof m->last_text - 1;
    memcpy(m->last_text, v->text.ptr != nullptr ? (const char *)v->text.ptr : "", n);
    m->last_text[n] = 0;
    if (!gates_is_ok(m->refuse)) return m->refuse;
    row_t *r = row_of(m, id);
    if (r == nullptr) return PROVEN_ERR_INVALID_ARG;
    if (col == C_NAME) memcpy(r->name, m->last_text, n + 1);
    if (col == C_FILE) memcpy(r->file, m->last_text, n + 1);
    if (col == C_DONE) r->done = v->checked;
    if (m->remove_on_set && m->n > 0) m->n--;
    return GATES_OK;
}

static void fill(model_t *m, gates_u64 n) {
    memset(m, 0, sizeof *m);
    m->n = n;
    for (gates_u64 i = 0; i < n; i++) {
        m->ids[i] = 100 + i;
        snprintf(m->rows[i].name, sizeof m->rows[i].name, "row%llu", (unsigned long long)i);
        snprintf(m->rows[i].file, sizeof m->rows[i].file, "f%llu.txt", (unsigned long long)i);
        m->rows[i].done = (i % 2) == 1;
        m->rows[i].pm = (gates_u32)(i * 250);
    }
}

static gates_rows_model_t bind(model_t *m, bool writable) {
    return (gates_rows_model_t){ .user = m, .count = m_count, .id_at = m_id_at, .index_of = m_index_of,
                                 .cell = m_cell, .set_cell = writable ? m_set_cell : nullptr };
}

/* -- a custom painted column --------------------------------------------------------------- */

typedef struct own_t {
    int calls;
    gates_item_id_t ids[64];
    gates_rect_t rects[64];
    bool selected[64];
    gates_err_t fail;
} own_t;

static gates_err_t own_paint(void *user, const gates_cell_paint_t *p) {
    own_t *o = user;
    if (o->calls < 64) {
        o->ids[o->calls] = p->id;
        o->rects[o->calls] = p->rect;
        o->selected[o->calls] = p->selected;
    }
    o->calls++;
    if (!gates_is_ok(o->fail)) return o->fail;
    return gates_draw_rect(p->dl, (gates_rect_t){ p->rect.x + 1, p->rect.y + 1, 5, 5 }, GATES_RGBA(1, 2, 3, 255));
}

/* -- the app ----------------------------------------------------------------------------- */

typedef struct rec_t {
    gates_event_kind_t kind[32];
    gates_u64 item[32];
    gates_u32 result[32];
    int n;
} rec_t;

static void record(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    rec_t *r = user;
    if (r->n < 32) {
        r->kind[r->n] = ev->kind;
        r->item[r->n] = ev->item;
        r->result[r->n] = ev->result;
        r->n++;
    }
}

static int count_kind(const rec_t *r, gates_event_kind_t k) {
    int n = 0;
    for (int i = 0; i < r->n; i++) n += r->kind[i] == k;
    return n;
}

typedef struct app_t {
    gates_tree_t *t;
    gates_node_t view, after;
    model_t m;
    own_t own;
    rec_t rec, bubbled;
} app_t;

static void bubble(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    record(tree, ev, user);
}

static void layout(gates_tree_t *t) {
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ VW, VH }, be));
}

static gates_column_desc_t cols[6];

static void make_cols(own_t *own) {
    gates_column_desc_t c[6] = {
        { .id = C_NAME, .label = GATES_STR_INIT("Name"), .width = 100, .editable = true },
        { .id = C_DONE, .label = GATES_STR_INIT("Done"), .width = 60, .kind = GATES_CELL_CHECK, .editable = true },
        { .id = C_PROG, .label = GATES_STR_INIT("Progress"), .width = 120, .kind = GATES_CELL_PROGRESS },
        { .id = C_FILE, .label = GATES_STR_INIT("File"), .width = 120, .kind = GATES_CELL_ICON_TEXT, .editable = true },
        { .id = C_NOTE, .label = GATES_STR_INIT("Note"), .width = 80 },
        { .id = C_OWN, .label = GATES_STR_INIT("Own"), .width = 60, .paint = own_paint, .paint_user = own },
    };
    memcpy(cols, c, sizeof c);
}

static void make_app(app_t *a, gates_u64 rows, bool writable) {
    memset(a, 0, sizeof *a);
    make_cols(&a->own);
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &a->t));
    gates_node_t root = gates_tree_root(a->t);
    GT_ASSERT_OK(gates_layout_set(a->t, root, GATES_LAYOUT_KIND_COLUMN));
    gates_view_desc_t d = { .columns = cols, .column_count = 6, .header = true };
    GT_ASSERT_OK(gates_view_create(a->t, root, &d, &a->view));
    GT_ASSERT_OK(gates_layout_set_child_grow(a->t, a->view, 1));
    GT_ASSERT_OK(gates_button_create(a->t, root, GATES_STR("after"), nullptr, nullptr, &a->after));
    GT_ASSERT_OK(gates_widget_set_handler(a->t, a->view, record, &a->rec));
    GT_ASSERT_OK(gates_node_set_bubble_handler(a->t, root, bubble, &a->bubbled));
    fill(&a->m, rows);
    gates_rows_model_t mb = bind(&a->m, writable);
    GT_ASSERT_OK(gates_view_set_model(a->t, a->view, &mb));
    layout(a->t);
}

static void free_app(app_t *a) {
    gates_tree_destroy(a->t);
}

static bool key(gates_tree_t *t, gates_key_t k) {
    gates_key_event_t e = { .key = k, .down = true };
    bool r = gates_input_key(t, &e);
    e.down = false;
    (void)gates_input_key(t, &e);
    return r;
}

static void type(gates_tree_t *t, const char *s) {
    for (; *s; s++) (void)gates_input_char(t, (gates_u32)(unsigned char)*s);
}

static void pointer(gates_tree_t *t, gates_pointer_action_t act, gates_point_t p, gates_u32 clicks) {
    gates_pointer_event_t e = { .action = act, .button = GATES_BUTTON_LEFT, .pos = p, .clicks = clicks };
    (void)gates_input_pointer(t, &e);
}

static void click(gates_tree_t *t, gates_point_t p, gates_u32 clicks) {
    pointer(t, GATES_POINTER_DOWN, p, clicks);
    pointer(t, GATES_POINTER_UP, p, clicks);
}

static void dispatch(app_t *a) {
    (void)gates_tree_dispatch_events(a->t, 0);
}

/* The rect of row `row`'s cell in column position c (after layout). */
static gates_rect_t cell_rect(app_t *a, gates_u64 row, gates_u32 c) {
    gates_rect_t r = gates_view_part_rect(a->t, a->view, GATES_VIEW_PART_ROW, row);
    gates_rect_t h = gates_view_part_rect(a->t, a->view, GATES_VIEW_PART_HEADER, c);
    return (gates_rect_t){ h.x, r.y, h.w, r.h };
}

static gates_point_t at(gates_rect_t r, gates_i32 dx) {
    return (gates_point_t){ r.x + dx, r.y + r.h / 2 };
}

static bool focused(app_t *a, gates_node_t n) {
    return gates_node_eq(gates_tree_focus(a->t), n);
}

static bool str_is(gates_str_t s, const char *z) {
    return s.size == strlen(z) && (s.size == 0 || memcmp(s.ptr, z, s.size) == 0);
}

/* -- creation ---------------------------------------------------------------------------- */

static void test_creation(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t), v = GATES_NODE_NULL;
    gates_column_desc_t bad[] = { { .id = 1, .kind = GATES_CELL_PROGRESS, .editable = true } };
    GT_ASSERT(gates_view_create(t, root, &(gates_view_desc_t){ .columns = bad, .column_count = 1 }, &v) ==
              PROVEN_ERR_INVALID_ARG);
    bad[0] = (gates_column_desc_t){ .id = 1, .kind = (gates_cell_kind_t)9 };
    GT_ASSERT(gates_view_create(t, root, &(gates_view_desc_t){ .columns = bad, .column_count = 1 }, &v) ==
              PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_node_eq(v, GATES_NODE_NULL));
    /* No editable text column: no editor. A check column alone needs none. */
    gates_column_desc_t checks[] = { { .id = 1, .kind = GATES_CELL_CHECK, .editable = true },
                                     { .id = 2, .editable = false } };
    GT_ASSERT_OK(gates_view_create(t, root, &(gates_view_desc_t){ .columns = checks, .column_count = 2 }, &v));
    GT_ASSERT(gates_node_eq(gates_view_editor(t, v), GATES_NODE_NULL));
    GT_ASSERT(gates_node_child_count(t, v) == 0);
    GT_ASSERT(gates_view_edit(t, v, 1, 2) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(!gates_view_editing(t, v, nullptr, nullptr));
    GT_ASSERT_OK(gates_view_end_edit(t, v, true)); /* nothing open */
    /* An editable text column: one hidden text box child. */
    gates_column_desc_t text[] = { { .id = 1, .editable = true } };
    GT_ASSERT_OK(gates_view_create(t, root, &(gates_view_desc_t){ .columns = text, .column_count = 1 }, &v));
    gates_node_t ed = gates_view_editor(t, v);
    GT_ASSERT(gates_node_kind(t, ed) == GATES_NODE_TEXTBOX);
    GT_ASSERT(gates_node_hidden(t, ed));
    GT_ASSERT(gates_node_eq(gates_node_parent(t, ed), v));
    /* Not a view, or a list: nothing. */
    GT_ASSERT(gates_node_eq(gates_view_editor(t, root), GATES_NODE_NULL));
    GT_ASSERT(gates_view_end_edit(t, root, true) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_view_edit(t, root, 1, 1) == PROVEN_ERR_INVALID_ARG);
    gates_item_id_t id = 9;
    gates_column_id_t col = 9;
    GT_ASSERT(!gates_view_editing(t, root, &id, &col));
    GT_ASSERT(id == 0 && col == 0);
    gates_tree_destroy(t);
}

/* -- painting ----------------------------------------------------------------------------- */

static const gates_draw_cmd_t *find_rect(const gates_draw_list_t *dl, gates_draw_kind_t kind, gates_rect_t r) {
    for (gates_u32 i = 0; i < dl->len; i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(dl, i);
        if (c->kind == kind && c->rect.x == r.x && c->rect.y == r.y && c->rect.w == r.w && c->rect.h == r.h) return c;
    }
    return nullptr;
}

static const gates_draw_cmd_t *find_text(const gates_draw_list_t *dl, const char *s, gates_i32 min_y) {
    for (gates_u32 i = 0; i < dl->len; i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(dl, i);
        if (c->kind != GATES_DRAW_TEXT || c->rect.y < min_y) continue;
        gates_str_t t = gates_draw_cmd_text(dl, c);
        if (t.size == strlen(s) && memcmp(t.ptr, s, t.size) == 0) return c;
    }
    return nullptr;
}

static void test_paint_kinds(void) {
    app_t a;
    make_app(&a, 6, true);
    gates_u8 px[4 * 4] = {0};
    GT_ASSERT_OK(gates_image_add_rgba(a.t, 2, 2, px, 0, &a.m.icon));
    a.m.rows[1].pm = 500;
    a.m.rows[2].pm = 4000; /* shown full */
    GT_ASSERT_OK(gates_view_set_selected(a.t, a.view, 101));
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(a.t, &dl, theme, be));
    gates_color_t sel_bg = gates_theme_color(theme, GATES_COLOR_SELECTION_BG);

    /* Check: a box in every row, a mark only where done (rows 1, 3, 5). */
    for (gates_u64 r = 0; r < 6; r++) {
        gates_rect_t c = cell_rect(&a, r, 1);
        gates_rect_t box = { c.x + 4, c.y + (c.h - 12) / 2, 12, 12 };
        GT_ASSERT(find_rect(&dl, GATES_DRAW_BORDER, box) != nullptr);
        const gates_draw_cmd_t *mark = find_rect(&dl, GATES_DRAW_RECT, (gates_rect_t){ box.x + 3, box.y + 3, 6, 6 });
        GT_ASSERT((mark != nullptr) == ((r % 2) == 1));
        if (mark != nullptr) GT_ASSERT(mark->color.r == sel_bg.r && mark->color.b == sel_bg.b);
        const gates_draw_cmd_t *yes = find_text(&dl, "yes", c.y);
        if ((r % 2) == 1) GT_ASSERT(yes != nullptr && yes->rect.x == box.x + 12 + 6); /* any text after the box */
    }
    /* Progress: the bar, filled by permille, the text right of it. */
    gates_size_t tw = be->measure(be->ctx, 0, GATES_STR("pct"));
    for (gates_u64 r = 0; r < 3; r++) {
        gates_rect_t c = cell_rect(&a, r, 2);
        gates_i32 bw = c.w - 8 - (tw.w + 4);
        gates_rect_t bar = { c.x + 4, c.y + 6, bw, c.h - 12 };
        GT_ASSERT(find_rect(&dl, GATES_DRAW_BORDER, bar) != nullptr);
        gates_i32 fw = r == 0 ? 0 : r == 1 ? (bar.w - 2) / 2 : bar.w - 2;
        const gates_draw_cmd_t *f = find_rect(&dl, GATES_DRAW_RECT, (gates_rect_t){ bar.x + 1, bar.y + 1, fw, bar.h - 2 });
        GT_ASSERT((f != nullptr) == (fw > 0));
        const gates_draw_cmd_t *txt = find_text(&dl, "pct", c.y);
        GT_ASSERT(txt != nullptr && txt->rect.x == bar.x + bw + 4);
    }
    /* Icon and text: the icon at the cell's start, the text after it. */
    gates_rect_t fc = cell_rect(&a, 0, 3);
    const gates_draw_cmd_t *im = find_rect(&dl, GATES_DRAW_IMAGE, (gates_rect_t){ fc.x + 4, fc.y + (fc.h - 16) / 2, 16, 16 });
    GT_ASSERT(im != nullptr && im->image == gates_tree_image(a.t, a.m.icon));
    const gates_draw_cmd_t *ft = find_text(&dl, "f0.txt", fc.y);
    GT_ASSERT(ft != nullptr && ft->rect.x == fc.x + 4 + 16 + 4);
    /* Custom paint: once per painted row, the whole cell, the selected flag. */
    GT_ASSERT(a.own.calls == 6);
    for (int i = 0; i < 6; i++) {
        GT_ASSERT(a.own.ids[i] == (gates_item_id_t)(100 + i));
        gates_rect_t oc = cell_rect(&a, (gates_u64)i, 5);
        GT_ASSERT(a.own.rects[i].x == oc.x && a.own.rects[i].y == oc.y && a.own.rects[i].w == oc.w &&
                  a.own.rects[i].h == oc.h);
        GT_ASSERT(a.own.selected[i] == (i == 1));
        GT_ASSERT(find_rect(&dl, GATES_DRAW_RECT, (gates_rect_t){ oc.x + 1, oc.y + 1, 5, 5 }) != nullptr);
    }
    /* A paint error stops the frame. */
    gates_draw_list_reset(&dl);
    a.own.fail = PROVEN_ERR_OUT_OF_BOUNDS;
    GT_ASSERT(gates_paint_tree(a.t, &dl, theme, be) == PROVEN_ERR_OUT_OF_BOUNDS);
    /* No icon: no image, the text keeps its place. */
    a.own.fail = GATES_OK;
    GT_ASSERT_OK(gates_image_remove(a.t, a.m.icon));
    gates_draw_list_reset(&dl);
    GT_ASSERT_OK(gates_paint_tree(a.t, &dl, theme, be));
    GT_ASSERT(find_rect(&dl, GATES_DRAW_IMAGE, (gates_rect_t){ fc.x + 4, fc.y + (fc.h - 16) / 2, 16, 16 }) == nullptr);
    ft = find_text(&dl, "f0.txt", fc.y);
    GT_ASSERT(ft != nullptr && ft->rect.x == fc.x + 4 + 16 + 4);
    gates_draw_list_deinit(&dl);
    free_app(&a);
}

/* -- editing by keyboard ---------------------------------------------------------------- */

static void test_f2_enter_escape(void) {
    app_t a;
    make_app(&a, 6, true);
    gates_node_t ed = gates_view_editor(a.t, a.view);
    gates_tree_set_focus(a.t, a.view);
    /* F2 without a selection: nothing. */
    GT_ASSERT(!key(a.t, GATES_KEY_F2));
    GT_ASSERT(!gates_view_editing(a.t, a.view, nullptr, nullptr));
    GT_ASSERT(key(a.t, GATES_KEY_DOWN)); /* row 100 */
    GT_ASSERT(key(a.t, GATES_KEY_DOWN)); /* row 101 */
    GT_ASSERT(key(a.t, GATES_KEY_F2));
    gates_item_id_t id = 0;
    gates_column_id_t col = 0;
    GT_ASSERT(gates_view_editing(a.t, a.view, &id, &col));
    GT_ASSERT(id == 101 && col == C_NAME);
    GT_ASSERT(focused(&a, ed) && !gates_node_hidden(a.t, ed));
    GT_ASSERT(str_is(gates_textbox_text(a.t, ed), "row1"));
    gates_u32 b = 0, e = 0;
    GT_ASSERT_OK(gates_textbox_selection(a.t, ed, &b, &e));
    GT_ASSERT(b == 0 && e == 4); /* the model's text, selected */
    layout(a.t);
    gates_rect_t er = gates_node_layout_rect(a.t, ed), cr = cell_rect(&a, 1, 0);
    GT_ASSERT(er.x == cr.x && er.y == cr.y && er.w == cr.w && er.h == cr.h);
    GT_ASSERT(er.h >= GATES_ACCESS_MIN_TARGET);
    /* Named after its column for assistive technology. */
    gates_access_info_t info;
    GT_ASSERT_OK(gates_access_info(a.t, ed, 0, &info));
    GT_ASSERT(str_is(info.name, "Name"));
    /* Escape: no change, focus back on the view. */
    GT_ASSERT(key(a.t, GATES_KEY_ESCAPE));
    GT_ASSERT(!gates_view_editing(a.t, a.view, nullptr, nullptr));
    GT_ASSERT(focused(&a, a.view) && gates_node_hidden(a.t, ed));
    GT_ASSERT(a.m.sets == 0);
    /* Enter with nothing typed: closes, reports nothing. */
    a.rec.n = 0;
    GT_ASSERT(key(a.t, GATES_KEY_F2));
    GT_ASSERT(key(a.t, GATES_KEY_ENTER));
    GT_ASSERT(!gates_view_editing(a.t, a.view, nullptr, nullptr) && a.m.sets == 0);
    dispatch(&a);
    GT_ASSERT(count_kind(&a.rec, GATES_EVENT_CELL_EDITED) == 0);
    /* Typed text and Enter: set_cell, CELL_EDITED, the new text shown. */
    GT_ASSERT(key(a.t, GATES_KEY_F2));
    type(a.t, "alpha");
    GT_ASSERT(key(a.t, GATES_KEY_ENTER));
    GT_ASSERT(a.m.sets == 1 && a.m.last_id == 101 && a.m.last_col == C_NAME);
    GT_ASSERT(strcmp(a.m.last_text, "alpha") == 0 && strcmp(a.m.rows[1].name, "alpha") == 0);
    GT_ASSERT(focused(&a, a.view) && gates_node_hidden(a.t, ed));
    dispatch(&a);
    GT_ASSERT(count_kind(&a.rec, GATES_EVENT_CELL_EDITED) == 1);
    GT_ASSERT(a.rec.kind[a.rec.n - 1] == GATES_EVENT_CELL_EDITED);
    GT_ASSERT(a.rec.item[a.rec.n - 1] == 101 && a.rec.result[a.rec.n - 1] == C_NAME);
    /* The editor's own events never reach a bubble handler. */
    GT_ASSERT(a.bubbled.n == 0);
    /* Enter in the editor is not the view's activation. */
    GT_ASSERT(count_kind(&a.rec, GATES_EVENT_ACTIVATED) == 0);
    /* Shift+F2 and Space on a view without the key's column do nothing. */
    gates_key_event_t sf2 = { .key = GATES_KEY_F2, .down = true, .shift = true };
    GT_ASSERT(!gates_input_key(a.t, &sf2));
    free_app(&a);
}

static void test_refusal(void) {
    app_t a;
    make_app(&a, 6, true);
    gates_node_t ed = gates_view_editor(a.t, a.view);
    GT_ASSERT_OK(gates_view_edit(a.t, a.view, 102, C_NAME));
    GT_ASSERT(gates_view_selected(a.t, a.view) == 102);
    GT_ASSERT(focused(&a, ed));
    type(a.t, "bad");
    a.m.refuse = PROVEN_ERR_INVALID_ARG;
    GT_ASSERT(key(a.t, GATES_KEY_ENTER));
    /* Refused: still open, marked invalid, nothing reported. */
    GT_ASSERT(gates_view_editing(a.t, a.view, nullptr, nullptr));
    GT_ASSERT(focused(&a, ed) && gates_textbox_invalid(a.t, ed));
    GT_ASSERT(strcmp(a.m.rows[2].name, "row2") == 0);
    GT_ASSERT(gates_view_end_edit(a.t, a.view, true) == PROVEN_ERR_INVALID_ARG);
    /* The view does not scroll under a refused edit. */
    gates_pointer_event_t w = { .action = GATES_POINTER_WHEEL, .pos = at(cell_rect(&a, 4, 4), 10), .wheel = { 0, -1 } };
    gates_u64 first = gates_view_first_row(a.t, a.view);
    (void)gates_input_pointer(a.t, &w);
    GT_ASSERT(gates_view_first_row(a.t, a.view) == first);
    GT_ASSERT(gates_view_editing(a.t, a.view, nullptr, nullptr));
    /* Another cell cannot open either. */
    GT_ASSERT(gates_view_edit(a.t, a.view, 103, C_NAME) == PROVEN_ERR_INVALID_ARG);
    /* Focus leaving drops the refused text. */
    int before = a.m.sets;
    gates_tree_set_focus(a.t, a.after);
    GT_ASSERT(a.m.sets == before + 1);
    GT_ASSERT(!gates_view_editing(a.t, a.view, nullptr, nullptr));
    GT_ASSERT(focused(&a, a.after) && gates_node_hidden(a.t, ed));
    dispatch(&a);
    GT_ASSERT(count_kind(&a.rec, GATES_EVENT_CELL_EDITED) == 0);
    /* Escape after a refusal closes it; the next edit starts valid. */
    GT_ASSERT_OK(gates_view_edit(a.t, a.view, 102, C_NAME));
    type(a.t, "x");
    GT_ASSERT(key(a.t, GATES_KEY_ENTER));
    GT_ASSERT(gates_textbox_invalid(a.t, ed));
    GT_ASSERT(key(a.t, GATES_KEY_ESCAPE));
    GT_ASSERT(!gates_view_editing(a.t, a.view, nullptr, nullptr) && focused(&a, a.view));
    a.m.refuse = GATES_OK;
    GT_ASSERT_OK(gates_view_edit(a.t, a.view, 102, C_NAME));
    GT_ASSERT(!gates_textbox_invalid(a.t, ed));
    GT_ASSERT_OK(gates_view_end_edit(a.t, a.view, false));
    free_app(&a);
}

static void test_focus_leaving_commits(void) {
    app_t a;
    make_app(&a, 6, true);
    gates_node_t ed = gates_view_editor(a.t, a.view);
    GT_ASSERT_OK(gates_view_edit(a.t, a.view, 100, C_FILE));
    type(a.t, "new.bin");
    /* Tab moves on: the edit commits. */
    GT_ASSERT(key(a.t, GATES_KEY_TAB));
    GT_ASSERT(focused(&a, a.after));
    GT_ASSERT(strcmp(a.m.rows[0].file, "new.bin") == 0 && a.m.last_col == C_FILE);
    GT_ASSERT(gates_node_hidden(a.t, ed) && !gates_view_editing(a.t, a.view, nullptr, nullptr));
    dispatch(&a);
    GT_ASSERT(count_kind(&a.rec, GATES_EVENT_CELL_EDITED) == 1);
    /* Opening another edit commits the open one. */
    GT_ASSERT_OK(gates_view_edit(a.t, a.view, 101, C_NAME));
    type(a.t, "one");
    GT_ASSERT_OK(gates_view_edit(a.t, a.view, 102, C_FILE));
    GT_ASSERT(strcmp(a.m.rows[1].name, "one") == 0);
    gates_item_id_t id = 0;
    GT_ASSERT(gates_view_editing(a.t, a.view, &id, nullptr) && id == 102);
    GT_ASSERT(str_is(gates_textbox_text(a.t, ed), "f2.txt"));
    /* The icon column's editor starts after the icon. */
    layout(a.t);
    gates_rect_t er = gates_node_layout_rect(a.t, ed), cr = cell_rect(&a, 2, 3);
    GT_ASSERT(er.x == cr.x + 4 + 16 && er.w == cr.w - 4 - 16);
    /* A click on another row commits too (focus moves to the view). */
    type(a.t, "two");
    click(a.t, at(cell_rect(&a, 4, 4), 5), 1);
    GT_ASSERT(strcmp(a.m.rows[2].file, "two") == 0);
    GT_ASSERT(focused(&a, a.view) && gates_view_selected(a.t, a.view) == 104);
    /* Programmatic end: commit and cancel. */
    GT_ASSERT_OK(gates_view_edit(a.t, a.view, 103, C_NAME));
    type(a.t, "three");
    GT_ASSERT_OK(gates_view_end_edit(a.t, a.view, false));
    GT_ASSERT(strcmp(a.m.rows[3].name, "row3") == 0 && focused(&a, a.view));
    GT_ASSERT_OK(gates_view_edit(a.t, a.view, 103, C_NAME));
    type(a.t, "three");
    GT_ASSERT_OK(gates_view_end_edit(a.t, a.view, true));
    GT_ASSERT(strcmp(a.m.rows[3].name, "three") == 0);
    /* Invalid requests. */
    GT_ASSERT(gates_view_edit(a.t, a.view, 103, C_NOTE) == PROVEN_ERR_INVALID_ARG);  /* not editable */
    GT_ASSERT(gates_view_edit(a.t, a.view, 103, C_DONE) == PROVEN_ERR_INVALID_ARG);  /* a check column */
    GT_ASSERT(gates_view_edit(a.t, a.view, 103, 77) == PROVEN_ERR_INVALID_ARG);      /* no such column */
    GT_ASSERT(gates_view_edit(a.t, a.view, 999, C_NAME) == PROVEN_ERR_INVALID_ARG);  /* no such row */
    GT_ASSERT(gates_view_edit(a.t, a.view, 0, C_NAME) == PROVEN_ERR_INVALID_ARG);
    free_app(&a);
}

/* -- editing by pointer ------------------------------------------------------------------ */

static void test_double_click(void) {
    app_t a;
    make_app(&a, 6, true);
    gates_node_t ed = gates_view_editor(a.t, a.view);
    gates_rect_t name = cell_rect(&a, 2, 0), note = cell_rect(&a, 2, 4), file = cell_rect(&a, 3, 3);
    /* A double click on a row that was not selected only selects it. */
    click(a.t, at(name, 10), 1);
    click(a.t, at(name, 10), 2);
    GT_ASSERT(gates_view_editing(a.t, a.view, nullptr, nullptr));
    gates_column_id_t col = 0;
    GT_ASSERT(gates_view_editing(a.t, a.view, nullptr, &col) && col == C_NAME);
    GT_ASSERT(focused(&a, ed));
    GT_ASSERT(key(a.t, GATES_KEY_ESCAPE));
    /* On a cell that is not editable: the row activates. */
    a.rec.n = 0;
    click(a.t, at(note, 10), 2);
    GT_ASSERT(!gates_view_editing(a.t, a.view, nullptr, nullptr));
    dispatch(&a);
    GT_ASSERT(count_kind(&a.rec, GATES_EVENT_ACTIVATED) == 1);
    /* The first click of a pair on another row selects; the second edits there. */
    click(a.t, at(file, 30), 1);
    GT_ASSERT(!gates_view_editing(a.t, a.view, nullptr, nullptr));
    click(a.t, at(file, 30), 2);
    GT_ASSERT(gates_view_editing(a.t, a.view, nullptr, &col) && col == C_FILE);
    GT_ASSERT_OK(gates_view_end_edit(a.t, a.view, false));
    /* Read-only model: a double click activates, F2 does nothing. */
    free_app(&a);
    make_app(&a, 6, false);
    name = cell_rect(&a, 2, 0);
    click(a.t, at(name, 10), 1);
    click(a.t, at(name, 10), 2);
    GT_ASSERT(!gates_view_editing(a.t, a.view, nullptr, nullptr));
    GT_ASSERT(!key(a.t, GATES_KEY_F2));
    GT_ASSERT(gates_view_edit(a.t, a.view, 102, C_NAME) == PROVEN_ERR_INVALID_ARG);
    dispatch(&a);
    GT_ASSERT(count_kind(&a.rec, GATES_EVENT_ACTIVATED) == 1);
    free_app(&a);
}

static void test_checks(void) {
    app_t a;
    make_app(&a, 6, true);
    gates_rect_t c0 = cell_rect(&a, 0, 1);
    /* A click on the box (the row's height tall) toggles row 0 on. */
    click(a.t, at(c0, 4 + 6), 1);
    GT_ASSERT(a.m.rows[0].done && a.m.sets == 1 && a.m.last_checked && a.m.last_col == C_DONE);
    GT_ASSERT(gates_view_selected(a.t, a.view) == 100);
    click(a.t, (gates_point_t){ c0.x + 2, c0.y + 1 }, 1); /* the top edge of the row still counts */
    GT_ASSERT(!a.m.rows[0].done && a.m.sets == 2);
    /* A click right of the box selects only. */
    click(a.t, at(c0, 4 + 12 + 4 + 2), 1);
    GT_ASSERT(a.m.sets == 2);
    dispatch(&a);
    GT_ASSERT(count_kind(&a.rec, GATES_EVENT_CELL_EDITED) == 2);
    GT_ASSERT(a.rec.result[a.rec.n - 1] == C_DONE || a.rec.kind[a.rec.n - 1] != GATES_EVENT_CELL_EDITED);
    /* Space toggles the selected row's first editable check column. */
    gates_tree_set_focus(a.t, a.view);
    GT_ASSERT(key(a.t, GATES_KEY_DOWN)); /* row 101, done */
    a.rec.n = 0;
    GT_ASSERT(key(a.t, GATES_KEY_SPACE));
    GT_ASSERT(!a.m.rows[1].done && a.m.last_id == 101);
    dispatch(&a);
    GT_ASSERT(count_kind(&a.rec, GATES_EVENT_CELL_EDITED) == 1);
    for (int i = 0; i < a.rec.n; i++) {
        if (a.rec.kind[i] == GATES_EVENT_CELL_EDITED) GT_ASSERT(a.rec.item[i] == 101 && a.rec.result[i] == C_DONE);
    }
    /* A refused toggle changes and reports nothing. */
    a.m.refuse = PROVEN_ERR_INVALID_ARG;
    a.rec.n = 0;
    GT_ASSERT(key(a.t, GATES_KEY_SPACE));
    GT_ASSERT(!a.m.rows[1].done);
    dispatch(&a);
    GT_ASSERT(count_kind(&a.rec, GATES_EVENT_CELL_EDITED) == 0);
    a.m.refuse = GATES_OK;
    /* Read-only: Space is not taken, a click on the box only selects. */
    free_app(&a);
    make_app(&a, 6, false);
    gates_tree_set_focus(a.t, a.view);
    GT_ASSERT(key(a.t, GATES_KEY_DOWN));
    gates_key_event_t sp = { .key = GATES_KEY_SPACE, .down = true };
    GT_ASSERT(!gates_input_key(a.t, &sp));
    sp.down = false;
    (void)gates_input_key(a.t, &sp);
    click(a.t, at(cell_rect(&a, 0, 1), 10), 1);
    GT_ASSERT(a.m.sets == 0 && !a.m.rows[0].done);
    free_app(&a);
}

/* -- 0.8.0: type-ahead, the current column, copying a row ------------------------------------ */

static gates_u64 fake_now(void *ctx) { return *(gates_u64 *)ctx; }
static void fake_changed(void *ctx) { (void)ctx; }
static char clip_text[128];
static gates_err_t clip_fail;
static gates_err_t clip_set(void *ctx, gates_str_t t) {
    (void)ctx;
    if (!gates_is_ok(clip_fail)) return clip_fail;
    size_t n = t.size < sizeof clip_text - 1 ? t.size : sizeof clip_text - 1;
    memcpy(clip_text, t.ptr, n);
    clip_text[n] = 0;
    return GATES_OK;
}
static gates_err_t clip_get(void *ctx, gates_allocator_t a, gates_u8 **out, gates_usize_t *n) {
    (void)ctx;
    (void)a;
    *out = nullptr;
    *n = 0;
    return GATES_OK;
}

static void test_keyboard_extras(void) {
    app_t a;
    make_app(&a, 6, true);
    const char *names[6] = { "apple", "banana", "avocado", "Berry", "cherry", "apricot" };
    for (int i = 0; i < 6; i++) snprintf(a.m.rows[i].name, sizeof a.m.rows[i].name, "%s", names[i]);
    gates_u64 now = 5000;
    gates_tree_set_clock(a.t, fake_now, fake_changed, &now);
    gates_tree_set_clipboard(a.t, &(gates_clipboard_t){ .get_text = clip_get, .set_text = clip_set });
    gates_tree_set_focus(a.t, a.view);
    /* Type-ahead: the next row starting with the letters, any case for ASCII. */
    GT_ASSERT(gates_input_char(a.t, ' ') == GATES_INPUT_IGNORED); /* Space starts no search */
    type(a.t, "b");
    GT_ASSERT(gates_view_selected(a.t, a.view) == 101);
    type(a.t, "e");
    GT_ASSERT(gates_view_selected(a.t, a.view) == 103); /* "be": Berry */
    now += 1000; /* a pause starts over */
    type(a.t, "a");
    GT_ASSERT(gates_view_selected(a.t, a.view) == 105);
    type(a.t, "a"); /* the same letter again: the next one, around the end */
    GT_ASSERT(gates_view_selected(a.t, a.view) == 100);
    type(a.t, "a");
    GT_ASSERT(gates_view_selected(a.t, a.view) == 102);
    now += 999;
    type(a.t, "v"); /* "aaav" matches nothing: the selection stays */
    GT_ASSERT(gates_view_selected(a.t, a.view) == 102);
    GT_ASSERT(key(a.t, GATES_KEY_DOWN)); /* a move ends the search */
    type(a.t, "c");
    GT_ASSERT(gates_view_selected(a.t, a.view) == 104);
    GT_ASSERT(key(a.t, GATES_KEY_RIGHT));
    type(a.t, "ap");
    GT_ASSERT(gates_view_selected(a.t, a.view) == 105);
    /* The current column: Ctrl+Right/Left over the shown columns; F2 edits it. */
    gates_key_event_t cr = { .key = GATES_KEY_RIGHT, .down = true, .ctrl = true };
    gates_key_event_t cl = { .key = GATES_KEY_LEFT, .down = true, .ctrl = true };
    GT_ASSERT(gates_input_key(a.t, &cl)); /* none yet: from the last */
    GT_ASSERT(gates_input_key(a.t, &cl) && gates_input_key(a.t, &cl)); /* Note, File */
    GT_ASSERT(key(a.t, GATES_KEY_F2));
    gates_column_id_t col = 0;
    GT_ASSERT(gates_view_editing(a.t, a.view, nullptr, &col) && col == C_FILE);
    GT_ASSERT(key(a.t, GATES_KEY_ESCAPE));
    for (int i = 0; i < 8; i++) (void)gates_input_key(a.t, &cl); /* stops at the first */
    GT_ASSERT(gates_input_key(a.t, &cr)); /* Done */
    bool was = a.m.rows[5].done;
    GT_ASSERT(key(a.t, GATES_KEY_SPACE) && a.m.rows[5].done != was);
    GT_ASSERT(gates_input_key(a.t, &cr)); /* Progress: F2 falls back to the first text column */
    GT_ASSERT(key(a.t, GATES_KEY_F2));
    GT_ASSERT(gates_view_editing(a.t, a.view, nullptr, &col) && col == C_NAME);
    GT_ASSERT(key(a.t, GATES_KEY_ESCAPE));
    /* It is drawn inside the selected row's ring. */
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(a.t, &dl, theme, be));
    gates_rect_t pc = cell_rect(&a, 5, 2);
    gates_i32 fw = gates_theme_focus_width(theme);
    bool outlined = false;
    for (gates_u32 i = 0; i < gates_draw_list_len(&dl); i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(&dl, i);
        if (c->kind == GATES_DRAW_BORDER && c->rect.x == pc.x + fw && c->rect.y == pc.y + fw &&
            c->rect.w == pc.w - 2 * fw) {
            outlined = true;
        }
    }
    GT_ASSERT(outlined);
    gates_draw_list_deinit(&dl);
    for (int i = 0; i < 10; i++) (void)gates_input_key(a.t, &cr); /* stops at the last (Own) */
    GT_ASSERT(gates_input_key(a.t, &cl) && gates_input_key(a.t, &cl)); /* Note, File */
    GT_ASSERT(key(a.t, GATES_KEY_F2));
    GT_ASSERT(gates_view_editing(a.t, a.view, nullptr, &col) && col == C_FILE);
    GT_ASSERT(key(a.t, GATES_KEY_ESCAPE));
    for (int i = 0; i < 6; i++) (void)gates_input_key(a.t, &cl); /* back to Name */
    /* A press on a cell makes its column current. */
    click(a.t, at(cell_rect(&a, 4, 3), 30), 1);
    GT_ASSERT(key(a.t, GATES_KEY_F2));
    GT_ASSERT(gates_view_editing(a.t, a.view, nullptr, &col) && col == C_FILE);
    GT_ASSERT(key(a.t, GATES_KEY_ESCAPE));
    /* Ctrl+C: the selected row's shown cells, tab-separated. */
    gates_key_event_t cc = { .key = GATES_KEY_C, .down = true, .ctrl = true };
    GT_ASSERT(gates_input_key(a.t, &cc));
    GT_ASSERT(strcmp(clip_text, "cherry\t\tpct\tf4.txt\tnote\tnote") == 0);
    GT_ASSERT_OK(gates_view_set_column_hidden(a.t, a.view, C_PROG, true));
    GT_ASSERT_OK(gates_view_move_column(a.t, a.view, C_NOTE, 0));
    GT_ASSERT(gates_input_key(a.t, &cc));
    GT_ASSERT(strcmp(clip_text, "note\tcherry\t\tf4.txt\tnote") == 0);
    clip_fail = PROVEN_ERR_BUSY;
    GT_ASSERT(gates_input_key(a.t, &cc));
    GT_ASSERT(gates_input_take_error(a.t) == PROVEN_ERR_BUSY);
    clip_fail = GATES_OK;
    free_app(&a);
    /* Nothing selected: Ctrl+C is not the view's. */
    make_app(&a, 3, true);
    gates_tree_set_clipboard(a.t, &(gates_clipboard_t){ .get_text = clip_get, .set_text = clip_set });
    gates_tree_set_focus(a.t, a.view);
    GT_ASSERT(!gates_input_key(a.t, &cc));
    type(a.t, "r"); /* no clock: letters still search */
    GT_ASSERT(gates_view_selected(a.t, a.view) == 100);
    free_app(&a);
}

/* -- edits across scrolling and model changes ---------------------------------------------- */

static void test_scroll_and_model(void) {
    app_t a;
    make_app(&a, 40, true);
    gates_node_t ed = gates_view_editor(a.t, a.view);
    /* An edit on a row below the fold scrolls it into view. */
    GT_ASSERT_OK(gates_view_edit(a.t, a.view, 130, C_NAME));
    gates_u64 first = gates_view_first_row(a.t, a.view);
    GT_ASSERT(first <= 30 && 30 < first + gates_view_visible_rows(a.t, a.view));
    layout(a.t);
    gates_rect_t er = gates_node_layout_rect(a.t, ed);
    GT_ASSERT(er.y == cell_rect(&a, 30, 0).y);
    /* The wheel commits first, then scrolls. */
    type(a.t, "w");
    gates_pointer_event_t w = { .action = GATES_POINTER_WHEEL, .pos = at(cell_rect(&a, first, 4), 5), .wheel = { 0, 1 } };
    (void)gates_input_pointer(a.t, &w);
    GT_ASSERT(strcmp(a.m.rows[30].name, "w") == 0);
    GT_ASSERT(!gates_view_editing(a.t, a.view, nullptr, nullptr));
    GT_ASSERT(gates_view_first_row(a.t, a.view) < first);
    /* A model change that keeps the row keeps the edit, placed again. */
    GT_ASSERT_OK(gates_view_edit(a.t, a.view, 105, C_NAME));
    memmove(&a.m.ids[1], &a.m.ids[0], 39 * sizeof a.m.ids[0]);
    memmove(&a.m.rows[1], &a.m.rows[0], 39 * sizeof a.m.rows[0]);
    a.m.ids[0] = 500;
    snprintf(a.m.rows[0].name, sizeof a.m.rows[0].name, "top");
    GT_ASSERT_OK(gates_view_model_changed(a.t, a.view));
    GT_ASSERT(gates_view_editing(a.t, a.view, nullptr, nullptr));
    layout(a.t);
    gates_u64 row = 0;
    GT_ASSERT(m_index_of(&a.m, 105, &row) && row == 6);
    GT_ASSERT(gates_node_layout_rect(a.t, ed).y == cell_rect(&a, 6, 0).y);
    /* A change that removes the row cancels the edit. */
    a.m.ids[6] = 999;
    GT_ASSERT_OK(gates_view_model_changed(a.t, a.view));
    GT_ASSERT(!gates_view_editing(a.t, a.view, nullptr, nullptr));
    GT_ASSERT(gates_node_hidden(a.t, ed) && focused(&a, a.view));
    GT_ASSERT(a.m.sets == 1);
    /* set_cell may change the model: the view re-reads it. */
    a.m.remove_on_set = true;
    gates_u64 n = a.m.n;
    GT_ASSERT_OK(gates_view_edit(a.t, a.view, 101, C_NAME));
    type(a.t, "z");
    GT_ASSERT(key(a.t, GATES_KEY_ENTER));
    GT_ASSERT(a.m.n == n - 1);
    layout(a.t);
    gates_rect_t last = gates_view_part_rect(a.t, a.view, GATES_VIEW_PART_ROW, n - 1);
    GT_ASSERT(last.w == 0);
    a.m.remove_on_set = false;
    /* Binding another model, or disabling the view, cancels an open edit. */
    GT_ASSERT_OK(gates_view_edit(a.t, a.view, 102, C_NAME));
    type(a.t, "q");
    gates_rows_model_t mb = bind(&a.m, true);
    GT_ASSERT_OK(gates_view_set_model(a.t, a.view, &mb));
    GT_ASSERT(!gates_view_editing(a.t, a.view, nullptr, nullptr) && gates_node_hidden(a.t, ed));
    GT_ASSERT(focused(&a, a.view));
    GT_ASSERT_OK(gates_view_edit(a.t, a.view, 102, C_NAME));
    type(a.t, "q");
    int sets = a.m.sets;
    GT_ASSERT_OK(gates_widget_set_disabled(a.t, a.view, true));
    GT_ASSERT(!gates_view_editing(a.t, a.view, nullptr, nullptr) && a.m.sets == sets);
    GT_ASSERT_OK(gates_widget_set_disabled(a.t, a.view, false));
    /* An edit scrolls its column into view: left to an edge, or right until it fits. */
    gates_rect_t body = gates_view_part_rect(a.t, a.view, GATES_VIEW_PART_BODY, 0);
    GT_ASSERT_OK(gates_view_set_column_width(a.t, a.view, C_NAME, 700));
    layout(a.t);
    GT_ASSERT(gates_view_scroll_x(a.t, a.view) == 0);
    GT_ASSERT_OK(gates_view_edit(a.t, a.view, 102, C_FILE));
    layout(a.t);
    er = gates_node_layout_rect(a.t, ed);
    GT_ASSERT(gates_view_scroll_x(a.t, a.view) == 700 + 60 + 120 + 120 - body.w);
    GT_ASSERT(er.x + er.w == body.x + body.w && er.w == 120 - 4 - 16);
    GT_ASSERT_OK(gates_view_edit(a.t, a.view, 102, C_NAME)); /* wider than the view: its left edge */
    layout(a.t);
    er = gates_node_layout_rect(a.t, ed);
    GT_ASSERT(gates_view_scroll_x(a.t, a.view) == 0 && er.x == body.x && er.w == body.w);
    /* Scrolled away sideways by the program: the editor shrinks to nothing, never stale. */
    GT_ASSERT_OK(gates_view_edit(a.t, a.view, 102, C_FILE));
    GT_ASSERT_OK(gates_view_set_column_width(a.t, a.view, C_NAME, 2000));
    layout(a.t);
    er = gates_node_layout_rect(a.t, ed);
    GT_ASSERT(er.w == 0 || (er.x >= body.x && er.x + er.w <= body.x + body.w));
    GT_ASSERT_OK(gates_view_end_edit(a.t, a.view, false));
    free_app(&a);
}

/* -- allocation failures ------------------------------------------------------------------ */

typedef struct fail_alloc_t {
    gates_allocator_t inner;
    int left;                    /* allocations that succeed before failing; -1 = never fail */
} fail_alloc_t;

static proven_result_mem_mut_t fa_alloc(void *ctx, proven_size_t size, proven_size_t align) {
    fail_alloc_t *f = ctx;
    if (f->left == 0) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    if (f->left > 0) f->left--;
    return f->inner.alloc_fn(f->inner.ctx, size, align);
}
static proven_result_mem_mut_t fa_realloc(void *ctx, void *p, proven_size_t os, proven_size_t ns,
                                          proven_size_t align) {
    fail_alloc_t *f = ctx;
    if (f->left == 0) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    if (f->left > 0) f->left--;
    return f->inner.realloc_fn(f->inner.ctx, p, os, ns, align);
}
static void fa_free(void *ctx, void *p) {
    fail_alloc_t *f = ctx;
    f->inner.free_fn(f->inner.ctx, p);
}

static void test_create_failures(void) {
    make_cols(nullptr);
    bool succeeded = false;
    for (int k = 0; k < 64 && !succeeded; k++) {
        fail_alloc_t f = { .inner = proven_heap_allocator(), .left = -1 };
        gates_allocator_t al = { .ctx = &f, .alloc_fn = fa_alloc, .realloc_fn = fa_realloc, .free_fn = fa_free };
        gates_tree_t *t = nullptr;
        GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){ .allocator = al }, &t));
        gates_u32 before = gates_node_child_count(t, gates_tree_root(t));
        f.left = k;
        gates_node_t v = GATES_NODE_NULL;
        gates_err_t err = gates_view_create(t, gates_tree_root(t), &(gates_view_desc_t){ .columns = cols, .column_count = 6 }, &v);
        f.left = -1;
        if (gates_is_ok(err)) {
            succeeded = true;
            GT_ASSERT(!gates_node_eq(gates_view_editor(t, v), GATES_NODE_NULL));
        } else {
            GT_ASSERT(err == PROVEN_ERR_NOMEM);
            GT_ASSERT(gates_node_eq(v, GATES_NODE_NULL));
            GT_ASSERT(gates_node_child_count(t, gates_tree_root(t)) == before); /* nothing left */
        }
        gates_tree_destroy(t); /* ASan: nothing leaked either way */
    }
    GT_ASSERT(succeeded);
}

/* -- choosing and ordering columns (stage 2) ------------------------------------------------ */

static void test_hide_and_move(void) {
    app_t a;
    make_app(&a, 6, true);
    gates_rect_t h_prog = gates_view_part_rect(a.t, a.view, GATES_VIEW_PART_HEADER, 2);
    GT_ASSERT(gates_view_set_column_hidden(a.t, a.view, 77, true) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(!gates_view_column_hidden(a.t, a.view, C_DONE));
    /* Hiding Done: no header cell, the columns after it move left by its width. */
    GT_ASSERT_OK(gates_view_set_column_hidden(a.t, a.view, C_DONE, true));
    GT_ASSERT_OK(gates_view_set_column_hidden(a.t, a.view, C_DONE, true)); /* again: no change */
    GT_ASSERT(gates_view_column_hidden(a.t, a.view, C_DONE));
    layout(a.t);
    GT_ASSERT(gates_view_part_rect(a.t, a.view, GATES_VIEW_PART_HEADER, 1).w == 0);
    GT_ASSERT(gates_view_part_rect(a.t, a.view, GATES_VIEW_PART_HEADER, 2).x == h_prog.x - 60);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(a.t, &dl, theme, be));
    GT_ASSERT(find_text(&dl, "Done", 0) == nullptr && find_text(&dl, "yes", 0) == nullptr);
    GT_ASSERT(find_text(&dl, "Progress", 0) != nullptr);
    /* Assistive technology sees five columns; the second cell is now Progress's text. */
    gates_access_info_t info;
    GT_ASSERT_OK(gates_access_info(a.t, a.view, 0, &info));
    GT_ASSERT(info.column_count == 5);
    GT_ASSERT_OK(gates_access_info(a.t, a.view, 101, &info));
    GT_ASSERT(info.column_count == 5);
    GT_ASSERT(str_is(info.name, "row1, pct, f1.txt, note, note")); /* Done's "yes" is left out */
    /* A double click where Done was now lands on Progress (not editable): it activates. */
    gates_rect_t pc = cell_rect(&a, 1, 2);
    click(a.t, at(pc, 10), 1);
    click(a.t, at(pc, 10), 2);
    GT_ASSERT(!gates_view_editing(a.t, a.view, nullptr, nullptr));
    /* Hiding Progress: a double click on File's cell edits File, not the hidden column before it. */
    GT_ASSERT_OK(gates_view_set_column_hidden(a.t, a.view, C_PROG, true));
    layout(a.t);
    gates_rect_t fcell = cell_rect(&a, 1, 3);
    click(a.t, at(fcell, 30), 2);
    gates_column_id_t ecol = 0;
    GT_ASSERT(gates_view_editing(a.t, a.view, nullptr, &ecol) && ecol == C_FILE);
    GT_ASSERT_OK(gates_view_end_edit(a.t, a.view, false));
    GT_ASSERT_OK(gates_view_set_column_hidden(a.t, a.view, C_PROG, false));
    layout(a.t);
    /* Space finds no shown check column. */
    gates_tree_set_focus(a.t, a.view);
    gates_key_event_t sp = { .key = GATES_KEY_SPACE, .down = true };
    GT_ASSERT(!gates_input_key(a.t, &sp));
    sp.down = false;
    (void)gates_input_key(a.t, &sp);
    /* The last shown column stays. */
    GT_ASSERT_OK(gates_view_set_column_hidden(a.t, a.view, C_NAME, true));
    GT_ASSERT_OK(gates_view_set_column_hidden(a.t, a.view, C_PROG, true));
    GT_ASSERT_OK(gates_view_set_column_hidden(a.t, a.view, C_FILE, true));
    GT_ASSERT_OK(gates_view_set_column_hidden(a.t, a.view, C_NOTE, true));
    GT_ASSERT(gates_view_set_column_hidden(a.t, a.view, C_OWN, true) == PROVEN_ERR_INVALID_STATE);
    GT_ASSERT(!gates_view_column_hidden(a.t, a.view, C_OWN));
    GT_ASSERT(!key(a.t, GATES_KEY_F2)); /* no shown editable column */
    GT_ASSERT(gates_view_edit(a.t, a.view, 101, C_NAME) == PROVEN_ERR_INVALID_ARG); /* hidden */
    for (gates_column_id_t c = C_NAME; c <= C_NOTE; c++) GT_ASSERT_OK(gates_view_set_column_hidden(a.t, a.view, c, false));
    /* Hiding the column being edited cancels the edit, without asking the model. */
    GT_ASSERT_OK(gates_view_edit(a.t, a.view, 101, C_FILE));
    type(a.t, "gone");
    GT_ASSERT_OK(gates_view_set_column_hidden(a.t, a.view, C_FILE, true));
    GT_ASSERT(!gates_view_editing(a.t, a.view, nullptr, nullptr) && a.m.sets == 0);
    GT_ASSERT(focused(&a, a.view));
    GT_ASSERT_OK(gates_view_set_column_hidden(a.t, a.view, C_FILE, false));
    /* Moving: positions count every column; the others keep their order. */
    GT_ASSERT(gates_view_move_column(a.t, a.view, C_NAME, 6) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_view_move_column(a.t, a.view, 77, 0) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT_OK(gates_view_move_column(a.t, a.view, C_FILE, 0));
    GT_ASSERT_OK(gates_view_move_column(a.t, a.view, C_FILE, 0)); /* already there */
    GT_ASSERT_OK(gates_view_move_column(a.t, a.view, C_NAME, 5));
    gates_column_id_t want[6] = { C_FILE, C_DONE, C_PROG, C_NOTE, C_OWN, C_NAME };
    for (gates_u32 i = 0; i < 6; i++) GT_ASSERT(gates_view_column_at(a.t, a.view, i) == want[i]);
    GT_ASSERT(gates_view_column_at(a.t, a.view, 6) == 0);
    GT_ASSERT(gates_view_column_at(a.t, gates_tree_root(a.t), 0) == 0);
    layout(a.t);
    gates_rect_t h0 = gates_view_part_rect(a.t, a.view, GATES_VIEW_PART_HEADER, 0);
    gates_rect_t body = gates_view_part_rect(a.t, a.view, GATES_VIEW_PART_BODY, 0);
    GT_ASSERT(h0.x == body.x && h0.w == 120);
    /* F2 now edits File, the first editable text column in order; the editor follows a move. */
    gates_tree_set_focus(a.t, a.view);
    GT_ASSERT_OK(gates_view_set_selected(a.t, a.view, 100));
    GT_ASSERT(key(a.t, GATES_KEY_F2));
    gates_column_id_t col = 0;
    GT_ASSERT(gates_view_editing(a.t, a.view, nullptr, &col) && col == C_FILE);
    GT_ASSERT_OK(gates_view_move_column(a.t, a.view, C_FILE, 2));
    GT_ASSERT(gates_view_editing(a.t, a.view, nullptr, nullptr));
    layout(a.t);
    gates_rect_t er = gates_node_layout_rect(a.t, gates_view_editor(a.t, a.view));
    GT_ASSERT(er.x == cell_rect(&a, 0, 2).x + 4 + 16);
    GT_ASSERT_OK(gates_view_end_edit(a.t, a.view, false));
    /* A list has no columns to choose. */
    gates_node_t list;
    GT_ASSERT_OK(gates_view_create(a.t, gates_tree_root(a.t), &(gates_view_desc_t){0}, &list));
    GT_ASSERT(gates_view_set_column_hidden(a.t, list, 0, true) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_view_move_column(a.t, list, 0, 0) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_view_open_column_menu(a.t, a.view, (gates_point_t){ 0, 0 }, nullptr) == PROVEN_ERR_INVALID_ARG);
    gates_draw_list_deinit(&dl);
    free_app(&a);
}

/* Trees: the marks and indentation live in the first shown column. */
static gates_err_t t_row_info(void *u, gates_item_id_t id, gates_row_info_t *out) {
    (void)u;
    *out = (gates_row_info_t){ .depth = id == 101 ? 1u : 0u, .expandable = id == 100, .expanded = true,
                               .parent = id == 101 ? 100 : 0 };
    return GATES_OK;
}

static void test_tree_first_column(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    model_t m;
    fill(&m, 2);
    gates_column_desc_t tc[] = { { .id = C_NAME, .label = GATES_STR_INIT("Name"), .width = 100, .editable = true },
                                 { .id = C_NOTE, .label = GATES_STR_INIT("Note"), .width = 100, .editable = true } };
    gates_node_t v;
    GT_ASSERT_OK(gates_layout_set(t, gates_tree_root(t), GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_view_create(t, gates_tree_root(t), &(gates_view_desc_t){ .columns = tc, .column_count = 2, .header = true, .tree = true }, &v));
    GT_ASSERT_OK(gates_layout_set_child_grow(t, v, 1));
    gates_rows_model_t mb = bind(&m, true);
    mb.row_info = t_row_info;
    GT_ASSERT_OK(gates_view_set_model(t, v, &mb));
    GT_ASSERT_OK(gates_view_set_column_hidden(t, v, C_NAME, true));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ VW, VH }, be));
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    gates_rect_t r1 = gates_view_part_rect(t, v, GATES_VIEW_PART_ROW, 1);
    const gates_draw_cmd_t *note = find_text(&dl, "note", r1.y);
    GT_ASSERT(note != nullptr && note->rect.x == r1.x + 4 + GATES_VIEW_INDENT * 2); /* depth 1, now in Note */
    /* The editor on the tree column starts after the indentation too. */
    GT_ASSERT_OK(gates_view_edit(t, v, 101, C_NOTE));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ VW, VH }, be));
    gates_rect_t er = gates_node_layout_rect(t, gates_view_editor(t, v));
    GT_ASSERT(er.x == r1.x + GATES_VIEW_INDENT * 2 && er.w == 100 - GATES_VIEW_INDENT * 2);
    /* A press on the mark in the shown first column asks to close the row. */
    rec_t rec = {0};
    GT_ASSERT_OK(gates_widget_set_handler(t, v, record, &rec));
    gates_rect_t r0 = gates_view_part_rect(t, v, GATES_VIEW_PART_ROW, 0);
    pointer(t, GATES_POINTER_DOWN, (gates_point_t){ r0.x + 4 + 8, r0.y + r0.h / 2 }, 1);
    pointer(t, GATES_POINTER_UP, (gates_point_t){ r0.x + 4 + 8, r0.y + r0.h / 2 }, 1);
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(count_kind(&rec, GATES_EVENT_EXPAND_REQUESTED) == 1);
    gates_draw_list_deinit(&dl);
    gates_tree_destroy(t);
}

/* -- the header menu ------------------------------------------------------------------------ */

typedef struct count_alloc_t {
    gates_allocator_t inner;
    long live;
} count_alloc_t;

static proven_result_mem_mut_t ca_alloc(void *ctx, proven_size_t size, proven_size_t align) {
    count_alloc_t *c = ctx;
    proven_result_mem_mut_t r = c->inner.alloc_fn(c->inner.ctx, size, align);
    if (proven_is_ok(r.err)) c->live++;
    return r;
}
static proven_result_mem_mut_t ca_realloc(void *ctx, void *p, proven_size_t os, proven_size_t ns, proven_size_t align) {
    count_alloc_t *c = ctx;
    proven_result_mem_mut_t r = c->inner.realloc_fn(c->inner.ctx, p, os, ns, align);
    if (proven_is_ok(r.err) && p == nullptr) c->live++;
    return r;
}
static void ca_free(void *ctx, void *p) {
    count_alloc_t *c = ctx;
    if (p != nullptr) c->live--;
    c->inner.free_fn(c->inner.ctx, p);
}

static void test_column_menu(void) {
    make_cols(nullptr);
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t), v;
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    /* Every column needs a label for its entry. */
    gates_column_desc_t nolabel[] = { { .id = 1 } };
    GT_ASSERT(gates_view_create(t, root, &(gates_view_desc_t){ .columns = nolabel, .column_count = 1, .column_menu = true }, &v) ==
              PROVEN_ERR_INVALID_ARG);
    GT_ASSERT_OK(gates_view_create(t, root, &(gates_view_desc_t){ .columns = cols, .column_count = 6, .header = true, .column_menu = true }, &v));
    GT_ASSERT_OK(gates_layout_set_child_grow(t, v, 1));
    model_t m;
    fill(&m, 4);
    gates_rows_model_t mb = bind(&m, true);
    GT_ASSERT_OK(gates_view_set_model(t, v, &mb));
    layout(t);
    /* A right press in the rows opens nothing; on the header, the menu with an entry per column. */
    gates_rect_t row = gates_view_part_rect(t, v, GATES_VIEW_PART_ROW, 0);
    gates_pointer_event_t rp = { .action = GATES_POINTER_DOWN, .button = GATES_BUTTON_RIGHT, .pos = at(row, 10) };
    (void)gates_input_pointer(t, &rp);
    GT_ASSERT(gates_tree_overlay_count(t) == 0);
    rp.pos = at(gates_view_part_rect(t, v, GATES_VIEW_PART_HEADER, 1), 5);
    GT_ASSERT(gates_node_eq(gates_input_pointer(t, &rp), v));
    GT_ASSERT(gates_tree_overlay_count(t) == 1);
    gates_tree_dismiss_menus(t); /* the same menu, reopened to get its node */
    (void)gates_tree_dispatch_events(t, 0);
    gates_node_t menu = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_view_open_column_menu(t, v, rp.pos, &menu));
    GT_ASSERT(gates_access_item_count(t, menu) == 6);
    for (gates_u32 i = 0; i < 6; i++) GT_ASSERT(gates_access_item_at(t, menu, i) == cols[i].id);
    GT_ASSERT(gates_command_checked(t, v, C_DONE));
    /* Choosing Done hides it; its entry is unchecked next time. */
    GT_ASSERT_OK(gates_access_invoke(t, menu, C_DONE));
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(gates_view_column_hidden(t, v, C_DONE));
    GT_ASSERT(!gates_command_checked(t, v, C_DONE));
    GT_ASSERT(gates_tree_overlay_count(t) == 0);
    /* The last shown column's entry is disabled. */
    for (gates_column_id_t c = C_NAME; c <= C_NOTE; c++) (void)gates_view_set_column_hidden(t, v, c, true);
    GT_ASSERT(gates_view_column_hidden(t, v, C_NOTE) && !gates_view_column_hidden(t, v, C_OWN));
    GT_ASSERT(!gates_command_enabled(t, v, C_OWN) && gates_command_enabled(t, v, C_NAME));
    GT_ASSERT_OK(gates_view_set_column_hidden(t, v, C_NAME, false));
    GT_ASSERT(gates_command_enabled(t, v, C_OWN));
    /* Shift+F10 on the view opens it too (F10 alone does not). */
    gates_tree_set_focus(t, v);
    gates_key_event_t f10 = { .key = GATES_KEY_F10, .down = true };
    GT_ASSERT(!gates_input_key(t, &f10));
    GT_ASSERT(gates_tree_overlay_count(t) == 0);
    gates_key_event_t sf10 = { .key = GATES_KEY_F10, .down = true, .shift = true };
    GT_ASSERT(gates_input_key(t, &sf10));
    GT_ASSERT(gates_tree_overlay_count(t) == 1);
    gates_tree_dismiss_menus(t);
    (void)gates_tree_dispatch_events(t, 0);
    /* The program opens it for a "Columns" command. */
    gates_node_t opened = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_view_open_column_menu(t, v, (gates_point_t){ 10, 10 }, &opened));
    GT_ASSERT(gates_node_kind(t, opened) == GATES_NODE_MENU);
    gates_tree_dismiss_menus(t);
    (void)gates_tree_dispatch_events(t, 0);
    /* Disabled: a right press on the header opens nothing. */
    GT_ASSERT_OK(gates_widget_set_disabled(t, v, true));
    (void)gates_input_pointer(t, &rp);
    GT_ASSERT(gates_tree_overlay_count(t) == 0);
    gates_tree_destroy(t);

    /* Without column_menu: Shift+F10 is not the view's, a right press opens nothing. */
    app_t a;
    make_app(&a, 3, true);
    gates_tree_set_focus(a.t, a.view);
    GT_ASSERT(!gates_input_key(a.t, &sf10));
    rp.pos = at(gates_view_part_rect(a.t, a.view, GATES_VIEW_PART_HEADER, 0), 5);
    (void)gates_input_pointer(a.t, &rp);
    GT_ASSERT(gates_tree_overlay_count(a.t) == 0);
    free_app(&a);

    /* The menu's commands go with the view: nothing stays allocated after it is destroyed. */
    count_alloc_t ca = { .inner = proven_heap_allocator() };
    gates_allocator_t al = { .ctx = &ca, .alloc_fn = ca_alloc, .realloc_fn = ca_realloc, .free_fn = ca_free };
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){ .allocator = al }, &t));
    long base = -1;
    for (int round = 0; round < 3; round++) {
        GT_ASSERT_OK(gates_view_create(t, gates_tree_root(t), &(gates_view_desc_t){ .columns = cols, .column_count = 6, .column_menu = true }, &v));
        GT_ASSERT_OK(gates_node_destroy(t, v));
        GT_ASSERT_OK(gates_tree_flush_destroys(t));
        if (round == 0) base = ca.live; /* the pools have grown once */
    }
    GT_ASSERT(ca.live == base);
    gates_tree_destroy(t);
    GT_ASSERT(ca.live == 0);
}

/* -- saved columns -------------------------------------------------------------------------- */

static void test_column_state(void) {
    app_t a;
    make_app(&a, 3, true);
    GT_ASSERT_OK(gates_node_set_automation_id(a.t, a.view, GATES_STR("files")));
    GT_ASSERT_OK(gates_view_move_column(a.t, a.view, C_NOTE, 0));
    GT_ASSERT_OK(gates_view_set_column_hidden(a.t, a.view, C_PROG, true));
    GT_ASSERT_OK(gates_view_set_column_width(a.t, a.view, C_NAME, 150));
    char text[512];
    gates_usize_t need = 0;
    GT_ASSERT_OK(gates_state_save(a.t, (gates_u8 *)text, sizeof text - 1, &need));
    text[need] = 0;
    GT_ASSERT(strstr(text, "columns 5:80,1:150,2:60,3:120h,4:120,6:60 files\n") != nullptr);
    free_app(&a);
    /* A fresh start takes back order, widths and hidden marks. */
    make_app(&a, 3, true);
    GT_ASSERT_OK(gates_node_set_automation_id(a.t, a.view, GATES_STR("files")));
    gates_u32 applied = 0;
    GT_ASSERT_OK(gates_state_load(a.t, (gates_str_t){ (const gates_u8 *)text, need }, &applied));
    GT_ASSERT(applied == 1);
    GT_ASSERT(gates_view_column_at(a.t, a.view, 0) == C_NOTE && gates_view_column_at(a.t, a.view, 1) == C_NAME);
    GT_ASSERT(gates_view_column_hidden(a.t, a.view, C_PROG) && gates_view_column_width(a.t, a.view, C_NAME) == 150);
    /* Damaged or foreign lines are skipped; a partial line moves the named columns first. */
    const char *junk = "columns 99:50 files\n"            /* no such column */
                       "columns 1:50h,2:50h,3:50h,4:50h,5:50h,6:50h files\n" /* every column hidden */
                       "columns 1:x files\n"
                       "columns 0:50 files\n"
                       "columns 1:50,2 files\n"
                       "columns 1: files\n"
                       "columns 1:h files\n"
                       "columns 6:77,99:10,6:1 files\n";   /* applied: Own first, unknown and repeats skipped */
    GT_ASSERT_OK(gates_state_load(a.t, (gates_str_t){ (const gates_u8 *)junk, strlen(junk) }, &applied));
    GT_ASSERT(applied == 1);
    GT_ASSERT(gates_view_column_at(a.t, a.view, 0) == C_OWN && gates_view_column_width(a.t, a.view, C_OWN) == 77);
    GT_ASSERT(gates_view_column_at(a.t, a.view, 1) == C_NOTE && gates_view_column_at(a.t, a.view, 2) == C_NAME);
    GT_ASSERT(gates_view_column_hidden(a.t, a.view, C_PROG)); /* not named: kept */
    /* The 0.5 format (widths by position) still loads. */
    const char *old = "columns 10,20,30,40,50,60 files\n";
    GT_ASSERT_OK(gates_state_load(a.t, (gates_str_t){ (const gates_u8 *)old, strlen(old) }, &applied));
    GT_ASSERT(applied == 1); /* order Own, Note, Name, Done, Progress, File */
    GT_ASSERT(gates_view_column_width(a.t, a.view, C_NAME) == 30 && gates_view_column_width(a.t, a.view, C_FILE) == 60);
    GT_ASSERT(gates_view_column_hidden(a.t, a.view, C_PROG)); /* widths only */
    free_app(&a);
}

/* -- property grid (stage 2b) --------------------------------------------------------------- */

typedef struct prec_t {
    int n;
    gates_node_t source[16];
    gates_u32 id[16];
    char text[16][16];
    bool checked[16];
    gates_i64 value[16];
    gates_event_kind_t kind[16];
} prec_t;

static void prop_record(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    prec_t *r = user;
    if (r->n >= 16) return;
    r->source[r->n] = ev->source;
    r->id[r->n] = ev->result;
    size_t n = ev->text.size < 15 ? ev->text.size : 15;
    memcpy(r->text[r->n], ev->text.ptr != nullptr ? (const char *)ev->text.ptr : "", n);
    r->text[r->n][n] = 0;
    r->checked[r->n] = ev->checked;
    r->value[r->n] = ev->value;
    r->kind[r->n] = ev->kind;
    r->n++;
}

static const gates_option_t sizes[] = { { .id = 1, .label = GATES_STR_INIT("Small") },
                                        { .id = 2, .label = GATES_STR_INIT("Large") } };

static void build_props(gates_tree_t *t, gates_node_t pg) {
    GT_ASSERT_OK(gates_propgrid_add_text(t, pg, GATES_STR("General"), 1, GATES_STR("Title"), GATES_STR("Report")));
    GT_ASSERT_OK(gates_propgrid_add_bool(t, pg, GATES_STR("General"), 2, GATES_STR("Visible"), true));
    GT_ASSERT_OK(gates_propgrid_add_choice(t, pg, GATES_STR("Layout"), 3, GATES_STR("Size"), sizes, 2, 1));
    gates_range_t r = { .min = 0, .max = 100, .value = 10 };
    GT_ASSERT_OK(gates_propgrid_add_number(t, pg, GATES_STR("Layout"), 4, GATES_STR("Margin"), &r));
    GT_ASSERT_OK(gates_propgrid_add_text(t, pg, GATES_STR(""), 5, GATES_STR("Name"), GATES_STR("")));
}

static gates_node_t child_at(gates_tree_t *t, gates_node_t n, gates_u32 k) {
    gates_node_t c = gates_node_first_child(t, n);
    while (k-- > 0) c = gates_node_next_sibling(t, c);
    return c;
}

static void test_propgrid(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t), pg;
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT(gates_propgrid_create(t, root, nullptr) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT_OK(gates_propgrid_create(t, root, &pg));
    build_props(t, pg);
    GT_ASSERT(gates_propgrid_count(t, pg) == 5);
    /* Refusals leave nothing behind: id 0, a repeated id, a bad option list, a bad range. */
    gates_u32 kids = gates_node_child_count(t, pg);
    GT_ASSERT(gates_propgrid_add_text(t, pg, GATES_STR(""), 0, GATES_STR("x"), GATES_STR("")) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_propgrid_add_bool(t, pg, GATES_STR("New"), 2, GATES_STR("x"), false) == PROVEN_ERR_INVALID_ARG);
    gates_option_t dup[] = { { .id = 1, .label = GATES_STR_INIT("a") }, { .id = 1, .label = GATES_STR_INIT("b") } };
    GT_ASSERT(gates_propgrid_add_choice(t, pg, GATES_STR("New"), 9, GATES_STR("x"), dup, 2, 0) == PROVEN_ERR_INVALID_ARG);
    gates_range_t bad = { .min = 5, .max = 1 };
    GT_ASSERT(gates_propgrid_add_number(t, pg, GATES_STR("New"), 9, GATES_STR("x"), &bad) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_propgrid_add_number(t, pg, GATES_STR("New"), 9, GATES_STR("x"), nullptr) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_propgrid_add_bool(t, root, GATES_STR(""), 9, GATES_STR("x"), false) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_node_child_count(t, pg) == kids);
    GT_ASSERT(gates_node_eq(gates_propgrid_category(t, pg, GATES_STR("New")), GATES_NODE_NULL));
    GT_ASSERT(gates_propgrid_count(t, pg) == 5);
    GT_ASSERT(gates_node_eq(gates_propgrid_editor(t, pg, 9), GATES_NODE_NULL));
    /* Structure: the uncategorized grid first, then General and Layout in order. */
    GT_ASSERT(kids == 3);
    gates_node_t general = gates_propgrid_category(t, pg, GATES_STR("General"));
    gates_node_t lay = gates_propgrid_category(t, pg, GATES_STR("Layout"));
    GT_ASSERT(gates_node_kind(t, general) == GATES_NODE_GROUP && gates_node_kind(t, lay) == GATES_NODE_GROUP);
    GT_ASSERT(gates_node_eq(child_at(t, pg, 1), general) && gates_node_eq(child_at(t, pg, 2), lay));
    /* A category of the same length is another category. */
    GT_ASSERT_OK(gates_propgrid_add_bool(t, pg, GATES_STR("Details"), 6, GATES_STR("Extra"), false));
    gates_node_t details = gates_propgrid_category(t, pg, GATES_STR("Details"));
    GT_ASSERT(!gates_node_eq(details, general) && gates_node_eq(child_at(t, pg, 3), details));
    GT_ASSERT(gates_node_eq(gates_node_parent(t, gates_node_parent(t, gates_propgrid_editor(t, pg, 6))), details));
    GT_ASSERT(gates_node_kind(t, gates_propgrid_editor(t, pg, 1)) == GATES_NODE_TEXTBOX);
    GT_ASSERT(gates_node_kind(t, gates_propgrid_editor(t, pg, 2)) == GATES_NODE_CHECKBOX);
    GT_ASSERT(gates_node_kind(t, gates_propgrid_editor(t, pg, 3)) == GATES_NODE_CHOICE);
    GT_ASSERT(gates_node_kind(t, gates_propgrid_editor(t, pg, 4)) == GATES_NODE_SPIN);
    gates_node_t name_ed = gates_propgrid_editor(t, pg, 5);
    GT_ASSERT(gates_node_eq(gates_node_parent(t, name_ed), child_at(t, pg, 0)));
    GT_ASSERT(str_is(gates_textbox_text(t, gates_propgrid_editor(t, pg, 1)), "Report"));
    GT_ASSERT(gates_checkbox_checked(t, gates_propgrid_editor(t, pg, 2)));
    GT_ASSERT(gates_range_value(t, gates_propgrid_editor(t, pg, 4)) == 10);
    /* Each editor is named by its label; the whole grid passes the audit. */
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 400, 400 }, be));
    gates_access_info_t info;
    GT_ASSERT_OK(gates_access_info(t, gates_propgrid_editor(t, pg, 2), 0, &info));
    GT_ASSERT(str_is(info.name, "Visible"));
    GT_ASSERT_OK(gates_access_info(t, gates_propgrid_editor(t, pg, 4), 0, &info));
    GT_ASSERT(str_is(info.name, "Margin"));
    gates_access_issue_t issues[8];
    GT_ASSERT(gates_access_audit(t, theme, issues, 8) == 0);
    /* A person's changes reach the grid's handler by property id; program changes are silent. */
    prec_t rec = {0};
    GT_ASSERT(gates_propgrid_set_handler(t, root, prop_record, &rec) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT_OK(gates_propgrid_set_handler(t, pg, prop_record, &rec));
    GT_ASSERT_OK(gates_textbox_set_text(t, gates_propgrid_editor(t, pg, 1), GATES_STR("Quiet")));
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(rec.n == 0);
    gates_tree_set_focus(t, gates_propgrid_editor(t, pg, 1));
    type(t, "!");
    gates_tree_set_focus(t, gates_propgrid_editor(t, pg, 2));
    GT_ASSERT(key(t, GATES_KEY_SPACE));
    GT_ASSERT_OK(gates_access_select(t, gates_propgrid_editor(t, pg, 3), 2));
    gates_tree_set_focus(t, gates_node_first_child(t, gates_propgrid_editor(t, pg, 4))); /* the spin box's text */
    GT_ASSERT(key(t, GATES_KEY_UP));
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(rec.n == 4);
    for (int i = 0; i < rec.n; i++) {
        GT_ASSERT(gates_node_eq(rec.source[i], pg) && rec.kind[i] == GATES_EVENT_VALUE_CHANGED);
    }
    GT_ASSERT(rec.id[0] == 1 && strcmp(rec.text[0], "Quiet!") == 0);
    GT_ASSERT(rec.id[1] == 2 && !rec.checked[1]);
    GT_ASSERT(rec.id[2] == 3 && rec.value[2] == 2);
    GT_ASSERT(rec.id[3] == 4 && rec.value[3] == 11);
    /* A composition is not a change. */
    gates_tree_set_focus(t, gates_propgrid_editor(t, pg, 1));
    (void)gates_input_preedit(t, GATES_STR("ka"), 2);
    (void)gates_input_preedit_cancel(t);
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(rec.n == 4);
    /* The editors take the spare width. */
    gates_rect_t gr = gates_node_layout_rect(t, child_at(t, pg, 0));
    gates_rect_t nr = gates_node_layout_rect(t, name_ed);
    GT_ASSERT(nr.x + nr.w == gr.x + gr.w);
    /* Folding a category hides its editors from the keyboard; its title is a Tab stop. */
    GT_ASSERT_OK(gates_group_set_expanded(t, lay, false));
    GT_ASSERT(!gates_node_eq(gates_tree_focus(t), gates_propgrid_editor(t, pg, 4)));
    /* Removing the handler: changes are heard no more. */
    GT_ASSERT_OK(gates_propgrid_set_handler(t, pg, nullptr, &rec));
    gates_tree_set_focus(t, gates_propgrid_editor(t, pg, 1));
    type(t, "?");
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(rec.n == 4);
    gates_tree_destroy(t);
}

static void test_propgrid_failures(void) {
    bool done = false;
    for (int k = 0; k < 400 && !done; k++) {
        fail_alloc_t f = { .inner = proven_heap_allocator(), .left = -1 };
        gates_allocator_t al = { .ctx = &f, .alloc_fn = fa_alloc, .realloc_fn = fa_realloc, .free_fn = fa_free };
        gates_tree_t *t = nullptr;
        GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){ .allocator = al }, &t));
        gates_node_t pg = GATES_NODE_NULL;
        f.left = k;
        gates_err_t err = gates_propgrid_create(t, gates_tree_root(t), &pg);
        gates_range_t r = { .min = 0, .max = 9 };
        gates_u32 added = 0;
        if (gates_is_ok(err)) {
            gates_err_t e[5] = {
                gates_propgrid_add_text(t, pg, GATES_STR("A"), 1, GATES_STR("t"), GATES_STR("v")),
                gates_propgrid_add_bool(t, pg, GATES_STR("A"), 2, GATES_STR("b"), true),
                gates_propgrid_add_choice(t, pg, GATES_STR("B"), 3, GATES_STR("c"), sizes, 2, 1),
                gates_propgrid_add_number(t, pg, GATES_STR(""), 4, GATES_STR("n"), &r),
                gates_propgrid_add_text(t, pg, GATES_STR("C"), 5, GATES_STR("t2"), GATES_STR("")),
            };
            for (int i = 0; i < 5; i++) {
                GT_ASSERT(gates_is_ok(e[i]) || e[i] == PROVEN_ERR_NOMEM);
                if (gates_is_ok(e[i])) {
                    added++;
                    GT_ASSERT(!gates_node_eq(gates_propgrid_editor(t, pg, (gates_prop_id_t)(i + 1)), GATES_NODE_NULL));
                } else {
                    GT_ASSERT(gates_node_eq(gates_propgrid_editor(t, pg, (gates_prop_id_t)(i + 1)), GATES_NODE_NULL));
                }
            }
            GT_ASSERT(gates_propgrid_count(t, pg) == added);
            /* No stray label or empty category: every grid holds label/editor pairs. */
            for (gates_node_t c = gates_node_first_child(t, pg); !gates_node_eq(c, GATES_NODE_NULL);
                 c = gates_node_next_sibling(t, c)) {
                gates_node_t content = gates_node_kind(t, c) == GATES_NODE_GROUP ? child_at(t, c, 1) : c;
                gates_u32 n = gates_node_child_count(t, content);
                GT_ASSERT(n % 2 == 0);
                if (gates_node_kind(t, c) == GATES_NODE_GROUP) GT_ASSERT(n > 0);
            }
            done = added == 5;
        } else {
            GT_ASSERT(err == PROVEN_ERR_NOMEM && gates_node_child_count(t, gates_tree_root(t)) == 0);
        }
        f.left = -1;
        GT_ASSERT_OK(gates_tree_flush_destroys(t));
        gates_tree_destroy(t); /* ASan: nothing leaked at any failure point */
    }
    GT_ASSERT(done);
}

/* Constructors that fail for memory leave nothing in the tree (a label once
 * stayed attached when its own rollback could not allocate). */
static void test_constructor_rollback(void) {
    for (int kind = 0; kind < 4; kind++) {
        bool made = false;
        for (int k = 0; k < 32 && !made; k++) {
            fail_alloc_t f = { .inner = proven_heap_allocator(), .left = -1 };
            gates_allocator_t al = { .ctx = &f, .alloc_fn = fa_alloc, .realloc_fn = fa_realloc, .free_fn = fa_free };
            gates_tree_t *t = nullptr;
            GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){ .allocator = al }, &t));
            gates_node_t root = gates_tree_root(t), n = GATES_NODE_NULL;
            f.left = k;
            gates_err_t err = kind == 0 ? gates_label_create(t, root, GATES_STR("name"), &n)
                              : kind == 1 ? gates_checkbox_create(t, root, GATES_STR("x"), false, nullptr, nullptr, &n)
                              : kind == 2 ? gates_textbox_create(t, root, GATES_STR("text"), 8, &n)
                                          : gates_button_create(t, root, GATES_STR("go"), nullptr, nullptr, &n);
            f.left = -1;
            made = gates_is_ok(err);
            GT_ASSERT(gates_node_child_count(t, root) == (made ? 1u : 0u));
            gates_tree_destroy(t);
        }
        GT_ASSERT(made);
    }
}

int main(void) {
    be = gates_text_backend_builtin();
    theme = gates_theme_light();
    test_creation();
    test_paint_kinds();
    test_f2_enter_escape();
    test_refusal();
    test_focus_leaving_commits();
    test_double_click();
    test_checks();
    test_keyboard_extras();
    test_scroll_and_model();
    test_create_failures();
    test_hide_and_move();
    test_tree_first_column();
    test_column_menu();
    test_column_state();
    test_propgrid();
    test_propgrid_failures();
    test_constructor_rollback();
    return gt_report("test_data");
}
