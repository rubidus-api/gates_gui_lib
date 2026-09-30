/* ctl_table - the table view: columns, header sorting, column resizing.
 *
 * Interaction: a table is a list with named columns and a header. Clicking a
 * header cell asks the application to sort (GATES_EVENT_SORT_REQUESTED with
 * ev->result = the column id); the model sorts and the view is told with
 * gates_view_model_changed. The selection is an item id, so the selected file
 * stays selected wherever sorting moves it. Dragging a header cell's right
 * edge resizes the column, never below its minimum; wide tables scroll
 * sideways (Left/Right, Shift+wheel, the lower scrollbar).
 *
 * Shows: 20 000 generated "files" with name, size and type; the sort order
 * and the selected file in a status line.
 *
 * Field check: select a file, click "Size": rows sort by size and the
 * same file is still selected and scrolled into view (the status line names
 * it); click "Size" again: descending; drag the right edge of "Name" wider and
 * narrower (it stops at its minimum); Left/Right scroll sideways once the
 * columns are wider than the window; ESC exits. */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/view.h>
#include <gates/ui.h>
#include <gates/access.h>

#include <stdio.h>
#include <stdlib.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)
#define FILES 20000u

enum { COL_NAME = 1, COL_SIZE, COL_TYPE };

typedef struct demo_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t table, status;
    gates_u32 order[FILES];      /* row -> file index; file id = index + 1 */
    gates_u32 row_of[FILES];     /* file index -> row, kept with the order */
    gates_column_id_t sort_col;
    bool descending;
    char buf[48];
} demo_t;

static const char *const types[] = { "text", "image", "audio", "archive", "source" };

static gates_u64 file_size(gates_u32 i) { return (gates_u64)((i * 2654435761u) % 5000000u); }
static const char *file_type(gates_u32 i) { return types[(i * 7u) % 5u]; }

static gates_u64 m_count(void *u) { (void)u; return FILES; }
static gates_item_id_t m_id_at(void *u, gates_u64 row) {
    demo_t *d = u;
    return row < FILES ? (gates_item_id_t)d->order[row] + 1 : 0;
}
static bool m_index_of(void *u, gates_item_id_t id, gates_u64 *row) {
    demo_t *d = u;
    if (id == 0 || id > FILES) return false;
    *row = d->row_of[id - 1];
    return true;
}
static gates_err_t m_cell(void *u, gates_item_id_t id, gates_column_id_t col, gates_cell_t *out) {
    demo_t *d = u;
    gates_u32 i = (gates_u32)(id - 1);
    int n = col == COL_NAME ? snprintf(d->buf, sizeof d->buf, "file-%05u.dat", i)
          : col == COL_SIZE ? snprintf(d->buf, sizeof d->buf, "%llu", (unsigned long long)file_size(i))
                            : snprintf(d->buf, sizeof d->buf, "%s", file_type(i));
    out->text = (gates_str_t){ .ptr = (const gates_u8 *)d->buf, .size = (gates_usize_t)n };
    return GATES_OK;
}

static demo_t *g_sort; /* qsort has no user pointer */

static int compare(const void *a, const void *b) {
    gates_u32 x = *(const gates_u32 *)a, y = *(const gates_u32 *)b;
    int r;
    if (g_sort->sort_col == COL_SIZE) {
        r = file_size(x) < file_size(y) ? -1 : file_size(x) > file_size(y) ? 1 : 0;
    } else if (g_sort->sort_col == COL_TYPE) {
        r = (int)((x * 7u) % 5u) - (int)((y * 7u) % 5u);
    } else {
        r = 0;
    }
    if (r == 0) r = x < y ? -1 : x > y ? 1 : 0; /* stable by name */
    return g_sort->descending ? -r : r;
}

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    demo_t *d = user;
    if (ev->down && ev->key == GATES_KEY_ESCAPE) {
        gates_app_quit(d->app);
    }
}

