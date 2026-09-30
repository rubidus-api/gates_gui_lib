/* T031: view adapters - tree (flattened rows, open/close requests, Left/Right,
 * loading/error rows, no hidden walks) and log (bounded ring, drops, ids,
 * follow) (plan-0011 stage 2, RFC-0003 8.2). */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/view.h>
#include "gates_test.h"
#include <proven/heap.h>

#include <stdio.h>
#include <string.h>

static const gates_text_backend_t *be;
static const gates_theme_t *theme;
#define VW 400
#define VH 300

static void layout(gates_tree_t *t) {
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ VW, VH }, be));
}

static bool key(gates_tree_t *t, gates_key_t k) {
    gates_key_event_t e = { .key = k, .down = true };
    return gates_input_key(t, &e);
}

static void click(gates_tree_t *t, gates_point_t p) {
    gates_pointer_event_t e = { .action = GATES_POINTER_DOWN, .button = GATES_BUTTON_LEFT, .pos = p,
                                .clicks = 1 };
    (void)gates_input_pointer(t, &e);
    e.action = GATES_POINTER_UP;
    (void)gates_input_pointer(t, &e);
}

static void wheel(gates_tree_t *t, gates_point_t p, float y) {
    gates_pointer_event_t e = { .action = GATES_POINTER_WHEEL, .pos = p, .wheel = { 0.0f, y } };
    (void)gates_input_pointer(t, &e);
}

static gates_point_t mid(gates_rect_t r) {
    return (gates_point_t){ r.x + r.w / 2, r.y + r.h / 2 };
}

/* Finds a TEXT command with exactly this text; returns its rect and colour. */
static bool find_text(const gates_draw_list_t *dl, const char *s, gates_rect_t *r, gates_color_t *c) {
    size_t n = strlen(s);
    for (gates_u32 i = 0; i < gates_draw_list_len(dl); i++) {
        const gates_draw_cmd_t *cmd = gates_draw_list_at(dl, i);
        if (cmd->kind != GATES_DRAW_TEXT) continue;
        gates_str_t t = gates_draw_cmd_text(dl, cmd);
        if (t.size == n && memcmp(t.ptr, s, n) == 0) {
            if (r) *r = cmd->rect;
            if (c) *c = cmd->color;
            return true;
        }
    }
    return false;
}

