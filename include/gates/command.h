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

/* -- keymap (plan-0018, RFC-0005 A4) ---------------------------------------------
 * The per-command shortcut is the keymap: a program can list a scope's commands,
 * rebind them (a user's keymap from a file) and print shortcuts the same way
 * menus and accessibility do. */

/* Rebinds (an all-zero shortcut removes it). INVALID_ARG for an invalid
 * shortcut; INVALID_STATE when another command of the scope has it, with that
 * command's id in *conflict (optional; 0 otherwise). NOT_FOUND: no command. */
[[nodiscard]] gates_err_t gates_command_set_shortcut(gates_tree_t *tree, gates_node_t scope,
                                                     gates_command_id_t id,
                                                     gates_shortcut_t shortcut,
                                                     gates_command_id_t *conflict);
/* The command's shortcut (all zero when none or no command). */
gates_shortcut_t gates_command_shortcut(const gates_tree_t *tree, gates_node_t scope,
                                        gates_command_id_t id);
/* The scope's live commands, in storage order (stable while none of the scope
 * is registered or unregistered). gates_command_at answers 0 past the end. */
gates_u32 gates_command_count(const gates_tree_t *tree, gates_node_t scope);
gates_command_id_t gates_command_at(const gates_tree_t *tree, gates_node_t scope,
                                    gates_u32 index);
/* "Ctrl+Shift+S", "F5", "Ctrl+Del" (empty for no shortcut) written to buf with a
 * terminating NUL when cap > 0 (cut to fit); returns the length it needs,
 * without the NUL. */
gates_usize_t gates_shortcut_format(const gates_shortcut_t *shortcut, char *buf,
                                    gates_usize_t cap);
/* Reads what gates_shortcut_format writes (case-insensitive; "Control", "Delete",
 * "Escape", "PageUp" and "PageDown" are accepted too). Empty text is the empty
 * shortcut. INVALID_ARG for unknown names or an invalid shortcut (no Ctrl and
 * no function key, or Alt). */
[[nodiscard]] gates_err_t gates_shortcut_parse(gates_str_t text, gates_shortcut_t *out);

/* Binds a button to a command: it shows the command's label, looks disabled
 * (and cannot be focused or pressed) while the command is disabled or gone,
 * and activating it invokes the command. id 0 unbinds. */
[[nodiscard]] gates_err_t gates_button_set_command(gates_tree_t *tree, gates_node_t button,
                                                   gates_node_t scope, gates_command_id_t id);

#endif /* GATES_COMMAND_H */
