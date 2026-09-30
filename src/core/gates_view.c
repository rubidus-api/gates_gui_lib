/* gates_gui_lib - virtual views: the shared rows engine behind list and table.
 * One node per view; the model is asked only for
 * painted rows; selection is an item id; offsets are 64-bit rows.
 * Platform-free. */
#include <gates/view.h>
#include <gates/event.h>
#include <gates/layout.h>
#include <gates/widget.h>
#include <gates/access.h>
#include <gates/overlay.h>
#include <gates/command.h>
#include "gates_tree_internal.h"

#include <string.h>

#define VIEW_ROW_PAD 4           /* row height = line height + this */
#define VIEW_CELL_PAD 4          /* text inset inside a cell */
#define VIEW_EDGE 3              /* header edge grab distance for resizing */
#define VIEW_WHEEL_ROWS 3
#define VIEW_STEP_CELLS 4        /* Left/Right and horizontal wheel step */
#define VIEW_BAR_INSET 6         /* progress cells: bar inset from the row's top and bottom */
#define VIEW_FIND_PAUSE_MS 1000  /* type-ahead: a pause this long starts a new search */
#define VIEW_FIND_SCAN 4096u     /* type-ahead: rows looked at per character, at most */

typedef struct gates_i_column {
    gates_column_id_t id;
    const gates_u8 *label;
    gates_u32 label_len;
    gates_i32 width;
    gates_i32 min_width;
    gates_cell_kind_t kind;
    bool editable;
    gates_cell_paint_fn paint;
    void *paint_user;
    bool hidden;                 /* takes no space, not painted, not in accessibility */
} gates_i_column_t;

/* Log view ring: lines in arrival order, ids consecutive. */
typedef struct gates_i_line {
    gates_item_id_t id;
    gates_u8 *text;
    gates_u32 len;
} gates_i_line_t;

struct gates_i_log {
    gates_u32 max_lines;
    gates_usize_t max_bytes;
    gates_i_line_t *lines;       /* max_lines slots */
    gates_u32 head, n;
    gates_usize_t bytes;
    gates_item_id_t next_id;
    gates_u64 dropped;
};

struct gates_i_view {
    bool header;
    bool tree;
    struct gates_i_log *log;     /* a log view owns its model */
    bool follow;                 /* log: keep the last line in view */
    gates_u32 ncol;
    gates_i_column_t *cols;      /* one block: array, then labels */
    bool has_model;
    gates_rows_model_t model;
    gates_u64 first;             /* first row shown */
    gates_i32 scroll_x;
    gates_item_id_t sel;
    gates_u64 sel_row;           /* last known row of the selection */
    gates_i32 press_col;         /* header cell pressed, -1 = none */
    gates_u64 drag_first;
    gates_i32 drag_value;
    gates_i32 drag_col;
    /* In-place editing (0.6.0): the editor text box child, GATES_NONE when
     * no column is an editable text column. */
    gates_u32 editor;
    bool editing;
    gates_item_id_t edit_id;
    gates_column_id_t edit_col;
    gates_u32 edit_rev;          /* the editor's revision when it opened */
    bool column_menu;            /* the header menu: commands in the view's own scope */
    gates_node_t self;
    /* 0.8.0: the keyboard's current column (0 = none), and type-ahead. */
    gates_column_id_t cur_col;
    gates_u8 find[64];
    gates_u32 find_len;
    gates_u64 find_at;           /* clock time of the last typed character */
    /* 0.9.0: multi-selection - the model owns it; the view keeps where a range starts. */
    bool multi;
    gates_item_id_t anchor;
};

typedef struct view_geom_t {
    gates_rect_t inner, header, body, vtrack, vthumb, htrack, hthumb;
    gates_i32 row_h;
    gates_u64 count;
    gates_u64 first;             /* clamped copy of the stored offset */
    gates_u64 max_first;
    gates_u32 visible;           /* whole rows that fit (at least 1) */
    gates_u32 painted;           /* rows touched by the body, capped */
    gates_i32 content_w;
    gates_i32 max_x;
    gates_i32 scroll_x;          /* clamped copy */
} view_geom_t;

static gates_err_t end_edit(gates_tree_t *tree, gates_u32 idx, struct gates_i_view *v, bool commit, bool left);
static void on_column_command(gates_tree_t *tree, gates_command_id_t id, void *user);

/* -- state -------------------------------------------------------------------------- */

void gates_i_view_free(gates_tree_t *tree, gates_widget_state_t *st) {
    if (st->view != nullptr) {
        struct gates_i_log *lg = st->view->log;
        if (lg != nullptr) {
            for (gates_u32 k = 0; k < lg->n; k++) {
                tree->alloc.free_fn(tree->alloc.ctx, lg->lines[(lg->head + k) % lg->max_lines].text);
            }
            tree->alloc.free_fn(tree->alloc.ctx, lg->lines);
            tree->alloc.free_fn(tree->alloc.ctx, lg);
        }
        if (st->view->cols != nullptr) {
            tree->alloc.free_fn(tree->alloc.ctx, st->view->cols);
        }
        tree->alloc.free_fn(tree->alloc.ctx, st->view);
    }
    st->view = nullptr;
}

gates_item_id_t gates_i_view_selected(const gates_widget_state_t *st) {
    return st != nullptr && st->view != nullptr ? st->view->sel : 0;
}

static struct gates_i_view *view_of(const gates_tree_t *tree, gates_node_t node) {
    if (tree == nullptr || !gates_i_valid(tree, node) ||
        gates_i_slot(tree, node.index)->kind != GATES_NODE_VIEW) {
        return nullptr;
    }
    const gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, node.index)->state_index);
    return st != nullptr ? st->view : nullptr;
}

static struct gates_i_view *view_at(const gates_tree_t *tree, gates_u32 idx) {
    const gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
    return st != nullptr ? st->view : nullptr;
}

static gates_u64 model_count(struct gates_i_view *v) {
    return v->has_model ? v->model.count(v->model.user) : 0;
}

/* -- 64-bit arithmetic that never narrows --------------------------------------------- */

/* a * b / c for a < 2^31 and b <= c: both are shifted until c fits 32 bits. */
static gates_u64 scale_down(gates_u64 a, gates_u64 b, gates_u64 c) {
    while (c > 0xFFFFFFFFull) {
        b >>= 1;
        c >>= 1;
    }
    return c != 0 ? a * b / c : 0;
}

/* max * pos / travel for pos <= travel < 2^31, exact at both ends. */
static gates_u64 scale_up(gates_u64 max, gates_u64 pos, gates_u64 travel) {
    if (travel == 0) return 0;
    return (max / travel) * pos + (max % travel) * pos / travel;
}

/* -- geometry, shared by layout queries, paint and hit testing ----------------------------- */

static gates_i32 row_height(gates_i32 line_height) {
    gates_i32 h = (line_height > 0 ? line_height : 16) + VIEW_ROW_PAD;
    return h < GATES_ACCESS_MIN_TARGET ? GATES_ACCESS_MIN_TARGET : h; /* WCAG 2.5.8 */
}

/* A column's width on screen: 0 while hidden. */
static gates_i32 shown_w(const gates_i_column_t *c) {
    return c->hidden ? 0 : c->width;
}

static gates_i32 content_width(const struct gates_i_view *v, gates_i32 body_w) {
    if (v->ncol == 0) return body_w;
    gates_i32 w = 0;
    for (gates_u32 i = 0; i < v->ncol; i++) w += shown_w(&v->cols[i]);
    return w;
}

/* Visible columns: how many, and the position of the k-th (or -1). */
static gates_u32 nvisible(const struct gates_i_view *v) {
    gates_u32 n = 0;
    for (gates_u32 i = 0; i < v->ncol; i++) n += v->cols[i].hidden ? 0u : 1u;
    return n;
}

static gates_i32 visible_at(const struct gates_i_view *v, gates_u32 k) {
    for (gates_u32 i = 0; i < v->ncol; i++) {
        if (v->cols[i].hidden) continue;
        if (k-- == 0) return (gates_i32)i;
    }
    return -1;
}

/* A tree's marks and indentation go in the first visible column. */
static gates_u32 tree_col(const struct gates_i_view *v) {
    gates_i32 c = visible_at(v, 0);
    return c > 0 ? (gates_u32)c : 0;
}

static void geom(const gates_tree_t *tree, gates_u32 idx, const struct gates_i_view *v,
                 gates_u64 count, gates_i32 line_height, view_geom_t *g) {
    memset(g, 0, sizeof *g);
    gates_rect_t r = gates_i_slot(tree, idx)->layout_rect;
    g->inner = (gates_rect_t){ r.x + 1, r.y + 1, r.w - 2, r.h - 2 };
    if (g->inner.w < 0) g->inner.w = 0;
    if (g->inner.h < 0) g->inner.h = 0;
    g->row_h = row_height(line_height);
    g->count = count;
    gates_i32 header_h = v->header && v->ncol > 0 ? g->row_h : 0;
    if (header_h > g->inner.h) header_h = g->inner.h;
    gates_i32 avail_h = g->inner.h - header_h;
    gates_i32 cw = content_width(v, 0);
    bool vbar = count > (gates_u64)(avail_h / g->row_h);
    bool hbar = v->ncol > 0 && cw > g->inner.w - (vbar ? GATES_SCROLLBAR_PX : 0);
    if (hbar && !vbar) {
        vbar = count > (gates_u64)((avail_h - GATES_SCROLLBAR_PX) / g->row_h);
    }
    g->body = (gates_rect_t){ g->inner.x, g->inner.y + header_h,
                              g->inner.w - (vbar ? GATES_SCROLLBAR_PX : 0),
                              avail_h - (hbar ? GATES_SCROLLBAR_PX : 0) };
    if (g->body.w < 0) g->body.w = 0;
    if (g->body.h < 0) g->body.h = 0;
    g->header = (gates_rect_t){ g->inner.x, g->inner.y, g->body.w, header_h };
    gates_u32 fit = (gates_u32)(g->body.h / g->row_h);
    g->visible = fit > 0 ? (fit < GATES_VIEW_MAX_ROWS ? fit : GATES_VIEW_MAX_ROWS) : 1;
    gates_u32 touched = (gates_u32)((g->body.h + g->row_h - 1) / g->row_h);
    if (touched > GATES_VIEW_MAX_ROWS) touched = GATES_VIEW_MAX_ROWS;
    g->max_first = count > g->visible ? count - g->visible : 0;
    g->first = v->first < g->max_first ? v->first : g->max_first;
    gates_u64 left = count - g->first;
    g->painted = left < touched ? (gates_u32)left : touched;
    g->content_w = content_width(v, g->body.w);
    g->max_x = g->content_w > g->body.w ? g->content_w - g->body.w : 0;
    g->scroll_x = v->scroll_x < g->max_x ? (v->scroll_x > 0 ? v->scroll_x : 0) : g->max_x;
    if (vbar) {
        g->vtrack = (gates_rect_t){ g->body.x + g->body.w, g->body.y, GATES_SCROLLBAR_PX, g->body.h };
        gates_i64 th = (gates_i64)scale_down((gates_u64)g->vtrack.h, g->visible,
                                             count > g->visible ? count : g->visible);
        if (th < GATES_SCROLLBAR_PX) th = GATES_SCROLLBAR_PX;
        if (th > g->vtrack.h) th = g->vtrack.h;
        gates_i32 travel = g->vtrack.h - (gates_i32)th;
        gates_i32 y = (gates_i32)scale_down((gates_u64)travel, g->first, g->max_first);
        g->vthumb = (gates_rect_t){ g->vtrack.x, g->vtrack.y + y, GATES_SCROLLBAR_PX, (gates_i32)th };
    }
    if (hbar) {
        g->htrack = (gates_rect_t){ g->body.x, g->body.y + g->body.h, g->body.w, GATES_SCROLLBAR_PX };
        gates_i32 tw = g->content_w > 0
                           ? (gates_i32)(((gates_i64)g->htrack.w * g->body.w) / g->content_w)
                           : g->htrack.w;
        if (tw < GATES_SCROLLBAR_PX) tw = GATES_SCROLLBAR_PX;
        if (tw > g->htrack.w) tw = g->htrack.w;
        gates_i32 travel = g->htrack.w - tw;
        gates_i32 x = g->max_x > 0 ? (gates_i32)(((gates_i64)travel * g->scroll_x) / g->max_x) : 0;
        g->hthumb = (gates_rect_t){ g->htrack.x + x, g->htrack.y, tw, GATES_SCROLLBAR_PX };
    }
}

/* Left edge of column position c (window x), after horizontal scrolling. */
static gates_i32 col_left(const struct gates_i_view *v, const view_geom_t *g, gates_u32 c) {
    gates_i32 x = g->body.x - g->scroll_x;
    for (gates_u32 i = 0; i < c && i < v->ncol; i++) x += shown_w(&v->cols[i]);
    return x;
}

static gates_i32 col_width(const struct gates_i_view *v, const view_geom_t *g, gates_u32 c) {
    return v->ncol == 0 ? g->body.w : shown_w(&v->cols[c]);
}

static void geom_now(gates_tree_t *tree, gates_u32 idx, struct gates_i_view *v, view_geom_t *g) {
    geom(tree, idx, v, model_count(v), tree->line_height, g);
}

/* -- creation and the public surface --------------------------------------------------------- */

