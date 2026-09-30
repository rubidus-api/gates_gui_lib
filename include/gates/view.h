/* gates_gui_lib - virtual views: a list or a table over a model the
 * application owns, of any size.
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
 * never below its minimum. The view is one Tab stop. Typing selects the next
 * row whose first shown cell starts with the letters typed (ASCII letters in
 * any case; a pause of a second starts over, the same letter again steps
 * through such rows; each character looks at up to 4096 rows). Ctrl+C copies
 * the selected row's shown cells, separated by tabs.
 * Events (a tree adds GATES_EVENT_EXPAND_REQUESTED, see gates_view_desc_t):
 * GATES_EVENT_SELECTION_CHANGED (ev->item = the selected id; a change
 * made by gates_view_model_changed because the selected item went away is
 * announced with origin PROGRAM), GATES_EVENT_ACTIVATED (ev->item = the row's
 * id), GATES_EVENT_SORT_REQUESTED (ev->result = the column id),
 * GATES_EVENT_CELL_EDITED (ev->item = the row, ev->result = the column).
 *
 * Cells (0.6.0): a column shows text, a check box, a progress bar or an
 * icon and text, or paints itself. An editable column lets a person change
 * the selected row's cell. A table has a current column (Ctrl+Left/Right, or
 * the cell pressed; outlined in the selected row): F2 edits it when it is an
 * editable text column, else the first such column; a double click edits the
 * text cell under it; a text box opens over the cell with the model's text
 * selected. Enter commits, Escape cancels, focus leaving commits. Space
 * toggles the current column when it is an editable check column, else the
 * first such column; a click on the box toggles it. A commit calls the model's set_cell; an error keeps the editor open and
 * marks it invalid (when focus left, the edit is dropped instead). After a
 * commit the view re-reads the model (as gates_view_model_changed) and reports
 * CELL_EDITED. Scrolling or resizing a column first commits an open edit; the
 * view does not move while the model refuses it. Platform-free. */
#ifndef GATES_VIEW_H
#define GATES_VIEW_H

#include <gates/tree.h>
#include <gates/geometry.h>
#include <gates/theme.h>
#include <gates/image.h>
#include <gates/event.h>

typedef gates_u64 gates_item_id_t;      /* stable, nonzero; 0 = none */
typedef gates_u32 gates_column_id_t;

/* Tree indentation per level, and the width of the open/close mark area at
 * the start of a tree row (after the cell padding). */
#define GATES_VIEW_INDENT 16

/* The most rows a view paints at once (taller views show empty space below). */
#define GATES_VIEW_MAX_ROWS 256

typedef struct gates_cell_t {
    gates_str_t text;            /* borrowed until the next model callback */
    bool checked;                /* CHECK columns */
    gates_u32 permille;          /* PROGRESS columns: 0..1000 (larger shows full) */
    gates_image_id_t icon;       /* ICON_TEXT columns: 0 = none (the text stays aligned) */
} gates_cell_t;

typedef enum gates_cell_kind_t {
    GATES_CELL_TEXT = 0,
    GATES_CELL_CHECK,            /* a check box from cell.checked, then any text */
    GATES_CELL_PROGRESS,         /* a bar from cell.permille, the text over it */
    GATES_CELL_ICON_TEXT,        /* a 16 x 16 icon from cell.icon, then the text */
} gates_cell_kind_t;

/* A column's own painting: called for each painted cell of the column, with
 * the draw list clipped to the cell. The cell is the model's (borrowed for the
 * call). Draw nothing outside `rect`; return a draw error as is. */
typedef struct gates_cell_paint_t {
    gates_draw_list_t *dl;
    gates_rect_t rect;           /* the whole cell, window coordinates */
    const gates_theme_t *theme;
    gates_i32 font;
    gates_item_id_t id;
    gates_column_id_t column;
    const gates_cell_t *cell;
    bool selected;               /* draw with GATES_COLOR_SELECTION_FG */
    bool disabled;               /* draw with GATES_COLOR_CONTROL_DISABLED_FG */
} gates_cell_paint_t;
typedef gates_err_t (*gates_cell_paint_fn)(void *user, const gates_cell_paint_t *p);

/* Trees: the model is the flattened sequence of visible rows; each
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
    /* Optional (0.6.0): a person changed a cell of an editable column -
     * value->text for a text or icon column (borrowed for the call), or
     * value->checked for a check column. The model may change here (it is the
     * one callback that may); any error refuses the change. Null = read-only. */
    gates_err_t (*set_cell)(void *user, gates_item_id_t id, gates_column_id_t column,
                            const gates_cell_t *value);
    /* Required for a multi-select view (0.9.0), ignored otherwise: the first
     * selected row at or after `row`, or GATES_ROW_NONE. The program keeps its
     * selection as it likes (a flag, ranges, a bitmap); gates asks only about
     * rows it paints, describes or copies. */
    gates_u64 (*next_selected)(void *user, gates_u64 row);
} gates_rows_model_t;

