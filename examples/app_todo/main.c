/* app_todo - a to-do list (sample application, 0.1.0).
 *
 * Shows: a text box whose Enter runs the form's default command, a list view
 * over the application's own tasks (stable ids, selection kept across
 * removals), commands shared by buttons, shortcuts and a context menu, a
 * live status line, and saving to a file after every change.
 *
 * Use: type a task and press Enter (or Add). In the list, Enter or a double
 * click marks a task done or not done, Delete removes it, Ctrl+L clears the
 * finished ones, Shift+F10 or a right click opens the context menu, Escape
 * quits. Tasks are kept in %LOCALAPPDATA%\gates-todo.txt.
 *
 * Check: add three tasks; mark one done ("[x]"); delete another - the
 * selection moves to its neighbour; clear finished; quit and start again - the
 * remaining task is still there. Narrator reads the field as "New task" and
 * the status line as it changes. */
#include <gates/gates.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)

enum { CMD_ADD = 1, CMD_DONE, CMD_DELETE, CMD_CLEAR, CMD_MENU };
enum { MAX_TASKS = 512, MAX_TEXT = 160 };

typedef struct task_t {
    gates_item_id_t id;
    bool done;
    char text[MAX_TEXT];
} task_t;

typedef struct app_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t entry, list, status;
    task_t tasks[MAX_TASKS];
    gates_u32 count;
    gates_item_id_t next_id;
    gates_u64 rev;
    char cell[MAX_TEXT + 8];
    char path[512];
} app_t;

/* -- the model: the application's tasks, read by the list ----------------------------- */

static gates_u64 m_revision(void *u) { return ((app_t *)u)->rev; }
static gates_u64 m_count(void *u) { return ((app_t *)u)->count; }
static gates_item_id_t m_id_at(void *u, gates_u64 row) {
    app_t *a = u;
    return row < a->count ? a->tasks[row].id : 0;
}
static bool m_index_of(void *u, gates_item_id_t id, gates_u64 *row) {
    app_t *a = u;
    for (gates_u32 i = 0; i < a->count; i++) {
        if (a->tasks[i].id == id) {
            *row = i;
            return true;
        }
    }
    return false;
}
static gates_err_t m_cell(void *u, gates_item_id_t id, gates_column_id_t col, gates_cell_t *out) {
    (void)col;
    app_t *a = u;
    gates_u64 row = 0;
    if (!m_index_of(a, id, &row)) return PROVEN_ERR_INVALID_ARG;
    int n = snprintf(a->cell, sizeof a->cell, "[%c] %s", a->tasks[row].done ? 'x' : ' ', a->tasks[row].text);
    out->text = (gates_str_t){ .ptr = (const gates_u8 *)a->cell, .size = n > 0 ? (gates_usize_t)n : 0 };
    return GATES_OK;
}

/* -- saving ---------------------------------------------------------------------------- */

static void save(app_t *a) {
    if (a->path[0] == '\0') return;
    FILE *f = fopen(a->path, "wb");
    if (f == nullptr) return;
    for (gates_u32 i = 0; i < a->count; i++) fprintf(f, "%d\t%s\n", a->tasks[i].done ? 1 : 0, a->tasks[i].text);
    fclose(f);
}

static void load(app_t *a) {
    const char *dir = getenv("LOCALAPPDATA");
    if (dir == nullptr || snprintf(a->path, sizeof a->path, "%s\\gates-todo.txt", dir) >= (int)sizeof a->path) {
        a->path[0] = '\0';
        return;
    }
    FILE *f = fopen(a->path, "rb");
    if (f == nullptr) return;
    char line[MAX_TEXT + 8];
    while (a->count < MAX_TASKS && fgets(line, sizeof line, f) != nullptr) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) < 3 || line[1] != '\t') continue;
        task_t *t = &a->tasks[a->count++];
        t->id = a->next_id++;
        t->done = line[0] == '1';
        snprintf(t->text, sizeof t->text, "%.*s", MAX_TEXT - 1, line + 2);
    }
    fclose(f);
}

/* -- changes --------------------------------------------------------------------------- */

static void show_status(app_t *a) {
    gates_u32 done = 0;
    for (gates_u32 i = 0; i < a->count; i++) done += a->tasks[i].done ? 1u : 0u;
    char buf[80];
    int n = a->count == 0 ? snprintf(buf, sizeof buf, "Nothing to do. Type a task and press Enter.")
                          : snprintf(buf, sizeof buf, "%u task%s, %u done", a->count, a->count == 1 ? "" : "s", done);
    (void)gates_widget_set_text(a->tree, a->status, (gates_str_t){ .ptr = (const gates_u8 *)buf, .size = (gates_usize_t)n });
    gates_node_t root = gates_tree_root(a->tree);
    (void)gates_command_set_enabled(a->tree, root, CMD_CLEAR, done > 0);
    bool any = gates_view_selected(a->tree, a->list) != 0;
    (void)gates_command_set_enabled(a->tree, root, CMD_DONE, any);
    (void)gates_command_set_enabled(a->tree, root, CMD_DELETE, any);
}

