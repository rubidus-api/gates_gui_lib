/* gates_gui_lib - tree paint walk and pointer routing (Phase 2).
 * The window drives these each frame: layout (gates/layout.h) -> paint walk
 * emits theme-token draw commands -> renderer; pointer events route to the
 * deepest hit widget (hover/press/click/toggle). Platform-free. */
#ifndef GATES_UI_H
#define GATES_UI_H

#include <gates/layout.h>
#include <gates/draw.h>
#include <gates/theme.h>
#include <gates/input.h>
#include <gates/event.h>

/* Emits draw commands for the tree (visible nodes only: inactive stack pages
 * are skipped) into dl. Run gates_layout_run first. Clears the tree's
 * paint-dirty bit. */
[[nodiscard]] gates_err_t gates_paint_tree(gates_tree_t *tree, gates_draw_list_t *dl,
                                           const gates_theme_t *theme,
                                           const gates_text_backend_t *text);

/* Deepest visible node whose layout rect contains p (section 9 non-overlap makes
 * this unambiguous in normal flow; stack considers the active page only). */
gates_node_t gates_hit_test(const gates_tree_t *tree, gates_point_t p);

/* Routes a pointer event: updates hover/pressed, fires button on_click and
 * checkbox on_toggle on release-inside, marks paint dirty on state changes.
 * Returns the node that consumed the event (or GATES_NODE_NULL). */
gates_node_t gates_input_pointer(gates_tree_t *tree, const gates_pointer_event_t *ev);

/* Moves keyboard focus to the next (or previous) focusable control in tree
 * order, wrapping inside the focus scope, and scrolls it into view. Returns
 * false when there is none. Tab / Shift+Tab do the same. */
bool gates_tree_focus_next(gates_tree_t *tree, bool backward);

/* Routes a key: the focused control first (textbox editing; Space/Enter on a
 * button or checkbox), then Tab traversal, Escape (press cancel, scope cancel
 * command), Enter (scope default command), then command shortcuts. Returns
 * true when the key was consumed; false leaves it to the application. Key-up
 * events matter only for Space (activation happens on release). */
bool gates_input_key(gates_tree_t *tree, const gates_key_event_t *ev);

/* Outcome of routing text input (RFC-0003 section 6.1). FAILED means a target
 * existed but the edit could not be applied (allocation): the text, selection
 * and preedit are unchanged, and the caller must not re-deliver the input to
 * a fallback path. IGNORED is 0, so the result still reads as "consumed" in a
 * boolean context. */
typedef enum gates_input_result_t {
    GATES_INPUT_IGNORED = 0,   /* no focused, enabled text target */
    GATES_INPUT_CONSUMED = 1,  /* applied */
    GATES_INPUT_FAILED = 2,    /* target present, edit failed, nothing changed */
} gates_input_result_t;

/* Routes a committed character (Unicode codepoint) to the focused node.
 * Control codepoints below U+0020 (and U+007F) are ignored. */
gates_input_result_t gates_input_char(gates_tree_t *tree, gates_u32 codepoint);

/* IME composition (plan-0006), called by the platform IME adapter.
 *
 * preedit: sets or replaces the in-progress text shown at the caret (after
 *   the selection); cursor is the IME cursor in bytes into the text, snapped
 *   back to a codepoint start. An empty text clears the composition without
 *   committing anything.
 * commit: the IME's result string. Replaces the selection exactly once and
 *   ends the composition. This is the only path that commits composed text.
 * preedit_cancel: drops the composition without committing; IGNORED when none
 *   is open. */
gates_input_result_t gates_input_preedit(gates_tree_t *tree, gates_str_t text,
                                         gates_u32 cursor);
gates_input_result_t gates_input_commit(gates_tree_t *tree, gates_str_t text);
gates_input_result_t gates_input_preedit_cancel(gates_tree_t *tree);
/* The last input-path failure since the previous call (OK when none), then
 * cleared. Input functions that cannot return an error (keys, pointer) record
 * failures here: for example an edit, toggle or activation that was skipped
 * because its notification could not be reserved (plan-0007). */
gates_err_t gates_input_take_error(gates_tree_t *tree);

/* Pointer capture was lost (another window took it, or the platform cancelled
 * the mode): drops the pressed widget and any handle, thumb or selection drag,
 * so a later release activates nothing. */
void gates_input_cancel_pointer(gates_tree_t *tree);

/* True while the focused textbox holds a preedit. */
bool gates_input_composing(const gates_tree_t *tree);
/* The focused textbox's caret (inside the preedit while composing) as of the
 * last paint, in window coordinates; false when there is none. The IME
 * adapter positions the composition and candidate windows from it. */
bool gates_input_caret_rect(const gates_tree_t *tree, gates_rect_t *out);

#endif /* GATES_UI_H */
