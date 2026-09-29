/* manual example (host): Undo and Redo commands over the program's own data.
 * expect: menu shows "Undo Rename"; after Undo: name Draft, menu shows "Redo Rename"; clean again: yes */
#include <gates/gates.h>

#include <stdio.h>
#include <string.h>

enum { CMD_UNDO = 1, CMD_REDO = 2 };

static char name[32] = "Draft";

/* One entry remembers the name before and after the change. */
typedef struct rename_t {
    char before[32], after[32];
} rename_t;

static gates_err_t undo_rename(void *data) {
    memcpy(name, ((rename_t *)data)->before, sizeof name);
    return GATES_OK;
}
static gates_err_t redo_rename(void *data) {
    memcpy(name, ((rename_t *)data)->after, sizeof name);
    return GATES_OK;
}

static void on_command(gates_tree_t *tree, gates_command_id_t id, void *user) {
    (void)tree;
    gates_undo_t *undo = user;
    (void)(id == CMD_UNDO ? gates_undo_undo(undo) : gates_undo_redo(undo));
}

static void print_label(gates_tree_t *t, gates_command_id_t id) {
    gates_str_t l = gates_command_label(t, gates_tree_root(t), id);
    printf("\"%.*s\"", (int)l.size, (const char *)l.ptr);
}

int main(void) {
    gates_tree_t *t = nullptr;
    gates_undo_t *undo = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t)) ||
        !gates_is_ok(gates_undo_create((gates_allocator_t){0}, 0, &undo))) {
        return 1;
    }
    gates_node_t root = gates_tree_root(t);
    gates_command_desc_t cmds[] = {
        { .id = CMD_UNDO, .label = GATES_STR_INIT("Undo"), .shortcut = { .key = GATES_KEY_Z, .ctrl = true },
          .enabled = true, .invoke = on_command, .user = undo },
        { .id = CMD_REDO, .label = GATES_STR_INIT("Redo"), .shortcut = { .key = GATES_KEY_Y, .ctrl = true },
          .enabled = true, .invoke = on_command, .user = undo },
    };
    if (!gates_is_ok(gates_command_register(t, root, &cmds[0])) || !gates_is_ok(gates_command_register(t, root, &cmds[1])) ||
        !gates_is_ok(gates_undo_bind(undo, t, root, CMD_UNDO, CMD_REDO, GATES_STR("Undo"), GATES_STR("Redo")))) {
        return 1;
    }
    gates_undo_mark_clean(undo); /* the name as saved */

    /* The program renames, then records how to take it back. */
    static rename_t change = { "Draft", "Final" };
    memcpy(name, change.after, sizeof name);
    gates_undo_entry_t entry = { .label = GATES_STR_INIT("Rename"), .undo = undo_rename, .undo_data = &change,
                                 .redo = redo_rename, .redo_data = &change };
    if (!gates_is_ok(gates_undo_push(undo, &entry))) return 1;
    printf("menu shows ");
    print_label(t, CMD_UNDO);

    /* A person presses Ctrl+Z. */
    gates_key_event_t ctrl_z = { .key = GATES_KEY_Z, .ctrl = true, .down = true };
    (void)gates_input_key(t, &ctrl_z);
    (void)gates_tree_dispatch_events(t, 0);
    printf("; after Undo: name %s, menu shows ", name);
    print_label(t, CMD_REDO);
    printf("; clean again: %s\n", gates_undo_is_clean(undo) ? "yes" : "no");

    gates_undo_destroy(undo);
    gates_tree_destroy(t);
    return 0;
}
