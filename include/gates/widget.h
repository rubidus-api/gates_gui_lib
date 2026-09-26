/* gates_gui_lib — L1 primitive widgets (RFC-0001 §12 level 1, Phase 2 set):
 * panel, label, button, checkbox. Widgets are tree nodes with semantic
 * state; they emit theme-token draw commands during the paint walk and
 * receive interaction through the pointer routing (gates/hit via window).
 * Platform-free. */
#ifndef GATES_WIDGET_H
#define GATES_WIDGET_H

#include <gates/tree.h>
#include <gates/geometry.h>
#include <gates/text_edit.h>

typedef void (*gates_click_fn)(gates_tree_t *tree, gates_node_t node, void *user);
typedef void (*gates_toggle_fn)(gates_tree_t *tree, gates_node_t node, bool checked,
                                void *user);

/* Creation: parent may be GATES_NODE_NULL (attach later). Text is copied. */
[[nodiscard]] gates_err_t gates_panel_create(gates_tree_t *tree, gates_node_t parent,
                                             gates_node_t *out_node);
[[nodiscard]] gates_err_t gates_label_create(gates_tree_t *tree, gates_node_t parent,
                                             gates_str_t text, gates_node_t *out_node);
[[nodiscard]] gates_err_t gates_button_create(gates_tree_t *tree, gates_node_t parent,
                                              gates_str_t text, gates_click_fn on_click,
                                              void *user, gates_node_t *out_node);
[[nodiscard]] gates_err_t gates_checkbox_create(gates_tree_t *tree, gates_node_t parent,
                                                gates_str_t text, bool checked,
                                                gates_toggle_fn on_toggle, void *user,
                                                gates_node_t *out_node);

/* Single-line textbox. `cols` is the intrinsic width in cells (0 -> 16).
 * Editing state lives in a gates_text_edit_t owned by the widget. */
[[nodiscard]] gates_err_t gates_textbox_create(gates_tree_t *tree, gates_node_t parent,
                                               gates_str_t text, gates_u32 cols,
                                               gates_node_t *out_node);
/* Replaces the whole text (copied) and puts the caret at the end. Returns
 * PROVEN_ERR_BUSY while an IME composition is open in this box (commit or
 * cancel it first); on failure the old text is kept. gates_widget_set_text
 * on a textbox does the same. */
[[nodiscard]] gates_err_t gates_textbox_set_text(gates_tree_t *tree, gates_node_t node,
                                                 gates_str_t text);
/* Borrowed committed text (invalid after the next edit). */
gates_str_t gates_textbox_text(const gates_tree_t *tree, gates_node_t node);
/* Copies the text into buf. *needed always receives the text's size; with
 * buf == null only the size is reported (OK), a too small cap returns
 * OVERFLOW and copies nothing. Works for password boxes (explicit getter). */
[[nodiscard]] gates_err_t gates_textbox_copy_text(const gates_tree_t *tree, gates_node_t node,
                                                  gates_u8 *buf, gates_usize_t cap,
                                                  gates_usize_t *needed);

/* Selection in UTF-8 byte offsets: text between anchor and caret. Offsets
 * inside a multibyte sequence or past the end are rejected (INVALID_ARG),
 * never clamped. BUSY while a composition is open. Editing is per codepoint,
 * not per grapheme cluster. */
[[nodiscard]] gates_err_t gates_textbox_selection(const gates_tree_t *tree, gates_node_t node,
                                                  gates_u32 *anchor, gates_u32 *caret);
[[nodiscard]] gates_err_t gates_textbox_set_selection(gates_tree_t *tree, gates_node_t node,
                                                      gates_u32 anchor, gates_u32 caret);
/* Replaces the selection with text (copied): silent (no event), one undo unit,
 * BUSY while composing, OUT_OF_BOUNDS over the maximum length. */
[[nodiscard]] gates_err_t gates_textbox_replace_selection(gates_tree_t *tree,
                                                          gates_node_t node, gates_str_t text);

/* Read-only: focus, selection and copy work; editing, paste, cut, undo and IME
 * composition are refused. */
[[nodiscard]] gates_err_t gates_textbox_set_read_only(gates_tree_t *tree, gates_node_t node,
                                                      bool read_only);