static bool same(gates_color_t a, gates_color_token_t tok) {
    gates_color_t b = gates_theme_color(theme, tok);
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

typedef struct rec_t {
    gates_event_kind_t kind[32];
    gates_event_origin_t origin[32];
    gates_u64 item[32];
    gates_u32 result[32];
    int n;
} rec_t;

static void record(rec_t *r, const gates_event_t *ev) {
    if (r->n < 32) {
        r->kind[r->n] = ev->kind;
        r->origin[r->n] = ev->origin;
        r->item[r->n] = ev->item;
        r->result[r->n] = ev->result;
        r->n++;
    }
}

/* -- a small tree model: the flattened sequence is rebuilt from open flags ------------ */

typedef struct node_t {
    gates_item_id_t id, parent;
    const char *name;
    gates_row_state_t state;
    bool expandable;
} node_t;

static node_t nodes[] = {
    { 1, 0, "one", GATES_ROW_NORMAL, true },
    { 11, 1, "one.one", GATES_ROW_NORMAL, false },
    { 12, 1, "one.two", GATES_ROW_NORMAL, true },
    { 121, 12, "one.two.one", GATES_ROW_NORMAL, false },
    { 2, 0, "two", GATES_ROW_NORMAL, true },
    { 29, 2, "loading...", GATES_ROW_LOADING, false },   /* until "loaded" */
    { 21, 2, "two.one", GATES_ROW_NORMAL, false },
    { 3, 0, "three", GATES_ROW_ERROR, false },
};
#define NODES (sizeof nodes / sizeof nodes[0])

typedef struct tmodel_t {
    bool open[NODES];
    bool loaded;                 /* node 2's children arrived */
    gates_item_id_t rows[NODES];
    gates_u64 n;
    int infos;
    bool asked_121;
    gates_tree_t *t;
    gates_node_t view;
    rec_t rec;
} tmodel_t;

static int index_of_node(gates_item_id_t id) {
    for (int i = 0; i < (int)NODES; i++) if (nodes[i].id == id) return i;
    return -1;
}

static bool visible_child(const tmodel_t *m, int i) {
    if (nodes[i].parent == 2) {
        return m->loaded ? nodes[i].id != 29 : nodes[i].id == 29;
    }
    return true;
}

static void add_children(tmodel_t *m, gates_item_id_t parent) {
    for (int i = 0; i < (int)NODES; i++) {
        if (nodes[i].parent != parent || !visible_child(m, i)) continue;
        m->rows[m->n++] = nodes[i].id;
        if (m->open[i]) add_children(m, nodes[i].id);
    }
}

static void rebuild(tmodel_t *m) {
    m->n = 0;
    add_children(m, 0);
}

static gates_u64 t_count(void *u) { return ((tmodel_t *)u)->n; }
static gates_item_id_t t_id_at(void *u, gates_u64 row) {
    tmodel_t *m = u;
    return row < m->n ? m->rows[row] : 0;
}
static bool t_index_of(void *u, gates_item_id_t id, gates_u64 *row) {
    tmodel_t *m = u;
    for (gates_u64 i = 0; i < m->n; i++) if (m->rows[i] == id) { *row = i; return true; }
    return false;
}
static gates_err_t t_cell(void *u, gates_item_id_t id, gates_column_id_t col, gates_cell_t *out) {
    (void)u; (void)col;
    int i = index_of_node(id);
    out->text = (gates_str_t){ .ptr = (const gates_u8 *)nodes[i].name, .size = strlen(nodes[i].name) };
    return GATES_OK;
}
static gates_err_t t_info(void *u, gates_item_id_t id, gates_row_info_t *out) {
    tmodel_t *m = u;
    m->infos++;
    if (id == 121) m->asked_121 = true;
    int i = index_of_node(id);
    gates_u32 depth = 0;
    for (gates_item_id_t p = nodes[i].parent; p != 0; p = nodes[index_of_node(p)].parent) depth++;
    *out = (gates_row_info_t){ .depth = depth, .expandable = nodes[i].expandable,
                               .expanded = m->open[i], .parent = nodes[i].parent,
                               .state = nodes[i].state };
    return GATES_OK;
}

static void on_tree(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    tmodel_t *m = user;
    record(&m->rec, ev);
    if (ev->kind == GATES_EVENT_EXPAND_REQUESTED) {
        m->open[index_of_node(ev->item)] = ev->result != 0;
        rebuild(m);
        (void)gates_view_model_changed(tree, m->view);
    }
}

static void paint_into(gates_tree_t *t, gates_draw_list_t *dl) {
    GT_ASSERT_OK(gates_draw_list_init(dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, dl, theme, be));
}

static void test_tree(void) {
    tmodel_t m = {0};
    rebuild(&m);
    GT_ASSERT(m.n == 3);
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &m.t));
    gates_tree_t *t = m.t;
    GT_ASSERT_OK(gates_layout_set(t, gates_tree_root(t), GATES_LAYOUT_KIND_COLUMN));
    gates_view_desc_t d = { .tree = true };
    GT_ASSERT_OK(gates_view_create(t, gates_tree_root(t), &d, &m.view));
    GT_ASSERT_OK(gates_layout_set_child_grow(t, m.view, 1));
    gates_rows_model_t model = { .user = &m, .count = t_count, .id_at = t_id_at,
                                 .index_of = t_index_of, .cell = t_cell };
    GT_ASSERT(gates_view_set_model(t, m.view, &model) == PROVEN_ERR_INVALID_ARG); /* needs row_info */
    model.row_info = t_info;
    GT_ASSERT_OK(gates_view_set_model(t, m.view, &model));
    GT_ASSERT_OK(gates_widget_set_handler(t, m.view, on_tree, &m));
    layout(t);

    /* Right opens a closed row (a request the model answers). */
    gates_tree_set_focus(t, m.view);
    GT_ASSERT(key(t, GATES_KEY_DOWN) && gates_view_selected(t, m.view) == 1);
    (void)gates_tree_dispatch_events(t, 0); /* deliver what came before */
    m.rec.n = 0;
    GT_ASSERT(key(t, GATES_KEY_RIGHT));
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(m.rec.n == 1 && m.rec.kind[0] == GATES_EVENT_EXPAND_REQUESTED && m.rec.item[0] == 1 &&
              m.rec.result[0] == 1);
    GT_ASSERT(m.n == 5 && gates_view_selected(t, m.view) == 1);
    /* Right on an open row moves to its first child; Right on a leaf does nothing. */
    GT_ASSERT(key(t, GATES_KEY_RIGHT) && gates_view_selected(t, m.view) == 11);
    (void)gates_tree_dispatch_events(t, 0); /* deliver what came before */
    m.rec.n = 0;
    GT_ASSERT(key(t, GATES_KEY_RIGHT) && gates_view_selected(t, m.view) == 11);
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(m.rec.n == 0);
    GT_ASSERT(gates_view_scroll_x(t, m.view) == 0);            /* not a sideways scroll */
    /* Left on a closed child moves to its parent; Left on an open row closes it. */
    GT_ASSERT(key(t, GATES_KEY_LEFT) && gates_view_selected(t, m.view) == 1);
    (void)gates_tree_dispatch_events(t, 0); /* deliver what came before */
    m.rec.n = 0;
    GT_ASSERT(key(t, GATES_KEY_LEFT));
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(m.rec.n == 1 && m.rec.kind[0] == GATES_EVENT_EXPAND_REQUESTED && m.rec.item[0] == 1 &&
              m.rec.result[0] == 0);
    GT_ASSERT(m.n == 3 && gates_view_selected(t, m.view) == 1);
    GT_ASSERT(key(t, GATES_KEY_LEFT) && gates_view_selected(t, m.view) == 1); /* top level: stays */

    /* Children are indented; a closed branch is never asked about. */
    m.open[index_of_node(1)] = true;
    rebuild(&m);
    GT_ASSERT_OK(gates_view_model_changed(t, m.view));
    m.infos = 0;
    gates_draw_list_t dl;
    paint_into(t, &dl);
    GT_ASSERT(m.infos > 0 && m.infos <= (int)m.n);              /* one per painted row */
    GT_ASSERT(!m.asked_121);                                    /* "one.two" is closed */
    gates_rect_t r_one, r_child, r_three;
    gates_color_t c_three;
    GT_ASSERT(find_text(&dl, "one", &r_one, nullptr));
    GT_ASSERT(find_text(&dl, "one.one", &r_child, nullptr));
    GT_ASSERT(find_text(&dl, "three", &r_three, &c_three));
    GT_ASSERT(r_child.x > r_one.x);
    GT_ASSERT(same(c_three, GATES_COLOR_ERROR));                /* error row */
    gates_draw_list_deinit(&dl);

    /* A click on a row's open mark asks to open it without selecting it; a lazy
     * branch shows its loading row dimmed until the model has the children. */
    gates_u64 row2 = 0;
    GT_ASSERT(t_index_of(&m, 2, &row2));
    gates_rect_t rr = gates_view_part_rect(t, m.view, GATES_VIEW_PART_ROW, row2);
    (void)gates_tree_dispatch_events(t, 0); /* deliver what came before */
    m.rec.n = 0;
    click(t, (gates_point_t){ rr.x + GATES_VIEW_INDENT / 2 + 2, rr.y + rr.h / 2 });
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(m.rec.n == 1 && m.rec.kind[0] == GATES_EVENT_EXPAND_REQUESTED && m.rec.item[0] == 2 &&
              m.rec.result[0] == 1);
    GT_ASSERT(gates_view_selected(t, m.view) == 1);
    paint_into(t, &dl);
    gates_color_t c_load;
    GT_ASSERT(find_text(&dl, "loading...", nullptr, &c_load));
    GT_ASSERT(same(c_load, GATES_COLOR_CONTROL_DISABLED_FG));
    gates_draw_list_deinit(&dl);
    m.loaded = true;
    rebuild(&m);
    GT_ASSERT_OK(gates_view_model_changed(t, m.view));
    paint_into(t, &dl);
    GT_ASSERT(find_text(&dl, "two.one", nullptr, nullptr) && !find_text(&dl, "loading...", nullptr, nullptr));
    gates_draw_list_deinit(&dl);
    /* Clicking the text of a row selects it (no request). */
    (void)gates_tree_dispatch_events(t, 0); /* deliver what came before */
    m.rec.n = 0;
    click(t, mid(rr));
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(gates_view_selected(t, m.view) == 2);
    GT_ASSERT(m.rec.n == 1 && m.rec.kind[0] == GATES_EVENT_SELECTION_CHANGED);
    gates_tree_destroy(t);
}

