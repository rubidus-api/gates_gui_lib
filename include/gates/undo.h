/* gates_gui_lib - undo stack (0.6.0): undo and redo for
 * the program's own data, with labels for menus and a "saved" mark.
 *
 * The program makes a change, then pushes an entry that can take it back
 * (undo) and make it again (redo); each has its own data pointer (often the
 * same one) and an optional drop function that frees the data when the entry
 * leaves the stack. Entries with the same nonzero merge key, pushed one after
 * another, merge into one (typing into one field): the first entry's undo
 * and label with the latest entry's redo; the data between is dropped.
 * Undoing, redoing, gates_undo_break_merge and gates_undo_mark_clean end a
 * merge run.
 * Pushing after an undo drops the entries that could have been redone. At
 * most `max_entries` are kept; the oldest go first.
 *
 * The clean mark remembers the state the program saved: gates_undo_is_clean
 * is true while undo and redo have brought the data back to it, and false for
 * good once that state can no longer be reached. Undo and redo functions run
 * on the calling thread and must not call into the same stack; an error from
 * them leaves the stack where it was.
 *
 * gates_undo_bind keeps two commands of a tree in step: enabled while there
 * is something to undo or redo, labelled with the word and the entry's label
 * ("Undo Rename"). Text boxes keep their own undo. Not a node; platform-free. */
#ifndef GATES_UNDO_H
#define GATES_UNDO_H

#include <gates/types.h>
#include <gates/tree.h>
#include <gates/command.h>

typedef struct gates_undo gates_undo_t;

typedef struct gates_undo_entry_t {
    gates_str_t label;                       /* copied; shown after "Undo" and "Redo" */
    gates_err_t (*undo)(void *data);         /* required */
    void *undo_data;
    gates_err_t (*redo)(void *data);         /* required */
    void *redo_data;
    void (*drop)(void *data);                /* may be null; called once per distinct pointer */
    gates_u32 merge_key;                     /* 0 = never merges */
} gates_undo_entry_t;

/* max_entries 0 -> 100. The allocator: zero -> the proven heap. */
[[nodiscard]] gates_err_t gates_undo_create(gates_allocator_t alloc, gates_u32 max_entries, gates_undo_t **out);
/* Drops every entry and unbinds; null is ignored. */
void gates_undo_destroy(gates_undo_t *u);

/* After the change was made. INVALID_ARG without undo or redo; on any error
 * the entry is not kept and its data is dropped (the change stays made). */
[[nodiscard]] gates_err_t gates_undo_push(gates_undo_t *u, const gates_undo_entry_t *entry);
/* INVALID_STATE with nothing to undo or redo; BUSY when called from inside an
 * undo or redo function; otherwise the function's own result. */
[[nodiscard]] gates_err_t gates_undo_undo(gates_undo_t *u);
[[nodiscard]] gates_err_t gates_undo_redo(gates_undo_t *u);
bool gates_undo_can_undo(const gates_undo_t *u);
bool gates_undo_can_redo(const gates_undo_t *u);
/* The label of what would be undone or redone next (empty when nothing). */
gates_str_t gates_undo_undo_label(const gates_undo_t *u);
gates_str_t gates_undo_redo_label(const gates_undo_t *u);
void gates_undo_break_merge(gates_undo_t *u);
void gates_undo_mark_clean(gates_undo_t *u);
bool gates_undo_is_clean(const gates_undo_t *u);
/* Drops every entry; the present state becomes the clean one. */
void gates_undo_clear(gates_undo_t *u);

/* Keeps commands undo_cmd and redo_cmd of `scope` in step from now on
 * (NOT_FOUND unless both exist). Words are copied ("Undo", "Redo", or the
 * program's language). A null tree unbinds; unbind (or destroy the stack)
 * before destroying the tree. The program's commands call gates_undo_undo /
 * _redo; a scope that goes away just stops the updates. */
[[nodiscard]] gates_err_t gates_undo_bind(gates_undo_t *u, gates_tree_t *tree, gates_node_t scope,
                                          gates_command_id_t undo_cmd, gates_command_id_t redo_cmd,
                                          gates_str_t undo_word, gates_str_t redo_word);

#endif /* GATES_UNDO_H */
