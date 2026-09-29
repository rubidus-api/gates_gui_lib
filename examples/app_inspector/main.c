/* app_inspector - reference application: a record inspector over a table.
 *
 * Interaction: the person browses records in a table and reads the selected
 * one in a detail form. The program owns the records; the view only asks for
 * the rows it paints and remembers the selection by record id, so when the
 * program adds, removes or reorders records the selected record stays
 * selected (or, when it was removed, its nearest neighbour takes over, which
 * the view announces with origin PROGRAM).
 *
 * Shows: 5 000 generated records (id, name, size, date) in a sortable table
 * whose names can be edited in place (0.6.0: F2 or a double click on a name;
 * the model refuses an empty name) and whose columns can be hidden from the
 * header menu (right press on the header, or Shift+F10); a read-only form
 * with the selected record; commands shared by buttons and keys: Add 1000
 * (F5), Shuffle (F6), Remove selected (F8); a status line with the record
 * count and any allocation failure.
 *
 * Field check (T032 stage 1): select a record, press F6: the rows reorder,
 * the same record stays selected and in view; F5 adds 1000 records (the count
 * grows, the selection stays); F8 removes the selected record and the next
 * one is selected, the form follows; clicking "Size" sorts, clicking again
 * reverses; Enter or a double click on a row (outside the name) says
 * "opened"; F2, a new name and Enter renames the record (the form follows),
 * an empty name stays in the editor marked invalid until Escape; the window's
 * close button exits. */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/command.h>
#include <gates/form.h>
#include <gates/view.h>
#include <gates/ui.h>
#include <gates/access.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)

enum { COL_ID = 1, COL_NAME, COL_SIZE, COL_DATE };
enum { F_ID = 1, F_NAME, F_SIZE, F_DATE };
enum { CMD_ADD = 1, CMD_SHUFFLE, CMD_REMOVE };

typedef struct record_t {
    gates_item_id_t id;
    char name[24];
    gates_u32 size;
    gates_u32 day;               /* days since 2020-01-01 */
} record_t;

typedef struct app_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t table, form, status;
    record_t *recs;
    gates_u64 count, cap;
    gates_item_id_t next_id;
    gates_u64 rev;
    gates_column_id_t sort_col;
    bool descending;
    gates_u32 seed;
    char buf[48];
} app_t;

static const char *const syllables[] = { "ka", "ro", "mi", "tel", "an", "vo", "su", "ne" };

static gates_u32 rnd(app_t *a) {
    a->seed = a->seed * 1103515245u + 12345u;
    return (a->seed >> 8) & 0xFFFFFFu;
}

static void make_record(app_t *a, record_t *r) {
    r->id = a->next_id++;
    gates_u32 x = rnd(a);
    snprintf(r->name, sizeof r->name, "%s%s%s-%u", syllables[x % 8], syllables[(x >> 3) % 8],
             syllables[(x >> 6) % 8], (unsigned)(r->id % 1000));
    r->size = rnd(a) % 900000u + 100u;
    r->day = rnd(a) % 2400u;
}

static void date_text(gates_u32 day, char *buf, size_t cap) {
    static const int mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int y = 2020, m = 0, d = (int)day;
    for (;;) {
        int ylen = (y % 4 == 0) ? 366 : 365;
        if (d < ylen) break;
        d -= ylen;
        y++;
    }
    while (m < 12) {
        int len = mdays[m] + (m == 1 && y % 4 == 0 ? 1 : 0);
        if (d < len) break;
        d -= len;
        m++;
    }
    snprintf(buf, cap, "%04d-%02d-%02d", y, m + 1, d + 1);
}

/* -- the model ---------------------------------------------------------------------- */

