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
  `gates_view_set_column_hidden` and `gates_view_move_column` choose and order the columns; with
  `column_menu` in the description a right press on the header (or Shift+F10 on the table)
  opens a menu with a checked entry per column, and the last shown column cannot be hidden.
  gates/state.h saves widths, order and hidden columns.
- A tree is a list whose model also answers `row_info`: depth, whether the row can open, whether
  it is open, a loading or error state. Opening and closing are requests (EXPAND_REQUESTED):
  the application changes its flattened rows and calls `gates_view_model_changed`. gates never
  walks rows it does not show.
- A log (`gates_log_create`) owns its lines: append text, keep a line and byte limit, drop the
  oldest, and follow the end while the person has not scrolled away. Scrolling away or back
  reports GATES_EVENT_FOLLOW_CHANGED (`ev->result` 1 while following), so a status line can say
  so without watching the pointer.

Every view also answers typing: the letters typed select the next row whose first shown cell
starts with them (a pause of a second starts over; the same letter again steps through such
rows). Ctrl+C puts the selected row's shown cells on the clipboard, separated by tabs.

## Cells people can change

A column can show more than text (`gates_column_desc_t.kind`): `GATES_CELL_CHECK` draws a check
box from `cell.checked`, `GATES_CELL_PROGRESS` a bar from `cell.permille` (0 to 1000), and
`GATES_CELL_ICON_TEXT` a 16 x 16 icon (`cell.icon`, see chapter 13) before the text. A column
with a `paint` function draws its cells itself, clipped to each cell.

A column marked `editable` lets a person change the selected row's cell, and the model says yes
or no through its `set_cell` function. F2 or a double click opens a text box over the cell with
the model's text selected; Enter commits, Escape cancels, and moving focus away commits. Space,
or a click on the box, toggles a check column. Which column F2 and Space use follows the table's
current column - the cell last pressed, or moved with Ctrl+Left and Ctrl+Right, outlined in the
selected row - when it is an editable column of the right kind, else the first such column. When `set_cell` returns an error the editor stays
open and is marked invalid (after focus has left, the edit is dropped instead). After a change
the view reads the model again and sends CELL_EDITED with the row (`ev->item`) and the column
(`ev->result`). The program can do the same with `gates_view_edit` and `gates_view_end_edit`.

Screen readers see every cell of a table row as an element of its own: its text, its column's
label, a check cell's state and a progress cell's percentage. They can toggle a check cell or set
a text cell's value when the column is editable - through `set_cell`, reported with CELL_EDITED,
refused exactly as a person's edit would be (`gates_access_cell_info`, `_toggle`, `_set_value`).
A table wider than its view scrolls sideways for them too.