/* Input the tree could not complete (usually out of memory): say so. */
static void on_input_error(gates_window_t *win, gates_err_t err, void *user) {
    (void)win;
    app_t *a = user;
    const char *msg = err == PROVEN_ERR_NOMEM ? "Out of memory: the last action was not done" : "The last action failed";
    (void)gates_widget_set_text(a->tree, a->status, (gates_str_t){ .ptr = (const gates_u8 *)msg, .size = strlen(msg) });
}

/* The tasks changed: tell the list, save, and update the status. */
static void changed(app_t *a) {
    a->rev++;
    (void)gates_view_model_changed(a->tree, a->list);
    save(a);
    show_status(a);
}

static task_t *selected(app_t *a, gates_u32 *row_out) {
    gates_u64 row = 0;
    if (!m_index_of(a, gates_view_selected(a->tree, a->list), &row)) return nullptr;
    if (row_out != nullptr) *row_out = (gates_u32)row;
    return &a->tasks[row];
}

static void open_menu(app_t *a, gates_point_t at) {
    static const gates_command_id_t ids[] = { CMD_DONE, CMD_DELETE, 0, CMD_CLEAR };
    gates_node_t menu = GATES_NODE_NULL;
    (void)gates_menu_open(a->tree, at, gates_tree_root(a->tree), ids, sizeof ids / sizeof ids[0], &menu);
}

static void on_command(gates_tree_t *tree, gates_command_id_t id, void *user) {
    app_t *a = user;
    gates_u32 row = 0;
    task_t *t = nullptr;
    switch (id) {
    case CMD_ADD: {
        gates_str_t text = gates_textbox_text(tree, a->entry);
        if (text.size == 0) {
            (void)gates_widget_set_text(tree, a->status, GATES_STR("Type a task first."));
            return;
        }
        if (a->count == MAX_TASKS) {
            (void)gates_widget_set_text(tree, a->status, GATES_STR("The list is full."));
            return;
        }
        task_t *n = &a->tasks[a->count++];
        n->id = a->next_id++;
        n->done = false;
        snprintf(n->text, sizeof n->text, "%.*s", (int)text.size, (const char *)text.ptr);
        (void)gates_textbox_set_text(tree, a->entry, GATES_STR(""));
        changed(a);
        (void)gates_view_set_selected(tree, a->list, n->id);
        (void)gates_view_scroll_to(tree, a->list, n->id);
        show_status(a);
        break;
    }
    case CMD_DONE:
        if ((t = selected(a, nullptr)) != nullptr) {
            t->done = !t->done;
            changed(a);
        }
        break;
    case CMD_DELETE:
        if ((t = selected(a, &row)) != nullptr) {
            memmove(&a->tasks[row], &a->tasks[row + 1], (size_t)(a->count - row - 1) * sizeof a->tasks[0]);
            a->count--;
            changed(a); /* the selection moves to the neighbour */
        }
        break;
    case CMD_CLEAR: {
        gates_u32 k = 0;
        for (gates_u32 i = 0; i < a->count; i++) {
            if (!a->tasks[i].done) a->tasks[k++] = a->tasks[i];
        }
        a->count = k;
        changed(a);
        break;
    }
    case CMD_MENU: {
        gates_rect_t r = gates_node_layout_rect(tree, a->list);
        open_menu(a, (gates_point_t){ r.x + 24, r.y + 24 });
        break;
    }
    default:
        break;
    }
}

static void on_list(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    app_t *a = user;
    if (ev->kind == GATES_EVENT_ACTIVATED) {
        on_command(tree, CMD_DONE, a); /* Enter or a double click */
    } else if (ev->kind == GATES_EVENT_SELECTION_CHANGED) {
        show_status(a);
    }
}

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    app_t *a = user;
    if (!ev->down) return;
    if (ev->key == GATES_KEY_ESCAPE) {
        gates_app_quit(a->app);
    } else if (ev->key == GATES_KEY_DELETE && gates_node_eq(gates_tree_focus(a->tree), a->list) &&
               gates_command_enabled(a->tree, gates_tree_root(a->tree), CMD_DELETE)) {
        (void)gates_command_invoke(a->tree, gates_tree_root(a->tree), CMD_DELETE); /* Delete in the list */
    }
}

static void on_pointer(gates_window_t *win, const gates_pointer_event_t *ev, void *user) {
    (void)win;
    if (ev->action == GATES_POINTER_DOWN && ev->button == GATES_BUTTON_RIGHT) open_menu(user, ev->pos);
}