#define GATES_ROW_NONE UINT64_MAX

typedef struct gates_column_desc_t {
    gates_column_id_t id;        /* nonzero, unique in the view */
    gates_str_t label;           /* copied; shown in the header */
    gates_i32 width;             /* logical units; 0 -> 12 average character widths */
    gates_i32 min_width;         /* px; 0 -> 3 cells */
    gates_cell_kind_t kind;      /* TEXT by default */
    bool editable;               /* TEXT, ICON_TEXT and CHECK columns (needs set_cell) */
    gates_cell_paint_fn paint;   /* optional: replaces the kind's drawing */
    void *paint_user;            /* borrowed while the view lives */
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
    /* A header menu (0.6.0): a right press on the header, or Shift+F10 on
     * the view, opens a menu with a checked entry per column to show or hide
     * it (the last shown column cannot be hidden). Its entries are commands in
     * the view's own scope, one per column, with the column ids; every column
     * needs a label. */
    bool column_menu;
    /* Multi-selection (0.9.0): the model owns the selection (next_selected)
     * and a person's gestures arrive as GATES_EVENT_SELECT_REQUESTED, which the
     * program applies before calling gates_view_model_changed. The view keeps
     * a focus row (gates_view_selected, SELECTION_CHANGED) and an anchor.
     *   click, arrows, PageUp/PageDown, Home/End, type-ahead   ONE
     *   Ctrl+click, Ctrl+Space                                 TOGGLE
     *   Shift+click, Shift with the moving keys                RANGE
     *   Ctrl+Shift+click                                       ADD_RANGE
     *   Ctrl+A                                                 ALL
     * Ctrl with the moving keys moves the focus row only. Ctrl+C copies the
     * selected rows (one per line, cells tab-separated, model order); more than
     * GATES_VIEW_COPY_MAX rows copy nothing and send GATES_EVENT_COPY_REQUESTED
     * instead. Not for logs. */
    bool multi_select;
} gates_view_desc_t;

/* What a person asked of a multi-select view (ev->result of SELECT_REQUESTED;
 * ev->item = the target row, ev->anchor = where a range starts). RANGE is
 * exactly anchor..target in model order (the rest unselected); ADD_RANGE adds
 * it; TOGGLE flips the target; ALL is every row. An anchor that is gone makes
 * a range the target alone. */
typedef enum gates_select_request_t {
    GATES_SELECT_ONE = 1,
    GATES_SELECT_TOGGLE,
    GATES_SELECT_RANGE,
    GATES_SELECT_ADD_RANGE,
    GATES_SELECT_ALL,
} gates_select_request_t;

#define GATES_VIEW_COPY_MAX 10000u  /* rows a multi-select view copies itself */

/* -- a selection store (0.10.0) --------------------------------------------------
 *
 * Rows kept as sorted, separate ranges, for a multi-select view's program:
 * answer the model's next_selected with gates_selection_next and hand every
 * SELECT_REQUESTED to gates_view_apply_selection. Rows are model positions,
 * so tell the store when rows come or go (rows_inserted / rows_removed).
 * A program may keep its own selection instead; the view never reads this. */
typedef struct gates_selection gates_selection_t;

/* `alloc` {0} = the heap. Every change below either happens whole or, on
 * NOMEM, leaves the selection as it was. */
[[nodiscard]] gates_err_t gates_selection_create(gates_allocator_t alloc, gates_selection_t **out);
void gates_selection_destroy(gates_selection_t *sel);
void gates_selection_clear(gates_selection_t *sel);
/* Rows lo..hi (inclusive; INVALID_ARG when lo > hi or hi is GATES_ROW_NONE). */
[[nodiscard]] gates_err_t gates_selection_add(gates_selection_t *sel, gates_u64 lo, gates_u64 hi);
[[nodiscard]] gates_err_t gates_selection_remove(gates_selection_t *sel, gates_u64 lo, gates_u64 hi);
[[nodiscard]] gates_err_t gates_selection_toggle(gates_selection_t *sel, gates_u64 row);
bool gates_selection_contains(const gates_selection_t *sel, gates_u64 row);
/* The first selected row at or after `row`, or GATES_ROW_NONE: what the
 * model's next_selected answers. */
gates_u64 gates_selection_next(const gates_selection_t *sel, gates_u64 row);
/* Selected rows in all, and the ranges one by one (in row order). */
gates_u64 gates_selection_count(const gates_selection_t *sel);
gates_u32 gates_selection_range_count(const gates_selection_t *sel);
bool gates_selection_range(const gates_selection_t *sel, gates_u32 index, gates_u64 *lo, gates_u64 *hi);
/* A request in rows: ONE and RANGE replace the selection, TOGGLE flips the
 * target, ADD_RANGE adds anchor..target, ALL selects rows 0..row_count - 1.
 * An anchor at or past row_count (GATES_ROW_NONE: none) makes a range the
 * target alone; a target past the end is OUT_OF_BOUNDS. */
