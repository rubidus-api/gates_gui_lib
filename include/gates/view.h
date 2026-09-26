/* gates_gui_lib - virtual views: a list or a table over a model the
 * application owns, of any size (plan-0011, RFC-0003 section 8).
 *
 * A view is one retained node. It never creates a node per row: it asks the
 * model only for the rows it paints (at most GATES_VIEW_MAX_ROWS at a time)
 * and keeps the selection as a stable item id, never as a row number, so the
 * selected item stays selected when rows are inserted, removed or reordered.
 * Row counts and offsets are 64-bit; the vertical position is kept in rows.
 *
 * Model rules (UI thread only): callbacks read, they never block, do I/O or
 * change the model; the model changes only between callbacks, after which
 * the application calls gates_view_model_changed. A cell's text must stay
 * valid until the next model callback; the view copies it before asking for
 * anything else. Callbacks are never made during event dispatch or from inside
 * another callback, and never after gates_view_set_model(view, null) returns
 * (which is safe inside a handler).
 *
 * Interaction: one selected item (0 = none). Up/Down, PageUp/PageDown,
 * Home/End move the selection and keep it in view; Enter or a double click on
 * the selected row activates it; Left/Right and Shift+wheel scroll sideways.
 * A click selects the row under it. Clicking a header cell asks for sorting
 * (the model sorts); dragging a header cell's right edge resizes the column,
 * never below its minimum. The view is one Tab stop.
 * Events (a tree adds GATES_EVENT_EXPAND_REQUESTED, see gates_view_desc_t):
 * GATES_EVENT_SELECTION_CHANGED (ev->item = the selected id; a change
 * made by gates_view_model_changed because the selected item went away is
 * announced with origin PROGRAM), GATES_EVENT_ACTIVATED (ev->item = the row's
 * id), GATES_EVENT_SORT_REQUESTED (ev->result = the column id). Platform-free. */
#ifndef GATES_VIEW_H
#define GATES_VIEW_H

#include <gates/tree.h>
#include <gates/geometry.h>

typedef gates_u64 gates_item_id_t;      /* stable, nonzero; 0 = none */
typedef gates_u32 gates_column_id_t;

/* Tree indentation per level, and the width of the open/close mark area at
 * the start of a tree row (after the cell padding). */
#define GATES_VIEW_INDENT 16

/* The most rows a view paints at once (taller views show empty space below). */
#define GATES_VIEW_MAX_ROWS 256

typedef struct gates_cell_t {
    gates_str_t text;            /* borrowed until the next model callback */
} gates_cell_t;

/* Trees (stage 2): the model is the flattened sequence of visible rows; each
 * row says how deep it is and whether it can open. */
typedef enum gates_row_state_t {
    GATES_ROW_NORMAL = 0,
    GATES_ROW_LOADING,           /* shown dimmed: the model is still fetching it */
    GATES_ROW_ERROR,             /* shown in the error colour */
} gates_row_state_t;

typedef struct gates_row_info_t {
    gates_u32 depth;             /* 0 = top level */
    bool expandable;
    bool expanded;
    gates_item_id_t parent;      /* 0 at top level */
    gates_row_state_t state;
} gates_row_info_t;

typedef struct gates_rows_model_t {
    void *user;
    gates_u64 (*revision)(void *user);   /* may be null */
    gates_u64 (*count)(void *user);
    gates_item_id_t (*id_at)(void *user, gates_u64 row);
    bool (*index_of)(void *user, gates_item_id_t id, gates_u64 *row);
    gates_err_t (*cell)(void *user, gates_item_id_t id, gates_column_id_t column,
                        gates_cell_t *out);
    /* Required for a tree view, ignored otherwise. */
    gates_err_t (*row_info)(void *user, gates_item_id_t id, gates_row_info_t *out);
} gates_rows_model_t;

typedef struct gates_column_desc_t {
    gates_column_id_t id;        /* nonzero, unique in the view */
    gates_str_t label;           /* copied; shown in the header */
    gates_i32 width;             /* px; 0 -> 12 cells */
    gates_i32 min_width;         /* px; 0 -> 3 cells */
} gates_column_desc_t;

typedef struct gates_view_desc_t {
    /* No columns: a plain list, one column (id 0) as wide as the view. */
    const gates_column_desc_t *columns;
    gates_u32 column_count;
    bool header;                 /* show a header row (tables) */
    /* Tree view: the first column shows indentation and an open/close mark.
     * Right opens a closed row or moves to its first child, Left closes an
     * open row or moves to its parent, a click on the mark opens or closes.
     * Opening and closing are requests: GATES_EVENT_EXPAND_REQUESTED with
     * ev->item = the row and ev->result = 1 (open) or 0 (close); the model
     * changes its rows and calls gates_view_model_changed. Gates never walks
     * rows it does not show. Left/Right do not scroll sideways in a tree. */
    bool tree;
} gates_view_desc_t;

