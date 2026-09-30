/* virtual views - rows engine, list and table: bounded model reads, 64-bit offsets, selection by id across
 * model changes, keyboard, pointer, header sort and resize, detach. */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/command.h>
#include <gates/view.h>
#include "gates_test.h"
#include <proven/heap.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const gates_text_backend_t *be;
static const gates_theme_t *theme;
#define VW 400
#define VH 300

static void layout(gates_tree_t *t) {
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ VW, VH }, be));
}

/* -- test models ------------------------------------------------------------------- */

typedef struct model_t {
    gates_item_id_t *ids;        /* array model; null = generated (id = row + 1) */
    gates_u64 n;
    gates_u64 cap;
    gates_u64 rev;
    int cells, counts, index_ofs, id_ats, revisions;
    gates_column_id_t last_column;
    gates_u32 columns_asked;     /* bit per column id asked for */
    char buf[64];
} model_t;

static gates_u64 m_revision(void *u) { model_t *m = u; m->revisions++; return m->rev; }
static gates_u64 m_count(void *u) { model_t *m = u; m->counts++; return m->n; }

static gates_item_id_t m_id_at(void *u, gates_u64 row) {
    model_t *m = u;
    m->id_ats++;
    if (row >= m->n) return 0;
    return m->ids != nullptr ? m->ids[row] : row + 1;
}

static bool m_index_of(void *u, gates_item_id_t id, gates_u64 *row) {
    model_t *m = u;
    m->index_ofs++;
    if (m->ids == nullptr) {
        if (id == 0 || id > m->n) return false;
        *row = id - 1;
        return true;
    }
    for (gates_u64 i = 0; i < m->n; i++) {
        if (m->ids[i] == id) { *row = i; return true; }
    }
    return false;
}

static gates_err_t m_cell(void *u, gates_item_id_t id, gates_column_id_t col, gates_cell_t *out) {
    model_t *m = u;
    m->cells++;
    m->last_column = col;
    if (col < 32) m->columns_asked |= 1u << col;
    int n = snprintf(m->buf, sizeof m->buf, "item %llu c%u", (unsigned long long)id, col);
    out->text = (gates_str_t){ .ptr = (const gates_u8 *)m->buf, .size = (gates_usize_t)n };
    return GATES_OK;
}

static gates_rows_model_t bind(model_t *m) {
    return (gates_rows_model_t){ .user = m, .revision = m_revision, .count = m_count,
                                 .id_at = m_id_at, .index_of = m_index_of, .cell = m_cell };
}

static void array_model(model_t *m, gates_u64 n) {
    memset(m, 0, sizeof *m);
    m->cap = n + 64;
    m->ids = malloc(m->cap * sizeof *m->ids);
    for (gates_u64 i = 0; i < n; i++) m->ids[i] = 1000 + i;
    m->n = n;
}

static void reset_counts(model_t *m) {
    m->cells = m->counts = m->index_ofs = m->id_ats = m->revisions = 0;
    m->columns_asked = 0;
}

/* -- helpers -------------------------------------------------------------------------- */

static bool key(gates_tree_t *t, gates_key_t k) {
    gates_key_event_t e = { .key = k, .down = true };
    return gates_input_key(t, &e);
}

static void pointer(gates_tree_t *t, gates_pointer_action_t a, gates_point_t p, gates_u32 clicks) {
    gates_pointer_event_t e = { .action = a, .button = GATES_BUTTON_LEFT, .pos = p, .clicks = clicks };
    (void)gates_input_pointer(t, &e);
}

static void click(gates_tree_t *t, gates_point_t p, gates_u32 clicks) {
    pointer(t, GATES_POINTER_DOWN, p, clicks);
    pointer(t, GATES_POINTER_UP, p, clicks);
}

static void wheel(gates_tree_t *t, gates_point_t p, float x, float y) {
    gates_pointer_event_t e = { .action = GATES_POINTER_WHEEL, .pos = p, .wheel = { x, y } };
    (void)gates_input_pointer(t, &e);
}

static gates_point_t mid(gates_rect_t r) {
    return (gates_point_t){ r.x + r.w / 2, r.y + r.h / 2 };
}

