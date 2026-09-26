# Chapter 5 - Views over your data

Header: `gates/view.h`.

## One node for any number of rows

A list with a million rows is still one node. The rows belong to the application: it describes
them through a model - how many there are, the id at a position, the position of an id, the
text of a cell - and gates asks only for the rows it shows (at most `GATES_VIEW_MAX_ROWS` at a
time). Nothing is copied into gates, and scrolling through a million rows costs what scrolling
through a screenful costs.

Rows have ids (`gates_item_id_t`, nonzero, stable): the selection is an id, not a position, so
it stays on the same row when rows are inserted above it, removed elsewhere, or sorted. After
the application changes its rows it calls `gates_view_model_changed`; when the selected row is
gone, the selection moves to the nearest remaining row and a SELECTION_CHANGED with origin
PROGRAM says so.

<!-- example: manual/examples/ex_06_view.c -->
```c
/* manual example (host): a list over the application's own rows.
 * expect: selected 3 (Cherry); after removing it: 4 (Damson), origin program */
#include <gates/gates.h>

#include <stdio.h>
#include <string.h>

typedef struct row_t {
    gates_item_id_t id;          /* stable: survives inserts, removals, sorting */
    const char *name;
} row_t;

typedef struct fruits_t {
    row_t rows[8];
    gates_u64 n;
} fruits_t;

/* The model: gates asks only for the rows it shows, by position and by id. */
static gates_u64 f_count(void *u) { return ((fruits_t *)u)->n; }
static gates_item_id_t f_id_at(void *u, gates_u64 row) { return ((fruits_t *)u)->rows[row].id; }
static bool f_index_of(void *u, gates_item_id_t id, gates_u64 *row) {
    fruits_t *f = u;
    for (gates_u64 i = 0; i < f->n; i++) {
        if (f->rows[i].id == id) { *row = i; return true; }
    }
    return false;
}
static gates_err_t f_cell(void *u, gates_item_id_t id, gates_column_id_t col, gates_cell_t *out) {
    (void)col;
    gates_u64 row;
    if (!f_index_of(u, id, &row)) return PROVEN_ERR_INVALID_ARG;
    const char *s = ((fruits_t *)u)->rows[row].name;
    out->text = (gates_str_t){ .ptr = (const gates_u8 *)s, .size = strlen(s) };
    return GATES_OK;
}

static gates_item_id_t last_id;
static gates_event_origin_t last_origin;

static void on_view(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    (void)user;
    if (ev->kind == GATES_EVENT_SELECTION_CHANGED) {
        last_id = ev->item;
        last_origin = ev->origin;
    }
}

static const char *name_of(fruits_t *f, gates_item_id_t id) {
    gates_u64 row;
    return f_index_of(f, id, &row) ? f->rows[row].name : "none";
}

int main(void) {
    fruits_t f = { .rows = { { 1, "Apple" }, { 2, "Banana" }, { 3, "Cherry" }, { 4, "Damson" }, { 5, "Elder" } }, .n = 5 };
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_node_t list;
    gates_rows_model_t model = { .user = &f, .count = f_count, .id_at = f_id_at, .index_of = f_index_of, .cell = f_cell };
    if (!gates_is_ok(gates_view_create(t, gates_tree_root(t), &(gates_view_desc_t){0}, &list)) ||
        !gates_is_ok(gates_view_set_model(t, list, &model)) ||
        !gates_is_ok(gates_widget_set_handler(t, list, on_view, nullptr)) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 200, 160 }, gates_text_backend_builtin()))) {
        return 1;
    }
    /* A person picks the third row. */
    if (!gates_is_ok(gates_access_select(t, list, 3))) return 1;
    (void)gates_tree_dispatch_events(t, 0);
    printf("selected %llu (%s); ", (unsigned long long)last_id, name_of(&f, last_id));

    /* The application removes that row, then tells the view its rows changed:
     * the selection moves to the nearest remaining row and says so. */
    memmove(&f.rows[2], &f.rows[3], 2 * sizeof f.rows[0]);
    f.n = 4;
    if (!gates_is_ok(gates_view_model_changed(t, list))) return 1;
    (void)gates_tree_dispatch_events(t, 0);
    printf("after removing it: %llu (%s), origin %s\n", (unsigned long long)last_id, name_of(&f, last_id),
           last_origin == GATES_ORIGIN_PROGRAM ? "program" : "user");
    gates_tree_destroy(t);
    return 0;
}
```

## Lists, tables, trees and logs

- A list has no columns; the cell asked for is column 0.
- A table has columns (`gates_column_desc_t`: id, label, width, minimum) and may show a header.
  Clicking a header cell sends SORT_REQUESTED with the column id: the application sorts its
  rows and calls `gates_view_model_changed`. Dragging a header edge resizes a column.
- A tree is a list whose model also answers `row_info`: depth, whether the row can open, whether
  it is open, a loading or error state. Opening and closing are requests (EXPAND_REQUESTED):
  the application changes its flattened rows and calls `gates_view_model_changed`. gates never
  walks rows it does not show.
- A log (`gates_log_create`) owns its lines: append text, keep a line and byte limit, drop the
  oldest, and follow the end while the person has not scrolled away.

## Rules for models

Model callbacks run on the UI thread, never during event dispatch or inside another callback,
and never after `gates_view_set_model(view, nullptr)` returns. A cell's text is borrowed until
the next call into the model. The model must not change between `gates_view_model_changed` calls
without telling the view.
