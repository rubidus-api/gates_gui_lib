/* gates_gui_lib - transient surfaces: modal dialogs and context menus as
 * overlays inside the window.
 *
 * Overlays sit above the window's content: they are laid out after it (a
 * dialog centred, a menu at its anchor, both kept inside the window), painted
 * on top of it and hit first. One set of rules dismisses them: a menu closes
 * on Escape, on an outside click (the click goes no further), when the window
 * loses focus, or when its commands' scope is destroyed. A dialog is modal:
 * input below it is blocked, it is the focus scope (Tab cycles inside, only
 * its commands' shortcuts apply, Enter runs its default command, Escape its
 * cancel command or else cancels it). Every dialog and menu reports its end
 * once, through its handler (GATES_EVENT_DIALOG_CLOSED / _MENU_CLOSED), and
 * its node is destroyed after that delivery: during the CLOSED handler the
 * dialog's content is still valid, so read what you need there. Opening a
 * dialog cancels any press or drag in progress below it. There is no nested event loop:
 * opening returns at once. At most 8 overlays are open at a time.
 * Platform-free. */
#ifndef GATES_OVERLAY_H
#define GATES_OVERLAY_H

#include <gates/tree.h>
#include <gates/command.h>

typedef enum gates_dialog_result_t {
    GATES_DIALOG_ACCEPTED = 1,
    GATES_DIALOG_CANCELED = 2,
} gates_dialog_result_t;

typedef struct gates_dialog_desc_t {
    gates_str_t title;          /* copied */
} gates_dialog_desc_t;

/* Opens a modal dialog. *out_dialog is the dialog (register its commands with
 * it as scope; set a handler on it for DIALOG_CLOSED); *out_content is a
 * column panel for the application's controls. The first focusable control
 * gets focus at the next layout run. OUT_OF_BOUNDS when 8 overlays are open. */
[[nodiscard]] gates_err_t gates_dialog_open(gates_tree_t *tree, const gates_dialog_desc_t *desc,
                                            gates_node_t *out_dialog, gates_node_t *out_content);
/* Ends the dialog once with this result: removes it, restores the focus that
 * was current when it opened (or the next control), and queues DIALOG_CLOSED.
 * INVALID_STATE when it is not an open dialog. On failure to queue the event
 * the dialog stays open (nothing is lost). */
[[nodiscard]] gates_err_t gates_dialog_close(gates_tree_t *tree, gates_node_t dialog,
                                             gates_dialog_result_t result);

/* Opens a context menu at `at` (window coordinates) listing the commands of
 * `scope` in order; id 0 draws a separator. Labels, checked marks, shortcuts
 * and enabled states come from the commands. Choosing an entry invokes its
 * command through the same queued, re-checked path as a shortcut.
 * Submenus (0.10.0, gates_command_set_submenu): an entry that has one shows an
 * arrow and opens it beside itself - Right, Enter, Space, a click, or the
 * pointer resting on it for GATES_MENU_SUB_DELAY_MS; Left or Escape close it;
 * another row of the parent closes it. Choosing in a submenu closes the whole
 * chain, and the menu the program opened reports MENU_CLOSED with the id. A
 * press outside every menu of the chain closes them all. Submenus count
 * against the 8 overlays. */
#define GATES_MENU_SUB_DELAY_MS 300u
[[nodiscard]] gates_err_t gates_menu_open(gates_tree_t *tree, gates_point_t at,
                                          gates_node_t scope, const gates_command_id_t *ids,
                                          gates_u32 count, gates_node_t *out_menu);
/* Closes a menu without invoking anything (reported with result 0). */
[[nodiscard]] gates_err_t gates_menu_close(gates_tree_t *tree, gates_node_t menu);

/* Closes every open menu (the platform calls this when the window loses
 * focus); dialogs stay. */
void gates_tree_dismiss_menus(gates_tree_t *tree);
/* Open overlays, topmost last. */
gates_u32 gates_tree_overlay_count(const gates_tree_t *tree);
bool gates_overlay_is_open(const gates_tree_t *tree, gates_node_t overlay);

#endif /* GATES_OVERLAY_H */