typedef struct rec_t {
    gates_event_kind_t kind[32];
    gates_event_origin_t origin[32];
    gates_u64 item[32];
    gates_u32 result[32];
    int n;
} rec_t;

static void record(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    rec_t *r = user;
    if (r->n < 32) {
        r->kind[r->n] = ev->kind;
        r->origin[r->n] = ev->origin;
        r->item[r->n] = ev->item;
        r->result[r->n] = ev->result;
        r->n++;
    }
}

static const gates_column_desc_t cols[] = {
    { .id = 7, .label = GATES_STR_INIT("Id"), .width = 60, .min_width = 30 },
    { .id = 8, .label = GATES_STR_INIT("Name"), .width = 140, .min_width = 40 },
    { .id = 9, .label = GATES_STR_INIT("Size"), .width = 90, .min_width = 30 },
};

typedef struct app_t {
    gates_tree_t *t;
    gates_node_t before, view;
    model_t m;
    rec_t rec;
    int defaults;
} app_t;

static void on_default(gates_tree_t *tree, gates_command_id_t id, void *user) {
    (void)tree; (void)id;
    ((app_t *)user)->defaults++;
}

static void make_app(app_t *a, gates_u64 rows, bool table, gates_allocator_t alloc) {
    memset(a, 0, sizeof *a);
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){ .allocator = alloc }, &a->t));
    gates_node_t root = gates_tree_root(a->t);
    GT_ASSERT_OK(gates_layout_set(a->t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_button_create(a->t, root, GATES_STR("before"), nullptr, nullptr, &a->before));
    gates_view_desc_t d = { .columns = table ? cols : nullptr, .column_count = table ? 3 : 0,
                            .header = table };
    GT_ASSERT_OK(gates_view_create(a->t, root, &d, &a->view));
    GT_ASSERT_OK(gates_layout_set_child_grow(a->t, a->view, 1));
    GT_ASSERT_OK(gates_widget_set_handler(a->t, a->view, record, &a->rec));
    gates_command_desc_t def = { .id = 1, .label = GATES_STR("OK"), .role = GATES_COMMAND_DEFAULT,
                                 .enabled = true, .invoke = on_default, .user = a };
    GT_ASSERT_OK(gates_command_register(a->t, root, &def));
    if (rows != 0) array_model(&a->m, rows);
    gates_rows_model_t mb = bind(&a->m);
    GT_ASSERT_OK(gates_view_set_model(a->t, a->view, &mb));
    layout(a->t);
}

static void free_app(app_t *a) {
    gates_tree_destroy(a->t);
    free(a->m.ids);
}

static void paint(gates_tree_t *t) {
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    gates_draw_list_deinit(&dl);
}

/* -- bounded work over 100 000 rows ------------------------------------------------------ */

static void test_bounded(void) {
    app_t a;
    make_app(&a, 100000, true, (gates_allocator_t){0});
    gates_u32 cap = gates_tree_capacity(a.t);
    gates_u32 live = gates_tree_live_count(a.t);
    gates_u32 vis = gates_view_visible_rows(a.t, a.view);
    GT_ASSERT(vis >= 5 && vis <= GATES_VIEW_MAX_ROWS);
    GT_ASSERT(gates_node_kind(a.t, a.view) == GATES_NODE_VIEW);
    reset_counts(&a.m);
    paint(a.t);
    GT_ASSERT(a.m.cells > 0 && a.m.cells <= (int)(vis + 1) * 3);  /* only what is painted */
    GT_ASSERT(a.m.counts <= 1 && a.m.index_ofs == 0 && a.m.revisions <= 1);
    GT_ASSERT(a.m.id_ats <= (int)vis + 1);
    /* Nothing grows with the data set. */
    GT_ASSERT(gates_tree_capacity(a.t) == cap && gates_tree_live_count(a.t) == live);
    GT_ASSERT(gates_view_first_row(a.t, a.view) == 0);
    GT_ASSERT(gates_view_selected(a.t, a.view) == 0);

    /* One Tab stop. */
    gates_tree_set_focus(a.t, a.before);
    GT_ASSERT(gates_tree_focus_next(a.t, false));
    GT_ASSERT(gates_node_eq(gates_tree_focus(a.t), a.view));
    GT_ASSERT(gates_tree_focus_next(a.t, false));
    GT_ASSERT(gates_node_eq(gates_tree_focus(a.t), a.before));
    free_app(&a);
}

