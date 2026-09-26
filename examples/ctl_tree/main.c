/* ctl_tree - the tree view: a model of visible rows that opens on request.
 *
 * Interaction: a tree shows nested items. The model hands the view only the
 * rows that are visible now (a flattened sequence), each with its depth and
 * whether it can open. Opening and closing are requests
 * (GATES_EVENT_EXPAND_REQUESTED): the program inserts or removes the children
 * and calls gates_view_model_changed. Children can arrive later: until then a
 * "loading" row is shown dimmed; a failure is an explicit error row.
 * Right opens a closed row or moves to its first child; Left closes an open
 * row or moves to its parent; a click on the mark opens or closes.
 *
 * Shows: a generated folder tree (children are made only when a folder is
 * opened), a "slow" folder whose children arrive when "finish loading" is
 * pressed, a "locked" folder that answers with an error row, and a status line.
 *
 * Field check (T032 stage 2): Down selects "folder 1", Right opens it, Right
 * again moves to its first child, Left goes back to the parent, Left closes
 * it; clicking a mark opens a folder without selecting it; opening "slow"
 * shows a dimmed "loading..." row until "finish loading"; opening "locked"
 * shows a red "access denied" row; the selection stays on its item while
 * folders above it open and close; ESC exits. */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/view.h>
#include <gates/ui.h>
#include <gates/access.h>

#include <stdio.h>
#include <string.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)
#define MAX_ROWS 8192
#define SLOW 4u                  /* top-level folder 4 loads slowly */
#define LOCKED 5u                /* top-level folder 5 cannot be read */
#define LOADING_CHILD 15u        /* child index of a "loading..." row */
#define ERROR_CHILD 14u          /* child index of an "access denied" row */

/* A node's id is its path: 4 bits per level (child numbers 1..15). */
static gates_u32 depth_of(gates_item_id_t id) {
    gates_u32 d = 0;
    while (id >= 16) { id >>= 4; d++; }
    return d;
}
static gates_item_id_t parent_of(gates_item_id_t id) { return id >> 4; }
static gates_u32 child_no(gates_item_id_t id) { return (gates_u32)(id & 15u); }
/* Every top-level item is a folder; below it children 1..4 are folders (down to
 * depth 2), 5..7 are files, 14 and 15 are the error and loading rows. */
static bool is_folder(gates_item_id_t id) {
    return depth_of(id) == 0 || (child_no(id) <= 4 && depth_of(id) < 3);
}

typedef struct row_t {
    gates_item_id_t id;
    bool expanded;
} row_t;

typedef struct demo_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t view, status, finish;
    row_t rows[MAX_ROWS];
    gates_u64 n;
    bool slow_pending;           /* folder 4 was opened and is still "loading" */
    char buf[64];
} demo_t;

static bool find_row(demo_t *d, gates_item_id_t id, gates_u64 *row) {
    for (gates_u64 i = 0; i < d->n; i++) {
        if (d->rows[i].id == id) { *row = i; return true; }
    }
    return false;
}

/* -- the model ------------------------------------------------------------------------- */

static gates_u64 m_count(void *u) { return ((demo_t *)u)->n; }
static gates_item_id_t m_id_at(void *u, gates_u64 row) {
    demo_t *d = u;
    return row < d->n ? d->rows[row].id : 0;
}
static bool m_index_of(void *u, gates_item_id_t id, gates_u64 *row) { return find_row(u, id, row); }

static gates_err_t m_cell(void *u, gates_item_id_t id, gates_column_id_t col, gates_cell_t *out) {
    demo_t *d = u;
    (void)col;
    int n;
    gates_u32 c = child_no(id);
    if (depth_of(id) == 0) {
        n = id == SLOW ? snprintf(d->buf, sizeof d->buf, "slow")
          : id == LOCKED ? snprintf(d->buf, sizeof d->buf, "locked")
                         : snprintf(d->buf, sizeof d->buf, "folder %llu", (unsigned long long)id);
    } else if (c == LOADING_CHILD) {
        n = snprintf(d->buf, sizeof d->buf, "loading...");
    } else if (c == ERROR_CHILD) {
        n = snprintf(d->buf, sizeof d->buf, "access denied");
    } else if (is_folder(id)) {
        n = snprintf(d->buf, sizeof d->buf, "folder %llx", (unsigned long long)id);
    } else {
        n = snprintf(d->buf, sizeof d->buf, "file %llx.txt", (unsigned long long)id);
    }
    out->text = (gates_str_t){ .ptr = (const gates_u8 *)d->buf, .size = (gates_usize_t)n };
    return GATES_OK;
}

static gates_err_t m_info(void *u, gates_item_id_t id, gates_row_info_t *out) {
    demo_t *d = u;
    gates_u64 row = 0;
    bool open = find_row(d, id, &row) && d->rows[row].expanded;
    gates_u32 c = child_no(id);
    *out = (gates_row_info_t){
        .depth = depth_of(id),
        .expandable = is_folder(id),
        .expanded = open,
        .parent = parent_of(id),
        .state = depth_of(id) > 0 && c == LOADING_CHILD ? GATES_ROW_LOADING
               : depth_of(id) > 0 && c == ERROR_CHILD   ? GATES_ROW_ERROR
                                                        : GATES_ROW_NORMAL,
    };
    return GATES_OK;
}

/* -- the program's side ----------------------------------------------------------------- */

static void insert_rows(demo_t *d, gates_u64 at, const gates_item_id_t *ids, gates_u32 count) {
    if (d->n + count > MAX_ROWS) return;
    memmove(&d->rows[at + count], &d->rows[at], (size_t)(d->n - at) * sizeof d->rows[0]);
    for (gates_u32 i = 0; i < count; i++) d->rows[at + i] = (row_t){ .id = ids[i] };
    d->n += count;
}