static void update(demo_t *d) {
    static const char *const names[] = { "", "name", "size", "type" };
    char buf[160];
    gates_item_id_t sel = gates_view_selected(d->tree, d->table);
    int n = snprintf(buf, sizeof buf, "sorted by %s%s   selected: %s",
                     names[d->sort_col], d->descending ? " (descending)" : "",
                     sel != 0 ? "" : "nothing");
    if (sel != 0 && n > 0 && (size_t)n < sizeof buf) {
        n += snprintf(buf + n, sizeof buf - (size_t)n, "file-%05u.dat", (unsigned)(sel - 1));
    }
    if (n > 0) {
        (void)gates_widget_set_text(d->tree, d->status,
                                    (gates_str_t){ .ptr = (const gates_u8 *)buf,
                                                   .size = (gates_usize_t)n });
    }
}

static void on_table(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    demo_t *d = user;
    if (ev->kind == GATES_EVENT_SORT_REQUESTED) {
        /* The model sorts; the view keeps the selection by id. */
        d->descending = d->sort_col == ev->result ? !d->descending : false;
        d->sort_col = ev->result;
        g_sort = d;
        qsort(d->order, FILES, sizeof d->order[0], compare);
        for (gates_u32 r = 0; r < FILES; r++) d->row_of[d->order[r]] = r;
        (void)gates_view_model_changed(tree, d->table);
        gates_item_id_t sel = gates_view_selected(tree, d->table);
        if (sel != 0) {
            (void)gates_view_scroll_to(tree, d->table, sel);
        }
    }
    update(d);
}

static gates_err_t build_ui(demo_t *d) {
    gates_tree_t *t = d->tree;
    gates_node_t root = gates_tree_root(t);
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 8));
    gates_node_t n = GATES_NODE_NULL;
    TRY(gates_label_create(t, root, GATES_STR("Table: click a header to sort, drag its edge to resize"), &n));
    static const gates_column_desc_t cols[] = {
        { .id = COL_NAME, .label = GATES_STR_INIT("Name"), .width = 160, .min_width = 60 },
        { .id = COL_SIZE, .label = GATES_STR_INIT("Size"), .width = 100, .min_width = 50 },
        { .id = COL_TYPE, .label = GATES_STR_INIT("Type"), .width = 90, .min_width = 40 },
    };
    gates_view_desc_t vd = { .columns = cols, .column_count = 3, .header = true };
    TRY(gates_view_create(t, root, &vd, &d->table));
    TRY(gates_node_set_access_name(t, d->table, GATES_STR("Files")));
    TRY(gates_layout_set_child_grow(t, d->table, 1));
    for (gates_u32 i = 0; i < FILES; i++) {
        d->order[i] = i;
        d->row_of[i] = i;
    }
    d->sort_col = COL_NAME;
    gates_rows_model_t model = { .user = d, .count = m_count, .id_at = m_id_at,
                                 .index_of = m_index_of, .cell = m_cell };
    TRY(gates_view_set_model(t, d->table, &model));
    TRY(gates_widget_set_handler(t, d->table, on_table, d));
    TRY(gates_label_create(t, root, GATES_STR(""), &d->status));
    TRY(gates_label_create(t, root, GATES_STR("ESC exits"), &n));
    update(d);
    gates_tree_set_focus(t, d->table);
    return GATES_OK;
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) {
        return 1;
    }
    demo_t *d = calloc(1, sizeof *d);
    if (d == nullptr) {
        gates_app_destroy(app);
        return 1;
    }
    d->app = app;
    gates_window_callbacks_t cb = { .on_key = on_key, .user_data = d };
    gates_window_desc_t desc = { .title = GATES_STR("gates: table"), .size = { 480, 420 } };
    gates_window_t *win = nullptr;
    if (!gates_is_ok(gates_window_create(app, &desc, &cb, &win))) {
        free(d);
        gates_app_destroy(app);
        return 1;
    }
    d->tree = gates_window_tree(win);
    gates_err_t err = build_ui(d);
    if (!gates_is_ok(err)) {
        fprintf(stderr, "ctl_table: building the window failed (%d)\n", (int)err);
    } else {
        gates_window_request_repaint(win);
        err = gates_app_run(app);
    }
    gates_window_destroy(win);
    gates_app_destroy(app);
    free(d);
    return gates_is_ok(err) ? 0 : 1;
}