/* -- log --------------------------------------------------------------------------------- */

static void on_log(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    record(user, ev);
}

static gates_node_t make_log(gates_tree_t **t, gates_u32 lines, gates_usize_t bytes,
                             gates_allocator_t alloc, rec_t *rec) {
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){ .allocator = alloc }, t));
    GT_ASSERT_OK(gates_layout_set(*t, gates_tree_root(*t), GATES_LAYOUT_KIND_COLUMN));
    gates_node_t log = GATES_NODE_NULL;
    gates_log_desc_t d = { .max_lines = lines, .max_bytes = bytes };
    GT_ASSERT_OK(gates_log_create(*t, gates_tree_root(*t), &d, &log));
    GT_ASSERT_OK(gates_layout_set_child_grow(*t, log, 1));
    if (rec != nullptr) GT_ASSERT_OK(gates_widget_set_handler(*t, log, on_log, rec));
    layout(*t);
    return log;
}

static gates_err_t append_n(gates_tree_t *t, gates_node_t log, int from, int count) {
    char buf[32];
    for (int i = from; i < from + count; i++) {
        int n = snprintf(buf, sizeof buf, "line %d", i);
        gates_err_t err = gates_log_append(t, log, (gates_str_t){ .ptr = (const gates_u8 *)buf,
                                                                  .size = (gates_usize_t)n });
        if (!gates_is_ok(err)) return err;
    }
    return GATES_OK;
}

