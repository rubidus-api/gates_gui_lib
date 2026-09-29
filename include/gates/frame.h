/* gates_gui_lib - the application frame (plan-0018): mnemonics, menu bar.
 *
 * Mnemonics. In the text of a button, check box, command, menu bar title or
 * tab, "&x" marks x as the mnemonic when x is a letter or digit (ASCII); "&&"
 * shows one '&'; any other '&' is shown as it is, so "Save & close" needs no
 * escaping. A label parses '&' only when it has a target (it often shows data
 * such as file names). Alt+x activates the control: a button is pressed, a
 * check box toggled, a label's target focused, a menu bar title opened. When
 * several reachable controls share the letter, each Alt+x moves the focus to
 * the next of them without activating anything. Menu bar titles come first.
 * The mnemonic letter is underlined while keyboard cues are visible: from an
 * Alt press or keyboard menu mode until the next pointer press, or always when
 * the platform says so. gates_widget_text returns the text as it was set;
 * accessible names drop the markup.
 *
 * Menu bar. One per tree: a row of titles, each over a list of commands of the
 * bar's scope (id 0 = separator), shown as an ordinary menu overlay (same
 * dismissal rules, same re-checked invocation, same MENU_CLOSED report). A
 * click on a title opens its menu; while one is open, moving over another title
 * switches to it and a click on the open title closes it. Keyboard: F10 (when
 * there is a reachable menu bar; otherwise F10 stays a command shortcut) or Alt
 * released alone enters menu mode - the first title is highlighted; Left/Right
 * move, Down/Up/Enter/Space open, a title's mnemonic letter opens it, Escape
 * leaves. In an open menu Left/Right go to the neighbouring menu, Escape goes
 * back to the highlighted title, an entry's mnemonic letter chooses it. The bar
 * is not a Tab stop; a modal dialog makes it unreachable. No submenus yet.
 * Platform-free. */
#ifndef GATES_FRAME_H
#define GATES_FRAME_H

#include <gates/tree.h>
#include <gates/command.h>
#include <gates/input.h>

/* -- mnemonics ------------------------------------------------------------------ */

/* The mnemonic of a text: 'A'-'Z' or '0'-'9' (upper case) for the first "&x",
 * or 0 when there is none. */
gates_u8 gates_mnemonic_of(gates_str_t text);

/* Gives a label a target: its text becomes mnemonic markup and Alt+x focuses
 * the target. GATES_NODE_NULL removes the target (the text is plain again). */
[[nodiscard]] gates_err_t gates_label_set_target(gates_tree_t *tree, gates_node_t label,
                                                 gates_node_t target);
gates_node_t gates_label_target(const gates_tree_t *tree, gates_node_t label);

/* Alt+codepoint (the platform's system character). true when a mnemonic took
 * it; false leaves it to the platform (Alt+F4, Alt+Space and unknown letters). */
bool gates_input_mnemonic(gates_tree_t *tree, gates_u32 codepoint);
/* Alt was pressed and released alone (or the platform's menu key): enters menu
 * mode on the menu bar, or leaves it when already in it. false when there is no
 * reachable menu bar (the platform then does its own thing). */
bool gates_input_menu_key(gates_tree_t *tree);
/* Alt went down: keyboard cues become visible until the next pointer press. */
void gates_input_show_cues(gates_tree_t *tree);
/* The platform's setting "always underline access keys". */
void gates_tree_set_cues_always(gates_tree_t *tree, bool always);
bool gates_tree_cues_visible(const gates_tree_t *tree);

/* -- menu bar --------------------------------------------------------------------- */

/* Creates the tree's menu bar (INVALID_STATE when it already has a live one).
 * Its menus list commands of `scope`. */
[[nodiscard]] gates_err_t gates_menubar_create(gates_tree_t *tree, gates_node_t parent,
                                               gates_node_t scope, gates_node_t *out_bar);
/* Adds a menu: its title (copied, mnemonic markup) and its command ids (copied;
 * 0 = separator; count >= 1). *out_index (optional) receives its position. */
[[nodiscard]] gates_err_t gates_menubar_add(gates_tree_t *tree, gates_node_t bar,
                                            gates_str_t title, const gates_command_id_t *ids,
                                            gates_u32 count, gates_u32 *out_index);
gates_u32 gates_menubar_count(const gates_tree_t *tree, gates_node_t bar);
gates_str_t gates_menubar_title(const gates_tree_t *tree, gates_node_t bar, gates_u32 index);
/* Opens menu `index` as if chosen from the keyboard (menu mode, first entry
 * selected). OUT_OF_BOUNDS for a bad index; INVALID_STATE when the bar is not
 * reachable. */
[[nodiscard]] gates_err_t gates_menubar_open(gates_tree_t *tree, gates_node_t bar,
                                             gates_u32 index);
/* The open menu's index and overlay node, or -1 / GATES_NODE_NULL. */
gates_i32 gates_menubar_open_index(const gates_tree_t *tree, gates_node_t bar);
gates_node_t gates_menubar_menu(const gates_tree_t *tree, gates_node_t bar);
/* In menu mode (a title highlighted or a menu open from the bar). */
bool gates_menubar_active(const gates_tree_t *tree, gates_node_t bar);
/* The highlighted title in menu mode, or -1. */
gates_i32 gates_menubar_highlighted(const gates_tree_t *tree, gates_node_t bar);

#endif /* GATES_FRAME_H */