static gates_u64 m_revision(void *u) { return ((app_t *)u)->rev; }
static gates_u64 m_count(void *u) { return ((app_t *)u)->count; }
static gates_item_id_t m_id_at(void *u, gates_u64 row) {
    app_t *a = u;
    return row < a->count ? a->recs[row].id : 0;
}
static bool m_index_of(void *u, gates_item_id_t id, gates_u64 *row) {
    app_t *a = u;
    for (gates_u64 i = 0; i < a->count; i++) {
        if (a->recs[i].id == id) { *row = i; return true; }
    }
    return false;
}
static const record_t *find(app_t *a, gates_item_id_t id) {
    gates_u64 row = 0;
    return id != 0 && m_index_of(a, id, &row) ? &a->recs[row] : nullptr;
}
static gates_err_t m_cell(void *u, gates_item_id_t id, gates_column_id_t col, gates_cell_t *out) {
    app_t *a = u;
    const record_t *r = find(a, id);
    if (r == nullptr) return PROVEN_ERR_INVALID_ARG;
    int n;
    switch (col) {
    case COL_ID:   n = snprintf(a->buf, sizeof a->buf, "%llu", (unsigned long long)r->id); break;
    case COL_NAME: n = snprintf(a->buf, sizeof a->buf, "%s", r->name); break;
    case COL_SIZE: n = snprintf(a->buf, sizeof a->buf, "%u", r->size); break;
    default:       date_text(r->day, a->buf, sizeof a->buf); n = (int)strlen(a->buf); break;
    }
    out->text = (gates_str_t){ .ptr = (const gates_u8 *)a->buf, .size = (gates_usize_t)n };
    return GATES_OK;
}

/* A person renamed a record in the table (0.6.0): an empty or too long name is refused. */
static gates_err_t m_set_cell(void *u, gates_item_id_t id, gates_column_id_t col, const gates_cell_t *v) {
    app_t *a = u;
    gates_u64 row = 0;
    if (col != COL_NAME || !m_index_of(a, id, &row)) return PROVEN_ERR_INVALID_ARG;
    record_t *r = &a->recs[row];
    if (v->text.size == 0 || v->text.size >= sizeof r->name) return PROVEN_ERR_INVALID_ARG;
    memcpy(r->name, v->text.ptr, v->text.size);
    r->name[v->text.size] = '\0';
    a->rev++;
    return GATES_OK;
}

/* -- the program's side: change, then tell the view ------------------------------------- */

static void set_status(app_t *a, const char *extra) {
    char buf[160];
    int n = snprintf(buf, sizeof buf, "%llu records%s%s", (unsigned long long)a->count,
                     extra[0] != '\0' ? "   " : "", extra);
    if (n > 0) {
        (void)gates_widget_set_text(a->tree, a->status,
                                    (gates_str_t){ .ptr = (const gates_u8 *)buf,
                                                   .size = (gates_usize_t)n });
    }
}

static void show_detail(app_t *a) {
    const record_t *r = find(a, gates_view_selected(a->tree, a->table));
    char id[24] = "", size[24] = "", date[24] = "";
    if (r != nullptr) {
        snprintf(id, sizeof id, "%llu", (unsigned long long)r->id);
        snprintf(size, sizeof size, "%u bytes", r->size);
        date_text(r->day, date, sizeof date);
    }
    const char *vals[] = { id, r != nullptr ? r->name : "", size, date };
    for (gates_u32 f = F_ID; f <= F_DATE; f++) {
        (void)gates_textbox_set_text(a->tree, gates_form_editor(a->tree, a->form, f),
                                     (gates_str_t){ .ptr = (const gates_u8 *)vals[f - 1],
                                                    .size = strlen(vals[f - 1]) });
    }
}

static void changed(app_t *a, const char *what) {
    a->rev++;
    gates_err_t err = gates_view_model_changed(a->tree, a->table);
    gates_item_id_t sel = gates_view_selected(a->tree, a->table);
    if (sel != 0) (void)gates_view_scroll_to(a->tree, a->table, sel);
    show_detail(a);
    set_status(a, gates_is_ok(err) ? what : "out of memory (selection not announced)");
}