static void test_log_ring(void) {
    rec_t rec = {0};
    gates_tree_t *t = nullptr;
    gates_node_t log = make_log(&t, 100, 100000, (gates_allocator_t){0}, &rec);
    GT_ASSERT(gates_node_kind(t, log) == GATES_NODE_VIEW);
    GT_ASSERT(gates_log_following(t, log));
    GT_ASSERT_OK(append_n(t, log, 1, 250));
    layout(t);
    GT_ASSERT(gates_log_count(t, log) == 100 && gates_log_dropped(t, log) == 150);
    gates_u32 vis = gates_view_visible_rows(t, log);
    GT_ASSERT(gates_view_first_row(t, log) == 100 - vis);        /* followed to the end */
    /* The newest line is on screen; ids are line numbers here (1..250). */
    gates_draw_list_t dl;
    paint_into(t, &dl);
    gates_rect_t last_text;
    GT_ASSERT(find_text(&dl, "line 250", &last_text, nullptr) && !find_text(&dl, "line 150", nullptr, nullptr));
    gates_rect_t last_row = gates_view_part_rect(t, log, GATES_VIEW_PART_ROW, 99);
    GT_ASSERT(last_text.y >= last_row.y && last_text.y < last_row.y + last_row.h); /* on the last row */
    gates_draw_list_deinit(&dl);
    gates_str_t l240 = gates_log_line(t, log, 240);
    GT_ASSERT(l240.size == 8 && memcmp(l240.ptr, "line 240", 8) == 0);
    GT_ASSERT(gates_log_line(t, log, 150).size == 0 && gates_log_line(t, log, 999).size == 0);
    /* Keys move by line id: Up from line 240 selects line 239. */
    GT_ASSERT_OK(gates_view_set_selected(t, log, 240));
    gates_tree_set_focus(t, log);
    GT_ASSERT(key(t, GATES_KEY_UP) && gates_view_selected(t, log) == 239);
    GT_ASSERT_OK(gates_log_set_following(t, log, true));
    (void)gates_tree_dispatch_events(t, 0);
    rec.n = 0;

    /* A selected line keeps its id while lines arrive, until it is dropped. */
    GT_ASSERT_OK(gates_view_set_selected(t, log, 200));
    GT_ASSERT(gates_view_set_selected(t, log, 100) == PROVEN_ERR_INVALID_ARG); /* dropped */
    GT_ASSERT_OK(append_n(t, log, 251, 30));                     /* oldest now 181 */
    GT_ASSERT(gates_view_selected(t, log) == 200);
    (void)gates_tree_dispatch_events(t, 0);
    rec.n = 0;
    GT_ASSERT_OK(append_n(t, log, 281, 30));                     /* oldest now 211: 200 goes */
    GT_ASSERT(gates_view_selected(t, log) == 211);
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(rec.n == 1 && rec.kind[0] == GATES_EVENT_SELECTION_CHANGED &&
              rec.origin[0] == GATES_ORIGIN_PROGRAM && rec.item[0] == 211);

    /* Lines are single lines; limits and bad input change nothing. */
    GT_ASSERT_OK(gates_log_append(t, log, GATES_STR("a\nb\tc\rd")));
    paint_into(t, &dl);
    GT_ASSERT(find_text(&dl, "a b c d", nullptr, nullptr));
    gates_draw_list_deinit(&dl);
    gates_rows_model_t m = {0};
    GT_ASSERT(gates_view_set_model(t, log, &m) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_view_set_model(t, log, nullptr) == PROVEN_ERR_INVALID_ARG);
    gates_log_clear(t, log);
    GT_ASSERT(gates_log_count(t, log) == 0 && gates_log_dropped(t, log) == 0);
    GT_ASSERT(gates_view_selected(t, log) == 0);
    GT_ASSERT_OK(append_n(t, log, 1, 3));
    GT_ASSERT(gates_log_count(t, log) == 3);
    gates_tree_destroy(t);

    /* The byte limit drops old lines too; a line that could never fit is refused. */
    t = nullptr;
    log = make_log(&t, 1000, 64, (gates_allocator_t){0}, nullptr);
    GT_ASSERT_OK(append_n(t, log, 10, 20));                     /* "line NN" = 7 bytes */
    GT_ASSERT(gates_log_count(t, log) == 64 / 7);
    GT_ASSERT(gates_log_dropped(t, log) == 20 - 64 / 7);
    char big[80];
    memset(big, 'x', sizeof big);
    GT_ASSERT(gates_log_append(t, log, (gates_str_t){ .ptr = (const gates_u8 *)big, .size = 65 }) ==
              PROVEN_ERR_OUT_OF_BOUNDS);
    GT_ASSERT(gates_log_count(t, log) == 64 / 7);
    GT_ASSERT(gates_log_append(t, GATES_NODE_NULL, GATES_STR("x")) == PROVEN_ERR_INVALID_ARG);
    gates_tree_destroy(t);
}