/* -- keyboard ----------------------------------------------------------------------------- */

static void test_keys(void) {
    app_t a;
    make_app(&a, 100000, true, (gates_allocator_t){0});
    gates_tree_t *t = a.t;
    gates_u32 vis = gates_view_visible_rows(t, a.view);
    gates_tree_set_focus(t, a.view);
    GT_ASSERT(key(t, GATES_KEY_DOWN) && gates_view_selected(t, a.view) == 1000); /* first row */
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(a.rec.n == 1 && a.rec.kind[0] == GATES_EVENT_SELECTION_CHANGED &&
              a.rec.item[0] == 1000 && a.rec.origin[0] == GATES_ORIGIN_USER);
    GT_ASSERT(key(t, GATES_KEY_DOWN) && gates_view_selected(t, a.view) == 1001);
    GT_ASSERT(key(t, GATES_KEY_UP) && gates_view_selected(t, a.view) == 1000);
    GT_ASSERT(key(t, GATES_KEY_UP) && gates_view_selected(t, a.view) == 1000); /* stops at top */
    GT_ASSERT(key(t, GATES_KEY_PAGE_DOWN) && gates_view_selected(t, a.view) == 1000 + vis);
    GT_ASSERT(gates_view_first_row(t, a.view) + vis > vis);     /* kept in view */
    GT_ASSERT(key(t, GATES_KEY_END) && gates_view_selected(t, a.view) == 1000 + 99999);
    GT_ASSERT(gates_view_first_row(t, a.view) == 100000 - vis);
    GT_ASSERT(key(t, GATES_KEY_PAGE_UP) && gates_view_selected(t, a.view) == 1000 + 99999 - vis);
    GT_ASSERT(key(t, GATES_KEY_HOME) && gates_view_selected(t, a.view) == 1000);
    GT_ASSERT(gates_view_first_row(t, a.view) == 0);
    a.rec.n = 0;
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(a.rec.n == 1 && a.rec.item[0] == 1000);   /* coalesced to the latest */

    /* Enter activates the selected row and is not the dialog's default. */
    a.rec.n = 0;
    GT_ASSERT(key(t, GATES_KEY_ENTER));
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(a.rec.n == 1 && a.rec.kind[0] == GATES_EVENT_ACTIVATED && a.rec.item[0] == 1000);
    GT_ASSERT(a.defaults == 0);
    /* Space does not press a view. */
    GT_ASSERT(!key(t, GATES_KEY_SPACE));

    /* Sideways: the columns are wider than the body. */
    gates_rect_t body = gates_view_part_rect(t, a.view, GATES_VIEW_PART_BODY, 0);
    GT_ASSERT_OK(gates_view_set_column_width(t, a.view, 8, 500));
    layout(t);
    GT_ASSERT(key(t, GATES_KEY_RIGHT) && gates_view_scroll_x(t, a.view) > 0);
    for (int i = 0; i < 200; i++) (void)key(t, GATES_KEY_RIGHT);
    GT_ASSERT(gates_view_scroll_x(t, a.view) == 60 + 500 + 90 - body.w); /* clamped */
    reset_counts(&a.m);
    paint(t);
    GT_ASSERT((a.m.columns_asked & (1u << 7)) == 0);    /* "Id" is scrolled out: not asked */
    GT_ASSERT((a.m.columns_asked & (1u << 9)) != 0);
    GT_ASSERT(!gates_rect_is_empty(gates_view_part_rect(t, a.view, GATES_VIEW_PART_HTHUMB, 0)));
    GT_ASSERT(key(t, GATES_KEY_LEFT));
    GT_ASSERT(gates_view_scroll_x(t, a.view) < 60 + 500 + 90 - body.w);
    free_app(&a);
}

/* -- model changes: selection follows its id --------------------------------------------- */

