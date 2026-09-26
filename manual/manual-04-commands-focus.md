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
