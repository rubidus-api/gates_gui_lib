/* manual example (host): a million rows, selected together - the program keeps ranges.
 * expect: 0..999999 after Shift+End; 1..999999 after Ctrl+Space on row 0; 0..999999 after Ctrl+A; copy asked for 1000000 rows */
#include <gates/gates.h>

#include <stdio.h>

#define ROWS 1000000u
#define MAX_RANGES 32

/* The program's selection: sorted, separate ranges of rows. "All" is one range. */
typedef struct sel_t {
    gates_u64 lo[MAX_RANGES], hi[MAX_RANGES];
    int n;
} sel_t;

static void sel_clear(sel_t *s) { s->n = 0; }

static void sel_add(sel_t *s, gates_u64 lo, gates_u64 hi) {
    sel_t out = {0};
    for (int i = 0; i < s->n; i++) {
        if (s->hi[i] + 1 < lo || hi + 1 < s->lo[i]) { /* apart: keep it */
            out.lo[out.n] = s->lo[i];
            out.hi[out.n++] = s->hi[i];
        } else { /* touching: merge into the new range */
            lo = s->lo[i] < lo ? s->lo[i] : lo;
            hi = s->hi[i] > hi ? s->hi[i] : hi;
        }
    }
    int at = 0;
    while (at < out.n && out.lo[at] < lo) at++;
    if (out.n == MAX_RANGES) return; /* a program would say so; the example keeps it short */
    for (int i = out.n; i > at; i--) { out.lo[i] = out.lo[i - 1]; out.hi[i] = out.hi[i - 1]; }
    out.lo[at] = lo;
    out.hi[at] = hi;
    out.n++;
    *s = out;
}

static bool sel_has(const sel_t *s, gates_u64 row) {
    for (int i = 0; i < s->n; i++) {
        if (row >= s->lo[i] && row <= s->hi[i]) return true;
    }
    return false;
}

static void sel_remove(sel_t *s, gates_u64 row) {
    for (int i = 0; i < s->n; i++) {
        if (row < s->lo[i] || row > s->hi[i]) continue;
        gates_u64 lo = s->lo[i], hi = s->hi[i];
        for (int k = i; k + 1 < s->n; k++) { s->lo[k] = s->lo[k + 1]; s->hi[k] = s->hi[k + 1]; }
        s->n--;
        if (row > lo) sel_add(s, lo, row - 1);
        if (row < hi) sel_add(s, row + 1, hi);
        return;
    }
}

/* The model: row r has id r + 1; the selection answers next_selected. */
typedef struct app_t {
    sel_t sel;
    gates_node_t view;
    int copy_asked;
} app_t;

static gates_u64 m_count(void *u) { (void)u; return ROWS; }
static gates_item_id_t m_id_at(void *u, gates_u64 row) { (void)u; return row < ROWS ? row + 1 : 0; }
static bool m_index_of(void *u, gates_item_id_t id, gates_u64 *row) {
    (void)u;
    if (id == 0 || id > ROWS) return false;
    *row = id - 1;
    return true;
}
static gates_err_t m_cell(void *u, gates_item_id_t id, gates_column_id_t col, gates_cell_t *out) {
    (void)u; (void)id; (void)col;
    out->text = GATES_STR("a row");
    return GATES_OK;
}
static gates_u64 m_next_selected(void *u, gates_u64 row) {
    const sel_t *s = &((app_t *)u)->sel;
    for (int i = 0; i < s->n; i++) {
        if (s->hi[i] >= row) return s->lo[i] > row ? s->lo[i] : row;
    }
    return GATES_ROW_NONE;
}

