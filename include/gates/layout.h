/* gates_gui_lib — intrinsic layout (RFC-0001 §11, Phase 2 set):
 * absolute, row, column, stack; padding/gap/grow/align; measure/arrange.
 * Split and scroll are staged next (see plan-0003). Platform-free.
 *
 * Invariant (§9, §30): after arrange, sibling layout rects in normal-flow
 * containers (row/column) do not overlap. Stack children intentionally share
 * their parent's content rect; only the active child is painted/hit. */
#ifndef GATES_LAYOUT_H
#define GATES_LAYOUT_H

#include <gates/tree.h>
#include <gates/geometry.h>
#include <gates/text.h>

typedef enum gates_layout_t {
    GATES_LAYOUT_KIND_NONE = 0,   /* leaf / no layout of children */
    GATES_LAYOUT_KIND_ABSOLUTE,   /* children use their abs rect */
    GATES_LAYOUT_KIND_ROW,
    GATES_LAYOUT_KIND_COLUMN,
    GATES_LAYOUT_KIND_STACK,
    GATES_LAYOUT_KIND_SPLIT,      /* exactly two panes + draggable handle */
    GATES_LAYOUT_KIND_SCROLL,     /* clipped viewport over a taller column */
    GATES_LAYOUT_KIND_FORM,       /* rows of label | editor (plan-0010), see below */
    GATES_LAYOUT_KIND_GRID,       /* rows and columns (plan-0019), see below */
    GATES_LAYOUT_KIND_WRAP,       /* a row that breaks onto new lines (plan-0019) */
} gates_layout_t;

/* GRID: children fill cells left to right, row by row, gates_layout_set_grid
 * columns to a row (default 2). A column is as wide as its widest child; the
 * spare width goes to columns by their grow weights (none by default), and
 * the spare height to the first GATES_GRID_MAX_GROW_ROWS rows by theirs. A child
 * may span columns (gates_layout_set_child_span). A row is as tall as its
 * tallest child; children are centred vertically in their row and take the
 * cell's width unless their align says START_V, CENTER_V or END_V (then their
 * own width, at the left, middle or right). The gap applies both ways.
 * WRAP: children at their preferred size, left to right, breaking onto a new
 * line when the next one does not fit; the gap applies both ways; a line is as
 * tall as its tallest child. A wrap container's height follows its width (the
 * layout runs a second pass when that width changes). Hidden children take no
 * place in either. At most GATES_GRID_MAX_COLUMNS columns. */
#define GATES_GRID_MAX_COLUMNS 16
#define GATES_GRID_MAX_GROW_ROWS 16  /* rows that can have a grow weight */

/* FORM: every child is a row whose first child is its label and whose second
 * child is its editor (further children are not shown). Labels share one
 * column as wide as the widest shown label; editors start after it plus
 * GATES_FORM_COLUMN_GAP and keep their preferred width unless their align is
 * STRETCH (then they take the rest). When the form is narrower than the label
 * column + gap + GATES_FORM_MIN_EDITOR_CELLS average character widths, every label goes above its
 * editor (GATES_FORM_STACK_GAP between). The measured size is the side-by-side
 * one; stacked rows are taller, so give a form that may become narrow room to
 * grow (for example a scroll container). Hidden rows take no space. */
#define GATES_FORM_COLUMN_GAP        8
#define GATES_FORM_STACK_GAP         2
#define GATES_FORM_MIN_EDITOR_CELLS 12

/* Split direction: HORIZONTAL puts the panes side by side (vertical handle). */
typedef enum gates_split_dir_t {
    GATES_SPLIT_HORIZONTAL = 0,
    GATES_SPLIT_VERTICAL,
} gates_split_dir_t;

#define GATES_SPLIT_HANDLE_PX   6
#define GATES_SPLIT_MIN_PANE_PX 16
#define GATES_SCROLLBAR_PX      10
#define GATES_SCROLL_WHEEL_LINES 3

typedef enum gates_align_t {
    GATES_ALIGN_STRETCH_V = 0,    /* default: fill the cross axis */
    GATES_ALIGN_START_V,
    GATES_ALIGN_CENTER_V,
    GATES_ALIGN_END_V,
} gates_align_t;