[[nodiscard]] gates_err_t gates_selection_apply(gates_selection_t *sel, gates_select_request_t what, gates_u64 target,
                                                gates_u64 anchor, gates_u64 row_count);
/* `count` rows were inserted before row `at`, or rows at..at + count - 1
 * removed: later rows move. Inserted rows are not selected (a range they land
 * in is split); ranges that meet once rows are removed join. */
[[nodiscard]] gates_err_t gates_selection_rows_inserted(gates_selection_t *sel, gates_u64 at, gates_u64 count);
[[nodiscard]] gates_err_t gates_selection_rows_removed(gates_selection_t *sel, gates_u64 at, gates_u64 count);
/* Applies a view's SELECT_REQUESTED event (its ids turned into rows through
 * the view's model) and calls gates_view_model_changed. INVALID_ARG for
 * another event; NOT_FOUND when the view or the target row is gone (nothing
 * changes). */
[[nodiscard]] gates_err_t gates_view_apply_selection(gates_tree_t *tree, const gates_event_t *ev,
                                                     gates_selection_t *sel);

/* INVALID_ARG for bad columns (id 0, duplicates); nothing is left on failure.
 * A multi-select view refuses a model without next_selected (set_model). */
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

/* -- choosing and ordering columns (0.6.0) ---------------------------------------
 *
 * A hidden column takes no space, is not painted or asked for, and is left
 * out of assistive technology's columns; hiding the column being edited
 * cancels the edit. INVALID_STATE when the call would hide the last shown
 * column. Positions count every column, hidden or not, from 0. Widths, order
 * and hidden marks are saved and loaded by gates_state (gates/state.h). */
[[nodiscard]] gates_err_t gates_view_set_column_hidden(gates_tree_t *tree, gates_node_t view,
                                                       gates_column_id_t column, bool hidden);
bool gates_view_column_hidden(const gates_tree_t *tree, gates_node_t view, gates_column_id_t column);
/* Moves the column to `position` (the others keep their order). */
[[nodiscard]] gates_err_t gates_view_move_column(gates_tree_t *tree, gates_node_t view,
                                                 gates_column_id_t column, gates_u32 position);
/* The column at `position`, or 0 past the end. */
gates_column_id_t gates_view_column_at(const gates_tree_t *tree, gates_node_t view, gates_u32 position);
/* Opens the header menu at `at` (window coordinates), as a right press on the
 * header would; INVALID_ARG without column_menu. For a "Columns" command. */
[[nodiscard]] gates_err_t gates_view_open_column_menu(gates_tree_t *tree, gates_node_t view,
                                                      gates_point_t at, gates_node_t *out_menu);

/* -- editing cells (0.6.0) -------------------------------------------------------
 *
 * gates_view_edit opens the editor on a cell as F2 would (selecting the row,
 * scrolling it into view, focusing the editor): INVALID_ARG when the column is
 * not an editable text or icon column, the model has no set_cell or the item
 * is not in the model; an open edit is committed first (its error is
 * returned and nothing else happens). gates_view_end_edit commits (true) or
 * cancels (false) an open edit and gives focus back to the view when the
 * editor had it; a refused commit returns the model's error and keeps the
 * editor open. OK when nothing was open. */
[[nodiscard]] gates_err_t gates_view_edit(gates_tree_t *tree, gates_node_t view, gates_item_id_t id,
                                          gates_column_id_t column);
[[nodiscard]] gates_err_t gates_view_end_edit(gates_tree_t *tree, gates_node_t view, bool commit);
/* true while an edit is open; the row and column through the pointers (may be null). */
bool gates_view_editing(const gates_tree_t *tree, gates_node_t view, gates_item_id_t *id,
                        gates_column_id_t *column);
/* The editor text box (GATES_NODE_NULL for a view without editable text
 * columns): for its text, a maximum length or validation while it is open. */
gates_node_t gates_view_editor(const gates_tree_t *tree, gates_node_t view);

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

/* -- log view ------------------------------------------------------------
 *
 * A view whose model is a bounded ring owned by gates: lines are appended on
 * the UI thread and copied; when a limit is passed the oldest lines are
 * dropped and counted. Line ids grow from 1 and never repeat, so a selected
 * line stays selected until it is dropped (then the oldest remaining line is
 * selected, announced with origin PROGRAM). While the view shows the last
 * line it follows new lines; scrolling up (wheel, keys, thumb) stops
 * following; reaching the end again (End, wheel, thumb) resumes it, and
 * both report GATES_EVENT_FOLLOW_CHANGED (the program's set_following is
 * silent). The
 * worker-thread producer uses gates/post.h through the same
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