static int count_follow(const rec_t *r, gates_u32 result) {
    int n = 0;
    for (int i = 0; i < r->n; i++) n += r->kind[i] == GATES_EVENT_FOLLOW_CHANGED && r->result[i] == result;
    return n;
}

static void test_log_follow(void) {
    gates_tree_t *t = nullptr;
    rec_t rec = {0};
    gates_node_t log = make_log(&t, 1000, 1000000, (gates_allocator_t){0}, &rec);
    GT_ASSERT_OK(append_n(t, log, 1, 50));
    layout(t);
    gates_u32 vis = gates_view_visible_rows(t, log);
    GT_ASSERT(gates_view_first_row(t, log) == 50 - vis);
    /* Scrolling up stops following: new lines do not move the view. */
    gates_rect_t body = gates_view_part_rect(t, log, GATES_VIEW_PART_BODY, 0);
    wheel(t, mid(body), 1.0f);
    gates_u64 first = gates_view_first_row(t, log);
    GT_ASSERT(first == 50 - vis - 3 && !gates_log_following(t, log));
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(count_follow(&rec, 0) == 1 && count_follow(&rec, 1) == 0); /* 0.8.0 */
    wheel(t, mid(body), 1.0f); /* further up: no second report */
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(count_follow(&rec, 0) == 1 && count_follow(&rec, 1) == 0);
    wheel(t, mid(body), -1.0f);
    GT_ASSERT_OK(append_n(t, log, 51, 10));
    GT_ASSERT(gates_view_first_row(t, log) == first);
    /* End resumes following. */
    gates_tree_set_focus(t, log);
    GT_ASSERT(key(t, GATES_KEY_END));
    GT_ASSERT(gates_log_following(t, log));
    GT_ASSERT_OK(append_n(t, log, 61, 5));
    GT_ASSERT(gates_view_first_row(t, log) == 65 - vis);
    /* Scrolling back down to the end resumes it as well. */
    wheel(t, mid(body), 1.0f);
    GT_ASSERT(!gates_log_following(t, log));
    wheel(t, mid(body), -1.0f);
    GT_ASSERT(gates_log_following(t, log));
    rec.n = 0;
    wheel(t, mid(body), 1.0f); /* stop and resume before delivery: one report, read at delivery */
    wheel(t, mid(body), -1.0f);
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(count_follow(&rec, 1) == 1 && count_follow(&rec, 0) == 0);
    rec.n = 0;
    /* The program can stop and resume (silently). */
    GT_ASSERT_OK(gates_log_set_following(t, log, false));
    GT_ASSERT_OK(append_n(t, log, 66, 5));
    GT_ASSERT(gates_view_first_row(t, log) == 65 - vis);
    GT_ASSERT_OK(gates_log_set_following(t, log, true));
    GT_ASSERT(gates_view_first_row(t, log) == 70 - vis);
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(count_follow(&rec, 0) + count_follow(&rec, 1) == 0);

    /* Not following while old lines are dropped: the same lines stay on screen. */
    gates_tree_destroy(t);
    t = nullptr;
    log = make_log(&t, 40, 1000000, (gates_allocator_t){0}, nullptr);
    GT_ASSERT_OK(append_n(t, log, 1, 40));
    layout(t);
    body = gates_view_part_rect(t, log, GATES_VIEW_PART_BODY, 0);
    wheel(t, mid(body), 2.0f);
    first = gates_view_first_row(t, log);
    GT_ASSERT(first >= 6);
    GT_ASSERT_OK(append_n(t, log, 41, 5));                      /* drops lines 1..5 */
    GT_ASSERT(gates_view_first_row(t, log) == first - 5);
    gates_tree_destroy(t);
}