static void test_model_changes(void) {
    app_t a;
    make_app(&a, 200, true, (gates_allocator_t){0});
    gates_tree_t *t = a.t;
    GT_ASSERT_OK(gates_view_set_selected(t, a.view, 1050));
    GT_ASSERT(gates_view_selected(t, a.view) == 1050);
    GT_ASSERT(gates_view_set_selected(t, a.view, 5) == PROVEN_ERR_INVALID_ARG);
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(a.rec.n == 0);                                /* setters are silent */

    /* Insert ten rows at the front: the same item stays selected. */
    memmove(a.m.ids + 10, a.m.ids, a.m.n * sizeof *a.m.ids);
    for (int i = 0; i < 10; i++) a.m.ids[i] = 5000 + (gates_u64)i;
    a.m.n += 10; a.m.rev++;
    GT_ASSERT_OK(gates_view_model_changed(t, a.view));
    GT_ASSERT(gates_view_selected(t, a.view) == 1050);
    /* Reverse the order: still the same item, and Down moves from its new row. */
    for (gates_u64 i = 0; i < a.m.n / 2; i++) {
        gates_item_id_t x = a.m.ids[i]; a.m.ids[i] = a.m.ids[a.m.n - 1 - i]; a.m.ids[a.m.n - 1 - i] = x;
    }
    a.m.rev++;
    GT_ASSERT_OK(gates_view_model_changed(t, a.view));
    GT_ASSERT(gates_view_selected(t, a.view) == 1050);
    gates_tree_set_focus(t, a.view);
    GT_ASSERT(key(t, GATES_KEY_DOWN) && gates_view_selected(t, a.view) == 1049);
    (void)gates_tree_dispatch_events(t, 0);
    a.rec.n = 0;

    /* Remove the selected item: the item now nearest its old row is selected,
     * announced with origin PROGRAM. */
    gates_u64 row = 0;
    GT_ASSERT(m_index_of(&a.m, 1049, &row));
    memmove(a.m.ids + row, a.m.ids + row + 1, (a.m.n - row - 1) * sizeof *a.m.ids);
    a.m.n--; a.m.rev++;
    GT_ASSERT_OK(gates_view_model_changed(t, a.view));
    GT_ASSERT(gates_view_selected(t, a.view) == a.m.ids[row]);
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(a.rec.n == 1 && a.rec.kind[0] == GATES_EVENT_SELECTION_CHANGED &&
              a.rec.origin[0] == GATES_ORIGIN_PROGRAM && a.rec.item[0] == a.m.ids[row]);

    /* Shrinking under a deep scroll position clamps it. */
    GT_ASSERT(key(t, GATES_KEY_END));
    GT_ASSERT(gates_view_first_row(t, a.view) > 0);
    a.m.n = 3; a.m.rev++;
    GT_ASSERT_OK(gates_view_model_changed(t, a.view));
    GT_ASSERT(gates_view_first_row(t, a.view) == 0);
    GT_ASSERT(gates_view_selected(t, a.view) == a.m.ids[2]);   /* nearest remaining row */
    /* Everything gone: nothing selected. */
    a.m.n = 0; a.m.rev++;
    GT_ASSERT_OK(gates_view_model_changed(t, a.view));
    GT_ASSERT(gates_view_selected(t, a.view) == 0);
    GT_ASSERT(key(t, GATES_KEY_DOWN) && gates_view_selected(t, a.view) == 0);
    paint(t);
    free_app(&a);
}

/* -- pointer, header, resize, wheel -------------------------------------------------------- */