gates_err_t gates_view_create(gates_tree_t *tree, gates_node_t parent, const gates_view_desc_t *desc,
                              gates_node_t *out_view) {
    if (tree == nullptr || desc == nullptr || out_view == nullptr ||
        (desc->column_count > 0 && desc->columns == nullptr)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *out_view = GATES_NODE_NULL;
    if (!gates_node_eq(parent, GATES_NODE_NULL) && !gates_i_valid(tree, parent)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_usize_t bytes = 0;
    for (gates_u32 i = 0; i < desc->column_count; i++) {
        const gates_column_desc_t *c = &desc->columns[i];
        if (c->id == 0 || (c->label.size > 0 && c->label.ptr == nullptr) ||
            (gates_u32)c->kind > (gates_u32)GATES_CELL_ICON_TEXT ||
            (c->editable && c->kind == GATES_CELL_PROGRESS) ||
            (desc->column_menu && c->label.size == 0)) {
            return PROVEN_ERR_INVALID_ARG;
        }
        for (gates_u32 k = 0; k < i; k++) {
            if (desc->columns[k].id == c->id) return PROVEN_ERR_INVALID_ARG;
        }
        bytes += c->label.size;
    }
    gates_allocator_t a = tree->alloc;
    proven_result_mem_mut_t rv = a.alloc_fn(a.ctx, sizeof(struct gates_i_view),
                                            alignof(struct gates_i_view));
    if (!proven_is_ok(rv.err)) {
        return rv.err;
    }
    struct gates_i_view *v = (struct gates_i_view *)rv.value.ptr;
    memset(v, 0, sizeof *v);
    v->header = desc->header;
    v->tree = desc->tree;
    v->press_col = -1;
    v->editor = GATES_NONE;
    v->column_menu = desc->column_menu && desc->column_count > 0;
    v->multi = desc->multi_select;
    bool wants_editor = false;
    gates_err_t err = GATES_OK;
    if (desc->column_count > 0) {
        gates_usize_t head = (gates_usize_t)desc->column_count * sizeof(gates_i_column_t);
        proven_result_mem_mut_t rc = a.alloc_fn(a.ctx, head + bytes, alignof(gates_i_column_t));
        err = rc.err;
        if (gates_is_ok(err)) {
            v->cols = (gates_i_column_t *)rc.value.ptr;
            v->ncol = desc->column_count;
            gates_u8 *text = (gates_u8 *)rc.value.ptr + head;
            gates_i32 adv = tree->advance > 0 ? tree->advance : 8;
            for (gates_u32 i = 0; i < v->ncol; i++) {
                const gates_column_desc_t *c = &desc->columns[i];
                if (c->label.size > 0) memcpy(text, c->label.ptr, c->label.size);
                gates_i32 minw = c->min_width > 0 ? c->min_width : 3 * adv;
                gates_i32 w = c->width > 0 ? c->width : 12 * adv;
                v->cols[i] = (gates_i_column_t){ .id = c->id, .label = text,
                                                 .label_len = (gates_u32)c->label.size,
                                                 .width = w < minw ? minw : w, .min_width = minw,
                                                 .kind = c->kind, .editable = c->editable,
                                                 .paint = c->paint, .paint_user = c->paint_user };
                if (c->editable && (c->kind == GATES_CELL_TEXT || c->kind == GATES_CELL_ICON_TEXT)) {
                    wants_editor = true;
                }
                text += c->label.size;
            }
        }
    }
    gates_node_t node = GATES_NODE_NULL;
    gates_u32 state = GATES_NONE;
    bool owned = false;          /* the node's state holds v: discarding the node frees it */
    if (gates_is_ok(err)) {
        gates_node_desc_t nd = { .kind = GATES_NODE_VIEW };
        err = gates_node_create(tree, GATES_NODE_NULL, &nd, &node);
    }
    if (gates_is_ok(err)) {
        err = gates_i_state_acquire(tree, &state);
        if (gates_is_ok(err)) {
            gates_i_slot(tree, node.index)->state_index = state;
            gates_i_state(tree, state)->view = v;
            owned = true;
        }
    }
    if (gates_is_ok(err)) v->self = node;
    for (gates_u32 i = 0; gates_is_ok(err) && v->column_menu && i < v->ncol; i++) {
        gates_command_desc_t cd = { .id = v->cols[i].id,
                                    .label = (gates_str_t){ .ptr = v->cols[i].label, .size = v->cols[i].label_len },
                                    .enabled = true, .checked = true, .invoke = on_column_command, .user = v };
        err = gates_command_register(tree, node, &cd); /* freed with the node */
    }
    if (gates_is_ok(err) && wants_editor) {
        gates_node_t box = GATES_NODE_NULL;
        err = gates_textbox_create(tree, node, GATES_STR(""), 8, &box);
        if (gates_is_ok(err)) err = gates_node_set_hidden(tree, box, true);
        if (gates_is_ok(err)) v->editor = box.index;
    }
    if (gates_is_ok(err) && !gates_node_eq(parent, GATES_NODE_NULL)) {
        err = gates_node_append(tree, parent, node);
    }
    if (!gates_is_ok(err)) {
        if (!owned) {
            if (v->cols != nullptr) a.free_fn(a.ctx, v->cols);
            a.free_fn(a.ctx, v);
        }
        if (!gates_node_eq(node, GATES_NODE_NULL)) gates_i_discard_detached(tree, node);
        return err;
    }
    *out_view = node;
    return GATES_OK;
}

gates_err_t gates_view_set_model(gates_tree_t *tree, gates_node_t view,
                                 const gates_rows_model_t *model) {
    struct gates_i_view *v = view_of(tree, view);
    if (v == nullptr || v->log != nullptr || /* a log owns its model */
        (model != nullptr && (model->count == nullptr || model->id_at == nullptr ||
                              model->index_of == nullptr || model->cell == nullptr ||
                              (v->tree && model->row_info == nullptr) ||
                              (v->multi && model->next_selected == nullptr)))) {
        return PROVEN_ERR_INVALID_ARG;
    }
    (void)end_edit(tree, view.index, v, false, false); /* a cancel never fails */
    v->has_model = model != nullptr;
    v->model = model != nullptr ? *model : (gates_rows_model_t){0};
    v->first = 0;
    v->scroll_x = 0;
    v->sel = 0;
    v->sel_row = 0;
    v->anchor = 0;
    v->press_col = -1;
    if (tree->drag_node == view.index &&
        (tree->drag_kind == GATES_DRAG_VIEW_VTHUMB || tree->drag_kind == GATES_DRAG_VIEW_HTHUMB ||
         tree->drag_kind == GATES_DRAG_VIEW_COLUMN)) {
        tree->drag_kind = GATES_DRAG_NONE;
        tree->drag_node = GATES_NONE;
    }
    gates_i_mark_dirty(tree, view.index, GATES_DIRTY_PAINT);
    return GATES_OK;
}

/* Keeps row `row` in view (stored offset). */
static void keep_visible(struct gates_i_view *v, const view_geom_t *g, gates_u64 row) {
    gates_u64 first = g->first;
    if (row < first) {
        first = row;
    } else if (row >= first + g->visible) {
        first = row - g->visible + 1;
    }
    v->first = first < g->max_first ? first : g->max_first;
}

static gates_err_t reconcile(gates_tree_t *tree, gates_node_t view, struct gates_i_view *v);

gates_err_t gates_view_model_changed(gates_tree_t *tree, gates_node_t view) {
    struct gates_i_view *v = view_of(tree, view);
    if (v == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_u64 row = 0;
    if (v->editing) {
        if (!v->has_model || !v->model.index_of(v->model.user, v->edit_id, &row)) {
            (void)end_edit(tree, view.index, v, false, false); /* its row went away */
        } else {
            gates_i_mark_dirty(tree, view.index, GATES_DIRTY_LAYOUT); /* the row may have moved */
        }
    }
    return reconcile(tree, view, v);
}

/* Clamps the position and moves a selection whose item went away (PROGRAM). */
static gates_err_t reconcile(gates_tree_t *tree, gates_node_t view, struct gates_i_view *v) {
    view_geom_t g;
    geom_now(tree, view.index, v, &g);
    v->first = g.first;
    v->scroll_x = g.scroll_x;
    gates_err_t err = GATES_OK;
    if (v->sel != 0 && v->has_model) {
        gates_u64 row = 0;
        if (v->model.index_of(v->model.user, v->sel, &row)) {
            v->sel_row = row;
        } else {
            /* Gone: the item now nearest its old row, or nothing. */
            gates_item_id_t next = 0;
            if (g.count > 0) {
                gates_u64 r = v->sel_row < g.count ? v->sel_row : g.count - 1;
                next = v->model.id_at(v->model.user, r);
                v->sel_row = r;
            }
            gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, view.index)->state_index);
            if (gates_i_wants_events(tree, view.index)) {
                err = gates_i_event_reserve(tree, 1, 0);
            }
            v->sel = next;
            st->revision++;
            if (gates_is_ok(err)) {
                gates_i_event_push_ex(tree, view.index, GATES_EVENT_SELECTION_CHANGED,
                                      GATES_ORIGIN_PROGRAM, 0, 0);
            }
        }
    }
    gates_i_mark_dirty(tree, view.index, GATES_DIRTY_PAINT);
    return err; /* the selection moved even when its announcement could not be queued */
}

gates_item_id_t gates_view_selected(const gates_tree_t *tree, gates_node_t view) {
    const struct gates_i_view *v = view_of(tree, view);
    return v != nullptr ? v->sel : 0;
}

gates_err_t gates_view_set_selected(gates_tree_t *tree, gates_node_t view, gates_item_id_t id) {
    struct gates_i_view *v = view_of(tree, view);
    if (v == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_u64 row = 0;
    if (id != 0 && (!v->has_model || !v->model.index_of(v->model.user, id, &row))) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (v->multi) v->anchor = id; /* a range starts here */
    if (v->sel != id) {
        v->sel = id;
        v->sel_row = row;
        gates_i_state(tree, gates_i_slot(tree, view.index)->state_index)->revision++;
        gates_i_mark_dirty(tree, view.index, GATES_DIRTY_PAINT);
    }
    return GATES_OK;
}

gates_err_t gates_view_scroll_to(gates_tree_t *tree, gates_node_t view, gates_item_id_t id) {
    struct gates_i_view *v = view_of(tree, view);
    gates_u64 row = 0;
    if (v == nullptr || id == 0 || !v->has_model || !v->model.index_of(v->model.user, id, &row)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    view_geom_t g;
    geom_now(tree, view.index, v, &g);
    keep_visible(v, &g, row);
    gates_i_mark_dirty(tree, view.index, GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_u64 gates_view_first_row(const gates_tree_t *tree, gates_node_t view) {
    const struct gates_i_view *v = view_of(tree, view);
    return v != nullptr ? v->first : 0;
}

gates_u32 gates_view_visible_rows(const gates_tree_t *tree, gates_node_t view) {
    struct gates_i_view *v = view_of(tree, view);
    if (v == nullptr) return 0;
    view_geom_t g;
    geom(tree, view.index, v, model_count(v), tree->line_height, &g);
    return g.visible;
}

gates_i32 gates_view_scroll_x(const gates_tree_t *tree, gates_node_t view) {
    const struct gates_i_view *v = view_of(tree, view);
    return v != nullptr ? v->scroll_x : 0;
}

static gates_i_column_t *find_col(struct gates_i_view *v, gates_column_id_t id) {
    for (gates_u32 i = 0; v != nullptr && i < v->ncol; i++) {
        if (v->cols[i].id == id) return &v->cols[i];
    }
    return nullptr;
}

gates_i32 gates_view_column_width(const gates_tree_t *tree, gates_node_t view,
                                  gates_column_id_t column) {
    gates_i_column_t *c = find_col(view_of(tree, view), column);
    return c != nullptr ? c->width : 0;
}

gates_err_t gates_view_set_column_width(gates_tree_t *tree, gates_node_t view,
                                        gates_column_id_t column, gates_i32 width) {
    gates_i_column_t *c = find_col(view_of(tree, view), column);
    if (c == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    c->width = width < c->min_width ? c->min_width : width;
    gates_i_mark_dirty(tree, view.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_rect_t gates_view_part_rect(const gates_tree_t *tree, gates_node_t view,
                                  gates_view_part_t part, gates_u64 index) {
    struct gates_i_view *v = view_of(tree, view);
    gates_rect_t none = { 0, 0, 0, 0 };
    if (v == nullptr) return none;
    view_geom_t g;
    geom(tree, view.index, v, model_count(v), tree->line_height, &g);
    switch (part) {
    case GATES_VIEW_PART_BODY:
        return g.body;
    case GATES_VIEW_PART_ROW:
        if (index < g.first || index - g.first >= g.painted) return none;
        return (gates_rect_t){ g.body.x, g.body.y + (gates_i32)(index - g.first) * g.row_h,
                               g.body.w, g.row_h };
    case GATES_VIEW_PART_HEADER:
        if (g.header.h == 0 || index >= v->ncol || v->cols[index].hidden) return none;
        return (gates_rect_t){ col_left(v, &g, (gates_u32)index), g.header.y,
                               v->cols[index].width, g.header.h };
    case GATES_VIEW_PART_VTHUMB:
        return g.vthumb;
    case GATES_VIEW_PART_HTHUMB:
        return g.hthumb;
    }
    return none;
}

/* -- tree rows ---------------------------------------------------------------------------------- */

/* The mark area of a tree row starts here (relative to the first column's left edge). */
static gates_i32 tree_mark_x(const gates_row_info_t *info) {
    return VIEW_CELL_PAD + (gates_i32)info->depth * GATES_VIEW_INDENT;
}

static gates_i32 tree_text_indent(const gates_row_info_t *info) {
    return (gates_i32)info->depth * GATES_VIEW_INDENT + GATES_VIEW_INDENT;
}

/* A small triangle: pointing right when closed, down when open. */
static gates_err_t paint_mark(gates_draw_list_t *dl, gates_i32 col_x, gates_i32 row_y, gates_i32 row_h,
                              const gates_row_info_t *info, gates_color_t color) {
    gates_i32 size = 9;
    gates_i32 mx = col_x + tree_mark_x(info) + (GATES_VIEW_INDENT - size) / 2;
    gates_i32 my = row_y + (row_h - size) / 2;
    for (gates_i32 k = 0; k <= size / 2; k++) {
        gates_rect_t r = info->expanded ? (gates_rect_t){ mx + k, my + 2 + k, size - 2 * k, 1 }
                                        : (gates_rect_t){ mx + 2 + k, my + k, 1, size - 2 * k };
        gates_err_t err = gates_draw_rect(dl, r, color);
        if (!gates_is_ok(err)) return err;
    }
    return GATES_OK;
}

/* -- measure and paint ----------------------------------------------------------------------- */

gates_size_t gates_i_view_measure(const gates_tree_t *tree, const gates_node_slot_t *s,
                                  const gates_text_backend_t *text) {
    const struct gates_i_view *v = gates_i_state(tree, s->state_index)->view;
    gates_text_metrics_t m = text->metrics(text->ctx, gates_i_slot_font(tree, s));
    gates_i32 row_h = row_height(m.line_height);
    gates_i32 w = v->ncol > 0 ? content_width(v, 0) : 20 * m.advance;
    gates_i32 h = (v->header && v->ncol > 0 ? row_h : 0) + 8 * row_h;
    return (gates_size_t){ w + GATES_SCROLLBAR_PX + 2, h + 2 };
}

#define TRY_DRAW(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)

gates_err_t gates_i_view_paint(const gates_tree_t *tree, gates_u32 idx, gates_draw_list_t *dl,
                               const gates_theme_t *theme, const gates_text_backend_t *text) {
    struct gates_i_view *v = view_at(tree, idx);
    const gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
    gates_rect_t r = gates_i_slot(tree, idx)->layout_rect;
    gates_i32 font = gates_i_font(tree, idx);
    gates_text_metrics_t m = text->metrics(text->ctx, font);
    view_geom_t g;
    geom(tree, idx, v, model_count(v), m.line_height, &g); /* count(): once per frame */
    bool focused = tree->focus == idx;
    bool inert = st->disabled;
    TRY_DRAW(gates_draw_rect(dl, r, gates_theme_color(theme, GATES_COLOR_CONTROL_BG)));

    /* Header: labels are ours, copied at creation. */
    if (g.header.h > 0) {
        TRY_DRAW(gates_draw_rect(dl, g.header, gates_theme_color(theme, GATES_COLOR_PANEL_BG)));
        TRY_DRAW(gates_draw_clip_push(dl, g.header));
        for (gates_u32 c = 0; c < v->ncol; c++) {
            if (v->cols[c].hidden) continue;
            gates_rect_t hc = { col_left(v, &g, c), g.header.y, v->cols[c].width, g.header.h };
            TRY_DRAW(gates_draw_rect(dl, (gates_rect_t){ hc.x + hc.w - 1, hc.y, 1, hc.h },
                                     gates_theme_color(theme, GATES_COLOR_CONTROL_BORDER)));
            if (v->cols[c].label_len > 0) {
                TRY_DRAW(gates_draw_text(dl, (gates_rect_t){ hc.x + VIEW_CELL_PAD,
                                                             hc.y + (hc.h - m.line_height) / 2,
                                                             hc.w - 2 * VIEW_CELL_PAD, m.line_height },
                                         (gates_str_t){ .ptr = v->cols[c].label,
                                                        .size = v->cols[c].label_len },
                                         font, gates_theme_color(theme, GATES_COLOR_PANEL_FG)));
            }
        }
        TRY_DRAW(gates_draw_clip_pop(dl));
        TRY_DRAW(gates_draw_rect(dl, (gates_rect_t){ g.header.x, g.header.y + g.header.h - 1,
                                                     g.header.w, 1 },
                                 gates_theme_color(theme, GATES_COLOR_CONTROL_BORDER)));
    }

    /* Rows: ids first (one id_at per painted row), then cells column by column. */
    gates_item_id_t ids[GATES_VIEW_MAX_ROWS];
    gates_row_info_t infos[GATES_VIEW_MAX_ROWS];
    bool selrow[GATES_VIEW_MAX_ROWS];
    gates_i32 sel_k = -1; /* the focus row (the selected row of a single-select view) */
    for (gates_u32 k = 0; k < g.painted; k++) {
        ids[k] = v->model.id_at(v->model.user, g.first + k);
        if (ids[k] != 0 && ids[k] == v->sel) sel_k = (gates_i32)k;
        selrow[k] = !v->multi && ids[k] != 0 && ids[k] == v->sel;
        infos[k] = (gates_row_info_t){0};
        if (v->tree && ids[k] != 0) {
            TRY_DRAW(v->model.row_info(v->model.user, ids[k], &infos[k])); /* painted rows only */
        }
    }
    if (v->multi) { /* the model's selection, asked for the painted rows only */
        for (gates_u64 r = g.first; r < g.first + g.painted;) {
            gates_u64 n = v->model.next_selected(v->model.user, r);
            if (n == GATES_ROW_NONE || n < r || n >= g.first + g.painted) break;
            selrow[n - g.first] = true;
            r = n + 1;
        }
    }
    TRY_DRAW(gates_draw_clip_push(dl, g.body));
    for (gates_u32 k = 0; k < g.painted; k++) {
        if (!selrow[k]) continue;
        TRY_DRAW(gates_draw_rect(dl, (gates_rect_t){ g.body.x, g.body.y + (gates_i32)k * g.row_h,
                                                     g.body.w, g.row_h },
                                 gates_theme_color(theme, GATES_COLOR_SELECTION_BG)));
    }
    gates_u32 ncol = v->ncol > 0 ? v->ncol : 1;
    for (gates_u32 c = 0; c < ncol; c++) {
        gates_rect_t cr = { col_left(v, &g, c), g.body.y, col_width(v, &g, c), g.body.h };
        gates_rect_t clip = gates_rect_intersect(cr, g.body);
        if (gates_rect_is_empty(clip)) continue; /* scrolled out or hidden: not asked for */
        gates_column_id_t cid = v->ncol > 0 ? v->cols[c].id : 0;
        const gates_i_column_t *col = v->ncol > 0 ? &v->cols[c] : nullptr;
        TRY_DRAW(gates_draw_clip_push(dl, clip));
        for (gates_u32 k = 0; k < g.painted; k++) {
            if (ids[k] == 0) continue;
            gates_cell_t cell = {0};
            TRY_DRAW(v->model.cell(v->model.user, ids[k], cid, &cell));
            gates_i32 row_y = g.body.y + (gates_i32)k * g.row_h;
            if (col != nullptr && col->paint != nullptr) {
                gates_rect_t cell_r = { cr.x, row_y, cr.w, g.row_h };
                TRY_DRAW(gates_draw_clip_push(dl, gates_rect_intersect(cell_r, clip)));
                gates_cell_paint_t cp = { .dl = dl, .rect = cell_r, .theme = theme, .font = font,
                                          .id = ids[k], .column = cid, .cell = &cell,
                                          .selected = selrow[k], .disabled = inert };
                TRY_DRAW(col->paint(col->paint_user, &cp));
                TRY_DRAW(gates_draw_clip_pop(dl));
                continue;
            }
            gates_color_token_t fg = inert                     ? GATES_COLOR_CONTROL_DISABLED_FG
                                     : selrow[k] ? GATES_COLOR_SELECTION_FG
                                     : infos[k].state == GATES_ROW_LOADING ? GATES_COLOR_CONTROL_DISABLED_FG
                                     : infos[k].state == GATES_ROW_ERROR   ? GATES_COLOR_ERROR
                                                                           : GATES_COLOR_CONTROL_FG;
            gates_i32 indent = v->tree && c == tree_col(v) ? tree_text_indent(&infos[k]) : 0;
            gates_i32 x = cr.x + VIEW_CELL_PAD + indent;
            gates_i32 right = cr.x + cr.w - VIEW_CELL_PAD;
            gates_cell_kind_t kind = col != nullptr ? col->kind : GATES_CELL_TEXT;
            if (kind == GATES_CELL_CHECK) {
                gates_rect_t box = { x, row_y + (g.row_h - GATES_CHECK_BOX) / 2, GATES_CHECK_BOX, GATES_CHECK_BOX };
                TRY_DRAW(gates_draw_rect(dl, box, gates_theme_color(theme, GATES_COLOR_CONTROL_BG)));
                TRY_DRAW(gates_draw_border(dl, box, 1, gates_theme_color(theme, GATES_COLOR_CONTROL_BORDER)));
                if (cell.checked) {
                    TRY_DRAW(gates_draw_rect(dl, (gates_rect_t){ box.x + 3, box.y + 3, box.w - 6, box.h - 6 },
                                             gates_theme_color(theme, inert ? GATES_COLOR_CONTROL_DISABLED_FG
                                                                            : GATES_COLOR_SELECTION_BG)));
                }
                x += GATES_CHECK_BOX + GATES_CHECK_GAP;
            } else if (kind == GATES_CELL_ICON_TEXT) {
                const gates_image_t *im = gates_tree_image(tree, cell.icon);
                if (im != nullptr) {
                    TRY_DRAW(gates_draw_image(dl, (gates_rect_t){ x, row_y + (g.row_h - GATES_ICON_SIZE) / 2,
                                                                  GATES_ICON_SIZE, GATES_ICON_SIZE }, im));
                }
                x += GATES_ICON_SIZE + VIEW_CELL_PAD;
            } else if (kind == GATES_CELL_PROGRESS) {
                /* The bar, then any text right of it. */
                gates_i32 tw = cell.text.size > 0 ? text->measure(text->ctx, font, cell.text).w : 0;
                gates_i32 bw = right - x - (tw > 0 ? tw + VIEW_CELL_PAD : 0);
                if (bw > 2) {
                    gates_rect_t bar = { x, row_y + VIEW_BAR_INSET, bw, g.row_h - 2 * VIEW_BAR_INSET };
                    TRY_DRAW(gates_draw_rect(dl, bar, gates_theme_color(theme, GATES_COLOR_CONTROL_BG)));
                    TRY_DRAW(gates_draw_border(dl, bar, 1, gates_theme_color(theme, GATES_COLOR_CONTROL_BORDER)));
                    gates_u32 pm = cell.permille < 1000u ? cell.permille : 1000u;
                    gates_i32 fw = (gates_i32)(((gates_i64)(bar.w - 2) * pm) / 1000);
                    if (fw > 0) {
                        TRY_DRAW(gates_draw_rect(dl, (gates_rect_t){ bar.x + 1, bar.y + 1, fw, bar.h - 2 },
                                                 gates_theme_color(theme, inert ? GATES_COLOR_CONTROL_DISABLED_FG
                                                                                : GATES_COLOR_SELECTION_BG)));
                    }
                    x += bw + VIEW_CELL_PAD;
                }
            }
            if (cell.text.size == 0 || x >= right) continue;
            /* Copied into the draw list before the model is asked for anything else. */
            TRY_DRAW(gates_draw_text(dl, (gates_rect_t){ x, row_y + (g.row_h - m.line_height) / 2,
                                                         right - x, m.line_height },
                                     cell.text, font, gates_theme_color(theme, fg)));
        }
        if (v->tree && c == tree_col(v)) {
            for (gates_u32 k = 0; k < g.painted; k++) {
                if (!infos[k].expandable) continue;
                gates_color_token_t mt = selrow[k] ? GATES_COLOR_SELECTION_FG
                                                               : GATES_COLOR_CONTROL_FG;
                TRY_DRAW(paint_mark(dl, cr.x, g.body.y + (gates_i32)k * g.row_h, g.row_h, &infos[k],
                                    gates_theme_color(theme, mt)));
            }
        }
        TRY_DRAW(gates_draw_clip_pop(dl));
    }
    if (focused && sel_k >= 0) {
        TRY_DRAW(gates_draw_border(dl, (gates_rect_t){ g.body.x, g.body.y + sel_k * g.row_h,
                                                       g.body.w, g.row_h },
                                   gates_theme_focus_width(theme),
                                   gates_theme_color(theme, GATES_COLOR_FOCUS_RING)));
        gates_i32 cc = -1; /* the current cell, inside the row's ring, in the selection's text colour (0.8.0) */
        for (gates_u32 c = 0; v->cur_col != 0 && c < v->ncol; c++) {
            if (v->cols[c].id == v->cur_col && !v->cols[c].hidden) cc = (gates_i32)c;
        }
        if (cc >= 0) {
            gates_i32 fw = gates_theme_focus_width(theme);
            TRY_DRAW(gates_draw_border(dl, (gates_rect_t){ col_left(v, &g, (gates_u32)cc) + fw, g.body.y + sel_k * g.row_h + fw,
                                                           v->cols[cc].width - 2 * fw, g.row_h - 2 * fw },
                                       1, gates_theme_color(theme, selrow[sel_k] ? GATES_COLOR_SELECTION_FG /* on the selection */
                                                                                 : GATES_COLOR_FOCUS_RING)));
        }
    }
    TRY_DRAW(gates_draw_clip_pop(dl));

    if (!gates_rect_is_empty(g.vtrack)) {
        TRY_DRAW(gates_draw_rect(dl, g.vtrack, gates_theme_color(theme, GATES_COLOR_PANEL_BG)));
        TRY_DRAW(gates_draw_rect(dl, g.vthumb, gates_theme_color(theme, GATES_COLOR_CONTROL_BORDER)));
    }
    if (!gates_rect_is_empty(g.htrack)) {
        TRY_DRAW(gates_draw_rect(dl, g.htrack, gates_theme_color(theme, GATES_COLOR_PANEL_BG)));
        TRY_DRAW(gates_draw_rect(dl, g.hthumb, gates_theme_color(theme, GATES_COLOR_CONTROL_BORDER)));
    }
    return gates_draw_border(dl, r, focused && sel_k < 0 ? gates_theme_focus_width(theme) : 1,
                             gates_theme_color(theme, focused && sel_k < 0
                                                                    ? GATES_COLOR_FOCUS_RING
                                                                    : GATES_COLOR_CONTROL_BORDER));
}

/* -- interaction ------------------------------------------------------------------------------ */

/* After a person moved the view: a log follows exactly when it shows its end,
 * and says so when that changes (0.8.0). */
static void note_scrolled(gates_tree_t *tree, gates_u32 idx, struct gates_i_view *v, const view_geom_t *g) {
    if (v->log == nullptr) return;
    bool follow = v->first >= g->max_first;
    if (follow != v->follow) {
        v->follow = follow;
        gates_i_event_try_push(tree, idx, GATES_EVENT_FOLLOW_CHANGED, 0);
    }
}

bool gates_i_view_following(const gates_widget_state_t *st) {
    return st->view != nullptr && st->view->follow;
}

/* Asks the model to open or close a tree row. */
static void request_expand(gates_tree_t *tree, gates_u32 idx, gates_item_id_t id, bool open) {
    if (!gates_i_wants_events(tree, idx)) return;
    gates_err_t err = gates_i_event_reserve(tree, 1, 0);
    if (!gates_is_ok(err)) {
        tree->input_error = err;
        return;
    }
    gates_i_event_push_ex(tree, idx, GATES_EVENT_EXPAND_REQUESTED, GATES_ORIGIN_USER, open ? 1u : 0u, id);
}

/* A person selects row `row`: announced first (no change without it), kept in view. */
/* Whether a row is selected: the model's answer in a multi-select view. */
static bool row_is_selected(struct gates_i_view *v, gates_u64 row, gates_item_id_t id) {
    if (!v->multi) return id != 0 && id == v->sel;
    return v->has_model && v->model.next_selected(v->model.user, row) == row;
}

bool gates_i_view_multi(const gates_tree_t *tree, gates_u32 idx) {
    const struct gates_i_view *v = view_at(tree, idx);
    return v != nullptr && v->multi;
}

/* A person's selection gesture in a multi-select view (RFC-0007): a request for
 * the program. ONE, TOGGLE and ALL start a new range at the target. */
static void select_request(gates_tree_t *tree, gates_u32 idx, struct gates_i_view *v, gates_select_request_t kind,
                           gates_item_id_t target) {
    if (!v->multi || target == 0) return;
    if (kind != GATES_SELECT_RANGE && kind != GATES_SELECT_ADD_RANGE) v->anchor = target;
    if (v->anchor == 0) v->anchor = target;
    if (!gates_i_wants_events(tree, idx)) return;
    gates_err_t err = gates_i_event_reserve(tree, 1, 0);
    if (!gates_is_ok(err)) {
        tree->input_error = err;
        return;
    }
    gates_i_event_push_req(tree, idx, GATES_EVENT_SELECT_REQUESTED, (gates_u32)kind, target, v->anchor);
}

/* After the focus row moved by a key: plain keys select it alone, Shift makes
 * a range from the anchor, Ctrl alone only moves the focus. */
static void moved(gates_tree_t *tree, gates_u32 idx, struct gates_i_view *v, gates_item_id_t was, bool shift, bool ctrl) {
    if (!v->multi || v->sel == 0 || (ctrl && !shift)) return;
    if (shift) {
        if (v->anchor == 0) v->anchor = was != 0 ? was : v->sel;
        select_request(tree, idx, v, GATES_SELECT_RANGE, v->sel);
    } else {
        select_request(tree, idx, v, GATES_SELECT_ONE, v->sel);
    }
}

static void pick(gates_tree_t *tree, gates_u32 idx, struct gates_i_view *v, const view_geom_t *g,
                 gates_u64 row) {
    if (!v->has_model || row >= g->count) return;
    gates_item_id_t id = v->model.id_at(v->model.user, row);
    if (id == 0) return;
    if (id != v->sel) {
        gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
        if (gates_i_wants_events(tree, idx)) {
            gates_err_t err = gates_i_event_reserve(tree, 1, 0);
            if (!gates_is_ok(err)) {
                tree->input_error = err;
                return;
            }
        }
        v->sel = id;
        st->revision++;
        gates_i_event_push_ex(tree, idx, GATES_EVENT_SELECTION_CHANGED, GATES_ORIGIN_USER, 0, 0);
    }
    v->sel_row = row;
    keep_visible(v, g, row);
    note_scrolled(tree, idx, v, g);
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
}

static void activate(gates_tree_t *tree, gates_u32 idx, struct gates_i_view *v) {
    if (v->sel == 0 || !v->has_model) return;
    if (!gates_i_wants_events(tree, idx)) return;
    gates_err_t err = gates_i_event_reserve(tree, 1, 0);
    if (!gates_is_ok(err)) {
        tree->input_error = err;
        return;
    }
    gates_i_event_push_ex(tree, idx, GATES_EVENT_ACTIVATED, GATES_ORIGIN_USER, 0, v->sel);
}

static void scroll_x_by(gates_tree_t *tree, gates_u32 idx, struct gates_i_view *v,
                        const view_geom_t *g, gates_i32 dx) {
    gates_i32 x = g->scroll_x + dx;
    if (x < 0) x = 0;
    if (x > g->max_x) x = g->max_x;
    if (x != v->scroll_x) {
        v->scroll_x = x;
        gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
    }
}

static void scroll_rows(gates_tree_t *tree, gates_u32 idx, struct gates_i_view *v,
                        const view_geom_t *g, gates_i64 rows) {
    gates_u64 f = g->first;
    if (rows < 0) {
        gates_u64 up = (gates_u64)(-rows);
        f = f > up ? f - up : 0;
    } else {
        gates_u64 down = (gates_u64)rows;
        f = g->max_first - f > down ? f + down : g->max_first;
    }
    if (f != v->first) {
        v->first = f;
        gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
    }
    note_scrolled(tree, idx, v, g);
}

/* -- editing cells (0.6.0) ------------------------------------------------------------- */

static bool text_kind(gates_cell_kind_t k) {
    return k == GATES_CELL_TEXT || k == GATES_CELL_ICON_TEXT;
}

static gates_i32 col_pos(const struct gates_i_view *v, gates_column_id_t id) {
    for (gates_u32 i = 0; i < v->ncol; i++) {
        if (v->cols[i].id == id) return (gates_i32)i;
    }
    return -1;
}

/* Column position `c` can be edited as text, or toggled as a check. */
static bool can_edit(const struct gates_i_view *v, gates_i32 c, bool text) {
    return c >= 0 && (gates_u32)c < v->ncol && v->has_model && v->model.set_cell != nullptr &&
           v->cols[c].editable && !v->cols[c].hidden && (text ? text_kind(v->cols[c].kind) && v->editor != GATES_NONE
                                        : v->cols[c].kind == GATES_CELL_CHECK);
}

/* The first column that F2 edits (text) or Space toggles (check), or -1. */
static gates_i32 first_editable(const struct gates_i_view *v, bool text) {
    for (gates_u32 i = 0; i < v->ncol; i++) {
        if (can_edit(v, (gates_i32)i, text)) return (gates_i32)i;
    }
    return -1;
}

/* Where a tree row's first cell starts its content (0 elsewhere). */
static gates_i32 row_indent(const struct gates_i_view *v, gates_i32 c, gates_item_id_t id) {
    gates_row_info_t info = {0};
    if (!v->tree || (gates_u32)c != tree_col(v) || !gates_is_ok(v->model.row_info(v->model.user, id, &info))) {
        return 0;
    }
    return tree_text_indent(&info);
}

/* The editor covers the cell's text, inside the rows area; empty when the row is not shown. */
static gates_rect_t editor_rect(gates_tree_t *tree, gates_u32 idx, struct gates_i_view *v) {
    view_geom_t g;
    geom_now(tree, idx, v, &g);
    gates_rect_t none = { g.body.x, g.body.y, 0, 0 };
    gates_u64 row = 0;
    gates_i32 c = col_pos(v, v->edit_col);
    if (!v->editing || c < 0 || !v->has_model || !v->model.index_of(v->model.user, v->edit_id, &row) ||
        row < g.first || row - g.first >= g.painted) {
        return none;
    }
    gates_i32 x = col_left(v, &g, (gates_u32)c) + row_indent(v, c, v->edit_id);
    gates_i32 w = v->cols[c].width - row_indent(v, c, v->edit_id);
    if (v->cols[c].kind == GATES_CELL_ICON_TEXT) {
        x += VIEW_CELL_PAD + GATES_ICON_SIZE;
        w -= VIEW_CELL_PAD + GATES_ICON_SIZE;
    }
    gates_rect_t r = gates_rect_intersect((gates_rect_t){ x, g.body.y + (gates_i32)(row - g.first) * g.row_h,
                                                          w, g.row_h }, g.body);
    return gates_rect_is_empty(r) ? none : r;
}

void gates_i_view_place_editor(gates_tree_t *tree, gates_u32 idx) {
    struct gates_i_view *v = view_at(tree, idx);
    if (v != nullptr && v->editor != GATES_NONE) {
        gates_i_slot(tree, v->editor)->layout_rect = editor_rect(tree, idx, v);
    }
}

gates_u32 gates_i_view_of_editor(const gates_tree_t *tree, gates_u32 box) {
    if (box == GATES_NONE) return GATES_NONE;
    gates_u32 p = gates_i_slot(tree, box)->parent;
    if (p == GATES_NONE || gates_i_slot(tree, p)->kind != GATES_NODE_VIEW) return GATES_NONE;
    const struct gates_i_view *v = view_at(tree, p);
    return v != nullptr && v->editor == box ? p : GATES_NONE;
}

/* Reports a changed cell and re-reads the model; the event was reserved. */
static void edited(gates_tree_t *tree, gates_u32 idx, struct gates_i_view *v, gates_item_id_t id,
                   gates_column_id_t col) {
    gates_i_event_push_ex(tree, idx, GATES_EVENT_CELL_EDITED, GATES_ORIGIN_USER, col, id);
    gates_err_t err = reconcile(tree, gates_i_handle(tree, idx), v); /* set_cell may have changed rows */
    if (!gates_is_ok(err)) tree->input_error = err;
}

/* Commits or cancels an open edit. `left`: focus already moved elsewhere, so a
 * refused commit drops the edit instead of keeping the editor open. */
static gates_err_t end_edit(gates_tree_t *tree, gates_u32 idx, struct gates_i_view *v, bool commit, bool left) {
    if (!v->editing) return GATES_OK;
    gates_node_t box = gates_i_handle(tree, v->editor);
    const gates_widget_state_t *bs = gates_i_state(tree, gates_i_slot(tree, v->editor)->state_index);
    if (bs->revision == v->edit_rev) commit = false; /* nothing typed: nothing to report */
    if (commit && v->has_model && v->model.set_cell != nullptr) {
        gates_err_t err = gates_i_wants_events(tree, idx) ? gates_i_event_reserve(tree, 1, 0) : GATES_OK;
        if (gates_is_ok(err)) {
            gates_cell_t value = { .text = gates_textbox_text(tree, box) };
            err = v->model.set_cell(v->model.user, v->edit_id, v->edit_col, &value);
        }
        if (!gates_is_ok(err)) {
            if (!left) {
                (void)gates_textbox_set_invalid(tree, box, true);
                return err;
            }
            commit = false; /* focus left: the refused text is dropped */
        }
    }
    gates_item_id_t id = v->edit_id;
    gates_column_id_t col = v->edit_col;
    v->editing = false;
    v->edit_id = 0;
    v->edit_col = 0;
    if (!left && tree->focus == v->editor) gates_tree_set_focus(tree, gates_i_handle(tree, idx));
    (void)gates_node_set_hidden(tree, box, true);
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    if (commit) edited(tree, idx, v, id, col);
    return GATES_OK;
}

void gates_i_view_editor_left(gates_tree_t *tree, gates_u32 box) {
    gates_u32 idx = gates_i_view_of_editor(tree, box);
    if (idx != GATES_NONE) (void)end_edit(tree, idx, view_at(tree, idx), true, true);
}

void gates_i_view_cancel_edit(gates_tree_t *tree, gates_u32 idx) {
    struct gates_i_view *v = view_at(tree, idx);
    if (v != nullptr) (void)end_edit(tree, idx, v, false, false);
}

/* Enter commits (a refusal keeps the editor, marked invalid), Escape cancels. */
bool gates_i_view_editor_key(gates_tree_t *tree, gates_u32 idx, const gates_key_event_t *ev) {
    struct gates_i_view *v = view_at(tree, idx);
    if (v == nullptr || !v->editing || ev->ctrl || ev->alt) return false;
    const gates_widget_state_t *bs = gates_i_state(tree, gates_i_slot(tree, v->editor)->state_index);
    if (gates_text_edit_preedit(bs->edit).size > 0) return false; /* the composition first */
    if (ev->key == GATES_KEY_ENTER) {
        gates_err_t err = end_edit(tree, idx, v, true, false);
        if (err == PROVEN_ERR_NOMEM) tree->input_error = err;
        return true;
    }
    if (ev->key == GATES_KEY_ESCAPE) {
        (void)end_edit(tree, idx, v, false, false);
        return true;
    }
    return false;
}

/* Scrolls sideways so column position c is in view (as much of it as fits). */
static void column_into_view(struct gates_i_view *v, const view_geom_t *g, gates_i32 c) {
    gates_i32 left = 0;
    for (gates_i32 i = 0; i < c; i++) left += shown_w(&v->cols[i]);
    gates_i32 x = g->scroll_x;
    if (left + v->cols[c].width > x + g->body.w) x = left + v->cols[c].width - g->body.w;
    if (left < x) x = left;
    v->scroll_x = x < 0 ? 0 : (x > g->max_x ? g->max_x : x);
}

/* Opens the editor on row `id`, column position `c` (the row is selected). */
static gates_err_t begin_edit(gates_tree_t *tree, gates_u32 idx, struct gates_i_view *v, gates_item_id_t id,
                              gates_i32 c) {
    gates_u64 row = 0;
    if (!can_edit(v, c, true) || id == 0 || !v->model.index_of(v->model.user, id, &row)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_err_t err = end_edit(tree, idx, v, true, false);
    if (!gates_is_ok(err)) return err;
    gates_cell_t cell = {0};
    err = v->model.cell(v->model.user, id, v->cols[c].id, &cell);
    gates_node_t box = gates_i_handle(tree, v->editor);
    if (gates_is_ok(err)) err = gates_textbox_set_text(tree, box, cell.text); /* copied at once */
    if (gates_is_ok(err)) {
        err = gates_node_set_access_name(tree, box, (gates_str_t){ .ptr = v->cols[c].label,
                                                                    .size = v->cols[c].label_len });
    }
    if (!gates_is_ok(err)) return err;
    gates_widget_state_t *bs = gates_i_state(tree, gates_i_slot(tree, v->editor)->state_index);
    (void)gates_textbox_set_invalid(tree, box, false);
    gates_text_edit_select_all(bs->edit);
    bs->view_x = 0;
    v->edit_rev = bs->revision;
    view_geom_t g;
    geom_now(tree, idx, v, &g);
    keep_visible(v, &g, row);
    column_into_view(v, &g, c);
    note_scrolled(tree, idx, v, &g);
    v->editing = true;
    v->edit_id = id;
    v->edit_col = v->cols[c].id;
    (void)gates_node_set_hidden(tree, box, false);
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    gates_tree_set_focus(tree, box);
    return GATES_OK;
}

/* A person toggles row `id`'s check cell at column position c. */
static void toggle_check(gates_tree_t *tree, gates_u32 idx, struct gates_i_view *v, gates_item_id_t id,
                         gates_i32 c) {
    gates_cell_t cur = {0};
    if (!can_edit(v, c, false) || id == 0 || !gates_is_ok(v->model.cell(v->model.user, id, v->cols[c].id, &cur))) {
        return;
    }
    gates_err_t err = gates_i_wants_events(tree, idx) ? gates_i_event_reserve(tree, 1, 0) : GATES_OK;
    if (!gates_is_ok(err)) {
        tree->input_error = err;
        return;
    }
    gates_cell_t value = { .checked = !cur.checked };
    if (gates_is_ok(v->model.set_cell(v->model.user, id, v->cols[c].id, &value))) {
        edited(tree, idx, v, id, v->cols[c].id);
    }
}

/* Before the view moves under an open edit: commit it; false while the model refuses. */
static bool settle(gates_tree_t *tree, gates_u32 idx, struct gates_i_view *v) {
    return gates_is_ok(end_edit(tree, idx, v, true, false));
}

gates_err_t gates_view_edit(gates_tree_t *tree, gates_node_t view, gates_item_id_t id, gates_column_id_t column) {
    struct gates_i_view *v = view_of(tree, view);
    if (v == nullptr) return PROVEN_ERR_INVALID_ARG;
    gates_i32 c = col_pos(v, column);
    gates_u64 row = 0;
    if (!can_edit(v, c, true) || id == 0 || !v->model.index_of(v->model.user, id, &row)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_err_t err = end_edit(tree, view.index, v, true, false);
    if (!gates_is_ok(err)) return err;
    err = gates_view_set_selected(tree, view, id);
    return gates_is_ok(err) ? begin_edit(tree, view.index, v, id, c) : err;
}

gates_err_t gates_view_end_edit(gates_tree_t *tree, gates_node_t view, bool commit) {
    struct gates_i_view *v = view_of(tree, view);
    return v != nullptr ? end_edit(tree, view.index, v, commit, false) : PROVEN_ERR_INVALID_ARG;
}

bool gates_view_editing(const gates_tree_t *tree, gates_node_t view, gates_item_id_t *id,
                        gates_column_id_t *column) {
    const struct gates_i_view *v = view_of(tree, view);
    bool open = v != nullptr && v->editing;
    if (id != nullptr) *id = open ? v->edit_id : 0;
    if (column != nullptr) *column = open ? v->edit_col : 0;
    return open;
}

gates_node_t gates_view_editor(const gates_tree_t *tree, gates_node_t view) {
    const struct gates_i_view *v = view_of(tree, view);
    return v != nullptr && v->editor != GATES_NONE ? gates_i_handle(tree, v->editor) : GATES_NODE_NULL;
}

/* Left/Right in a tree: close or go to the parent, open or go to the first child. */
static void tree_key(gates_tree_t *tree, gates_u32 idx, struct gates_i_view *v, const view_geom_t *g,
                     bool right) {
    gates_u64 row = 0;
    if (v->sel == 0 || !v->has_model || !v->model.index_of(v->model.user, v->sel, &row)) return;
    gates_row_info_t info = {0};
    if (!gates_is_ok(v->model.row_info(v->model.user, v->sel, &info))) return;
    if (right) {
        if (info.expandable && !info.expanded) {
            request_expand(tree, idx, v->sel, true);
        } else if (info.expanded && row + 1 < g->count) {
            pick(tree, idx, v, g, row + 1); /* the first child follows its parent */
        }
        return;
    }
    if (info.expanded) {
        request_expand(tree, idx, v->sel, false);
    } else if (info.parent != 0) {
        gates_u64 prow = 0;
        if (v->model.index_of(v->model.user, info.parent, &prow)) pick(tree, idx, v, g, prow);
    }
}

/* The column F2 edits or Space toggles: the current one when it can, else the first (0.8.0). */
static gates_i32 key_col(const struct gates_i_view *v, bool text) {
    gates_i32 c = v->cur_col != 0 ? col_pos(v, v->cur_col) : -1;
    return c >= 0 && can_edit(v, c, text) ? c : first_editable(v, text);
}

/* Ctrl+Left/Right: the current column moves over the shown columns. */
static void move_cur_col(gates_tree_t *tree, gates_u32 idx, struct gates_i_view *v, const view_geom_t *g, bool right) {
    gates_u32 n = nvisible(v);
    if (v->ncol == 0 || n == 0) return;
    gates_i32 at = -1; /* the current column's place among the shown ones */
    for (gates_u32 k = 0; k < n; k++) {
        if (v->cols[visible_at(v, k)].id == v->cur_col) at = (gates_i32)k;
    }
    gates_i32 to = at < 0 ? (right ? 0 : (gates_i32)n - 1) : right ? (at + 1 < (gates_i32)n ? at + 1 : at) : (at > 0 ? at - 1 : 0);
    gates_i32 c = visible_at(v, (gates_u32)to);
    v->cur_col = v->cols[c].id;
    column_into_view(v, g, c);
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
}

static void copy_rows(gates_tree_t *tree, gates_u32 idx, struct gates_i_view *v);

/* Ctrl+C: the selected row's shown cells, tab-separated, on the clipboard (0.8.0). */
static void copy_row(gates_tree_t *tree, gates_u32 idx, struct gates_i_view *v) {
    if (v->multi) {
        copy_rows(tree, idx, v);
        return;
    }
    if (!tree->has_clipboard || v->sel == 0 || !v->has_model) return;
    gates_allocator_t a = tree->alloc;
    gates_u8 *buf = nullptr;
    gates_usize_t len = 0, cap = 0;
    gates_err_t err = GATES_OK;
    gates_u32 n = v->ncol > 0 ? nvisible(v) : 1;
    for (gates_u32 k = 0; k < n && gates_is_ok(err); k++) {
        gates_column_id_t cid = v->ncol > 0 ? v->cols[visible_at(v, k)].id : 0;
        gates_cell_t cell = {0};
        err = v->model.cell(v->model.user, v->sel, cid, &cell);
        if (!gates_is_ok(err)) break;
        gates_usize_t need = len + (k > 0 ? 1 : 0) + cell.text.size;
        if (need > cap) {
            gates_usize_t nc = cap < 64 ? 64 : cap;
            while (nc < need) nc *= 2;
            proven_result_mem_mut_t m = buf == nullptr ? a.alloc_fn(a.ctx, nc, 1) : a.realloc_fn(a.ctx, buf, cap, nc, 1);
            if (!proven_is_ok(m.err)) {
                err = m.err;
                break;
            }
            buf = m.value.ptr;
            cap = nc;
        }
        if (k > 0) buf[len++] = '\t';
        if (cell.text.size > 0) memcpy(buf + len, cell.text.ptr, cell.text.size);
        len += cell.text.size;
    }
    if (gates_is_ok(err)) err = tree->clipboard.set_text(tree->clipboard.ctx, (gates_str_t){ .ptr = buf, .size = len });
    if (buf != nullptr) a.free_fn(a.ctx, buf);
    if (!gates_is_ok(err)) tree->input_error = err;
}

/* Appends a row's shown cells (tab-separated) to buf, growing it. */
static gates_err_t append_row(gates_allocator_t a, struct gates_i_view *v, gates_item_id_t id, gates_u8 **buf,
                              gates_usize_t *len, gates_usize_t *cap, bool newline) {
    gates_u32 n = v->ncol > 0 ? nvisible(v) : 1;
    for (gates_u32 k = 0; k < n; k++) {
        gates_column_id_t cid = v->ncol > 0 ? v->cols[visible_at(v, k)].id : 0;
        gates_cell_t cell = {0};
        gates_err_t err = v->model.cell(v->model.user, id, cid, &cell);
        if (!gates_is_ok(err)) return err;
        gates_usize_t need = *len + 1 + cell.text.size;
        if (need > *cap) {
            gates_usize_t nc = *cap < 256 ? 256 : *cap;
            while (nc < need) nc *= 2;
            proven_result_mem_mut_t m = *buf == nullptr ? a.alloc_fn(a.ctx, nc, 1) : a.realloc_fn(a.ctx, *buf, *cap, nc, 1);
            if (!proven_is_ok(m.err)) return m.err;
            *buf = m.value.ptr;
            *cap = nc;
        }
        if (k > 0) (*buf)[(*len)++] = '\t';
        else if (newline) (*buf)[(*len)++] = '\n';
        if (cell.text.size > 0) memcpy(*buf + *len, cell.text.ptr, cell.text.size);
        *len += cell.text.size;
    }
    return GATES_OK;
}

/* Ctrl+C in a multi-select view: the selected rows in model order, one per line,
 * walked with next_selected; past GATES_VIEW_COPY_MAX rows the program is asked. */
static void copy_rows(gates_tree_t *tree, gates_u32 idx, struct gates_i_view *v) {
    if (!v->has_model) return;
    gates_u64 count = model_count(v), n = 0;
    for (gates_u64 r = 0; r < count && n <= GATES_VIEW_COPY_MAX;) {
        gates_u64 s = v->model.next_selected(v->model.user, r);
        if (s == GATES_ROW_NONE || s < r || s >= count) break;
        n++;
        r = s + 1;
    }
    if (n > GATES_VIEW_COPY_MAX) {
        if (!gates_i_wants_events(tree, idx)) return;
        gates_err_t err = gates_i_event_reserve(tree, 1, 0);
        if (!gates_is_ok(err)) {
            tree->input_error = err;
            return;
        }
        gates_i_event_push_req(tree, idx, GATES_EVENT_COPY_REQUESTED, 0, v->sel, 0);
        return;
    }
    if (n == 0 || !tree->has_clipboard) return;
    gates_allocator_t a = tree->alloc;
    gates_u8 *buf = nullptr;
    gates_usize_t len = 0, cap = 0;
    gates_err_t err = GATES_OK;
    bool first = true;
    for (gates_u64 r = 0; r < count && gates_is_ok(err);) {
        gates_u64 s = v->model.next_selected(v->model.user, r);
        if (s == GATES_ROW_NONE || s < r || s >= count) break;
        gates_item_id_t id = v->model.id_at(v->model.user, s);
        if (id != 0) {
            err = append_row(a, v, id, &buf, &len, &cap, !first);
            first = false;
        }
        r = s + 1;
    }
    if (gates_is_ok(err)) err = tree->clipboard.set_text(tree->clipboard.ctx, (gates_str_t){ .ptr = buf, .size = len });
    if (buf != nullptr) a.free_fn(a.ctx, buf);
    if (!gates_is_ok(err)) tree->input_error = err;
}

static bool nav_key(gates_key_t k) {
    return k == GATES_KEY_UP || k == GATES_KEY_DOWN || k == GATES_KEY_PAGE_UP || k == GATES_KEY_PAGE_DOWN ||
           k == GATES_KEY_HOME || k == GATES_KEY_END;
}

bool gates_i_view_key(gates_tree_t *tree, gates_u32 idx, const gates_key_event_t *ev) {
    struct gates_i_view *v = view_at(tree, idx);
    if (v == nullptr) return false;
    view_geom_t g;
    geom_now(tree, idx, v, &g);
    if (ev->ctrl && !ev->alt && !ev->shift) {
        if (ev->key == GATES_KEY_C) {
            copy_row(tree, idx, v);
            return v->sel != 0 || v->multi;
        }
        if (v->multi && ev->key == GATES_KEY_A) {
            select_request(tree, idx, v, GATES_SELECT_ALL, v->sel != 0 ? v->sel : (g.count > 0 ? v->model.id_at(v->model.user, 0) : 0));
            return true;
        }
        if (v->multi && ev->key == GATES_KEY_SPACE && v->sel != 0) {
            select_request(tree, idx, v, GATES_SELECT_TOGGLE, v->sel);
            return true;
        }
        if ((ev->key == GATES_KEY_LEFT || ev->key == GATES_KEY_RIGHT) && v->ncol > 0) {
            move_cur_col(tree, idx, v, &g, ev->key == GATES_KEY_RIGHT);
            return true;
        }
    }
    if (ev->alt || (ev->ctrl && !(v->multi && nav_key(ev->key)))) {
        return false;
    }
    gates_i32 step = VIEW_STEP_CELLS * (tree->advance > 0 ? tree->advance : 8);
    switch (ev->key) {
    case GATES_KEY_ENTER:
        activate(tree, idx, v);
        return true; /* never falls through to a default command */
    case GATES_KEY_F2: {
        gates_i32 c = key_col(v, true);
        if (ev->shift || c < 0 || v->sel == 0) return false;
        gates_err_t err = begin_edit(tree, idx, v, v->sel, c);
        if (err == PROVEN_ERR_NOMEM) tree->input_error = err;
        return true;
    }
    case GATES_KEY_F10:
        if (!ev->shift || !v->column_menu) return false;
        {
            gates_err_t err = gates_view_open_column_menu(tree, gates_i_handle(tree, idx),
                                                          (gates_point_t){ g.header.x, g.header.y + g.header.h }, nullptr);
            if (!gates_is_ok(err)) tree->input_error = err;
        }
        return true;
    case GATES_KEY_SPACE: {
        gates_i32 c = key_col(v, false);
        if (v->multi && c < 0 && !ev->shift && v->sel != 0) { /* no check column: select the focus row */
            select_request(tree, idx, v, GATES_SELECT_ONE, v->sel);
            return true;
        }
        if (ev->shift || c < 0 || v->sel == 0) return false;
        toggle_check(tree, idx, v, v->sel, c);
        return true;
    }
    case GATES_KEY_LEFT:
    case GATES_KEY_RIGHT:
        v->find_len = 0; /* moving ends a type-ahead search */
        if (v->tree) {
            gates_item_id_t was = v->sel;
            tree_key(tree, idx, v, &g, ev->key == GATES_KEY_RIGHT);
            if (v->sel != was) moved(tree, idx, v, was, false, false);
        } else {
            scroll_x_by(tree, idx, v, &g, ev->key == GATES_KEY_RIGHT ? step : -step);
        }
        return true;
    case GATES_KEY_UP:
    case GATES_KEY_DOWN:
    case GATES_KEY_PAGE_UP:
    case GATES_KEY_PAGE_DOWN:
    case GATES_KEY_HOME:
    case GATES_KEY_END:
        break;
    default:
        return false;
    }
    v->find_len = 0;
    if (g.count == 0) {
        return true;
    }
    gates_u64 row = 0;
    bool have = v->sel != 0 && v->model.index_of(v->model.user, v->sel, &row);
    gates_u64 last = g.count - 1;
    gates_u64 target;
    switch (ev->key) {
    case GATES_KEY_DOWN:      target = !have ? 0 : (row < last ? row + 1 : last); break;
    case GATES_KEY_UP:        target = !have || row == 0 ? 0 : row - 1; break;
    case GATES_KEY_PAGE_DOWN: target = !have ? 0 : (last - row > g.visible ? row + g.visible : last); break;
    case GATES_KEY_PAGE_UP:   target = !have || row < g.visible ? 0 : row - g.visible; break;
    case GATES_KEY_HOME:      target = 0; break;
    case GATES_KEY_END:
    default:                  target = last; break;
    }
    gates_item_id_t was = v->sel;
    pick(tree, idx, v, &g, target);
    moved(tree, idx, v, was, ev->shift, ev->ctrl);
    return true;
}

static gates_u8 fold(gates_u8 c) { return c >= 'A' && c <= 'Z' ? (gates_u8)(c + 32) : c; }

/* Whether the row's first shown cell starts with needle (ASCII letters in any case). */
static bool row_starts(struct gates_i_view *v, gates_u64 row, const gates_u8 *needle, gates_u32 n) {
    gates_item_id_t id = v->model.id_at(v->model.user, row);
    gates_cell_t cell = {0};
    gates_column_id_t cid = v->ncol > 0 && nvisible(v) > 0 ? v->cols[visible_at(v, 0)].id : 0;
    if (id == 0 || !gates_is_ok(v->model.cell(v->model.user, id, cid, &cell)) || cell.text.size < n) return false;
    for (gates_u32 k = 0; k < n; k++) {
        if (fold(cell.text.ptr[k]) != fold(needle[k])) return false;
    }
    return true;
}

/* Type-ahead (0.8.0): typed characters, within a pause of each other, select
 * the next row whose first shown cell starts with them; the same letter again
 * steps through the rows that start with it. A character looks at no more
 * than VIEW_FIND_SCAN rows, so a keystroke's cost stays bounded. */
bool gates_i_view_char(gates_tree_t *tree, gates_u32 idx, gates_str_t ch) {
    struct gates_i_view *v = view_at(tree, idx);
    const gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
    if (v == nullptr || !v->has_model || st->disabled || v->editing) return false;
    gates_u64 now = tree->clock != nullptr ? tree->clock(tree->clock_ctx) : 0;
    if (tree->clock != nullptr && v->find_len > 0 && now - v->find_at >= VIEW_FIND_PAUSE_MS) v->find_len = 0;
    if (v->find_len == 0 && ch.size == 1 && ch.ptr[0] == ' ') return false; /* Space is a key, not a search */
    if (v->find_len + ch.size <= sizeof v->find) {
        memcpy(v->find + v->find_len, ch.ptr, ch.size);
        v->find_len += (gates_u32)ch.size;
    }
    v->find_at = now;
    view_geom_t g;
    geom_now(tree, idx, v, &g);
    if (g.count == 0) return true;
    bool same = true; /* "aaa": the rows starting with 'a', one by one */
    for (gates_u32 k = 1; k < v->find_len; k++) same = same && v->find[k] == v->find[0];
    gates_u32 n = same ? 1u : v->find_len;
    gates_u64 row = 0;
    bool have = v->sel != 0 && v->model.index_of(v->model.user, v->sel, &row);
    gates_u64 start = !have ? 0 : (n == 1 ? row + 1 : row) % g.count;
    gates_u64 scan = g.count < VIEW_FIND_SCAN ? g.count : VIEW_FIND_SCAN;
    for (gates_u64 k = 0; k < scan; k++) {
        gates_u64 r = (start + k) % g.count;
        if (row_starts(v, r, v->find, n)) {
            gates_item_id_t was = v->sel;
            pick(tree, idx, v, &g, r);
            moved(tree, idx, v, was, false, false);
            break;
        }
    }
    return true;
}

/* The column position under p in the rows area, or -1 (a list has none). */
static gates_i32 body_col_at(const struct gates_i_view *v, const view_geom_t *g, gates_point_t p) {
    for (gates_u32 c = 0; c < v->ncol; c++) {
        gates_i32 x0 = col_left(v, g, c);
        if (!v->cols[c].hidden && p.x >= x0 && p.x < x0 + v->cols[c].width) return (gates_i32)c;
    }
    return -1;
}

/* Header column position under x (or -1); *edge: within reach of its right edge. */
static gates_i32 header_col_at(const struct gates_i_view *v, const view_geom_t *g, gates_point_t p,
                               bool *edge) {
    *edge = false;
    if (g->header.h == 0 || !gates_rect_contains(g->header, p)) return -1;
    for (gates_u32 c = 0; c < v->ncol; c++) {
        if (v->cols[c].hidden) continue;
        gates_i32 x0 = col_left(v, g, c), x1 = x0 + v->cols[c].width;
        if (p.x >= x1 - VIEW_EDGE && p.x <= x1 + VIEW_EDGE) {
            *edge = true;
            return (gates_i32)c;
        }
        if (p.x >= x0 && p.x < x1) return (gates_i32)c;
    }
    return -1;
}

bool gates_i_view_pointer_down(gates_tree_t *tree, gates_u32 idx, gates_point_t p,
                               gates_u32 clicks, bool shift, bool ctrl) {
    struct gates_i_view *v = view_at(tree, idx);
    gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
    if (v == nullptr || st->disabled) {
        return false;
    }
    gates_tree_set_focus(tree, gates_i_handle(tree, idx)); /* an open edit commits (or drops) as focus leaves it */
    if (!settle(tree, idx, v)) return true;
    view_geom_t g;
    geom_now(tree, idx, v, &g);
    if (gates_rect_contains(g.vthumb, p)) {
        tree->drag_kind = GATES_DRAG_VIEW_VTHUMB;
        tree->drag_node = idx;
        tree->drag_start = p;
        v->drag_first = g.first;
        return true;
    }
    if (gates_rect_contains(g.vtrack, p)) {
        scroll_rows(tree, idx, v, &g, p.y < g.vthumb.y ? -(gates_i64)g.visible : (gates_i64)g.visible);
        return true;
    }
    if (gates_rect_contains(g.hthumb, p)) {
        tree->drag_kind = GATES_DRAG_VIEW_HTHUMB;
        tree->drag_node = idx;
        tree->drag_start = p;
        v->drag_value = g.scroll_x;
        return true;
    }
    if (gates_rect_contains(g.htrack, p)) {
        scroll_x_by(tree, idx, v, &g, p.x < g.hthumb.x ? -g.body.w : g.body.w);
        return true;
    }
    bool edge = false;
    gates_i32 c = header_col_at(v, &g, p, &edge);
    if (c >= 0 && edge) {
        tree->drag_kind = GATES_DRAG_VIEW_COLUMN;
        tree->drag_node = idx;
        tree->drag_start = p;
        v->drag_col = c;
        v->drag_value = v->cols[c].width;
        return true;
    }
    if (c >= 0) {
        v->press_col = c; /* sorts on release over the same column */
        tree->pressed = idx;
        return true;
    }
    if (gates_rect_contains(g.body, p) && v->has_model) {
        gates_u64 k = (gates_u64)((p.y - g.body.y) / g.row_h);
        if (k < g.painted && v->tree) {
            /* A press on a row's open/close mark asks for it and selects nothing. */
            gates_item_id_t id = v->model.id_at(v->model.user, g.first + k);
            gates_row_info_t info = {0};
            if (id != 0 && gates_is_ok(v->model.row_info(v->model.user, id, &info)) &&
                info.expandable) {
                gates_i32 mx = col_left(v, &g, tree_col(v)) + tree_mark_x(&info);
                if (p.x >= mx && p.x < mx + GATES_VIEW_INDENT) {
                    request_expand(tree, idx, id, !info.expanded);
                    return true;
                }
            }
        }
        if (k < g.painted) {
            gates_item_id_t before = v->sel;
            gates_item_id_t id = v->model.id_at(v->model.user, g.first + k);
            gates_i32 c = body_col_at(v, &g, p);
            if (c >= 0 && v->ncol > 0) v->cur_col = v->cols[c].id; /* the cell pressed is current (0.8.0) */
            pick(tree, idx, v, &g, g.first + k);
            if (v->multi && id != 0) { /* RFC-0007: the press asks for a selection change */
                if (shift) {
                    if (v->anchor == 0) v->anchor = before != 0 ? before : id;
                    select_request(tree, idx, v, ctrl ? GATES_SELECT_ADD_RANGE : GATES_SELECT_RANGE, id);
                } else {
                    select_request(tree, idx, v, ctrl ? GATES_SELECT_TOGGLE : GATES_SELECT_ONE, id);
                }
            }
            if (can_edit(v, c, false) && v->sel == id &&
                p.x < col_left(v, &g, (gates_u32)c) + row_indent(v, c, id) + VIEW_CELL_PAD + GATES_CHECK_BOX +
                          VIEW_CELL_PAD) {
                toggle_check(tree, idx, v, id, c); /* a click on the box (the row's height tall) */
            } else if (clicks >= 2 && v->sel != 0 && v->sel == before) {
                if (can_edit(v, c, true)) {
                    gates_err_t err = begin_edit(tree, idx, v, v->sel, c); /* double click on an editable cell */
                    if (err == PROVEN_ERR_NOMEM) tree->input_error = err;
                } else {
                    activate(tree, idx, v); /* double click on the selected row */
                }
            }
        }
    }
    return true;
}

void gates_i_view_pointer_up(gates_tree_t *tree, gates_u32 idx, gates_point_t p) {
    struct gates_i_view *v = view_at(tree, idx);
    if (v == nullptr || v->press_col < 0) {
        return;
    }
    view_geom_t g;
    geom_now(tree, idx, v, &g);
    bool edge = false;
    gates_i32 c = header_col_at(v, &g, p, &edge);
    gates_i32 pressed = v->press_col;
    v->press_col = -1;
    if (c != pressed) {
        return;
    }
    if (!gates_i_wants_events(tree, idx)) return;
    gates_err_t err = gates_i_event_reserve(tree, 1, 0);
    if (!gates_is_ok(err)) {
        tree->input_error = err;
        return;
    }
    gates_i_event_push_ex(tree, idx, GATES_EVENT_SORT_REQUESTED, GATES_ORIGIN_USER,
                          v->cols[c].id, 0);
}

void gates_i_view_drag(gates_tree_t *tree, gates_point_t p) {
    gates_u32 idx = tree->drag_node;
    struct gates_i_view *v = view_at(tree, idx);
    if (v == nullptr) return;
    view_geom_t g;
    geom_now(tree, idx, v, &g);
    if (tree->drag_kind == GATES_DRAG_VIEW_COLUMN) {
        gates_i_column_t *c = &v->cols[v->drag_col];
        gates_i32 w = v->drag_value + (p.x - tree->drag_start.x);
        if (w < c->min_width) w = c->min_width;
        if (w != c->width) {
            c->width = w;
            gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
        }
        return;
    }
    if (tree->drag_kind == GATES_DRAG_VIEW_HTHUMB) {
        gates_i32 travel = g.htrack.w - g.hthumb.w;
        if (travel <= 0) return;
        gates_i32 x0 = g.max_x > 0 ? (gates_i32)(((gates_i64)travel * v->drag_value) / g.max_x) : 0;
        gates_i32 pos = x0 + (p.x - tree->drag_start.x);
        if (pos < 0) pos = 0;
        if (pos > travel) pos = travel;
        gates_i32 x = (gates_i32)(((gates_i64)g.max_x * pos) / travel);
        if (x != v->scroll_x) {
            v->scroll_x = x;
            gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
        }
        return;
    }
    /* Vertical thumb: pixels map to rows through the thumb's travel, in 64 bits. */
    gates_i32 travel = g.vtrack.h - g.vthumb.h;
    if (travel <= 0) return;
    gates_i64 start = (gates_i64)scale_down((gates_u64)travel, v->drag_first, g.max_first);
    gates_i64 pos = start + (p.y - tree->drag_start.y);
    if (pos < 0) pos = 0;
    if (pos > travel) pos = travel;
    gates_u64 f = scale_up(g.max_first, (gates_u64)pos, (gates_u64)travel);
    if (f != v->first) {
        v->first = f;
        gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
    }
    note_scrolled(tree, idx, v, &g);
}

bool gates_i_view_wheel(gates_tree_t *tree, gates_u32 idx, gates_vec2_t wheel) {
    struct gates_i_view *v = view_at(tree, idx);
    if (v == nullptr) return false;
    if (!settle(tree, idx, v)) return true; /* the view does not move under a refused edit */
    view_geom_t g;
    geom_now(tree, idx, v, &g);
    if (wheel.y != 0.0f) {
        scroll_rows(tree, idx, v, &g, (gates_i64)(-wheel.y * (float)VIEW_WHEEL_ROWS));
    }
    if (wheel.x != 0.0f) {
        gates_i32 step = VIEW_STEP_CELLS * (tree->advance > 0 ? tree->advance : 8);
        scroll_x_by(tree, idx, v, &g, (gates_i32)(wheel.x * (float)step));
    }
    return true;
}

/* -- log view ---------------------------------------------------------------- */

static gates_u64 log_count(void *u) { return ((struct gates_i_log *)u)->n; }

static gates_item_id_t log_id_at(void *u, gates_u64 row) {
    struct gates_i_log *lg = u;
    return row < lg->n ? lg->lines[(lg->head + row) % lg->max_lines].id : 0;
}

static bool log_index_of(void *u, gates_item_id_t id, gates_u64 *row) {
    struct gates_i_log *lg = u;
    if (lg->n == 0) return false;
    gates_item_id_t oldest = lg->lines[lg->head].id;
    if (id < oldest || id - oldest >= lg->n) return false;
    *row = id - oldest; /* ids are consecutive */
    return true;
}

static gates_err_t log_cell(void *u, gates_item_id_t id, gates_column_id_t col, gates_cell_t *out) {
    struct gates_i_log *lg = u;
    (void)col;
    gates_u64 row = 0;
    if (!log_index_of(lg, id, &row)) return PROVEN_ERR_INVALID_ARG;
    const gates_i_line_t *l = &lg->lines[(lg->head + row) % lg->max_lines];
    out->text = (gates_str_t){ .ptr = l->text, .size = l->len };
    return GATES_OK;
}

static struct gates_i_view *log_view(const gates_tree_t *tree, gates_node_t log) {
    struct gates_i_view *v = view_of(tree, log);
    return v != nullptr && v->log != nullptr ? v : nullptr;
}

gates_err_t gates_log_create(gates_tree_t *tree, gates_node_t parent, const gates_log_desc_t *desc,
                             gates_node_t *out_log) {
    if (tree == nullptr || desc == nullptr || out_log == nullptr ||
        (!gates_node_eq(parent, GATES_NODE_NULL) && !gates_i_valid(tree, parent))) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *out_log = GATES_NODE_NULL;
    gates_node_t node = GATES_NODE_NULL;
    gates_err_t err = gates_view_create(tree, GATES_NODE_NULL, &(gates_view_desc_t){0}, &node);
    if (!gates_is_ok(err)) {
        return err;
    }
    gates_allocator_t a = tree->alloc;
    struct gates_i_log *lg = nullptr;
    proven_result_mem_mut_t r = a.alloc_fn(a.ctx, sizeof *lg, alignof(struct gates_i_log));
    err = r.err;
    if (gates_is_ok(err)) {
        lg = (struct gates_i_log *)r.value.ptr;
        memset(lg, 0, sizeof *lg);
        lg->max_lines = desc->max_lines != 0 ? desc->max_lines : 1000u;
        lg->max_bytes = desc->max_bytes != 0 ? desc->max_bytes : (gates_usize_t)1 << 20;
        lg->next_id = 1;
        proven_result_mem_mut_t rl = a.alloc_fn(a.ctx, (gates_usize_t)lg->max_lines * sizeof(gates_i_line_t),
                                                alignof(gates_i_line_t));
        err = rl.err;
        if (gates_is_ok(err)) {
            lg->lines = (gates_i_line_t *)rl.value.ptr;
        } else {
            a.free_fn(a.ctx, lg);
            lg = nullptr;
        }
    }
    if (gates_is_ok(err) && !gates_node_eq(parent, GATES_NODE_NULL)) {
        err = gates_node_append(tree, parent, node);
        if (!gates_is_ok(err)) {
            a.free_fn(a.ctx, lg->lines);
            a.free_fn(a.ctx, lg);
        }
    }
    if (!gates_is_ok(err)) {
        gates_i_discard_detached(tree, node); /* frees the view state too */
        return err;
    }
    struct gates_i_view *v = view_of(tree, node);
    v->log = lg;
    v->follow = true;
    v->has_model = true;
    v->model = (gates_rows_model_t){ .user = lg, .count = log_count, .id_at = log_id_at,
                                     .index_of = log_index_of, .cell = log_cell };
    *out_log = node;
    return GATES_OK;
}

static void log_drop_oldest(gates_tree_t *tree, struct gates_i_log *lg) {
    gates_i_line_t *l = &lg->lines[lg->head];
    lg->bytes -= l->len;
    tree->alloc.free_fn(tree->alloc.ctx, l->text);
    *l = (gates_i_line_t){0};
    lg->head = (lg->head + 1) % lg->max_lines;
    lg->n--;
    lg->dropped++;
}

gates_err_t gates_log_append(gates_tree_t *tree, gates_node_t log, gates_str_t line) {
    struct gates_i_view *v = log_view(tree, log);
    if (v == nullptr || (line.size > 0 && line.ptr == nullptr)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    struct gates_i_log *lg = v->log;
    if (line.size > lg->max_bytes || line.size > 0xFFFFFFFFu) {
        return PROVEN_ERR_OUT_OF_BOUNDS;
    }
    gates_u8 *copy = nullptr;
    if (line.size > 0) {
        proven_result_mem_mut_t r = tree->alloc.alloc_fn(tree->alloc.ctx, line.size, 1);
        if (!proven_is_ok(r.err)) {
            return r.err; /* nothing dropped, nothing added */
        }
        copy = (gates_u8 *)r.value.ptr;
        for (gates_usize_t i = 0; i < line.size; i++) {
            gates_u8 c = line.ptr[i];
            copy[i] = (c == '\r' || c == '\n' || c == '\t') ? (gates_u8)' ' : c;
        }
    }
    gates_u64 dropped_now = 0;
    while (lg->n > 0 && (lg->n == lg->max_lines || lg->bytes + line.size > lg->max_bytes)) {
        log_drop_oldest(tree, lg);
        dropped_now++;
    }
    lg->lines[(lg->head + lg->n) % lg->max_lines] =
        (gates_i_line_t){ .id = lg->next_id++, .text = copy, .len = (gates_u32)line.size };
    lg->n++;
    lg->bytes += line.size;

    /* Keep the same lines on screen, or follow the end. */
    v->first = v->first > dropped_now ? v->first - dropped_now : 0;
    v->sel_row = v->sel_row > dropped_now ? v->sel_row - dropped_now : 0;
    view_geom_t g;
    geom_now(tree, log.index, v, &g);
    if (v->follow) {
        v->first = g.max_first;
    }
    gates_err_t err = GATES_OK;
    if (v->sel != 0 && dropped_now > 0) {
        err = reconcile(tree, log, v); /* a dropped selection moves to the oldest line */
        if (!gates_is_ok(err)) tree->input_error = err;
    }
    gates_i_mark_dirty(tree, log.index, GATES_DIRTY_PAINT);
    return GATES_OK;
}

void gates_log_clear(gates_tree_t *tree, gates_node_t log) {
    struct gates_i_view *v = log_view(tree, log);
    if (v == nullptr) return;
    struct gates_i_log *lg = v->log;
    while (lg->n > 0) log_drop_oldest(tree, lg);
    lg->head = 0;
    lg->bytes = 0;
    lg->dropped = 0;
    v->first = 0;
    v->sel = 0;
    v->sel_row = 0;
    v->follow = true;
    gates_i_mark_dirty(tree, log.index, GATES_DIRTY_PAINT);
}

gates_u64 gates_log_count(const gates_tree_t *tree, gates_node_t log) {
    const struct gates_i_view *v = log_view(tree, log);
    return v != nullptr ? v->log->n : 0;
}

gates_u64 gates_log_dropped(const gates_tree_t *tree, gates_node_t log) {
    const struct gates_i_view *v = log_view(tree, log);
    return v != nullptr ? v->log->dropped : 0;
}

bool gates_log_following(const gates_tree_t *tree, gates_node_t log) {
    const struct gates_i_view *v = log_view(tree, log);
    return v != nullptr && v->follow;
}

gates_str_t gates_log_line(const gates_tree_t *tree, gates_node_t log, gates_item_id_t id) {
    const struct gates_i_view *v = log_view(tree, log);
    gates_cell_t cell = {0};
    if (v == nullptr || !gates_is_ok(log_cell(v->log, id, 0, &cell))) {
        return (gates_str_t){0};
    }
    return cell.text;
}

gates_err_t gates_log_set_following(gates_tree_t *tree, gates_node_t log, bool follow) {
    struct gates_i_view *v = log_view(tree, log);
    if (v == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    v->follow = follow;
    if (follow) {
        view_geom_t g;
        geom_now(tree, log.index, v, &g);
        v->first = g.max_first;
        gates_i_mark_dirty(tree, log.index, GATES_DIRTY_PAINT);
    }
    return GATES_OK;
}

/* -- accessibility ------------------------------------------------------
 * Rows are items: the rows shown now, plus the selection wherever it is. Cells
 * are read for shown rows only - gates never walks rows it does not show. */

gates_u32 gates_i_view_kind(const gates_tree_t *tree, gates_u32 idx) {
    const struct gates_i_view *v = view_at(tree, idx);
    if (v == nullptr) return GATES_I_VIEW_LIST;
    return v->tree ? GATES_I_VIEW_TREE : v->ncol > 0 && v->log == nullptr ? GATES_I_VIEW_TABLE : GATES_I_VIEW_LIST;
}

gates_u64 gates_i_view_item_count(gates_tree_t *tree, gates_u32 idx) {
    struct gates_i_view *v = view_at(tree, idx);
    if (v == nullptr || !v->has_model) return 0;
    view_geom_t g;
    geom_now(tree, idx, v, &g);
    return g.painted;
}

gates_item_id_t gates_i_view_item_at(gates_tree_t *tree, gates_u32 idx, gates_u64 k) {
    struct gates_i_view *v = view_at(tree, idx);
    if (v == nullptr || !v->has_model) return 0;
    view_geom_t g;
    geom_now(tree, idx, v, &g);
    return k < g.painted ? v->model.id_at(v->model.user, g.first + k) : 0;
}

bool gates_i_view_item(gates_tree_t *tree, gates_u32 idx, gates_item_id_t id, gates_i_view_item_t *out) {
    struct gates_i_view *v = view_at(tree, idx);
    if (v == nullptr || !v->has_model || id == 0) return false;
    gates_u64 row = 0;
    if (!v->model.index_of(v->model.user, id, &row)) return false;
    view_geom_t g;
    geom_now(tree, idx, v, &g);
    bool shown = row >= g.first && row - g.first < g.painted;
    if (!shown && id != v->sel) return false;
    memset(out, 0, sizeof *out);
    out->row = row;
    out->count = g.count;
    out->shown = shown;
    out->selected = row_is_selected(v, row, id);
    out->columns = v->ncol > 0 ? nvisible(v) : 1;
    if (shown) {
        out->rect = (gates_rect_t){ g.body.x, g.body.y + (gates_i32)(row - g.first) * g.row_h, g.body.w, g.row_h };
    }
    if (v->tree && v->model.row_info != nullptr && gates_is_ok(v->model.row_info(v->model.user, id, &out->info))) {
        out->has_info = true;
    }
    return true;
}

gates_str_t gates_i_view_cell(gates_tree_t *tree, gates_u32 idx, gates_item_id_t id, gates_u32 col) {
    struct gates_i_view *v = view_at(tree, idx);
    gates_i_view_item_t it;
    if (v == nullptr || !gates_i_view_item(tree, idx, id, &it) || !it.shown) return (gates_str_t){0};
    gates_cell_t cell = {0};
    gates_i32 pos = v->ncol > 0 ? visible_at(v, col) : 0; /* the col-th visible column */
    if (pos < 0) return (gates_str_t){0};
    gates_column_id_t cid = v->ncol > 0 ? v->cols[pos].id : 0;
    return gates_is_ok(v->model.cell(v->model.user, id, cid, &cell)) ? cell.text : (gates_str_t){0};
}

gates_item_id_t gates_i_view_row_at(gates_tree_t *tree, gates_u32 idx, gates_point_t p) {
    struct gates_i_view *v = view_at(tree, idx);
    if (v == nullptr || !v->has_model) return 0;
    view_geom_t g;
    geom_now(tree, idx, v, &g);
    if (!gates_rect_contains(g.body, p)) return 0;
    gates_u64 k = (gates_u64)((p.y - g.body.y) / g.row_h);
    return k < g.painted ? v->model.id_at(v->model.user, g.first + k) : 0;
}

gates_err_t gates_i_view_pick_id(gates_tree_t *tree, gates_u32 idx, gates_item_id_t id) {
    struct gates_i_view *v = view_at(tree, idx);
    gates_u64 row = 0;
    if (v == nullptr || !v->has_model || id == 0 || !v->model.index_of(v->model.user, id, &row)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    view_geom_t g;
    geom_now(tree, idx, v, &g);
    gates_err_t before = tree->input_error; /* the input paths report here; keep the caller's */
    tree->input_error = GATES_OK;
    pick(tree, idx, v, &g, row);
    select_request(tree, idx, v, GATES_SELECT_ONE, id); /* multi-select: the program applies it */
    gates_err_t err = tree->input_error;
    tree->input_error = before;
    return err;
}

gates_err_t gates_i_view_set_item_selected(gates_tree_t *tree, gates_u32 idx, gates_item_id_t id, bool selected) {
    struct gates_i_view *v = view_at(tree, idx);
    gates_u64 row = 0;
    if (v == nullptr || !v->multi || !v->has_model || id == 0 || !v->model.index_of(v->model.user, id, &row)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (row_is_selected(v, row, id) == selected) return GATES_OK;
    gates_err_t before = tree->input_error;
    tree->input_error = GATES_OK;
    select_request(tree, idx, v, GATES_SELECT_TOGGLE, id);
    gates_err_t err = tree->input_error;
    tree->input_error = before;
    return err;
}

gates_err_t gates_i_view_activate_id(gates_tree_t *tree, gates_u32 idx, gates_item_id_t id) {
    gates_err_t err = gates_i_view_pick_id(tree, idx, id);
    if (!gates_is_ok(err)) return err;
    gates_err_t before = tree->input_error;
    tree->input_error = GATES_OK;
    activate(tree, idx, view_at(tree, idx));
    err = tree->input_error;
    tree->input_error = before;
    return err;
}

gates_err_t gates_i_view_expand_id(gates_tree_t *tree, gates_u32 idx, gates_item_id_t id, bool open) {
    gates_i_view_item_t it;
    if (!gates_i_view_item(tree, idx, id, &it) || !it.has_info || !it.info.expandable) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (it.info.expanded == open) return GATES_OK;
    gates_err_t before = tree->input_error;
    tree->input_error = GATES_OK;
    request_expand(tree, idx, id, open);
    gates_err_t err = tree->input_error;
    tree->input_error = before;
    return err;
}

bool gates_i_view_scroll_info(gates_tree_t *tree, gates_u32 idx, gates_u32 *pos, gates_u32 *page) {
    struct gates_i_view *v = view_at(tree, idx);
    if (v == nullptr) return false;
    view_geom_t g;
    geom_now(tree, idx, v, &g);
    if (g.max_first == 0) return false;
    *pos = (gates_u32)scale_down(GATES_ACCESS_SCROLL_MAX, g.first, g.max_first);
    *page = (gates_u32)scale_down(GATES_ACCESS_SCROLL_MAX, g.visible, g.count);
    return true;
}

gates_err_t gates_i_view_scroll_set(gates_tree_t *tree, gates_u32 idx, gates_u32 pos) {
    struct gates_i_view *v = view_at(tree, idx);
    if (v == nullptr) return PROVEN_ERR_INVALID_ARG;
    view_geom_t g;
    geom_now(tree, idx, v, &g);
    gates_u64 f = scale_up(g.max_first, pos > GATES_ACCESS_SCROLL_MAX ? GATES_ACCESS_SCROLL_MAX : pos,
                           GATES_ACCESS_SCROLL_MAX);
    if (f != v->first) {
        v->first = f;
        gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
    }
    note_scrolled(tree, idx, v, &g);
    return GATES_OK;
}

gates_err_t gates_i_view_scroll_step(gates_tree_t *tree, gates_u32 idx, gates_i32 amount, bool page) {
    struct gates_i_view *v = view_at(tree, idx);
    if (v == nullptr) return PROVEN_ERR_INVALID_ARG;
    view_geom_t g;
    geom_now(tree, idx, v, &g);
    scroll_rows(tree, idx, v, &g, (gates_i64)amount * (page ? (gates_i64)g.visible : 1));
    return GATES_OK;
}

/* -- columns by position (persisted state, 0.3.0) ------------------------------- */

gates_u32 gates_i_view_ncol(const gates_tree_t *tree, gates_u32 idx) {
    const gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
    return st != nullptr && st->view != nullptr ? st->view->ncol : 0;
}

gates_i32 gates_i_view_col_width(const gates_tree_t *tree, gates_u32 idx, gates_u32 k) {
    const gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
    return st != nullptr && st->view != nullptr && k < st->view->ncol ? st->view->cols[k].width : 0;
}

void gates_i_view_set_col_width(gates_tree_t *tree, gates_u32 idx, gates_u32 k, gates_i32 width) {
    gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
    if (st == nullptr || st->view == nullptr || k >= st->view->ncol) return;
    gates_i_column_t *c = &st->view->cols[k];
    c->width = width < c->min_width ? c->min_width : width;
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
}

/* The saved state (0.6.0): each column's id, width and hidden mark, in order. */
bool gates_i_view_col_info(const gates_tree_t *tree, gates_u32 idx, gates_u32 k, gates_column_id_t *id,
                           gates_i32 *width, bool *hidden) {
    const gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
    if (st == nullptr || st->view == nullptr || k >= st->view->ncol) return false;
    const gates_i_column_t *c = &st->view->cols[k];
    *id = c->id;
    *width = c->width;
    *hidden = c->hidden;
    return true;
}

static void sync_menu(gates_tree_t *tree, struct gates_i_view *v) {
    if (!v->column_menu) return;
    bool last = nvisible(v) == 1;
    for (gates_u32 i = 0; i < v->ncol; i++) {
        (void)gates_command_set_checked(tree, v->self, v->cols[i].id, !v->cols[i].hidden);
        (void)gates_command_set_enabled(tree, v->self, v->cols[i].id, !(last && !v->cols[i].hidden));
    }
}

/* After columns moved or were hidden: the edit follows its column, or ends with it. */
static void columns_changed(gates_tree_t *tree, gates_u32 idx, struct gates_i_view *v) {
    v->press_col = -1;
    if (tree->drag_node == idx && tree->drag_kind == GATES_DRAG_VIEW_COLUMN) {
        tree->drag_kind = GATES_DRAG_NONE;
        tree->drag_node = GATES_NONE;
    }
    if (v->editing && v->cols[col_pos(v, v->edit_col)].hidden) {
        (void)end_edit(tree, idx, v, false, false); /* never a model call from here */
    }
    sync_menu(tree, v);
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
}

/* Applies saved columns: those named take the first places in the given
 * order with their widths and marks; the rest follow as they were. false when
 * nothing matched or every column would be hidden. */
bool gates_i_view_apply_cols(gates_tree_t *tree, gates_u32 idx, const gates_column_id_t *ids, const gates_i32 *widths,
                             const bool *hidden, gates_u32 n) {
    struct gates_i_view *v = view_at(tree, idx);
    if (v == nullptr || v->ncol == 0) return false;
    gates_u32 matched = 0, shown = 0;
    for (gates_u32 i = 0; i < v->ncol; i++) {
        bool named = false, hide = v->cols[i].hidden;
        for (gates_u32 k = 0; k < n; k++) {
            if (ids[k] == v->cols[i].id) {
                named = true;
                hide = hidden[k];
            }
        }
        matched += named ? 1u : 0u;
        shown += hide ? 0u : 1u;
    }
    if (matched == 0 || shown == 0) return false;
    gates_u32 place = 0;
    for (gates_u32 k = 0; k < n; k++) {
        gates_i32 c = col_pos(v, ids[k]);
        if (c < 0 || (gates_u32)c < place) continue; /* unknown, or named twice */
        gates_i_column_t moved = v->cols[c];
        memmove(&v->cols[place + 1], &v->cols[place], (gates_usize_t)((gates_u32)c - place) * sizeof moved);
        moved.width = widths[k] < moved.min_width ? moved.min_width : widths[k];
        moved.hidden = hidden[k];
        v->cols[place++] = moved;
    }
    columns_changed(tree, idx, v);
    return true;
}

static struct gates_i_view *table_of(const gates_tree_t *tree, gates_node_t view) {
    struct gates_i_view *v = view_of(tree, view);
    return v != nullptr && v->ncol > 0 ? v : nullptr;
}

gates_err_t gates_view_set_column_hidden(gates_tree_t *tree, gates_node_t view, gates_column_id_t column,
                                         bool hidden) {
    struct gates_i_view *v = table_of(tree, view);
    gates_i32 c = v != nullptr ? col_pos(v, column) : -1;
    if (c < 0) return PROVEN_ERR_INVALID_ARG;
    if (v->cols[c].hidden == hidden) return GATES_OK;
    if (hidden && nvisible(v) == 1) return PROVEN_ERR_INVALID_STATE; /* one column stays */
    v->cols[c].hidden = hidden;
    columns_changed(tree, view.index, v);
    return GATES_OK;
}

bool gates_view_column_hidden(const gates_tree_t *tree, gates_node_t view, gates_column_id_t column) {
    struct gates_i_view *v = table_of(tree, view);
    gates_i32 c = v != nullptr ? col_pos(v, column) : -1;
    return c >= 0 && v->cols[c].hidden;
}

gates_err_t gates_view_move_column(gates_tree_t *tree, gates_node_t view, gates_column_id_t column,
                                   gates_u32 position) {
    struct gates_i_view *v = table_of(tree, view);
    gates_i32 c = v != nullptr ? col_pos(v, column) : -1;
    if (c < 0 || position >= v->ncol) return PROVEN_ERR_INVALID_ARG;
    if ((gates_u32)c == position) return GATES_OK;
    gates_i_column_t moved = v->cols[c];
    if ((gates_u32)c < position) {
        memmove(&v->cols[c], &v->cols[c + 1], (gates_usize_t)(position - (gates_u32)c) * sizeof moved);
    } else {
        memmove(&v->cols[position + 1], &v->cols[position], (gates_usize_t)((gates_u32)c - position) * sizeof moved);
    }
    v->cols[position] = moved;
    columns_changed(tree, view.index, v);
    return GATES_OK;
}

gates_column_id_t gates_view_column_at(const gates_tree_t *tree, gates_node_t view, gates_u32 position) {
    struct gates_i_view *v = table_of(tree, view);
    return v != nullptr && position < v->ncol ? v->cols[position].id : 0;
}

gates_err_t gates_view_open_column_menu(gates_tree_t *tree, gates_node_t view, gates_point_t at,
                                        gates_node_t *out_menu) {
    struct gates_i_view *v = table_of(tree, view);
    if (out_menu != nullptr) *out_menu = GATES_NODE_NULL;
    if (v == nullptr || !v->column_menu) return PROVEN_ERR_INVALID_ARG;
    gates_command_id_t ids[64];
    gates_u32 n = 0;
    for (gates_u32 i = 0; i < v->ncol && n < 64; i++) ids[n++] = v->cols[i].id; /* in their order */
    sync_menu(tree, v);
    gates_node_t menu = GATES_NODE_NULL;
    gates_err_t err = gates_menu_open(tree, at, view, ids, n, &menu);
    if (gates_is_ok(err) && out_menu != nullptr) *out_menu = menu;
    return err;
}

/* A header menu entry: show or hide its column. */
static void on_column_command(gates_tree_t *tree, gates_command_id_t id, void *user) {
    struct gates_i_view *v = user;
    (void)gates_view_set_column_hidden(tree, v->self, id, !gates_view_column_hidden(tree, v->self, id));
}

/* A right press on the header opens the column menu there. */
bool gates_i_view_context(gates_tree_t *tree, gates_u32 idx, gates_point_t p) {
    struct gates_i_view *v = view_at(tree, idx);
    if (v == nullptr || !v->column_menu || gates_i_state(tree, gates_i_slot(tree, idx)->state_index)->disabled) {
        return false;
    }
    view_geom_t g;
    geom_now(tree, idx, v, &g);
    if (!gates_rect_contains(g.header, p)) return false;
    gates_err_t err = gates_view_open_column_menu(tree, v->self, p, nullptr);
    if (!gates_is_ok(err)) tree->input_error = err;
    return true;
}
