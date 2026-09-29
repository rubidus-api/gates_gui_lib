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
    test_scroll_and_model();
    test_create_failures();
    return gt_report("test_data");
}
