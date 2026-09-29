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