<!-- example: manual/examples/ex_05_cells.c -->
```c
/* manual example (host): a shopping list edited in place - a text column and a check column.
 * expect: item 2 is now Bread (column 1), done: yes (column 2); refused empty name: editor still open */
#include <gates/gates.h>

#include <stdio.h>
#include <string.h>

enum { COL_NAME = 1, COL_DONE = 2 };

typedef struct item_t {
    gates_item_id_t id;
    char name[32];
    bool done;
} item_t;

typedef struct list_t {
    item_t items[4];
    gates_u64 n;
} list_t;

static gates_u64 l_count(void *u) { return ((list_t *)u)->n; }
static gates_item_id_t l_id_at(void *u, gates_u64 row) { return ((list_t *)u)->items[row].id; }
static bool l_index_of(void *u, gates_item_id_t id, gates_u64 *row) {
    list_t *l = u;
    for (gates_u64 i = 0; i < l->n; i++) {
        if (l->items[i].id == id) { *row = i; return true; }
    }
    return false;
}
static gates_err_t l_cell(void *u, gates_item_id_t id, gates_column_id_t col, gates_cell_t *out) {
    gates_u64 row;
    if (!l_index_of(u, id, &row)) return PROVEN_ERR_INVALID_ARG;
    item_t *it = &((list_t *)u)->items[row];
    if (col == COL_DONE) {
        out->checked = it->done;
    } else {
        out->text = (gates_str_t){ .ptr = (const gates_u8 *)it->name, .size = strlen(it->name) };
    }
    return GATES_OK;
}
/* A person changed a cell: the model decides. An empty name is refused. */
static gates_err_t l_set_cell(void *u, gates_item_id_t id, gates_column_id_t col, const gates_cell_t *value) {
    gates_u64 row;
    if (!l_index_of(u, id, &row)) return PROVEN_ERR_INVALID_ARG;
    item_t *it = &((list_t *)u)->items[row];
    if (col == COL_DONE) {
        it->done = value->checked;
        return GATES_OK;
    }
    if (value->text.size == 0 || value->text.size >= sizeof it->name) return PROVEN_ERR_INVALID_ARG;
    memcpy(it->name, value->text.ptr, value->text.size);
    it->name[value->text.size] = 0;
    return GATES_OK;
}

static list_t list = { .items = { { 1, "Milk", false }, { 2, "Bred", false }, { 3, "Eggs", true } }, .n = 3 };

static void on_view(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    (void)user;
    if (ev->kind != GATES_EVENT_CELL_EDITED) return;
    gates_u64 row;
    if (!l_index_of(&list, ev->item, &row)) return;
    if (ev->result == COL_NAME) {
        printf("item %llu is now %s (column %u), ", (unsigned long long)ev->item, list.items[row].name, ev->result);
    } else {
        printf("done: %s (column %u); ", list.items[row].done ? "yes" : "no", ev->result);
    }
}

static void press(gates_tree_t *t, gates_key_t key) {
    gates_key_event_t ev = { .key = key, .down = true };
    (void)gates_input_key(t, &ev);
    ev.down = false;
    (void)gates_input_key(t, &ev);
}

static void type(gates_tree_t *t, const char *s) {
    for (; *s != 0; s++) (void)gates_input_char(t, (gates_u32)(unsigned char)*s);
}

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    const gates_column_desc_t cols[] = {
        { .id = COL_NAME, .label = GATES_STR_INIT("Item"), .width = 120, .editable = true },
        { .id = COL_DONE, .label = GATES_STR_INIT("Done"), .width = 60, .kind = GATES_CELL_CHECK, .editable = true },
    };
    gates_rows_model_t model = { .user = &list, .count = l_count, .id_at = l_id_at, .index_of = l_index_of,
                                 .cell = l_cell, .set_cell = l_set_cell };
    gates_node_t table;
    if (!gates_is_ok(gates_view_create(t, gates_tree_root(t),
                                       &(gates_view_desc_t){ .columns = cols, .column_count = 2, .header = true },
                                       &table)) ||
        !gates_is_ok(gates_view_set_model(t, table, &model)) ||
        !gates_is_ok(gates_widget_set_handler(t, table, on_view, nullptr)) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 240, 160 }, gates_text_backend_builtin()))) {
        return 1;
    }
    /* A person selects the second row, presses F2, retypes the name and presses Enter. */
    gates_tree_set_focus(t, table);
    press(t, GATES_KEY_DOWN);
    press(t, GATES_KEY_DOWN);
    press(t, GATES_KEY_F2);
    type(t, "Bread");
    press(t, GATES_KEY_ENTER);
    /* Space toggles the row's check column. */
    press(t, GATES_KEY_SPACE);
    (void)gates_tree_dispatch_events(t, 0);

    /* An empty name is refused: the editor stays open, marked invalid, until Escape. */
    press(t, GATES_KEY_F2);
    press(t, GATES_KEY_DELETE);
    press(t, GATES_KEY_ENTER);
    printf("refused empty name: editor %s\n", gates_view_editing(t, table, nullptr, nullptr) ? "still open" : "closed");
    press(t, GATES_KEY_ESCAPE);
    gates_tree_destroy(t);
    return 0;
}
```

## Many rows at once: multi-selection

A view made with `multi_select` lets a person select many rows: Shift+click and Shift with the
arrows make a range from an anchor, Ctrl+click and Ctrl+Space add or remove one row, Ctrl+A takes
all, and Ctrl with the arrows moves the focus without changing the selection. The selection is
the program's, like the rows: the model answers `next_selected(row)` - the first selected row at
or after `row` - and each gesture arrives as GATES_EVENT_SELECT_REQUESTED (`ev->result` says ONE,
TOGGLE, RANGE, ADD_RANGE or ALL; `ev->item` is the target row and `ev->anchor` where the range
starts). The program changes its selection and calls `gates_view_model_changed`.