bool gates_textbox_read_only(const gates_tree_t *tree, gates_node_t node);
/* Maximum length in UTF-8 bytes (0 = unlimited). OUT_OF_BOUNDS when the
 * current text is already longer. User edits past it are refused whole and
 * reported with GATES_EVENT_LIMIT_EXCEEDED. */
[[nodiscard]] gates_err_t gates_textbox_set_max_bytes(gates_tree_t *tree, gates_node_t node,
                                                      gates_u32 max_bytes);
gates_u32 gates_textbox_max_bytes(const gates_tree_t *tree, gates_node_t node);
/* Password: one '*' per codepoint, no copy or cut, events carry no text, no
 * undo history, no IME. The widget does not promise secure erasure of copies
 * held by the process or the operating system. */
[[nodiscard]] gates_err_t gates_textbox_set_password(gates_tree_t *tree, gates_node_t node,
                                                     bool password);
bool gates_textbox_password(const gates_tree_t *tree, gates_node_t node);

/* Undo history bounds (defaults 64 entries, 16384 bytes of edit content;
 * allocation overhead is not counted). Oldest entries go first; when memory is
 * short the oldest are dropped and the edit still happens. set_text clears the
 * history. A read-only box keeps its history but reports nothing to undo or
 * redo until it is editable again. */
[[nodiscard]] gates_err_t gates_textbox_set_undo_limits(gates_tree_t *tree, gates_node_t node,
                                                        gates_u32 max_entries,
                                                        gates_u32 max_bytes);
bool gates_textbox_can_undo(const gates_tree_t *tree, gates_node_t node);
bool gates_textbox_can_redo(const gates_tree_t *tree, gates_node_t node);

/* The offer kept after GATES_EVENT_LIMIT_EXCEEDED. accept_fit inserts the part
 * that fits (codepoint boundary) where the refused edit would have gone, as a
 * user edit with its own event and undo unit; INVALID_STATE when there is no
 * offer or the text changed since. discard drops the offer. */
[[nodiscard]] gates_err_t gates_textbox_accept_fit(gates_tree_t *tree, gates_node_t node);
void gates_textbox_discard_rejected(gates_tree_t *tree, gates_node_t node);

/* DEPRECATED (RFC-0003 4.2): the mutable edit core, for the IME adapter's
 * history and advanced use. Changes made through it bypass events, limits and
 * undo; call gates_textbox_edit_commit afterwards. New code uses the safe
 * operations above. Null for non-textboxes. */
gates_text_edit_t *gates_textbox_edit(gates_tree_t *tree, gates_node_t node);
/* Re-validates caret and selection after raw edits, clears the undo history
 * and pending offer, bumps the revision and repaints. Cannot repair text that
 * was made invalid UTF-8. */
void gates_textbox_edit_commit(gates_tree_t *tree, gates_node_t node);

/* Error state (plan-0010, RFC-0003 4.2 validation display): the border is
 * drawn with GATES_COLOR_ERROR (a focused box also shows its focus ring inside
 * it). The text is untouched; validating is the application's job. */
[[nodiscard]] gates_err_t gates_textbox_set_invalid(gates_tree_t *tree, gates_node_t node,
                                                    bool invalid);
bool gates_textbox_invalid(const gates_tree_t *tree, gates_node_t node);

/* -- options: radio group and choice (plan-0010) -------------------------------
 *
 * Both are one node holding a list of options with stable ids; the
 * application reads the selected id, never a row number. Ids are nonzero and
 * unique within the node; labels are copied. 0 means "nothing selected".
 * A person's change queues GATES_EVENT_VALUE_CHANGED with ev->result = the
 * newly selected id (the change is not made when the event cannot be queued);
 * the setters below are silent, and every selection change bumps the revision.
 * The node is a single Tab stop, focusable while it has an enabled option.
 *
 * Radio group: every option is a row. Up/Left and Down/Right move to the
 * previous/next enabled option and select it (wrapping), Home/End go to the
 * first/last, Space selects the first enabled option when none is selected; a
 * click on a row (press and release on the same row) selects it.
 * Choice: shows the selected option's label; Space, Enter, Alt+Down or a click
 * open its option list, an overlay with the menu's rules (Up/Down, Enter or
 * Space chooses, Escape or an outside click only closes). The list also
 * closes when the window loses focus, or the choice is disabled, hidden, given
 * new options or destroyed. */
