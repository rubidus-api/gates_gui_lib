/* ctl_list - the list view over a large model.
 *
 * Interaction: a list shows rows from a model the application owns, however
 * many there are; only the rows on screen are ever asked for. The person
 * moves the selection with the arrows, PageUp/PageDown, Home/End or a click,
 * and activates a row with Enter or a double click. The application names
 * rows by stable ids and hears GATES_EVENT_SELECTION_CHANGED and
 * GATES_EVENT_ACTIVATED with ev->item = the id.
 *
 * Shows: 1 000 000 generated rows (the program stores none of them), a line
 * with the selected and the last activated row, a count of how many cells the
 * view asked for in its last paint, and a "go to row 500000" button that
 * selects and reveals a row from code.
 *
 * Field check: the list scrolls with the wheel and the thumb; Down,
 * PageDown, End, Home move the selection and keep it in view; a click selects;
 * Enter or a double click reports "activated"; "go to row 500000" jumps there
 * and selects it; the cells-per-paint number stays small whatever the
 * position; ESC exits. */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/view.h>
#include <gates/ui.h>
#include <gates/access.h>

#include <stdio.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)
#define ROWS 1000000ull

typedef struct demo_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t list, status, jump;
    gates_item_id_t activated;
    unsigned cells;              /* cells asked for since the last status update */
    char buf[48];
} demo_t;

/* The model: row r has id r + 1 and the text "line <r+1>". Nothing is stored. */
static gates_u64 m_count(void *u) { (void)u; return ROWS; }
static gates_item_id_t m_id_at(void *u, gates_u64 row) { (void)u; return row < ROWS ? row + 1 : 0; }
static bool m_index_of(void *u, gates_item_id_t id, gates_u64 *row) {
    (void)u;
    if (id == 0 || id > ROWS) return false;
    *row = id - 1;
    return true;
}
static gates_err_t m_cell(void *u, gates_item_id_t id, gates_column_id_t col, gates_cell_t *out) {
    demo_t *d = u;
    (void)col;
    d->cells++;
    int n = snprintf(d->buf, sizeof d->buf, "line %llu", (unsigned long long)id);
    out->text = (gates_str_t){ .ptr = (const gates_u8 *)d->buf, .size = (gates_usize_t)n };
    return GATES_OK;
}

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    demo_t *d = user;
    if (ev->down && ev->key == GATES_KEY_ESCAPE) {
        gates_app_quit(d->app);
    }
}

static void update(demo_t *d) {
    char buf[160];
    int n = snprintf(buf, sizeof buf, "selected: %llu   activated: %llu   cells asked: %u",
                     (unsigned long long)gates_view_selected(d->tree, d->list),
                     (unsigned long long)d->activated, d->cells);
    d->cells = 0;
    if (n > 0) {
        (void)gates_widget_set_text(d->tree, d->status,
                                    (gates_str_t){ .ptr = (const gates_u8 *)buf,
                                                   .size = (gates_usize_t)n });
    }
}

static void on_list(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    demo_t *d = user;
    if (ev->kind == GATES_EVENT_ACTIVATED) {
        d->activated = ev->item;
    }
    update(d);
}

static void on_jump(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)ev;
    demo_t *d = user;
    if (gates_is_ok(gates_view_set_selected(tree, d->list, 500000)) &&
        gates_is_ok(gates_view_scroll_to(tree, d->list, 500000))) {
        gates_tree_set_focus(tree, d->list);
        update(d);
    }
}

static gates_err_t build_ui(demo_t *d) {
    gates_tree_t *t = d->tree;
    gates_node_t root = gates_tree_root(t);
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 8));
    gates_node_t n = GATES_NODE_NULL;
    TRY(gates_label_create(t, root, GATES_STR("List: one million rows, none stored"), &n));
    TRY(gates_view_create(t, root, &(gates_view_desc_t){0}, &d->list));
    TRY(gates_node_set_access_name(t, d->list, GATES_STR("Rows")));
    TRY(gates_layout_set_child_grow(t, d->list, 1));
    gates_rows_model_t model = { .user = d, .count = m_count, .id_at = m_id_at,
                                 .index_of = m_index_of, .cell = m_cell };
    TRY(gates_view_set_model(t, d->list, &model));
    TRY(gates_widget_set_handler(t, d->list, on_list, d));
    TRY(gates_button_create(t, root, GATES_STR("go to row 500000"), nullptr, nullptr, &d->jump));
    TRY(gates_layout_set_child_align(t, d->jump, GATES_ALIGN_START_V));
    TRY(gates_widget_set_handler(t, d->jump, on_jump, d));
    TRY(gates_label_create(t, root, GATES_STR(""), &d->status));
    TRY(gates_label_create(t, root, GATES_STR("ESC exits"), &n));
    update(d);
    gates_tree_set_focus(t, d->list);
    return GATES_OK;
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) {
        return 1;
    }
    demo_t d = { .app = app };
    gates_window_callbacks_t cb = { .on_key = on_key, .user_data = &d };
    gates_window_desc_t desc = { .title = GATES_STR("gates: list"), .size = { 480, 420 } };
    gates_window_t *win = nullptr;
    if (!gates_is_ok(gates_window_create(app, &desc, &cb, &win))) {
        gates_app_destroy(app);
        return 1;
    }
    d.tree = gates_window_tree(win);
    gates_err_t err = build_ui(&d);
    if (!gates_is_ok(err)) {
        fprintf(stderr, "ctl_list: building the window failed (%d)\n", (int)err);
    } else {
        gates_window_request_repaint(win);
        err = gates_app_run(app);
    }
    gates_window_destroy(win);
    gates_app_destroy(app);
    return gates_is_ok(err) ? 0 : 1;
}