Because gates only asks about rows it paints, describes or copies, the program can keep any
number of selected rows in a small form - below, a few ranges; "all" of a million rows is one
range. Ctrl+C copies up to `GATES_VIEW_COPY_MAX` (10000) selected rows itself, one per line;
past that it sends GATES_EVENT_COPY_REQUESTED and leaves the copying to the program.
`gates_view_selected` stays the focus row. Screen readers see a list that selects many
(`gates_access_set_item_selected` joins or leaves a row, as a request).

<!-- example: manual/examples/ex_05_multi.c -->
```c
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
```

## A record's fields: the property grid

When one record has many typed fields - a document's settings, the selected object in an
inspector - `gates_propgrid_create` (gates/propgrid.h) lays them out as rows of a name and an
editor: `gates_propgrid_add_text`, `_bool`, `_choice` and `_number` make a text box, check box,
choice or spin box. Properties are grouped by category, each a collapsible group box; properties
without one come first. Every property has a stable id: `gates_propgrid_editor` gives its editor
for setting and reading the value with the editor's own functions, and one handler
(`gates_propgrid_set_handler`) hears every change a person makes as VALUE_CHANGED with the
property id in `ev->result` and the value in `ev->text`, `ev->checked` or `ev->value`.

<!-- example: manual/examples/ex_05_props.c -->
```c
/* manual example (host): a property grid over a document's settings.
 * expect: 4 properties in 2 categories; property 2 (Pages) is now 13; property 3 (Draft) is now off */
#include <gates/gates.h>

#include <stdio.h>

enum { P_TITLE = 1, P_PAGES, P_DRAFT, P_PAPER };

/* One handler hears every change, by property id. */
static void on_property(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    (void)user;
    if (ev->result == P_PAGES) printf("property %u (Pages) is now %lld; ", ev->result, (long long)ev->value);
    if (ev->result == P_DRAFT) printf("property %u (Draft) is now %s\n", ev->result, ev->checked ? "on" : "off");
}

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_node_t grid;
    const gates_option_t papers[] = { { .id = 1, .label = GATES_STR_INIT("A4") }, { .id = 2, .label = GATES_STR_INIT("Letter") } };
    gates_range_t pages = { .min = 1, .max = 999, .value = 12 };
    if (!gates_is_ok(gates_propgrid_create(t, gates_tree_root(t), &grid)) ||
        !gates_is_ok(gates_propgrid_add_text(t, grid, GATES_STR("Document"), P_TITLE, GATES_STR("Title"), GATES_STR("Notes"))) ||
        !gates_is_ok(gates_propgrid_add_number(t, grid, GATES_STR("Document"), P_PAGES, GATES_STR("Pages"), &pages)) ||
        !gates_is_ok(gates_propgrid_add_bool(t, grid, GATES_STR("Print"), P_DRAFT, GATES_STR("Draft"), true)) ||
        !gates_is_ok(gates_propgrid_add_choice(t, grid, GATES_STR("Print"), P_PAPER, GATES_STR("Paper"), papers, 2, 1)) ||
        !gates_is_ok(gates_propgrid_set_handler(t, grid, on_property, nullptr)) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 320, 320 }, gates_text_backend_builtin()))) {
        return 1;
    }
    printf("%u properties in %d categories; ", gates_propgrid_count(t, grid),
           !gates_node_eq(gates_propgrid_category(t, grid, GATES_STR("Print")), GATES_NODE_NULL) ? 2 : 1);

    /* A person steps Pages up in its spin box, then turns Draft off with Space. */
    gates_tree_set_focus(t, gates_node_first_child(t, gates_propgrid_editor(t, grid, P_PAGES)));
    gates_key_event_t up = { .key = GATES_KEY_UP, .down = true };
    (void)gates_input_key(t, &up);
    gates_tree_set_focus(t, gates_propgrid_editor(t, grid, P_DRAFT));
    gates_key_event_t space = { .key = GATES_KEY_SPACE, .down = true };
    (void)gates_input_key(t, &space);
    space.down = false;
    (void)gates_input_key(t, &space);
    (void)gates_tree_dispatch_events(t, 0);
    gates_tree_destroy(t);
    return 0;
}
```

## Rules for models

Model callbacks run on the UI thread, never during event dispatch or inside another callback,
and never after `gates_view_set_model(view, nullptr)` returns. A cell's text is borrowed until
the next call into the model. The model must not change between `gates_view_model_changed` calls
without telling the view; `set_cell` is the one callback that may change it (the view reads it
again afterwards).