/* -- building -------------------------------------------------------------------------- */

static gates_err_t command(app_t *a, gates_command_id_t id, const char *label, gates_shortcut_t key,
                           gates_command_role_t role) {
    gates_command_desc_t c = { .id = id, .label = { .ptr = (const gates_u8 *)label, .size = strlen(label) },
                               .shortcut = key, .role = role, .enabled = true, .invoke = on_command, .user = a };
    return gates_command_register(a->tree, gates_tree_root(a->tree), &c);
}

static gates_err_t button(app_t *a, gates_node_t parent, gates_command_id_t id) {
    gates_node_t b = GATES_NODE_NULL;
    TRY(gates_button_create(a->tree, parent, GATES_STR(""), nullptr, nullptr, &b));
    return gates_button_set_command(a->tree, b, gates_tree_root(a->tree), id);
}

static gates_err_t build(app_t *a) {
    gates_tree_t *t = a->tree;
    gates_node_t root = gates_tree_root(t), label, row, bar;
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 8));

    TRY(command(a, CMD_ADD, "Add", (gates_shortcut_t){0}, GATES_COMMAND_DEFAULT));
    TRY(command(a, CMD_DONE, "Done (Ctrl+D)", (gates_shortcut_t){ .letter = 'D', .ctrl = true },
                GATES_COMMAND_NORMAL));
    /* Shortcuts are Ctrl combinations or function keys; Delete is handled in on_key. */
    TRY(command(a, CMD_DELETE, "Delete (Del)", (gates_shortcut_t){0}, GATES_COMMAND_NORMAL));
    TRY(command(a, CMD_CLEAR, "Clear done (Ctrl+L)", (gates_shortcut_t){ .letter = 'L', .ctrl = true },
                GATES_COMMAND_NORMAL));
    TRY(command(a, CMD_MENU, "Menu", (gates_shortcut_t){ .key = GATES_KEY_F10, .shift = true }, GATES_COMMAND_NORMAL));

    TRY(gates_label_create(t, root, GATES_STR("New task"), &label));
    TRY(gates_panel_create(t, root, &row));
    TRY(gates_layout_set(t, row, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, row, 8));
    TRY(gates_textbox_create(t, row, GATES_STR(""), 40, &a->entry));
    TRY(gates_layout_set_child_grow(t, a->entry, 1));
    TRY(gates_textbox_set_max_bytes(t, a->entry, MAX_TEXT - 1));
    TRY(gates_node_set_labelled_by(t, a->entry, label));
    TRY(button(a, row, CMD_ADD));

    TRY(gates_view_create(t, root, &(gates_view_desc_t){0}, &a->list));
    TRY(gates_node_set_access_name(t, a->list, GATES_STR("Tasks")));
    TRY(gates_layout_set_child_grow(t, a->list, 1));
    gates_rows_model_t model = { .user = a, .revision = m_revision, .count = m_count, .id_at = m_id_at,
                                 .index_of = m_index_of, .cell = m_cell };
    TRY(gates_view_set_model(t, a->list, &model));
    TRY(gates_widget_set_handler(t, a->list, on_list, a));

    TRY(gates_panel_create(t, root, &bar));
    TRY(gates_layout_set(t, bar, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, bar, 8));
    TRY(button(a, bar, CMD_DONE));
    TRY(button(a, bar, CMD_DELETE));
    TRY(button(a, bar, CMD_CLEAR));

    TRY(gates_label_create(t, root, GATES_STR(""), &a->status));
    TRY(gates_node_set_live(t, a->status, GATES_LIVE_POLITE));
    if (a->count > 0) (void)gates_view_set_selected(t, a->list, a->tasks[0].id);
    show_status(a);
    gates_tree_set_focus(t, a->entry);
    return GATES_OK;
}

int main(void) {
    static app_t a = { .next_id = 1 };
    load(&a);
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &a.app))) return 1;
    gates_window_callbacks_t cb = { .on_key = on_key, .on_pointer = on_pointer, .user_data = &a,
                                    .on_input_error = on_input_error };
    gates_window_desc_t desc = { .title = GATES_STR("To-do"), .size = { 480, 460 } };
    gates_window_t *win = nullptr;
    if (!gates_is_ok(gates_window_create(a.app, &desc, &cb, &win))) {
        gates_app_destroy(a.app);
        return 1;
    }
    a.tree = gates_window_tree(win);
    gates_err_t err = build(&a);
    if (gates_is_ok(err)) err = gates_app_run(a.app);
    gates_window_destroy(win);
    gates_app_destroy(a.app);
    return gates_is_ok(err) ? 0 : 1;
}