static void test_pointer(void) {
    app_t a;
    make_app(&a, 1000, true, (gates_allocator_t){0});
    gates_tree_t *t = a.t;
    gates_tree_set_focus(t, a.before);
    click(t, mid(gates_view_part_rect(t, a.view, GATES_VIEW_PART_ROW, 3)), 1);
    GT_ASSERT(gates_view_selected(t, a.view) == 1003);
    GT_ASSERT(gates_node_eq(gates_tree_focus(t), a.view));
    GT_ASSERT(gates_rect_is_empty(gates_view_part_rect(t, a.view, GATES_VIEW_PART_ROW, 900)));
    (void)gates_tree_dispatch_events(t, 0);
    a.rec.n = 0;
    /* A double click on a row activates it. */
    click(t, mid(gates_view_part_rect(t, a.view, GATES_VIEW_PART_ROW, 4)), 1);
    click(t, mid(gates_view_part_rect(t, a.view, GATES_VIEW_PART_ROW, 4)), 2);
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(a.rec.n == 2 && a.rec.kind[1] == GATES_EVENT_ACTIVATED && a.rec.item[1] == 1004);
    GT_ASSERT(a.defaults == 0);

    /* Header: press and release on the same column asks for sorting by it. */
    a.rec.n = 0;
    gates_rect_t h1 = gates_view_part_rect(t, a.view, GATES_VIEW_PART_HEADER, 1);
    gates_rect_t h2 = gates_view_part_rect(t, a.view, GATES_VIEW_PART_HEADER, 2);
    GT_ASSERT(h1.w == 140 && h2.x == h1.x + h1.w);
    click(t, mid(h1), 1);
    click(t, mid(h1), 1);
    pointer(t, GATES_POINTER_DOWN, mid(h1), 1);
    pointer(t, GATES_POINTER_UP, mid(h2), 1);            /* released elsewhere: nothing */
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(a.rec.n == 2 && a.rec.kind[0] == GATES_EVENT_SORT_REQUESTED && a.rec.result[0] == 8 &&
              a.rec.kind[1] == GATES_EVENT_SORT_REQUESTED);  /* two requests, not coalesced */
    GT_ASSERT(gates_view_selected(t, a.view) == 1004);        /* the header selects nothing */

    /* Dragging the right edge of "Name" resizes it, never below its minimum. */
    gates_point_t edge = { h1.x + h1.w - 1, h1.y + h1.h / 2 };
    pointer(t, GATES_POINTER_DOWN, edge, 1);
    pointer(t, GATES_POINTER_MOVE, (gates_point_t){ edge.x + 40, edge.y }, 1);
    pointer(t, GATES_POINTER_UP, (gates_point_t){ edge.x + 40, edge.y }, 1);
    GT_ASSERT(gates_view_column_width(t, a.view, 8) == 180);
    pointer(t, GATES_POINTER_DOWN, (gates_point_t){ edge.x + 40, edge.y }, 1);
    pointer(t, GATES_POINTER_MOVE, (gates_point_t){ edge.x - 400, edge.y }, 1);
    pointer(t, GATES_POINTER_UP, (gates_point_t){ edge.x - 400, edge.y }, 1);
    GT_ASSERT(gates_view_column_width(t, a.view, 8) == 40);
    a.rec.n = 0;
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(a.rec.n == 0);                                    /* a resize is not a sort */
    GT_ASSERT(gates_view_set_column_width(t, a.view, 8, 1) == GATES_OK);
    GT_ASSERT(gates_view_column_width(t, a.view, 8) == 40);
    GT_ASSERT(gates_view_set_column_width(t, a.view, 99, 50) == PROVEN_ERR_INVALID_ARG);

    /* Wheel: three rows per notch; Shift+wheel arrives as x and scrolls sideways. */
    gates_rect_t body = gates_view_part_rect(t, a.view, GATES_VIEW_PART_BODY, 0);
    wheel(t, mid(body), 0.0f, -1.0f);
    GT_ASSERT(gates_view_first_row(t, a.view) == 3);
    wheel(t, mid(body), 0.0f, 1.0f);
    GT_ASSERT(gates_view_first_row(t, a.view) == 0);
    GT_ASSERT_OK(gates_view_set_column_width(t, a.view, 8, 600));
    layout(t);
    wheel(t, mid(body), 1.0f, 0.0f);
    GT_ASSERT(gates_view_scroll_x(t, a.view) > 0);

    /* The thumb drags through the rows. */
    gates_rect_t th = gates_view_part_rect(t, a.view, GATES_VIEW_PART_VTHUMB, 0);
    GT_ASSERT(!gates_rect_is_empty(th));
    pointer(t, GATES_POINTER_DOWN, mid(th), 1);
    pointer(t, GATES_POINTER_MOVE, (gates_point_t){ mid(th).x, mid(th).y + 2000 }, 1);
    pointer(t, GATES_POINTER_UP, (gates_point_t){ mid(th).x, mid(th).y + 2000 }, 1);
    GT_ASSERT(gates_view_first_row(t, a.view) == 1000 - gates_view_visible_rows(t, a.view));
    free_app(&a);
}