/* INVALID_ARG for bad columns (id 0, duplicates); nothing is left on failure. */
[[nodiscard]] gates_err_t gates_view_create(gates_tree_t *tree, gates_node_t parent,
                                            const gates_view_desc_t *desc, gates_node_t *out_view);
/* Binds (copies the struct; `user` and callbacks are borrowed) or, with null,
 * detaches. Binding resets scrolling and selection. */
[[nodiscard]] gates_err_t gates_view_set_model(gates_tree_t *tree, gates_node_t view,
                                               const gates_rows_model_t *model);
/* After the model changed: keeps the selected item if it is still there,
 * otherwise selects the item now nearest to its old row (or none), clamps the
 * scroll position and repaints. */
[[nodiscard]] gates_err_t gates_view_model_changed(gates_tree_t *tree, gates_node_t view);

gates_item_id_t gates_view_selected(const gates_tree_t *tree, gates_node_t view);
/* Silent. 0 clears; an id the model does not have is INVALID_ARG. */
[[nodiscard]] gates_err_t gates_view_set_selected(gates_tree_t *tree, gates_node_t view,
                                                  gates_item_id_t id);
/* Scrolls so the item's row is visible (INVALID_ARG when it is not in the model). */
[[nodiscard]] gates_err_t gates_view_scroll_to(gates_tree_t *tree, gates_node_t view,
                                               gates_item_id_t id);
/* The first row shown, and how many whole rows fit (valid after layout). */
gates_u64 gates_view_first_row(const gates_tree_t *tree, gates_node_t view);
gates_u32 gates_view_visible_rows(const gates_tree_t *tree, gates_node_t view);
/* Horizontal scroll position in px. */
gates_i32 gates_view_scroll_x(const gates_tree_t *tree, gates_node_t view);
gates_i32 gates_view_column_width(const gates_tree_t *tree, gates_node_t view,
                                  gates_column_id_t column);
[[nodiscard]] gates_err_t gates_view_set_column_width(gates_tree_t *tree, gates_node_t view,
                                                      gates_column_id_t column, gates_i32 width);

/* Where parts of the view are now (window coordinates, valid after layout;
 * empty when the part is not shown): ROW = row `index` if it is visible,
 * HEADER = the header cell of the column at position `index`, BODY = the rows
 * area, VTHUMB / HTHUMB = the scrollbar thumbs. For hit tests and tooltips. */
typedef enum gates_view_part_t {
    GATES_VIEW_PART_BODY,
    GATES_VIEW_PART_ROW,
    GATES_VIEW_PART_HEADER,
    GATES_VIEW_PART_VTHUMB,
    GATES_VIEW_PART_HTHUMB,
} gates_view_part_t;
gates_rect_t gates_view_part_rect(const gates_tree_t *tree, gates_node_t view,
                                  gates_view_part_t part, gates_u64 index);

/* -- log view (stage 2) ------------------------------------------------------------
 *
 * A view whose model is a bounded ring owned by gates: lines are appended on
 * the UI thread and copied; when a limit is passed the oldest lines are
 * dropped and counted. Line ids grow from 1 and never repeat, so a selected
 * line stays selected until it is dropped (then the oldest remaining line is
 * selected, announced with origin PROGRAM). While the view shows the last
 * line it follows new lines; scrolling up (wheel, keys, thumb) stops
 * following; reaching the end again (End, wheel, thumb) resumes it. The
 * worker-thread producer arrives with RFC-0003 phase G through the same
 * append path. gates_view_set_model is refused on a log view. */
typedef struct gates_log_desc_t {
    gates_u32 max_lines;         /* 0 -> 1000 */
    gates_usize_t max_bytes;     /* text bytes kept; 0 -> 1 MiB */
} gates_log_desc_t;

[[nodiscard]] gates_err_t gates_log_create(gates_tree_t *tree, gates_node_t parent,
                                           const gates_log_desc_t *desc, gates_node_t *out_log);
/* Appends one line (copied; CR, LF and TAB become spaces). OUT_OF_BOUNDS for
 * a line longer than max_bytes; on failure nothing changes. */
[[nodiscard]] gates_err_t gates_log_append(gates_tree_t *tree, gates_node_t log, gates_str_t line);
void gates_log_clear(gates_tree_t *tree, gates_node_t log);
gates_u64 gates_log_count(const gates_tree_t *tree, gates_node_t log);
/* Lines dropped to stay within the limits since creation or the last clear. */
gates_u64 gates_log_dropped(const gates_tree_t *tree, gates_node_t log);
bool gates_log_following(const gates_tree_t *tree, gates_node_t log);
/* A kept line's text (borrowed until the next append or clear), empty when the
 * id was dropped or never existed. For showing or copying the selected line. */
gates_str_t gates_log_line(const gates_tree_t *tree, gates_node_t log, gates_item_id_t id);
/* true jumps to the end and follows; false stops following. */
[[nodiscard]] gates_err_t gates_log_set_following(gates_tree_t *tree, gates_node_t log, bool follow);

#endif /* GATES_VIEW_H */
