# Chapter 4 - Commands, focus, dialogs and menus

Headers: `gates/command.h`, `gates/overlay.h`.

## One action, many ways to ask for it

"Save" is one action whether the person clicks a button, presses Ctrl+S or picks it from a
context menu. A command holds it once: an id, a label, an optional shortcut, enabled and
checked states, and the function to run. Commands belong to a scope node; a shortcut works
while focus is inside the scope. Buttons bound to a command (`gates_button_set_command`) take
its label and enabled state, and follow it when it changes: disable the command and every way
to invoke it is disabled at once.

An invocation is queued and runs at the next safe point, after the command is checked again -
a command disabled in between does not run.

<!-- example: manual/examples/ex_05_commands.c -->
```c
/* manual example (host): one command for a button and a shortcut.
 * expect: saved 1 time; button disabled with the command */
#include <gates/gates.h>

#include <stdio.h>

enum { CMD_SAVE = 1 };

static void on_save(gates_tree_t *tree, gates_command_id_t id, void *user) {
    (void)tree;
    (void)id;
    (*(int *)user)++;
}

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_node_t root = gates_tree_root(t), save;
    int saves = 0;
    gates_command_desc_t cmd = { .id = CMD_SAVE, .label = GATES_STR("Save"), .enabled = true,
                                 .shortcut = { .letter = 'S', .ctrl = true },
                                 .invoke = on_save, .user = &saves };
    if (!gates_is_ok(gates_command_register(t, root, &cmd)) ||
        !gates_is_ok(gates_button_create(t, root, GATES_STR(""), nullptr, nullptr, &save)) ||
        !gates_is_ok(gates_button_set_command(t, save, root, CMD_SAVE))) { /* label, state from the command */
        return 1;
    }
    /* Ctrl+S: the command is queued and runs at the next safe point. */
    gates_key_event_t ctrl_s = { .letter = 'S', .ctrl = true, .down = true };
    (void)gates_input_key(t, &ctrl_s);
    (void)gates_tree_dispatch_events(t, 0);

    /* Disabling the command disables every button bound to it, and its shortcut. */
    if (!gates_is_ok(gates_command_set_enabled(t, root, CMD_SAVE, false))) return 1;
    (void)gates_input_key(t, &ctrl_s);
    (void)gates_tree_dispatch_events(t, 0);

    gates_access_info_t info;
    if (!gates_is_ok(gates_access_info(t, save, 0, &info))) return 1;
    printf("saved %d time%s; button %s with the command\n", saves, saves == 1 ? "" : "s",
           (info.states & GATES_ACCESS_DISABLED) != 0 ? "disabled" : "enabled");
    gates_tree_destroy(t);
    return 0;
}
```

A command may have a role: the scope's DEFAULT command runs when Enter is pressed in a text box
or check box of the scope, its CANCEL command on Escape anywhere in the scope (for a dialog: OK
and Cancel).

## Focus

One control has keyboard focus (`gates_tree_focus`, `gates_tree_set_focus`). Tab and Shift+Tab
move it in tree order and wrap inside the current focus scope; the window's scope is the whole
tree, an open dialog's scope is the dialog. A focused control that is hidden or destroyed lets
focus go (a press or drag inside it is let go too); the program decides where focus goes next
with `gates_tree_set_focus`. When a dialog closes, focus returns to the control that had it.

## Dialogs

`gates_dialog_open` shows a modal dialog inside the window: the rest of the window is dimmed
and ignores input, Tab stays inside, and the window's own shortcuts wait. Opening returns at
once - there is no nested event loop. The program fills the dialog's content panel, registers
the dialog's commands with the dialog as scope, and learns the answer from one
DIALOG_CLOSED event (`ev->result`: ACCEPTED or CANCELED). Focus returns to where it was. At most
eight dialogs and menus are open at a time.

## Context menus

`gates_menu_open` lists commands of a scope at a point: their labels, check marks, shortcuts and
enabled states come from the commands. Choosing an entry invokes its command through the same
queued, checked path as a shortcut. Arrows move, Enter chooses, Escape or a click outside only
closes (a click outside never also clicks what is under it).

## Undo and redo

Text boxes undo their own typing. For the program's own data - a renamed item, a moved row, a
changed setting - `gates_undo_create` (gates/undo.h) gives a stack: after making a change, push an
entry with a label and two functions that take it back and make it again. Entries with the same
merge key in a row become one (typing into one field), the stack keeps a bounded number, and
`gates_undo_mark_clean` / `gates_undo_is_clean` tell whether the data is as it was saved.
`gates_undo_bind` keeps two commands in step: enabled only when there is something to undo or
redo, and labelled "Undo Rename" / "Redo Rename" (with words in the program's language).

<!-- example: manual/examples/ex_04_undo.c -->
```c
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
```