/* -- 2^40 rows: the arithmetic never narrows ----------------------------------------------- */

static void test_huge(void) {
    app_t a;
    make_app(&a, 0, true, (gates_allocator_t){0});
    gates_tree_t *t = a.t;
    a.m.ids = nullptr;
    a.m.n = 1ull << 40;
    a.m.rev = 1;
    GT_ASSERT_OK(gates_view_model_changed(t, a.view));
    layout(t);
    gates_u32 vis = gates_view_visible_rows(t, a.view);
    gates_u64 max_first = a.m.n - vis;
    gates_tree_set_focus(t, a.view);
    GT_ASSERT(key(t, GATES_KEY_END));
    GT_ASSERT(gates_view_selected(t, a.view) == a.m.n);
    GT_ASSERT(gates_view_first_row(t, a.view) == max_first);
    GT_ASSERT(key(t, GATES_KEY_PAGE_UP) && gates_view_selected(t, a.view) == a.m.n - vis);
    reset_counts(&a.m);
    paint(t);
    GT_ASSERT(a.m.cells <= (int)(vis + 1) * 3 && a.m.index_ofs == 0);

    /* The thumb is at the bottom; dragging it to the top and back is exact at the ends
     * and monotonic in between. */
    gates_rect_t th = gates_view_part_rect(t, a.view, GATES_VIEW_PART_VTHUMB, 0);
    gates_rect_t body = gates_view_part_rect(t, a.view, GATES_VIEW_PART_BODY, 0);
    GT_ASSERT(th.y + th.h == body.y + body.h);                /* at the end: thumb at the bottom */
    gates_point_t at = mid(th);
    pointer(t, GATES_POINTER_DOWN, at, 1);
    pointer(t, GATES_POINTER_MOVE, (gates_point_t){ at.x, at.y - 5000 }, 1);
    GT_ASSERT(gates_view_first_row(t, a.view) == 0);
    gates_u64 prev = 0;
    bool mono = true;
    for (int dy = -300; dy <= 0; dy += 7) {  /* the drag started with the thumb at the bottom */
        pointer(t, GATES_POINTER_MOVE, (gates_point_t){ at.x, at.y + dy }, 1);
        gates_u64 f = gates_view_first_row(t, a.view);
        if (f < prev) mono = false;
        prev = f;
    }
    GT_ASSERT(mono && prev > (1ull << 30));   /* far beyond any 32-bit row number */
    pointer(t, GATES_POINTER_MOVE, (gates_point_t){ at.x, at.y + 5000 }, 1);
    pointer(t, GATES_POINTER_UP, (gates_point_t){ at.x, at.y + 5000 }, 1);
    GT_ASSERT(gates_view_first_row(t, a.view) == max_first);
    GT_ASSERT_OK(gates_view_scroll_to(t, a.view, 1ull << 39));
    GT_ASSERT(gates_view_first_row(t, a.view) <= (1ull << 39) - 1 &&
              gates_view_first_row(t, a.view) + vis > (1ull << 39) - 1);
    /* Half way through the rows: the thumb is half way down its travel. */
    th = gates_view_part_rect(t, a.view, GATES_VIEW_PART_VTHUMB, 0);
    gates_i32 travel = body.h - th.h;
    gates_i32 off = th.y - body.y;
    GT_ASSERT(off >= travel / 2 - 1 && off <= travel / 2 + 1);
    a.m.ids = nullptr;
    free_app(&a);
}

/* -- list mode, detach, reentrancy ----------------------------------------------------------- */

typedef struct detach_t {
    gates_node_t view;
    int calls;
} detach_t;

static void detach_on_select(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    detach_t *d = user;
    d->calls++;
    if (ev->kind == GATES_EVENT_SELECTION_CHANGED) {
        (void)gates_view_set_model(tree, d->view, nullptr);
    }
}