static void open_folder(demo_t *d, gates_u64 row) {
    gates_item_id_t id = d->rows[row].id;
    gates_item_id_t kids[8];
    gates_u32 k = 0;
    if (id == SLOW) {
        kids[k++] = id * 16 + LOADING_CHILD;
        d->slow_pending = true;
    } else if (id == LOCKED) {
        kids[k++] = id * 16 + ERROR_CHILD;
    } else {
        for (gates_u32 c = 1; c <= 7; c++) kids[k++] = id * 16 + c; /* 4 folders, 3 files */
    }
    d->rows[row].expanded = true;
    insert_rows(d, row + 1, kids, k);
}

static void close_folder(demo_t *d, gates_u64 row) {
    gates_u32 depth = depth_of(d->rows[row].id);
    gates_u64 end = row + 1;
    while (end < d->n && depth_of(d->rows[end].id) > depth) end++;
    memmove(&d->rows[row + 1], &d->rows[end], (size_t)(d->n - end) * sizeof d->rows[0]);
    d->n -= end - row - 1;
    d->rows[row].expanded = false;
    if (d->rows[row].id == SLOW) d->slow_pending = false;
}

static void update(demo_t *d) {
    char buf[160];
    gates_item_id_t sel = gates_view_selected(d->tree, d->view);
    int n = snprintf(buf, sizeof buf, "%llu visible rows   selected: %llx%s",
                     (unsigned long long)d->n, (unsigned long long)sel,
                     d->slow_pending ? "   (slow is loading)" : "");
    if (n > 0) {
        (void)gates_widget_set_text(d->tree, d->status,
                                    (gates_str_t){ .ptr = (const gates_u8 *)buf,
                                                   .size = (gates_usize_t)n });
    }
}

static void on_view(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    demo_t *d = user;
    if (ev->kind == GATES_EVENT_EXPAND_REQUESTED) {
        gates_u64 row = 0;
        if (find_row(d, ev->item, &row)) {
            if (ev->result != 0 && !d->rows[row].expanded) open_folder(d, row);
            if (ev->result == 0 && d->rows[row].expanded) close_folder(d, row);
            (void)gates_view_model_changed(tree, d->view);
        }
    }
    update(d);
}

static void on_finish(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    demo_t *d = user;
    (void)ev;
    gates_u64 row = 0;
    if (!d->slow_pending || !find_row(d, SLOW * 16 + LOADING_CHILD, &row)) return;
    /* The children arrived: the loading row is replaced by them. */
    memmove(&d->rows[row], &d->rows[row + 1], (size_t)(d->n - row - 1) * sizeof d->rows[0]);
    d->n--;
    gates_item_id_t kids[3] = { SLOW * 16 + 5, SLOW * 16 + 6, SLOW * 16 + 7 };
    insert_rows(d, row, kids, 3);
    d->slow_pending = false;
    (void)gates_view_model_changed(tree, d->view);
    update(d);
}

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    demo_t *d = user;
    if (ev->down && ev->key == GATES_KEY_ESCAPE) {
        gates_app_quit(d->app);
    }
}

static gates_err_t build_ui(demo_t *d) {
    gates_tree_t *t = d->tree;
    gates_node_t root = gates_tree_root(t);
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 8));
    gates_node_t n = GATES_NODE_NULL;
    TRY(gates_label_create(t, root, GATES_STR("Tree: Right opens, Left closes, click a mark"), &n));
    for (gates_item_id_t id = 1; id <= 5; id++) d->rows[d->n++] = (row_t){ .id = id };
    TRY(gates_view_create(t, root, &(gates_view_desc_t){ .tree = true }, &d->view));
    TRY(gates_node_set_access_name(t, d->view, GATES_STR("Folders")));
    TRY(gates_layout_set_child_grow(t, d->view, 1));
    gates_rows_model_t model = { .user = d, .count = m_count, .id_at = m_id_at,
                                 .index_of = m_index_of, .cell = m_cell, .row_info = m_info };
    TRY(gates_view_set_model(t, d->view, &model));
    TRY(gates_widget_set_handler(t, d->view, on_view, d));
    TRY(gates_button_create(t, root, GATES_STR("finish loading"), nullptr, nullptr, &d->finish));
    TRY(gates_layout_set_child_align(t, d->finish, GATES_ALIGN_START_V));
    TRY(gates_widget_set_handler(t, d->finish, on_finish, d));
    TRY(gates_label_create(t, root, GATES_STR(""), &d->status));
    TRY(gates_label_create(t, root, GATES_STR("ESC exits"), &n));
    update(d);
    gates_tree_set_focus(t, d->view);
    return GATES_OK;
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) {
        return 1;
    }
    static demo_t d;
    d.app = app;
    gates_window_callbacks_t cb = { .on_key = on_key, .user_data = &d };
    gates_window_desc_t desc = { .title = GATES_STR("gates: tree"), .size = { 460, 440 } };
    gates_window_t *win = nullptr;
    if (!gates_is_ok(gates_window_create(app, &desc, &cb, &win))) {
        gates_app_destroy(app);
        return 1;
    }
    d.tree = gates_window_tree(win);
    gates_err_t err = build_ui(&d);
    if (!gates_is_ok(err)) {
        fprintf(stderr, "ctl_tree: building the window failed (%d)\n", (int)err);
    } else {
        gates_window_request_repaint(win);
        err = gates_app_run(app);
    }
    gates_window_destroy(win);
    gates_app_destroy(app);
    return gates_is_ok(err) ? 0 : 1;
}
