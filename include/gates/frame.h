/* gates_gui_lib - the application frame (plan-0018): mnemonics, menu bar,
 * toolbar, status bar, tooltips, tabs.
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

/* -- toolbar -------------------------------------------------------------------------
 *
 * A row of compact buttons, each bound to a command of the bar's scope (label,
 * enabled and checked state come from the command; a checked command shows its
 * button pressed). Flat until hovered or focused. One Tab stop: Left/Right/
 * Home/End move between buttons, Space or Enter invokes. A click invokes
 * without taking the focus away from where the person was working (so Cut and
 * Paste buttons act on the focused text box). Buttons that do not fit are left
 * out and a ">>" button at the end lists their commands in a menu. Every
 * button's tooltip is its command's label and shortcut. */

[[nodiscard]] gates_err_t gates_toolbar_create(gates_tree_t *tree, gates_node_t parent,
                                               gates_node_t scope, gates_node_t *out_bar);
/* Adds a button for command `id` (0 = a separator). */
[[nodiscard]] gates_err_t gates_toolbar_add(gates_tree_t *tree, gates_node_t bar,
                                            gates_command_id_t id);
/* Entries (buttons and separators), and how many are shown after the last layout
 * (the rest are in the ">>" menu). */
gates_u32 gates_toolbar_count(const gates_tree_t *tree, gates_node_t bar);
gates_u32 gates_toolbar_shown(const gates_tree_t *tree, gates_node_t bar);

/* -- status bar ------------------------------------------------------------------------
 *
 * A row of text segments along the bottom of a window, separated by thin
 * lines. A segment is a label: change it with gates_widget_set_text. Segments
 * are not announced unless the program makes one a live region
 * (gates_node_set_live), so a clock does not chatter. */

[[nodiscard]] gates_err_t gates_statusbar_create(gates_tree_t *tree, gates_node_t parent,
                                                 gates_node_t *out_bar);
/* Adds a segment (a label, text copied). grow > 0 takes a share of the spare
 * width, like a layout child's grow. */
[[nodiscard]] gates_err_t gates_statusbar_add(gates_tree_t *tree, gates_node_t bar,
                                              gates_str_t text, gates_u8 grow,
                                              gates_node_t *out_segment);

/* -- tooltips ---------------------------------------------------------------------------
 *
 * A short help text shown in a small box below a node (above when there is no
 * room) after the pointer rests on it for GATES_TOOLTIP_DELAY_MS, or that long
 * after keyboard focus reaches it; it hides on a press, a key, when the pointer
 * or the focus leaves, when the node is disabled, hidden or destroyed, and
 * after GATES_TOOLTIP_SHOW_MS. Moving from one node with a tooltip to another
 * while one is shown switches at once. It never takes input. Assistive
 * technology reads it as the node's help text. Tooltips need the tree's clock
 * (every window has one); nothing is timed while no hovered or focused node has
 * a tooltip. */
#define GATES_TOOLTIP_DELAY_MS 500u
#define GATES_TOOLTIP_SHOW_MS 10000u

/* Copied; an empty text removes it. */
[[nodiscard]] gates_err_t gates_node_set_tooltip(gates_tree_t *tree, gates_node_t node,
                                                 gates_str_t text);
gates_str_t gates_node_tooltip(const gates_tree_t *tree, gates_node_t node);
/* The tooltip shown now: true with its node, item (a toolbar button: its
 * entry index + 1, else 0), text (borrowed until the next change) and box
 * (window coordinates, after layout). */
bool gates_tooltip_shown(const gates_tree_t *tree, gates_node_t *node, gates_u64 *item,
                         gates_str_t *text, gates_rect_t *box);

/* -- tabs ----------------------------------------------------------------------------------
 *
 * A strip of titles over pages, one page shown at a time. gates_tabs_add
 * returns the page: a column panel for the application's controls. The strip
 * is one Tab stop (the selected tab): Left/Right/Home/End select at once (no
 * wrapping); Ctrl+Tab / Ctrl+Shift+Tab and Ctrl+PgDn / Ctrl+PgUp switch from
 * anywhere inside the tabs (wrapping); a click on a title selects it and
 * focuses the strip; a title's mnemonic selects it. When the focus was inside
 * the page that goes away, it moves into the new page (its first control), or
 * to the strip. A person's switch queues GATES_EVENT_VALUE_CHANGED on the tabs
 * node (ev->result = the new index); the program's gates_tabs_set_selected is
 * silent. Titles that do not fit are cut off at the right edge in this version. */

[[nodiscard]] gates_err_t gates_tabs_create(gates_tree_t *tree, gates_node_t parent,
                                            gates_node_t *out_tabs);
/* Adds a tab with a title (copied, mnemonic markup) and returns its page. */
[[nodiscard]] gates_err_t gates_tabs_add(gates_tree_t *tree, gates_node_t tabs, gates_str_t title,
                                         gates_node_t *out_page);
[[nodiscard]] gates_err_t gates_tabs_set_title(gates_tree_t *tree, gates_node_t tabs, gates_u32 index,
                                               gates_str_t title);
gates_u32 gates_tabs_count(const gates_tree_t *tree, gates_node_t tabs);
gates_str_t gates_tabs_title(const gates_tree_t *tree, gates_node_t tabs, gates_u32 index);
gates_node_t gates_tabs_page(const gates_tree_t *tree, gates_node_t tabs, gates_u32 index);
/* Silent. OUT_OF_BOUNDS for a bad index. */
[[nodiscard]] gates_err_t gates_tabs_set_selected(gates_tree_t *tree, gates_node_t tabs, gates_u32 index);
gates_u32 gates_tabs_selected(const gates_tree_t *tree, gates_node_t tabs);

#endif /* GATES_FRAME_H */