static void test_list_and_detach(void) {
    app_t a;
    make_app(&a, 50, false, (gates_allocator_t){0});
    gates_tree_t *t = a.t;
    reset_counts(&a.m);
    paint(t);
    GT_ASSERT(a.m.cells > 0 && a.m.last_column == 0);          /* one implicit column */
    GT_ASSERT(gates_rect_is_empty(gates_view_part_rect(t, a.view, GATES_VIEW_PART_HEADER, 0)));
    gates_rect_t row0 = gates_view_part_rect(t, a.view, GATES_VIEW_PART_ROW, 0);
    gates_rect_t body = gates_view_part_rect(t, a.view, GATES_VIEW_PART_BODY, 0);
    GT_ASSERT(row0.w == body.w && row0.y == body.y);

    /* Detaching from inside a handler: no model call afterwards. */
    detach_t d = { .view = a.view };
    GT_ASSERT_OK(gates_widget_set_handler(t, a.view, detach_on_select, &d));
    gates_tree_set_focus(t, a.view);
    GT_ASSERT(key(t, GATES_KEY_DOWN));
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(d.calls == 1);
    reset_counts(&a.m);
    paint(t);
    (void)key(t, GATES_KEY_DOWN);
    click(t, mid(row0), 2);
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(a.m.cells == 0 && a.m.counts == 0 && a.m.id_ats == 0 && a.m.index_ofs == 0);
    GT_ASSERT(gates_view_selected(t, a.view) == 0);

    /* Bad descriptors are refused and leave nothing behind. */
    gates_u32 live = gates_tree_live_count(t);
    static const gates_column_desc_t dup[] = { { .id = 1 }, { .id = 1 } };
    static const gates_column_desc_t zero[] = { { .id = 0 } };
    gates_view_desc_t bad = { .columns = dup, .column_count = 2 };
    gates_node_t v = GATES_NODE_NULL;
    GT_ASSERT(gates_view_create(t, gates_tree_root(t), &bad, &v) == PROVEN_ERR_INVALID_ARG);
    bad = (gates_view_desc_t){ .columns = zero, .column_count = 1 };
    GT_ASSERT(gates_view_create(t, gates_tree_root(t), &bad, &v) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_tree_live_count(t) == live);
    GT_ASSERT(gates_view_set_selected(t, a.before, 1) == PROVEN_ERR_INVALID_ARG);
    free_app(&a);
}

/* -- allocation failure ------------------------------------------------------------------------ */

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

static void test_allocation_failure(void) {
    fail_alloc_t fa = { .inner = proven_heap_allocator() };
    gates_allocator_t alloc = { .ctx = &fa, .alloc_fn = fa_alloc, .realloc_fn = fa_realloc,
                                .free_fn = fa_free };
    app_t a;
    make_app(&a, 100, true, alloc);
    gates_tree_t *t = a.t;
    gates_u32 live = gates_tree_live_count(t);
    fa.fail = true;
    gates_view_desc_t d = { .columns = cols, .column_count = 3, .header = true };
    gates_node_t v = GATES_NODE_NULL;
    GT_ASSERT(gates_view_create(t, gates_tree_root(t), &d, &v) == PROVEN_ERR_NOMEM);
    /* A selection that cannot be announced does not happen. */
    gates_tree_set_focus(t, a.view);
    (void)key(t, GATES_KEY_DOWN);
    GT_ASSERT(gates_view_selected(t, a.view) == 0);
    GT_ASSERT(gates_input_take_error(t) == PROVEN_ERR_NOMEM);
    fa.fail = false;
    GT_ASSERT_OK(gates_tree_flush_destroys(t));
    GT_ASSERT(gates_tree_live_count(t) == live);

    /* A frame whose draw list cannot grow stops asking the model at once. */
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, alloc, 0));
    fa.fail = true;
    reset_counts(&a.m);
    GT_ASSERT(gates_paint_tree(t, &dl, theme, be) == PROVEN_ERR_NOMEM);
    GT_ASSERT(a.m.cells <= 1);
    fa.fail = false;
    gates_draw_list_deinit(&dl);
    free_app(&a);
}

int main(void) {
    be = gates_text_backend_builtin();
    theme = gates_theme_light();
    test_bounded();
    test_keys();
    test_model_changes();
    test_pointer();
    test_huge();
    test_list_and_detach();
    test_allocation_failure();
    return gt_report("test_view");
}