static app_t *g_sort;

static int compare(const void *x, const void *y) {
    const record_t *p = x, *q = y;
    int r = 0;
    switch (g_sort->sort_col) {
    case COL_NAME: r = strcmp(p->name, q->name); break;
    case COL_SIZE: r = p->size < q->size ? -1 : p->size > q->size; break;
    case COL_DATE: r = p->day < q->day ? -1 : p->day > q->day; break;
    default: break;
    }
    if (r == 0) r = p->id < q->id ? -1 : p->id > q->id;
    return g_sort->descending ? -r : r;
}

static void on_command(gates_tree_t *tree, gates_command_id_t id, void *user) {
    app_t *a = user;
    if (id == CMD_ADD) {
        if (a->count + 1000 > a->cap) {
            gates_u64 cap = a->cap * 2 + 1000;
            record_t *grown = realloc(a->recs, (size_t)cap * sizeof *grown);
            if (grown == nullptr) {
                set_status(a, "out of memory: nothing added");
                return;
            }
            a->recs = grown;
            a->cap = cap;
        }
        for (int i = 0; i < 1000; i++) make_record(a, &a->recs[a->count++]);
        changed(a, "added 1000");
    } else if (id == CMD_SHUFFLE) {
        for (gates_u64 i = a->count; i > 1; i--) {
            gates_u64 k = rnd(a) % i;
            record_t t = a->recs[i - 1]; a->recs[i - 1] = a->recs[k]; a->recs[k] = t;
        }
        a->sort_col = 0;
        changed(a, "shuffled");
    } else if (id == CMD_REMOVE) {
        gates_u64 row = 0;
        if (!m_index_of(a, gates_view_selected(tree, a->table), &row)) {
            set_status(a, "nothing selected");
            return;
        }
        memmove(&a->recs[row], &a->recs[row + 1], (size_t)(a->count - row - 1) * sizeof *a->recs);
        a->count--;
        changed(a, "removed one");
    }
}

static void on_table(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    app_t *a = user;
    if (ev->kind == GATES_EVENT_SORT_REQUESTED) {
        a->descending = a->sort_col == ev->result ? !a->descending : false;
        a->sort_col = ev->result;
        g_sort = a;
        qsort(a->recs, (size_t)a->count, sizeof *a->recs, compare);
        changed(a, a->descending ? "sorted (descending)" : "sorted");
    } else if (ev->kind == GATES_EVENT_SELECTION_CHANGED) {
        show_detail(a);
        set_status(a, ev->origin == GATES_ORIGIN_PROGRAM ? "selection moved to a neighbour" : "");
    } else if (ev->kind == GATES_EVENT_CELL_EDITED) {
        show_detail(a);
        const record_t *r = find(a, ev->item);
        char buf[64];
        snprintf(buf, sizeof buf, "renamed to %s", r != nullptr ? r->name : "?");
        set_status(a, buf);
    } else if (ev->kind == GATES_EVENT_ACTIVATED) {
        char buf[64];
        const record_t *r = find(a, ev->item);
        snprintf(buf, sizeof buf, "opened %s", r != nullptr ? r->name : "?");
        set_status(a, buf);
    }
    (void)tree;
}

