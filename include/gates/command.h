/* gates_gui_lib - one command model for buttons, shortcuts and menus
 * (plan-0009, RFC-0003 section 5.2).
 *
 * A command is an action the application offers, with a label, an optional
 * shortcut, and enabled/checked state that the application keeps current.
 * Commands live in a scope: the tree root for the whole window, or a dialog.
 * Buttons (and later menus) refer to a command by scope and id and take its
 * label and enabled state from it. Invoking a command - by shortcut, bound
 * button, menu or gates_command_invoke - is queued and delivered at the same
 * safe point as widget events; the command is looked up again and must still
 * be enabled when it runs. Each delivered invocation runs at most once.
 * Handlers run on the UI thread, return quickly, and may unregister commands
 * or destroy nodes. Platform-free. */
#ifndef GATES_COMMAND_H
#define GATES_COMMAND_H

#include <gates/tree.h>
#include <gates/input.h>

typedef gates_u32 gates_command_id_t;   /* 0 means "no command" */

/* A shortcut needs Ctrl (without Alt: Ctrl+Alt is AltGr on some layouts), or
 * is a function key F1-F12. Typed characters are never shortcuts. The key is
 * either a semantic key or, with key = GATES_KEY_NONE, a letter/digit. */
typedef struct gates_shortcut_t {
    gates_key_t key;
    gates_u8 letter;        /* 'A'-'Z' or '0'-'9' when key is GATES_KEY_NONE */
    bool ctrl;
    bool shift;
    bool alt;
} gates_shortcut_t;

typedef enum gates_command_role_t {
    GATES_COMMAND_NORMAL = 0,
    GATES_COMMAND_DEFAULT,  /* Enter in a textbox/checkbox of the scope runs it */
    GATES_COMMAND_CANCEL,   /* Escape in the scope runs it */
} gates_command_role_t;

typedef void (*gates_command_fn)(gates_tree_t *tree, gates_command_id_t id, void *user);

typedef struct gates_command_desc_t {
    gates_command_id_t id;
    gates_str_t label;          /* copied */
    gates_shortcut_t shortcut;  /* all zero = none */
    gates_command_role_t role;
    bool enabled;
    bool checked;
    gates_command_fn invoke;    /* borrowed while registered */
    void *user;
} gates_command_desc_t;

/* INVALID_ARG: id 0, null invoke, bad scope or an invalid shortcut.
 * INVALID_STATE: the scope already has this id, this shortcut, or a command
 * with the same DEFAULT/CANCEL role. */
[[nodiscard]] gates_err_t gates_command_register(gates_tree_t *tree, gates_node_t scope,
                                                 const gates_command_desc_t *desc);
[[nodiscard]] gates_err_t gates_command_unregister(gates_tree_t *tree, gates_node_t scope,
                                                   gates_command_id_t id);
/* Setters repaint bound buttons; identical values are no-ops. */
[[nodiscard]] gates_err_t gates_command_set_enabled(gates_tree_t *tree, gates_node_t scope,
                                                    gates_command_id_t id, bool enabled);
[[nodiscard]] gates_err_t gates_command_set_checked(gates_tree_t *tree, gates_node_t scope,
                                                    gates_command_id_t id, bool checked);
[[nodiscard]] gates_err_t gates_command_set_label(gates_tree_t *tree, gates_node_t scope,
                                                  gates_command_id_t id, gates_str_t label);
bool gates_command_exists(const gates_tree_t *tree, gates_node_t scope, gates_command_id_t id);
bool gates_command_enabled(const gates_tree_t *tree, gates_node_t scope, gates_command_id_t id);
bool gates_command_checked(const gates_tree_t *tree, gates_node_t scope, gates_command_id_t id);
gates_str_t gates_command_label(const gates_tree_t *tree, gates_node_t scope,
                                gates_command_id_t id);

/* Queues an invocation (delivered by gates_tree_dispatch_events).
 * NOT_FOUND when the command does not exist. */
[[nodiscard]] gates_err_t gates_command_invoke(gates_tree_t *tree, gates_node_t scope,
                                               gates_command_id_t id);

/* Binds a button to a command: it shows the command's label, looks disabled
 * (and cannot be focused or pressed) while the command is disabled or gone,
 * and activating it invokes the command. id 0 unbinds. */
[[nodiscard]] gates_err_t gates_button_set_command(gates_tree_t *tree, gates_node_t button,
                                                   gates_node_t scope, gates_command_id_t id);

#endif /* GATES_COMMAND_H */