typedef struct gates_option_t {
    gates_u32 id;                /* nonzero, unique in its node */
    gates_str_t label;           /* copied */
    bool disabled;               /* shown, but cannot be selected by the person */
} gates_option_t;

/* selected_id: 0 or one of the ids. INVALID_ARG for a bad list (id 0,
 * duplicates) or an unknown selected_id; nothing is left behind on failure. */
[[nodiscard]] gates_err_t gates_radio_create(gates_tree_t *tree, gates_node_t parent,
                                             const gates_option_t *options, gates_u32 count,
                                             gates_u32 selected_id, gates_node_t *out_node);
[[nodiscard]] gates_err_t gates_choice_create(gates_tree_t *tree, gates_node_t parent,
                                              const gates_option_t *options, gates_u32 count,
                                              gates_u32 selected_id, gates_node_t *out_node);
/* Replaces the options (count may be 0). The selection stays when its id is
 * still there, otherwise it is cleared, silently. On failure the old list stays. */
[[nodiscard]] gates_err_t gates_options_set(gates_tree_t *tree, gates_node_t node,
                                            const gates_option_t *options, gates_u32 count);
/* Silent. 0 clears; an id that is not an option is INVALID_ARG. A disabled
 * option can be selected by the program. */
[[nodiscard]] gates_err_t gates_options_set_selected(gates_tree_t *tree, gates_node_t node,
                                                     gates_u32 id);
gates_u32 gates_options_selected(const gates_tree_t *tree, gates_node_t node);
[[nodiscard]] gates_err_t gates_options_set_enabled(gates_tree_t *tree, gates_node_t node,
                                                    gates_u32 id, bool enabled);
gates_u32 gates_options_count(const gates_tree_t *tree, gates_node_t node);
/* The choice's open option list (an overlay node), or GATES_NODE_NULL. */
gates_node_t gates_choice_list(const gates_tree_t *tree, gates_node_t choice);
bool gates_choice_list_open(const gates_tree_t *tree, gates_node_t choice);

/* -- separator and progress (plan-0010) ----------------------------------------- */

/* A one-pixel line (CONTROL_BORDER) with space around it: horizontal in a
 * column, vertical in a row. Not focusable. */
[[nodiscard]] gates_err_t gates_separator_create(gates_tree_t *tree, gates_node_t parent,
                                                 gates_node_t *out_node);
/* A track and a fill for a value in per-mille (clamped to 0..1000). Not
 * focusable; put a label beside it for the words. */
[[nodiscard]] gates_err_t gates_progress_create(gates_tree_t *tree, gates_node_t parent,
                                                gates_i32 permille, gates_node_t *out_node);
[[nodiscard]] gates_err_t gates_progress_set_value(gates_tree_t *tree, gates_node_t node,
                                                   gates_i32 permille);
gates_i32 gates_progress_value(const gates_tree_t *tree, gates_node_t node);

/* Properties (setters mark the node layout/paint dirty as appropriate). */
[[nodiscard]] gates_err_t gates_widget_set_text(gates_tree_t *tree, gates_node_t node,
                                                gates_str_t text);
gates_str_t gates_widget_text(const gates_tree_t *tree, gates_node_t node);

[[nodiscard]] gates_err_t gates_widget_set_disabled(gates_tree_t *tree, gates_node_t node,
                                                    bool disabled);
bool gates_widget_disabled(const gates_tree_t *tree, gates_node_t node);

[[nodiscard]] gates_err_t gates_checkbox_set_checked(gates_tree_t *tree, gates_node_t node,
                                                     bool checked);
bool gates_checkbox_checked(const gates_tree_t *tree, gates_node_t node);

/* Keyboard focus (plan-0009): buttons, checkboxes and textboxes are focusable
 * while enabled and reachable (not on an inactive stack page, inside the
 * current focus scope). false takes a control out of the Tab order. Clicking a
 * button or checkbox focuses it. */
[[nodiscard]] gates_err_t gates_widget_set_focusable(gates_tree_t *tree, gates_node_t node,
                                                     bool focusable);
bool gates_widget_focusable(const gates_tree_t *tree, gates_node_t node);

/* Interaction state (driven by pointer routing; read-only for apps). */
bool gates_widget_hovered(const gates_tree_t *tree, gates_node_t node);
bool gates_widget_pressed(const gates_tree_t *tree, gates_node_t node);

#endif /* GATES_WIDGET_H */