static gates_err_t build_ui(app_t *a) {
    gates_tree_t *t = a->tree;
    gates_node_t root = gates_tree_root(t);
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 8));

    static const gates_column_desc_t cols[] = {
        { .id = COL_ID, .label = GATES_STR_INIT("Id"), .width = 60, .min_width = 40 },
        { .id = COL_NAME, .label = GATES_STR_INIT("Name"), .width = 150, .min_width = 60, .editable = true },
        { .id = COL_SIZE, .label = GATES_STR_INIT("Size"), .width = 80, .min_width = 50 },
        { .id = COL_DATE, .label = GATES_STR_INIT("Date"), .width = 100, .min_width = 60 },
    };
    gates_view_desc_t vd = { .columns = cols, .column_count = 4, .header = true, .column_menu = true };
    TRY(gates_view_create(t, root, &vd, &a->table));
    TRY(gates_node_set_access_name(t, a->table, GATES_STR("Files")));
    TRY(gates_layout_set_child_grow(t, a->table, 1));
    gates_rows_model_t model = { .user = a, .revision = m_revision, .count = m_count,
                                 .id_at = m_id_at, .index_of = m_index_of, .cell = m_cell, .set_cell = m_set_cell };
    TRY(gates_view_set_model(t, a->table, &model));
    TRY(gates_widget_set_handler(t, a->table, on_table, a));

    TRY(gates_form_create(t, root, &a->form));
    static const char *const labels[] = { "Id", "Name", "Size", "Date" };
    for (gates_u32 f = F_ID; f <= F_DATE; f++) {
        gates_field_desc_t d = { .label = { .ptr = (const gates_u8 *)labels[f - 1],
                                            .size = strlen(labels[f - 1]) },
                                 .read_only = true, .cols = 28 };
        TRY(gates_form_add_text(t, a->form, f, &d, nullptr));
    }

    struct { gates_command_id_t id; const char *label; gates_key_t key; } cmds[] = {
        { CMD_ADD, "Add 1000 (F5)", GATES_KEY_F5 },
        { CMD_SHUFFLE, "Shuffle (F6)", GATES_KEY_F6 },
        { CMD_REMOVE, "Remove selected (F8)", GATES_KEY_F8 },
    };
    gates_node_t row = GATES_NODE_NULL;
    TRY(gates_panel_create(t, root, &row));
    TRY(gates_layout_set(t, row, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, row, 8));
    for (size_t i = 0; i < sizeof cmds / sizeof cmds[0]; i++) {
        gates_command_desc_t c = { .id = cmds[i].id,
                                   .label = { .ptr = (const gates_u8 *)cmds[i].label,
                                              .size = strlen(cmds[i].label) },
                                   .shortcut = { .key = cmds[i].key },
                                   .enabled = true, .invoke = on_command, .user = a };
        TRY(gates_command_register(t, root, &c));
        gates_node_t b = GATES_NODE_NULL;
        TRY(gates_button_create(t, row, GATES_STR(""), nullptr, nullptr, &b));
        TRY(gates_button_set_command(t, b, root, cmds[i].id));
    }
    TRY(gates_label_create(t, root, GATES_STR(""), &a->status));
    set_status(a, "");
    show_detail(a);
    gates_tree_set_focus(t, a->table);
    return GATES_OK;
}

int main(void) {
    app_t a = { .next_id = 1, .seed = 20260926u };
    a.cap = 8000;
    a.recs = malloc((size_t)a.cap * sizeof *a.recs);
    if (a.recs == nullptr) {
        return 1;
    }
    for (int i = 0; i < 5000; i++) make_record(&a, &a.recs[a.count++]);
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &a.app))) {
        free(a.recs);
        return 1;
    }
    gates_window_desc_t desc = { .title = GATES_STR("gates: inspector"), .size = { 560, 560 } };
    gates_window_t *win = nullptr;
    if (!gates_is_ok(gates_window_create(a.app, &desc, &(gates_window_callbacks_t){0}, &win))) {
        gates_app_destroy(a.app);
        free(a.recs);
        return 1;
    }
    a.tree = gates_window_tree(win);
    gates_err_t err = build_ui(&a);
    if (!gates_is_ok(err)) {
        fprintf(stderr, "app_inspector: building the window failed (%d)\n", (int)err);
    } else {
        gates_window_request_repaint(win);
        err = gates_app_run(a.app);
    }
    gates_window_destroy(win);
    gates_app_destroy(a.app);
    free(a.recs);
    return gates_is_ok(err) ? 0 : 1;
}