/* -- allocation failure ---------------------------------------------------------------------- */

typedef struct fail_alloc_t {
    gates_allocator_t inner;
    bool fail;
} fail_alloc_t;

static proven_result_mem_mut_t fa_alloc(void *ctx, proven_size_t size, proven_size_t align) {
    fail_alloc_t *f = ctx;
    if (f->fail) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    return f->inner.alloc_fn(f->inner.ctx, size, align);
}

static proven_result_mem_mut_t fa_realloc(void *ctx, void *p, proven_size_t os, proven_size_t ns,
                                          proven_size_t align) {
    fail_alloc_t *f = ctx;
    if (f->fail) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    return f->inner.realloc_fn(f->inner.ctx, p, os, ns, align);
}

static void fa_free(void *ctx, void *p) {
    fail_alloc_t *f = ctx;
    f->inner.free_fn(f->inner.ctx, p);
}

static void test_log_failure(void) {
    fail_alloc_t fa = { .inner = proven_heap_allocator() };
    gates_allocator_t alloc = { .ctx = &fa, .alloc_fn = fa_alloc, .realloc_fn = fa_realloc,
                                .free_fn = fa_free };
    gates_tree_t *t = nullptr;
    gates_node_t log = make_log(&t, 10, 1000, alloc, nullptr);
    GT_ASSERT_OK(append_n(t, log, 1, 12));
    gates_u32 live = gates_tree_live_count(t);
    fa.fail = true;
    GT_ASSERT(append_n(t, log, 13, 1) == PROVEN_ERR_NOMEM);
    GT_ASSERT(gates_log_count(t, log) == 10 && gates_log_dropped(t, log) == 2); /* unchanged */
    gates_node_t other = GATES_NODE_NULL;
    gates_log_desc_t d = {0};
    GT_ASSERT(gates_log_create(t, gates_tree_root(t), &d, &other) == PROVEN_ERR_NOMEM);
    fa.fail = false;
    GT_ASSERT_OK(gates_tree_flush_destroys(t));
    GT_ASSERT(gates_tree_live_count(t) == live);
    GT_ASSERT_OK(append_n(t, log, 13, 1));
    GT_ASSERT(gates_log_dropped(t, log) == 3);
    gates_tree_destroy(t);
}

int main(void) {
    be = gates_text_backend_builtin();
    theme = gates_theme_light();
    test_tree();
    test_log_ring();
    test_log_follow();
    test_log_failure();
    return gt_report("test_view_adapters");
}