/* A person's gesture is a request: the program changes its ranges, then the view repaints. */
static void on_view(gates_tree_t *t, const gates_event_t *ev, void *user) {
    app_t *a = user;
    if (ev->kind == GATES_EVENT_COPY_REQUESTED) a->copy_asked++;
    if (ev->kind != GATES_EVENT_SELECT_REQUESTED) return;
    gates_u64 target = ev->item - 1, anchor = ev->anchor != 0 ? ev->anchor - 1 : target;
    gates_u64 lo = anchor < target ? anchor : target, hi = anchor < target ? target : anchor;
    switch ((gates_select_request_t)ev->result) {
    case GATES_SELECT_ONE: sel_clear(&a->sel); sel_add(&a->sel, target, target); break;
    case GATES_SELECT_TOGGLE:
        if (sel_has(&a->sel, target)) sel_remove(&a->sel, target);
        else sel_add(&a->sel, target, target);
        break;
    case GATES_SELECT_RANGE: sel_clear(&a->sel); sel_add(&a->sel, lo, hi); break;
    case GATES_SELECT_ADD_RANGE: sel_add(&a->sel, lo, hi); break;
    case GATES_SELECT_ALL: sel_clear(&a->sel); sel_add(&a->sel, 0, ROWS - 1); break;
    }
    (void)gates_view_model_changed(t, ev->source);
}

static void press(gates_tree_t *t, gates_key_t key, bool ctrl, bool shift) {
    gates_key_event_t ev = { .key = key, .ctrl = ctrl, .shift = shift, .down = true };
    (void)gates_input_key(t, &ev);
    (void)gates_tree_dispatch_events(t, 0);
}

static void show(const app_t *a, const char *after, bool last) {
    for (int i = 0; i < a->sel.n; i++) {
        printf("%s%llu..%llu", i > 0 ? "," : "", (unsigned long long)a->sel.lo[i], (unsigned long long)a->sel.hi[i]);
    }
    printf(" after %s%s", after, last ? "" : "; ");
}

static gates_err_t no_clip_get(void *ctx, gates_allocator_t al, gates_u8 **out, gates_usize_t *n) {
    (void)ctx; (void)al;
    *out = nullptr;
    *n = 0;
    return GATES_OK;
}
static gates_err_t no_clip_set(void *ctx, gates_str_t text) { (void)ctx; (void)text; return GATES_OK; }

int main(void) {
    gates_tree_t *t = nullptr;
    static app_t a;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_rows_model_t model = { .user = &a, .count = m_count, .id_at = m_id_at, .index_of = m_index_of,
                                 .cell = m_cell, .next_selected = m_next_selected };
    if (!gates_is_ok(gates_view_create(t, gates_tree_root(t), &(gates_view_desc_t){ .multi_select = true }, &a.view)) ||
        !gates_is_ok(gates_view_set_model(t, a.view, &model)) ||
        !gates_is_ok(gates_widget_set_handler(t, a.view, on_view, &a)) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 300, 200 }, gates_text_backend_builtin()))) {
        return 1;
    }
    gates_tree_set_clipboard(t, &(gates_clipboard_t){ .get_text = no_clip_get, .set_text = no_clip_set });
    gates_tree_set_focus(t, a.view);
    press(t, GATES_KEY_DOWN, false, false);  /* row 0 alone */
    press(t, GATES_KEY_END, false, true);    /* Shift+End: 0 to the last row */
    show(&a, "Shift+End", false);
    press(t, GATES_KEY_HOME, true, false);   /* Ctrl+Home: the focus moves, nothing asked */
    press(t, GATES_KEY_SPACE, true, false);  /* Ctrl+Space: row 0 leaves the selection */
    show(&a, "Ctrl+Space on row 0", false);
    press(t, GATES_KEY_A, true, false);      /* Ctrl+A: all, one range */
    show(&a, "Ctrl+A", false);
    press(t, GATES_KEY_C, true, false);      /* more than GATES_VIEW_COPY_MAX rows: the program is asked */
    printf("copy asked for %u rows\n", a.copy_asked == 1 ? ROWS : 0u);
    gates_tree_destroy(t);
    return 0;
}
