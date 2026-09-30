/* gates_gui_lib - persisted UI state (0.3.0).
 *
 * What a person arranges - split positions, the selected tab, column widths,
 * scroll positions - can outlive the program: gates_state_save writes it as
 * text, the program keeps the text where it likes (a file beside its
 * settings), and gates_state_load applies it at the next start. Only nodes with
 * an automation id (gates_node_set_automation_id) take part, and the id is the
 * key, so ids must stay the same between versions of the program.
 *
 * Format: a first line "# gates state 1", then one line per node,
 * "<kind> <value> <id>" - kind is split (ratio per mille), tabs (selected
 * index), columns ("<column id>:<width>" per column in display order, comma
 * separated, "h" after a hidden one; the older widths-only form still loads)
 * or scroll (offset);
 * the value has no spaces; the id runs to the end of the line, so any id text is
 * safe except a line break. Loading skips lines it does not understand, ids it
 * does not find and kinds that do not match the node - old files never break a
 * newer program. Platform-free; the window's placement is in gates/window.h. */
#ifndef GATES_STATE_H
#define GATES_STATE_H

#include <gates/tree.h>

/* Writes the state into buf. *needed always receives the text's length; with
 * buf == null only the length is reported (OK); a too small cap returns
 * OVERFLOW and writes nothing. */
[[nodiscard]] gates_err_t gates_state_save(const gates_tree_t *tree, gates_u8 *buf, gates_usize_t cap,
                                           gates_usize_t *needed);
/* Applies the lines that match; *applied (optional) receives how many did.
 * Never fails on content: only a bad argument is an error. Run it after the
 * tree is built (a scroll offset is clamped at the next layout). */
[[nodiscard]] gates_err_t gates_state_load(gates_tree_t *tree, gates_str_t text, gates_u32 *applied);

#endif /* GATES_STATE_H */