/* GRID (plan-0019): columns 1..GATES_GRID_MAX_COLUMNS; a column's grow weight;
 * a child's span in columns (1 by default; wider than the grid is cut to fit). */
[[nodiscard]] gates_err_t gates_layout_set_grid(gates_tree_t *tree, gates_node_t node, gates_u32 columns);
[[nodiscard]] gates_err_t gates_layout_set_grid_column_grow(gates_tree_t *tree, gates_node_t node,
                                                            gates_u32 column, gates_u8 weight);
/* A row's grow weight (0.8.0): row < GATES_GRID_MAX_GROW_ROWS, counted over shown rows. */
[[nodiscard]] gates_err_t gates_layout_set_grid_row_grow(gates_tree_t *tree, gates_node_t node, gates_u32 row,
                                                         gates_u8 weight);
[[nodiscard]] gates_err_t gates_layout_set_child_span(gates_tree_t *tree, gates_node_t child, gates_u32 columns);

/* Container properties. */
[[nodiscard]] gates_err_t gates_layout_set(gates_tree_t *tree, gates_node_t node,
                                           gates_layout_t kind);
[[nodiscard]] gates_err_t gates_layout_set_padding(gates_tree_t *tree, gates_node_t node,
                                                   gates_i32 padding);
[[nodiscard]] gates_err_t gates_layout_set_gap(gates_tree_t *tree, gates_node_t node,
                                               gates_i32 gap);
/* STACK: which child (by sibling order, 0-based) is visible/interactive. */
[[nodiscard]] gates_err_t gates_layout_set_stack_active(gates_tree_t *tree, gates_node_t node,
                                                        gates_u32 child_index);
gates_u32 gates_layout_stack_active(const gates_tree_t *tree, gates_node_t node);

/* SPLIT: direction plus the first pane's share in per-mille (1..999).
 * Panes never shrink below GATES_SPLIT_MIN_PANE_PX; dragging the handle
 * updates the ratio. */
[[nodiscard]] gates_err_t gates_layout_set_split(gates_tree_t *tree, gates_node_t node,
                                                 gates_split_dir_t dir,
                                                 gates_i32 ratio_permille);
gates_i32 gates_layout_split_ratio(const gates_tree_t *tree, gates_node_t node);

/* SCROLL: vertical offset in px, clamped to [0, content_h - viewport_h].
 * Horizontal scrolling is not implemented in this version. */
[[nodiscard]] gates_err_t gates_layout_set_scroll_offset(gates_tree_t *tree, gates_node_t node,
                                                         gates_i32 offset_y);
gates_i32 gates_layout_scroll_offset(const gates_tree_t *tree, gates_node_t node);
/* Measured content size (valid after gates_layout_run). */
gates_size_t gates_layout_scroll_content(const gates_tree_t *tree, gates_node_t node);

/* Child properties. */
[[nodiscard]] gates_err_t gates_layout_set_child_grow(gates_tree_t *tree, gates_node_t node,
                                                      gates_u8 weight);
[[nodiscard]] gates_err_t gates_layout_set_child_align(gates_tree_t *tree, gates_node_t node,
                                                       gates_align_t align);
/* ABSOLUTE child: requested rect relative to the parent's content box. */
[[nodiscard]] gates_err_t gates_layout_set_abs_rect(gates_tree_t *tree, gates_node_t node,
                                                    gates_rect_t rect);

/* Runs measure + arrange over the whole tree; root gets {0,0,viewport}.
 * Clears the tree's layout-dirty bit. */
[[nodiscard]] gates_err_t gates_layout_run(gates_tree_t *tree, gates_size_t viewport,
                                           const gates_text_backend_t *text);

/* Results (valid after gates_layout_run). */
gates_rect_t gates_node_layout_rect(const gates_tree_t *tree, gates_node_t node);
gates_size_t gates_node_preferred_size(const gates_tree_t *tree, gates_node_t node);

/* §30 checker: recursively verifies that sibling layout rects in row/column
 * containers do not overlap. Used by tests and debug builds. */
bool gates_layout_validate(const gates_tree_t *tree, gates_node_t node);

#endif /* GATES_LAYOUT_H */
