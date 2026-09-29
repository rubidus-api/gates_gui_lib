/* gates_gui_lib — measure/arrange layout (RFC-0001 §11, Phase 2 set).
 * Two passes: bottom-up measure (intrinsic sizes from the text metrics
 * contract, RFC-0002 §3) then top-down arrange. Platform-free. */
#include <gates/layout.h>
#include <gates/widget.h>
#include "gates_tree_internal.h"

/* -- property setters -------------------------------------------------------- */

static gates_node_slot_t *slot_checked(gates_tree_t *tree, gates_node_t node) {
    return gates_i_valid(tree, node) ? gates_i_slot(tree, node.index) : nullptr;
}

gates_err_t gates_layout_set(gates_tree_t *tree, gates_node_t node, gates_layout_t kind) {
    gates_node_slot_t *s = slot_checked(tree, node);
    if (s == nullptr || kind > GATES_LAYOUT_KIND_WRAP) {
        return PROVEN_ERR_INVALID_ARG;
    }
    s->layout_kind = (gates_u8)kind;
    gates_i_mark_dirty(tree, node.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_err_t gates_layout_set_padding(gates_tree_t *tree, gates_node_t node, gates_i32 padding) {
    gates_node_slot_t *s = slot_checked(tree, node);
    if (s == nullptr || padding < 0) {
        return PROVEN_ERR_INVALID_ARG;
    }
    s->padding = padding;
    gates_i_mark_dirty(tree, node.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_err_t gates_layout_set_gap(gates_tree_t *tree, gates_node_t node, gates_i32 gap) {
    gates_node_slot_t *s = slot_checked(tree, node);
    if (s == nullptr || gap < 0) {
        return PROVEN_ERR_INVALID_ARG;
    }
    s->gap = gap;
    gates_i_mark_dirty(tree, node.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_err_t gates_layout_set_stack_active(gates_tree_t *tree, gates_node_t node,
                                          gates_u32 child_index) {
    gates_node_slot_t *s = slot_checked(tree, node);
    if (s == nullptr || child_index >= s->child_count) {
        return PROVEN_ERR_INVALID_ARG;
    }
    s->active_child = child_index;
    gates_i_mark_dirty(tree, node.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    gates_i_focus_check(tree); /* focus on a page that is now hidden moves on */
    return GATES_OK;
}

gates_u32 gates_layout_stack_active(const gates_tree_t *tree, gates_node_t node) {
    return gates_i_valid(tree, node) ? gates_i_slot(tree, node.index)->active_child : 0;
}

gates_err_t gates_layout_set_split(gates_tree_t *tree, gates_node_t node,
                                   gates_split_dir_t dir, gates_i32 ratio_permille) {
    gates_node_slot_t *s = slot_checked(tree, node);
    if (s == nullptr || dir > GATES_SPLIT_VERTICAL ||
        ratio_permille < 1 || ratio_permille > 999) {
        return PROVEN_ERR_INVALID_ARG;
    }
    s->layout_kind = GATES_LAYOUT_SPLIT;
    s->split_vertical = (gates_u8)(dir == GATES_SPLIT_VERTICAL);
    s->split_ratio = ratio_permille;
    gates_i_mark_dirty(tree, node.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_i32 gates_layout_split_ratio(const gates_tree_t *tree, gates_node_t node) {
    return gates_i_valid(tree, node) ? gates_i_slot(tree, node.index)->split_ratio : 0;
}

gates_err_t gates_layout_set_scroll_offset(gates_tree_t *tree, gates_node_t node,
                                           gates_i32 offset_y) {
    gates_node_slot_t *s = slot_checked(tree, node);
    if (s == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_i32 clamped = gates_i_scroll_clamp(tree, node.index, offset_y);
    if (clamped != s->scroll_offset) {
        s->scroll_offset = clamped;
        gates_i_mark_dirty(tree, node.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    }
    return GATES_OK;
}

gates_i32 gates_layout_scroll_offset(const gates_tree_t *tree, gates_node_t node) {
    return gates_i_valid(tree, node) ? gates_i_slot(tree, node.index)->scroll_offset : 0;
}

gates_size_t gates_layout_scroll_content(const gates_tree_t *tree, gates_node_t node) {
    return gates_i_valid(tree, node) ? gates_i_slot(tree, node.index)->content_size
                                     : (gates_size_t){ 0, 0 };
}

gates_err_t gates_layout_set_child_grow(gates_tree_t *tree, gates_node_t node, gates_u8 weight) {
    gates_node_slot_t *s = slot_checked(tree, node);
    if (s == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    s->grow = weight;
    gates_i_mark_dirty(tree, node.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_err_t gates_layout_set_child_align(gates_tree_t *tree, gates_node_t node,
                                         gates_align_t align) {
    gates_node_slot_t *s = slot_checked(tree, node);
    if (s == nullptr || align > GATES_ALIGN_END_V) {
        return PROVEN_ERR_INVALID_ARG;
    }
    s->align = (gates_u8)align;
    gates_i_mark_dirty(tree, node.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_err_t gates_layout_set_abs_rect(gates_tree_t *tree, gates_node_t node, gates_rect_t rect) {
    gates_node_slot_t *s = slot_checked(tree, node);
    if (s == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    s->abs_rect = rect;
    gates_i_mark_dirty(tree, node.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    return GATES_OK;
}

/* -- GRID and WRAP settings (plan-0019) ------------------------------------------ */

static gates_i_grid_grow *grow_of(const gates_tree_t *tree, gates_u32 idx) {
    gates_u32 gen = gates_i_slot(tree, idx)->generation;
    for (gates_u32 i = 0; i < tree->grid_grow_count; i++) {
        if (tree->grid_grows[i].index == idx && tree->grid_grows[i].generation == gen) return &tree->grid_grows[i];
    }
    return nullptr;
}

gates_err_t gates_layout_set_grid(gates_tree_t *tree, gates_node_t node, gates_u32 columns) {
    gates_node_slot_t *s = slot_checked(tree, node);
    if (s == nullptr || columns == 0 || columns > GATES_GRID_MAX_COLUMNS) return PROVEN_ERR_INVALID_ARG;
    s->grid_cols = (gates_u8)columns;
    gates_i_mark_dirty(tree, node.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    return GATES_OK;
}

static gates_u32 grid_columns(const gates_node_slot_t *s) {
    return s->grid_cols != 0 ? s->grid_cols : 2u;
}

gates_err_t gates_layout_set_grid_column_grow(gates_tree_t *tree, gates_node_t node, gates_u32 column,
                                              gates_u8 weight) {
    gates_node_slot_t *s = slot_checked(tree, node);
    if (s == nullptr || column >= grid_columns(s)) return PROVEN_ERR_INVALID_ARG;
    gates_i_grid_grow *g = grow_of(tree, node.index);
    if (g == nullptr) {
        for (gates_u32 i = 0; i < tree->grid_grow_count && g == nullptr; i++) {
            gates_node_t n = { .index = tree->grid_grows[i].index, .generation = tree->grid_grows[i].generation };
            if (!gates_i_valid(tree, n)) g = &tree->grid_grows[i]; /* reuse a dead node's entry */
        }
    }
    if (g == nullptr) {
        if (tree->grid_grow_count == tree->grid_grow_cap) {
            gates_u32 cap = tree->grid_grow_cap == 0 ? 4u : tree->grid_grow_cap * 2u;
            gates_allocator_t a = tree->alloc;
            proven_result_mem_mut_t m =
                tree->grid_grows == nullptr
                    ? a.alloc_fn(a.ctx, cap * sizeof *tree->grid_grows, alignof(gates_i_grid_grow))
                    : a.realloc_fn(a.ctx, tree->grid_grows, tree->grid_grow_cap * sizeof *tree->grid_grows,
                                   cap * sizeof *tree->grid_grows, alignof(gates_i_grid_grow));
            if (!proven_is_ok(m.err)) return m.err;
            tree->grid_grows = (gates_i_grid_grow *)m.value.ptr;
            tree->grid_grow_cap = cap;
        }
        g = &tree->grid_grows[tree->grid_grow_count++];
    }
    if (g->index != node.index || g->generation != s->generation) {
        *g = (gates_i_grid_grow){ .index = node.index, .generation = s->generation };
    }
    g->weight[column] = weight;
    gates_i_mark_dirty(tree, node.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_err_t gates_layout_set_child_span(gates_tree_t *tree, gates_node_t child, gates_u32 columns) {
    gates_node_slot_t *s = slot_checked(tree, child);
    if (s == nullptr || columns == 0 || columns > GATES_GRID_MAX_COLUMNS) return PROVEN_ERR_INVALID_ARG;
    s->span = (gates_u8)columns;
    gates_i_mark_dirty(tree, child.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    return GATES_OK;
}

void gates_i_grid_free(gates_tree_t *tree) {
    if (tree->grid_grows != nullptr) tree->alloc.free_fn(tree->alloc.ctx, tree->grid_grows);
    tree->grid_grows = nullptr;
    tree->grid_grow_count = tree->grid_grow_cap = 0;
}

/* GRID rows are walked one at a time: a row is the shown children that fit
 * its columns (with their spans), so no per-row storage is needed. */
static gates_u32 span_of(const gates_node_slot_t *cs, gates_u32 cols) {
    gates_u32 span = cs->span != 0 ? cs->span : 1u;
    return span > cols ? cols : span;
}

static gates_u32 next_shown(const gates_tree_t *tree, gates_u32 c) {
    while (c != GATES_NONE && gates_i_slot(tree, c)->hidden) c = gates_i_slot(tree, c)->next_sibling;
    return c;
}

/* The row starting at `start`: returns the first child of the next row and the
 * row's height. */
static gates_u32 grid_row(const gates_tree_t *tree, gates_u32 start, gates_u32 cols, gates_i32 *height) {
    gates_u32 col = 0, c = next_shown(tree, start);
    *height = 0;
    while (c != GATES_NONE) {
        const gates_node_slot_t *cs = gates_i_slot(tree, c);
        gates_u32 span = span_of(cs, cols);
        if (col + span > cols) break; /* (a span is at most cols: only after the first cell) */
        if (cs->pref.h > *height) *height = cs->pref.h;
        col += span;
        c = next_shown(tree, cs->next_sibling);
        if (col >= cols) break;
    }
    return c;
}

/* Column widths: single-column children first, then spans widen their last column. */
static void grid_columns_w(const gates_tree_t *tree, const gates_node_slot_t *s, gates_i32 *col_w) {
    gates_u32 cols = grid_columns(s);
    for (int pass = 0; pass < 2; pass++) {
        gates_u32 col = 0;
        for (gates_u32 c = next_shown(tree, s->first_child); c != GATES_NONE;
             c = next_shown(tree, gates_i_slot(tree, c)->next_sibling)) {
            const gates_node_slot_t *cs = gates_i_slot(tree, c);
            gates_u32 span = span_of(cs, cols);
            if (col + span > cols) col = 0;
            if (pass == 0 && span == 1 && cs->pref.w > col_w[col]) col_w[col] = cs->pref.w;
            if (pass == 1 && span > 1) {
                gates_i32 have = s->gap * (gates_i32)(span - 1);
                for (gates_u32 k = 0; k < span; k++) have += col_w[col + k];
                if (cs->pref.w > have) col_w[col + span - 1] += cs->pref.w - have;
            }
            col += span;
            if (col >= cols) col = 0;
        }
    }
}

static gates_size_t grid_measure(const gates_tree_t *tree, const gates_node_slot_t *s) {
    gates_i32 col_w[GATES_GRID_MAX_COLUMNS] = {0};
    grid_columns_w(tree, s, col_w);
    gates_u32 cols = grid_columns(s);
    gates_size_t content = { s->gap * (gates_i32)(cols - 1), 0 };
    for (gates_u32 k = 0; k < cols; k++) content.w += col_w[k];
    gates_u32 rows = 0;
    for (gates_u32 c = next_shown(tree, s->first_child); c != GATES_NONE; rows++) {
        gates_i32 h;
        c = grid_row(tree, c, cols, &h);
        content.h += h;
    }
    if (rows > 0) content.h += s->gap * (gates_i32)(rows - 1);
    return content;
}

/* WRAP: lays children into lines of at most `width`; returns the total size.
 * With `place`, sets their rects from `origin`. */
static gates_size_t wrap_lines(gates_tree_t *tree, gates_node_slot_t *s, gates_i32 width, bool place,
                               gates_point_t origin) {
    gates_i32 x = 0, y = 0, line_h = 0, max_w = 0;
    bool first_in_line = true;
    for (gates_u32 c = s->first_child; c != GATES_NONE; c = gates_i_slot(tree, c)->next_sibling) {
        gates_node_slot_t *cs = gates_i_slot(tree, c);
        if (cs->hidden) continue;
        gates_size_t p = cs->pref;
        if (!first_in_line && x + s->gap + p.w > width) {
            y += line_h + s->gap;
            x = 0;
            line_h = 0;
            first_in_line = true;
        }
        if (!first_in_line) x += s->gap;
        if (place) cs->layout_rect = (gates_rect_t){ origin.x + x, origin.y + y, p.w, p.h };
        x += p.w;
        if (x > max_w) max_w = x;
        if (p.h > line_h) line_h = p.h;
        first_in_line = false;
    }
    return (gates_size_t){ max_w, y + line_h };
}

/* -- measure (bottom-up) ------------------------------------------------------ */

static gates_size_t widget_intrinsic(const gates_tree_t *tree, const gates_node_slot_t *s,
                                     const gates_text_backend_t *text) {
    const gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    gates_str_t txt = st != nullptr ? gates_i_widget_label(tree, st) : (gates_str_t){0};
    gates_i32 fsz = gates_i_slot_font(tree, s);
    gates_size_t ts = text->measure(text->ctx, fsz, txt);
    gates_text_metrics_t m = text->metrics(text->ctx, fsz);
    if (txt.size == 0) {
        ts = (gates_size_t){ 0, m.line_height };
    } else if (gates_i_mn_markup(tree, (gates_u32)(s - tree->slots))) {
        ts.w = gates_i_mn_width(text, fsz, txt); /* markup takes no room (plan-0018) */
    }

    switch (s->kind) {
    case GATES_NODE_LABEL:
        return ts;
    case GATES_NODE_MENU:
        return st != nullptr ? gates_i_menu_measure(tree, st, text, fsz) : (gates_size_t){ 0, 0 };
    case GATES_NODE_MENUBAR:
        return gates_i_menubar_measure(tree, s, text);
    case GATES_NODE_TOOLBAR:
        return gates_i_toolbar_measure(tree, s, text);
    case GATES_NODE_TABSTRIP:
        return gates_i_tabstrip_measure(tree, s, text);
    case GATES_NODE_SPINARROWS:
        return gates_i_spin_arrows_measure();
    case GATES_NODE_GROUPHEAD:
        return gates_i_group_head_measure(tree, s, text);
    case GATES_NODE_SLIDER:
        return gates_i_slider_measure(tree, s);
    case GATES_NODE_IMAGE:
        return gates_i_image_measure(tree, s);
    case GATES_NODE_EDITOR:
        return gates_i_editor_measure(tree, s, text);
    case GATES_NODE_BUTTON: {
        gates_i32 h = ts.h + 2 * (GATES_BUTTON_PAD_Y + GATES_BUTTON_BORDER);
        gates_i32 w = ts.w + 2 * (GATES_BUTTON_PAD_X + GATES_BUTTON_BORDER);
        if (st != nullptr && gates_tree_image(tree, st->icon) != nullptr) { /* plan-0020 */
            w += GATES_ICON_SIZE + (txt.size > 0 ? 4 : 0);
            gates_i32 ih = GATES_ICON_SIZE + 2 * (GATES_BUTTON_PAD_Y + GATES_BUTTON_BORDER);
            if (ih > h) h = ih;
        }
        return (gates_size_t){ w < GATES_ACCESS_MIN_TARGET ? GATES_ACCESS_MIN_TARGET : w,
                               h < GATES_ACCESS_MIN_TARGET ? GATES_ACCESS_MIN_TARGET : h };
    }
    case GATES_NODE_CHECKBOX: {
        gates_i32 h = ts.h > GATES_CHECK_BOX ? ts.h : GATES_CHECK_BOX;
        if (h < GATES_ACCESS_MIN_TARGET) h = GATES_ACCESS_MIN_TARGET; /* WCAG 2.5.8 target */
        return (gates_size_t){ GATES_CHECK_BOX + GATES_CHECK_GAP + ts.w, h };
    }
    case GATES_NODE_RADIO:
    case GATES_NODE_CHOICE:
        return gates_i_options_measure(tree, s, text);
    case GATES_NODE_VIEW:
        return gates_i_view_measure(tree, s, text);
    case GATES_NODE_PROGRESS:
        return (gates_size_t){ 16 * m.advance, GATES_PROGRESS_H };
    case GATES_NODE_SEPARATOR: {
        /* Across the parent's flow: a vertical line in a row, else horizontal. */
        gates_i32 thick = 1 + 2 * GATES_SEPARATOR_SPACE;
        bool in_row = s->parent != GATES_NONE &&
                      gates_i_slot(tree, s->parent)->layout_kind == GATES_LAYOUT_ROW;
        return in_row ? (gates_size_t){ thick, 0 } : (gates_size_t){ 0, thick };
    }
    case GATES_NODE_TEXTBOX: {
        gates_i32 cols = (gates_i32)(st != nullptr && st->cols != 0 ? st->cols : 16u);
        gates_i32 h = m.line_height + 2 * (GATES_TEXTBOX_PAD_Y + GATES_TEXTBOX_BORDER);
        return (gates_size_t){
            cols * m.advance + 2 * (GATES_TEXTBOX_PAD_X + GATES_TEXTBOX_BORDER),
            h < GATES_ACCESS_MIN_TARGET ? GATES_ACCESS_MIN_TARGET : h, /* WCAG 2.5.8 */
        };
    }
    default:
        return (gates_size_t){ 0, 0 };
    }
}

/* Children that take part in layout (plan-0010: hidden ones do not). */
static gates_u32 shown_count(const gates_tree_t *tree, const gates_node_slot_t *s) {
    gates_u32 n = 0;
    for (gates_u32 c = s->first_child; c != GATES_NONE; c = gates_i_slot(tree, c)->next_sibling) {
        n += gates_i_slot(tree, c)->hidden ? 0u : 1u;
    }
    return n;
}

/* A FORM row's label and editor preferred sizes (zero when missing or hidden)
 * and the editor's first line height (for lining the label up with it).
 * false for a hidden row. */
static bool form_row_parts(const gates_tree_t *tree, gates_u32 row, gates_size_t *lp,
                           gates_size_t *ep, gates_size_t *fp) {
    const gates_node_slot_t *rs = gates_i_slot(tree, row);
    *lp = *ep = *fp = (gates_size_t){ 0, 0 };
    if (rs->hidden) {
        return false;
    }
    gates_u32 l = rs->first_child;
    gates_u32 e = l != GATES_NONE ? gates_i_slot(tree, l)->next_sibling : GATES_NONE;
    if (l != GATES_NONE) *lp = gates_i_slot(tree, l)->pref;
    if (e != GATES_NONE) {
        const gates_node_slot_t *es = gates_i_slot(tree, e);
        *ep = es->pref;
        *fp = es->pref;
        for (gates_u32 f = es->first_child; f != GATES_NONE; f = gates_i_slot(tree, f)->next_sibling) {
            if (!gates_i_slot(tree, f)->hidden) {
                *fp = gates_i_slot(tree, f)->pref; /* the first shown line of the editor cell */
                break;
            }
        }
    }
    return true;
}

static void measure_node(gates_tree_t *tree, gates_u32 idx, const gates_text_backend_t *text) {
    gates_node_slot_t *s = gates_i_slot(tree, idx);
    if (s->hidden) {
        s->pref = (gates_size_t){ 0, 0 };
        s->content_size = (gates_size_t){ 0, 0 };
        return; /* takes no space; its subtree is not measured */
    }

    /* Children first. */
    for (gates_u32 c = s->first_child; c != GATES_NONE; c = gates_i_slot(tree, c)->next_sibling) {
        measure_node(tree, c, text);
    }

    gates_size_t intrinsic = (s->kind != GATES_NODE_CUSTOM && s->kind != GATES_NODE_PANEL &&
                              s->kind != GATES_NODE_DIALOG)
                                 ? widget_intrinsic(tree, s, text)
                                 : (gates_size_t){ 0, 0 };

    gates_size_t content = { 0, 0 };
    s->content_size = (gates_size_t){ 0, 0 }; /* SCROLL fills this in below */
    gates_u32 n = shown_count(tree, s);
    if (n > 0) {
        switch (s->layout_kind) {
        case GATES_LAYOUT_ROW:
        case GATES_LAYOUT_COLUMN: {
            bool row = s->layout_kind == GATES_LAYOUT_ROW;
            gates_i32 main = 0, cross = 0;
            for (gates_u32 c = s->first_child; c != GATES_NONE;
                 c = gates_i_slot(tree, c)->next_sibling) {
                gates_size_t cp = gates_i_slot(tree, c)->pref;
                main += row ? cp.w : cp.h;
                gates_i32 cc = row ? cp.h : cp.w;
                if (cc > cross) cross = cc;
            }
            main += s->gap * (gates_i32)(n - 1);
            content = row ? (gates_size_t){ main, cross } : (gates_size_t){ cross, main };
            break;
        }
        case GATES_LAYOUT_STACK: {
            for (gates_u32 c = s->first_child; c != GATES_NONE;
                 c = gates_i_slot(tree, c)->next_sibling) {
                gates_size_t cp = gates_i_slot(tree, c)->pref;
                if (cp.w > content.w) content.w = cp.w;
                if (cp.h > content.h) content.h = cp.h;
            }
            break;
        }
        case GATES_LAYOUT_SPLIT: {
            gates_i32 main = 0, cross = 0;
            for (gates_u32 c = s->first_child; c != GATES_NONE;
                 c = gates_i_slot(tree, c)->next_sibling) {
                gates_size_t cp = gates_i_slot(tree, c)->pref;
                main += s->split_vertical ? cp.h : cp.w;
                gates_i32 cc = s->split_vertical ? cp.w : cp.h;
                if (cc > cross) cross = cc;
            }
            main += GATES_SPLIT_HANDLE_PX;
            content = s->split_vertical ? (gates_size_t){ cross, main }
                                        : (gates_size_t){ main, cross };
            break;
        }
        case GATES_LAYOUT_SCROLL: {
            /* Content is measured for clamping and the thumb, but a viewport
             * does not grow with it (plan-0004 decision 3): pref stays at the
             * container's own padding, so the parent sizes the viewport. */
            gates_i32 total_h = 0, max_w = 0;
            for (gates_u32 c = s->first_child; c != GATES_NONE;
                 c = gates_i_slot(tree, c)->next_sibling) {
                gates_size_t cp = gates_i_slot(tree, c)->pref;
                total_h += cp.h;
                if (cp.w > max_w) max_w = cp.w;
            }
            total_h += s->gap * (gates_i32)(n - 1);
            s->content_size = (gates_size_t){ max_w, total_h };
            break;
        }
        case GATES_LAYOUT_FORM: {
            /* One label column for all shown rows (plan-0010). */
            gates_i32 label_w = 0, editor_w = 0, total_h = 0;
            for (gates_u32 c = s->first_child; c != GATES_NONE;
                 c = gates_i_slot(tree, c)->next_sibling) {
                gates_size_t lp, ep, fp;
                if (!form_row_parts(tree, c, &lp, &ep, &fp)) continue;
                if (lp.w > label_w) label_w = lp.w;
                if (ep.w > editor_w) editor_w = ep.w;
                total_h += lp.h > ep.h ? lp.h : ep.h;
            }
            s->form_label_w = label_w;
            content = (gates_size_t){ label_w + GATES_FORM_COLUMN_GAP + editor_w,
                                      total_h + s->gap * (gates_i32)(n - 1) };
            break;
        }
        case GATES_LAYOUT_GRID:
            content = grid_measure(tree, s);
            break;
        case GATES_LAYOUT_WRAP: {
            /* At the width of the last arrange (one line before the first). */
            gates_i32 w = s->wrap_w > 0 ? s->wrap_w - 2 * s->padding : INT32_MAX / 2;
            content = wrap_lines(tree, s, w, false, (gates_point_t){ 0, 0 });
            if (s->wrap_w > 0 && content.w > w) content.w = w > 0 ? w : 0;
            break;
        }
        case GATES_LAYOUT_ABSOLUTE:
        default:
            /* Absolute/none: children do not drive the parent's size. */
            break;
        }
    }

    gates_size_t pref = {
        (intrinsic.w > content.w ? intrinsic.w : content.w) + 2 * s->padding,
        (intrinsic.h > content.h ? intrinsic.h : content.h) + 2 * s->padding,
    };
    s->pref = pref;
}

/* -- arrange (top-down) ------------------------------------------------------- */

static void arrange_node(gates_tree_t *tree, gates_u32 idx);

static void place_cross(gates_node_slot_t *c, gates_i32 content_pos, gates_i32 content_size,
                        bool row) {
    gates_i32 pref = row ? c->pref.h : c->pref.w;
    gates_i32 pos, size;
    switch ((gates_align_i)c->align) {
    case GATES_ALIGN_STRETCH: pos = content_pos; size = content_size; break;
    case GATES_ALIGN_START:   pos = content_pos; size = pref; break;
    case GATES_ALIGN_CENTER:  pos = content_pos + (content_size - pref) / 2; size = pref; break;
    case GATES_ALIGN_END:
    default:                  pos = content_pos + content_size - pref; size = pref; break;
    }
    if (row) { c->layout_rect.y = pos; c->layout_rect.h = size; }
    else     { c->layout_rect.x = pos; c->layout_rect.w = size; }
}

static void arrange_rowcol(gates_tree_t *tree, gates_node_slot_t *s, gates_rect_t content,
                           bool row) {
    gates_i32 avail = row ? content.w : content.h;
    gates_i32 fixed = 0;
    gates_i32 total_grow = 0;
    gates_u32 n = shown_count(tree, s);
    for (gates_u32 c = s->first_child; c != GATES_NONE; c = gates_i_slot(tree, c)->next_sibling) {
        gates_node_slot_t *cs = gates_i_slot(tree, c);
        if (cs->hidden) continue;
        fixed += row ? cs->pref.w : cs->pref.h;
        total_grow += cs->grow;
    }
    fixed += s->gap * (gates_i32)(n > 0 ? n - 1 : 0);
    gates_i32 extra = avail - fixed;
    if (extra < 0) {
        extra = 0; /* overflow: children keep preferred sizes (v1, no shrink) */
    }

    gates_i32 cursor = row ? content.x : content.y;
    gates_i32 given = 0;
    gates_i32 grow_seen = 0;
    for (gates_u32 c = s->first_child; c != GATES_NONE; c = gates_i_slot(tree, c)->next_sibling) {
        gates_node_slot_t *cs = gates_i_slot(tree, c);
        if (cs->hidden) {
            cs->layout_rect = (gates_rect_t){ content.x, content.y, 0, 0 };
            continue;
        }
        gates_i32 main = row ? cs->pref.w : cs->pref.h;
        if (cs->grow > 0 && total_grow > 0) {
            grow_seen += cs->grow;
            /* Exact distribution: cumulative shares, last grower absorbs. */
            gates_i32 upto = (gates_i32)((gates_i64)extra * grow_seen / total_grow);
            main += upto - given;
            given = upto;
        }
        if (row) {
            cs->layout_rect.x = cursor;
            cs->layout_rect.w = main;
        } else {
            cs->layout_rect.y = cursor;
            cs->layout_rect.h = main;
        }
        place_cross(cs, row ? content.y : content.x, row ? content.h : content.w, row);
        cursor += main + s->gap;
    }
}

/* SPLIT: pane A | handle | pane B, sized by ratio and clamped so neither
 * pane drops below GATES_SPLIT_MIN_PANE_PX. */
static void arrange_split(gates_tree_t *tree, gates_node_slot_t *s, gates_rect_t content) {
    gates_u32 first = s->first_child;
    if (first == GATES_NONE) {
        return;
    }
    gates_u32 second = gates_i_slot(tree, first)->next_sibling;
    bool vert = s->split_vertical != 0;
    gates_i32 total = (vert ? content.h : content.w) - GATES_SPLIT_HANDLE_PX;
    if (total < 0) {
        total = 0;
    }
    gates_i32 a = (gates_i32)(((gates_i64)total * s->split_ratio) / 1000);
    gates_i32 min = GATES_SPLIT_MIN_PANE_PX;
    if (total >= 2 * min) {
        if (a < min) a = min;
        if (a > total - min) a = total - min;
    } else {
        a = total / 2; /* too small to honor the minimum: split evenly */
    }
    gates_i32 b = total - a;

    gates_node_slot_t *sa = gates_i_slot(tree, first);
    if (vert) {
        sa->layout_rect = (gates_rect_t){ content.x, content.y, content.w, a };
    } else {
        sa->layout_rect = (gates_rect_t){ content.x, content.y, a, content.h };
    }
    if (second != GATES_NONE) {
        gates_node_slot_t *sb = gates_i_slot(tree, second);
        if (vert) {
            sb->layout_rect = (gates_rect_t){ content.x, content.y + a + GATES_SPLIT_HANDLE_PX,
                                              content.w, b };
        } else {
            sb->layout_rect = (gates_rect_t){ content.x + a + GATES_SPLIT_HANDLE_PX, content.y,
                                              b, content.h };
        }
        /* Extra children (a spec violation the validator reports) are parked
         * empty rather than left with stale rects. */
        for (gates_u32 c = gates_i_slot(tree, second)->next_sibling; c != GATES_NONE;
             c = gates_i_slot(tree, c)->next_sibling) {
            gates_i_slot(tree, c)->layout_rect = (gates_rect_t){ content.x, content.y, 0, 0 };
        }
    }
}

/* SCROLL: children stack in a column at preferred heights, then the whole
 * content is translated by -offset. layout_rect therefore holds final window
 * coordinates, so hit testing needs no scroll-specific case (plan-0004 §1). */
static void arrange_scroll(gates_tree_t *tree, gates_node_slot_t *s, gates_rect_t content) {
    gates_i32 viewport_h = content.h;
    bool bar = s->content_size.h > viewport_h;
    gates_i32 viewport_w = content.w - (bar ? GATES_SCROLLBAR_PX : 0);
    if (viewport_w < 0) {
        viewport_w = 0;
    }
    gates_i32 max_off = s->content_size.h - viewport_h;
    if (max_off < 0) {
        max_off = 0;
    }
    if (s->scroll_offset > max_off) {
        s->scroll_offset = max_off; /* content shrank under a stale offset */
    }
    if (s->scroll_offset < 0) {
        s->scroll_offset = 0;
    }

    gates_i32 y = content.y - s->scroll_offset;
    s->scroll_arranged = s->scroll_offset;
    for (gates_u32 c = s->first_child; c != GATES_NONE; c = gates_i_slot(tree, c)->next_sibling) {
        gates_node_slot_t *cs = gates_i_slot(tree, c);
        if (cs->hidden) {
            cs->layout_rect = (gates_rect_t){ content.x, content.y, 0, 0 };
            continue;
        }
        cs->layout_rect = (gates_rect_t){ content.x, y, viewport_w, cs->pref.h };
        place_cross(cs, content.x, viewport_w, false);
        y += cs->pref.h + s->gap;
    }
}

/* A hidden subtree keeps no stale geometry: every rect collapses to a point. */
static void park(gates_tree_t *tree, gates_u32 idx, gates_point_t at) {
    gates_node_slot_t *s = gates_i_slot(tree, idx);
    s->layout_rect = (gates_rect_t){ at.x, at.y, 0, 0 };
    s->dirty &= ~GATES_DIRTY_LAYOUT;
    for (gates_u32 c = s->first_child; c != GATES_NONE; c = gates_i_slot(tree, c)->next_sibling) {
        park(tree, c, at);
    }
}

/* FORM: rows top to bottom; label | editor, or label above editor when narrow. */
static void arrange_form(gates_tree_t *tree, gates_node_slot_t *s, gates_rect_t content) {
    gates_i32 label_w = s->form_label_w;
    gates_i32 cells = GATES_FORM_MIN_EDITOR_CELLS * (tree->advance > 0 ? tree->advance : 8);
    bool stacked = content.w < label_w + GATES_FORM_COLUMN_GAP + cells;
    gates_i32 y = content.y;
    for (gates_u32 c = s->first_child; c != GATES_NONE; c = gates_i_slot(tree, c)->next_sibling) {
        gates_node_slot_t *rs = gates_i_slot(tree, c);
        gates_size_t lp, ep, fp;
        if (!form_row_parts(tree, c, &lp, &ep, &fp)) {
            rs->layout_rect = (gates_rect_t){ content.x, content.y, 0, 0 };
            continue;
        }
        gates_u32 l = rs->first_child;
        gates_u32 e = l != GATES_NONE ? gates_i_slot(tree, l)->next_sibling : GATES_NONE;
        gates_i32 row_h;
        gates_rect_t lr, er;
        if (stacked) {
            lr = (gates_rect_t){ content.x, y, content.w, lp.h };
            er = (gates_rect_t){ content.x, y + lp.h + GATES_FORM_STACK_GAP, content.w, ep.h };
            row_h = lp.h + GATES_FORM_STACK_GAP + ep.h;
        } else {
            row_h = lp.h > ep.h ? lp.h : ep.h;
            /* Beside the editor's first line: at most one text line box high, so a
             * radio group's label sits by its first option, not its middle. */
            gates_i32 line = lp.h + 2 * (GATES_TEXTBOX_PAD_Y + GATES_TEXTBOX_BORDER);
            gates_i32 first = fp.h < line ? fp.h : line;
            gates_i32 ly = y + (first > lp.h ? (first - lp.h) / 2 : 0);
            gates_i32 ex = content.x + label_w + GATES_FORM_COLUMN_GAP;
            lr = (gates_rect_t){ content.x, ly, label_w, lp.h };
            er = (gates_rect_t){ ex, y, content.x + content.w - ex, ep.h };
            if (er.w < 0) er.w = 0;
        }
        rs->layout_rect = (gates_rect_t){ content.x, y, content.w, row_h };
        if (l != GATES_NONE) {
            gates_i_slot(tree, l)->layout_rect = lr;
        }
        if (e != GATES_NONE) {
            gates_node_slot_t *es = gates_i_slot(tree, e);
            es->layout_rect = er;
            if (es->align != GATES_ALIGN_STRETCH) {
                es->layout_rect.w = ep.w < er.w ? ep.w : er.w; /* keeps its own width */
            }
            for (gates_u32 x = es->next_sibling; x != GATES_NONE; x = gates_i_slot(tree, x)->next_sibling) {
                gates_i_slot(tree, x)->layout_rect = (gates_rect_t){ content.x, y, 0, 0 };
            }
        }
        y += row_h + s->gap;
    }
}

static void grid_place(gates_node_slot_t *cs, gates_i32 x, gates_i32 w, gates_i32 row_y, gates_i32 rh) {
    gates_i32 h = cs->pref.h < rh ? cs->pref.h : rh;
    gates_i32 cw = w;
    switch ((gates_align_i)cs->align) {
    case GATES_ALIGN_START: cw = cs->pref.w < w ? cs->pref.w : w; break;
    case GATES_ALIGN_CENTER: cw = cs->pref.w < w ? cs->pref.w : w; x += (w - cw) / 2; break;
    case GATES_ALIGN_END: cw = cs->pref.w < w ? cs->pref.w : w; x += w - cw; break;
    case GATES_ALIGN_STRETCH:
    default: break;
    }
    cs->layout_rect = (gates_rect_t){ x, row_y + (rh - h) / 2, cw, h }; /* centred in its row */
}

static void arrange_grid(gates_tree_t *tree, gates_node_slot_t *s, gates_rect_t content) {
    gates_u32 cols = grid_columns(s);
    gates_i32 col_w[GATES_GRID_MAX_COLUMNS] = {0}, col_x[GATES_GRID_MAX_COLUMNS];
    grid_columns_w(tree, s, col_w);
    /* Spare width by the columns' grow weights. */
    gates_i32 used = s->gap * (gates_i32)(cols - 1);
    for (gates_u32 k = 0; k < cols; k++) used += col_w[k];
    const gates_i_grid_grow *gw = grow_of(tree, (gates_u32)(s - tree->slots));
    gates_i32 total_w = 0;
    for (gates_u32 k = 0; gw != nullptr && k < cols; k++) total_w += gw->weight[k];
    gates_i32 spare = content.w - used;
    if (spare > 0 && total_w > 0) {
        gates_i32 given = 0, last = -1;
        for (gates_u32 k = 0; k < cols; k++) {
            if (gw->weight[k] == 0) continue;
            gates_i32 add = (gates_i32)((gates_i64)spare * gw->weight[k] / total_w);
            col_w[k] += add;
            given += add;
            last = (gates_i32)k;
        }
        col_w[last] += spare - given; /* rounding goes to the last growing column */
    }
    gates_i32 x = content.x;
    for (gates_u32 k = 0; k < cols; k++) {
        col_x[k] = x;
        x += col_w[k] + s->gap;
    }
    gates_i32 row_y = content.y;
    for (gates_u32 c = next_shown(tree, s->first_child); c != GATES_NONE;) {
        gates_i32 rh;
        gates_u32 next = grid_row(tree, c, cols, &rh);
        gates_u32 col = 0;
        for (gates_u32 k = c; k != next; k = next_shown(tree, gates_i_slot(tree, k)->next_sibling)) {
            gates_node_slot_t *cs = gates_i_slot(tree, k);
            gates_u32 span = span_of(cs, cols);
            gates_i32 w = s->gap * (gates_i32)(span - 1);
            for (gates_u32 j = 0; j < span; j++) w += col_w[col + j];
            grid_place(cs, col_x[col], w, row_y, rh);
            col += span;
        }
        row_y += rh + s->gap;
        c = next;
    }
}

static void arrange_node(gates_tree_t *tree, gates_u32 idx) {
    gates_node_slot_t *s = gates_i_slot(tree, idx);
    if (s->hidden) {
        park(tree, idx, (gates_point_t){ s->layout_rect.x, s->layout_rect.y });
        return;
    }
    /* A FORM row's children were placed by the form; only descend. */
    if (s->parent != GATES_NONE &&
        gates_i_slot(tree, s->parent)->layout_kind == GATES_LAYOUT_FORM) {
        for (gates_u32 c = s->first_child; c != GATES_NONE; c = gates_i_slot(tree, c)->next_sibling) {
            arrange_node(tree, c);
        }
        s->dirty &= ~GATES_DIRTY_LAYOUT;
        return;
    }
    gates_rect_t content = {
        s->layout_rect.x + s->padding,
        s->layout_rect.y + s->padding,
        s->layout_rect.w - 2 * s->padding,
        s->layout_rect.h - 2 * s->padding,
    };
    if (content.w < 0) content.w = 0;
    if (content.h < 0) content.h = 0;

    switch (s->layout_kind) {
    case GATES_LAYOUT_ROW:
        arrange_rowcol(tree, s, content, true);
        break;
    case GATES_LAYOUT_COLUMN:
        arrange_rowcol(tree, s, content, false);
        break;
    case GATES_LAYOUT_STACK:
        for (gates_u32 c = s->first_child; c != GATES_NONE;
             c = gates_i_slot(tree, c)->next_sibling) {
            gates_i_slot(tree, c)->layout_rect = content; /* children share (§10) */
        }
        break;
    case GATES_LAYOUT_SPLIT:
        arrange_split(tree, s, content);
        break;
    case GATES_LAYOUT_SCROLL:
        arrange_scroll(tree, s, content);
        break;
    case GATES_LAYOUT_FORM:
        arrange_form(tree, s, content);
        break;
    case GATES_LAYOUT_GRID:
        arrange_grid(tree, s, content);
        break;
    case GATES_LAYOUT_WRAP:
        if (s->layout_rect.w != s->wrap_w) {
            s->wrap_w = s->layout_rect.w;
            tree->wrap_changed = true; /* measured for another width: one more pass */
        }
        (void)wrap_lines(tree, s, content.w, true, (gates_point_t){ content.x, content.y });
        break;
    case GATES_LAYOUT_ABSOLUTE:
    case GATES_LAYOUT_NONE:
    default:
        for (gates_u32 c = s->first_child; c != GATES_NONE;
             c = gates_i_slot(tree, c)->next_sibling) {
            gates_node_slot_t *cs = gates_i_slot(tree, c);
            gates_rect_t r = cs->abs_rect;
            if (r.w <= 0) r.w = cs->pref.w;
            if (r.h <= 0) r.h = cs->pref.h;
            cs->layout_rect = (gates_rect_t){ content.x + r.x, content.y + r.y, r.w, r.h };
        }
        break;
    }
    if (s->kind == GATES_NODE_VIEW) {
        gates_i_view_place_editor(tree, idx); /* over the edited cell (plan-0021) */
    }

    for (gates_u32 c = s->first_child; c != GATES_NONE; c = gates_i_slot(tree, c)->next_sibling) {
        arrange_node(tree, c);
    }
    s->dirty &= ~GATES_DIRTY_LAYOUT;
}

gates_err_t gates_layout_run(gates_tree_t *tree, gates_size_t viewport,
                             const gates_text_backend_t *text) {
    if (tree == nullptr || text == nullptr || text->measure == nullptr ||
        text->metrics == nullptr || viewport.w < 0 || viewport.h < 0) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_text_metrics_t root_metrics = text->metrics(text->ctx, 0);
    tree->line_height = root_metrics.line_height; /* wheel step unit */
    tree->advance = root_metrics.advance;         /* average width: sizing hints */
    tree->text_backend = text;                    /* for hit testing between layouts */
    tree->wrap_changed = false;
    measure_node(tree, tree->root, text);
    gates_i_slot(tree, tree->root)->layout_rect = (gates_rect_t){ 0, 0, viewport.w, viewport.h };
    arrange_node(tree, tree->root);
    if (tree->wrap_changed) {
        /* A wrap container got another width than it was measured for: measure
         * again with that width (its height follows it), once. */
        tree->wrap_changed = false;
        measure_node(tree, tree->root, text);
        arrange_node(tree, tree->root);
    }
    /* Overlays (plan-0009 stage 2): measured on their own, placed in the viewport. */
    for (gates_u32 i = 0; i < tree->overlay_count; i++) {
        gates_u32 idx = tree->overlays[i].index;
        measure_node(tree, idx, text);
        gates_node_slot_t *os = gates_i_slot(tree, idx);
        os->layout_rect = gates_i_overlay_place(tree, i, os->pref, viewport);
        arrange_node(tree, idx);
    }
    gates_i_overlays_after_layout(tree);
    tree->dirty_bits &= ~(gates_u32)GATES_TREE_DIRTY_LAYOUT;
    tree->dirty_bits |= GATES_TREE_DIRTY_PAINT;
    return GATES_OK;
}

/* -- results / validation ------------------------------------------------------ */

gates_rect_t gates_node_layout_rect(const gates_tree_t *tree, gates_node_t node) {
    return gates_i_valid(tree, node) ? gates_i_slot(tree, node.index)->layout_rect
                                     : (gates_rect_t){ 0, 0, 0, 0 };
}

gates_size_t gates_node_preferred_size(const gates_tree_t *tree, gates_node_t node) {
    return gates_i_valid(tree, node) ? gates_i_slot(tree, node.index)->pref
                                     : (gates_size_t){ 0, 0 };
}

/* -- split/scroll chrome geometry (shared with paint and hit) --------------- */

static gates_rect_t content_box(const gates_node_slot_t *s) {
    gates_rect_t c = {
        s->layout_rect.x + s->padding,
        s->layout_rect.y + s->padding,
        s->layout_rect.w - 2 * s->padding,
        s->layout_rect.h - 2 * s->padding,
    };
    if (c.w < 0) c.w = 0;
    if (c.h < 0) c.h = 0;
    return c;
}

gates_rect_t gates_i_split_handle(const gates_tree_t *tree, gates_u32 idx) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    if (s->layout_kind != GATES_LAYOUT_SPLIT || s->first_child == GATES_NONE) {
        return (gates_rect_t){ 0, 0, 0, 0 };
    }
    gates_rect_t content = content_box(s);
    gates_rect_t a = gates_i_slot(tree, s->first_child)->layout_rect;
    if (s->split_vertical) {
        return (gates_rect_t){ content.x, a.y + a.h, content.w, GATES_SPLIT_HANDLE_PX };
    }
    return (gates_rect_t){ a.x + a.w, content.y, GATES_SPLIT_HANDLE_PX, content.h };
}

bool gates_i_scrollable(const gates_tree_t *tree, gates_u32 idx) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    return s->layout_kind == GATES_LAYOUT_SCROLL && s->content_size.h > content_box(s).h;
}

gates_rect_t gates_i_scroll_viewport(const gates_tree_t *tree, gates_u32 idx) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    if (s->layout_kind != GATES_LAYOUT_SCROLL) {
        return (gates_rect_t){ 0, 0, 0, 0 };
    }
    gates_rect_t c = content_box(s);
    if (gates_i_scrollable(tree, idx)) {
        c.w -= GATES_SCROLLBAR_PX;
        if (c.w < 0) c.w = 0;
    }
    return c;
}

gates_rect_t gates_i_scroll_track(const gates_tree_t *tree, gates_u32 idx) {
    if (!gates_i_scrollable(tree, idx)) {
        return (gates_rect_t){ 0, 0, 0, 0 };
    }
    gates_rect_t c = content_box(gates_i_slot(tree, idx));
    return (gates_rect_t){ c.x + c.w - GATES_SCROLLBAR_PX, c.y, GATES_SCROLLBAR_PX, c.h };
}

gates_rect_t gates_i_scroll_thumb(const gates_tree_t *tree, gates_u32 idx) {
    gates_rect_t track = gates_i_scroll_track(tree, idx);
    if (gates_rect_is_empty(track)) {
        return track;
    }
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    gates_i32 content_h = s->content_size.h;
    gates_i32 view_h = content_box(s).h;
    gates_i32 thumb_h = (gates_i32)(((gates_i64)track.h * view_h) / content_h);
    if (thumb_h < GATES_SCROLLBAR_PX) {
        thumb_h = GATES_SCROLLBAR_PX;
    }
    if (thumb_h > track.h) {
        thumb_h = track.h;
    }
    gates_i32 max_off = content_h - view_h;
    gates_i32 travel = track.h - thumb_h;
    gates_i32 y = max_off > 0
                      ? (gates_i32)(((gates_i64)travel * s->scroll_offset) / max_off)
                      : 0;
    return (gates_rect_t){ track.x, track.y + y, track.w, thumb_h };
}

gates_i32 gates_i_scroll_clamp(const gates_tree_t *tree, gates_u32 idx, gates_i32 offset) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    gates_i32 max_off = s->content_size.h - content_box(s).h;
    if (max_off < 0) {
        max_off = 0;
    }
    if (offset < 0) {
        return 0;
    }
    return offset > max_off ? max_off : offset;
}

gates_rect_t gates_i_textbox_inner(const gates_tree_t *tree, gates_u32 idx) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    if (s->kind != GATES_NODE_TEXTBOX) {
        return (gates_rect_t){ 0, 0, 0, 0 };
    }
    gates_i32 inset_x = GATES_TEXTBOX_PAD_X + GATES_TEXTBOX_BORDER;
    gates_i32 inset_y = GATES_TEXTBOX_PAD_Y + GATES_TEXTBOX_BORDER;
    gates_rect_t r = s->layout_rect;
    gates_rect_t inner = { r.x + inset_x, r.y + inset_y,
                           r.w - 2 * inset_x, r.h - 2 * inset_y };
    if (inner.w < 0) inner.w = 0;
    if (inner.h < 0) inner.h = 0;
    return inner;
}

/* -- validation ------------------------------------------------------------- */

static bool rects_overlap(gates_rect_t a, gates_rect_t b) {
    return !gates_rect_is_empty(gates_rect_intersect(a, b));
}

static bool validate_idx(const gates_tree_t *tree, gates_u32 idx) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    if (s->layout_kind == GATES_LAYOUT_SPLIT && s->child_count != 2) {
        return false; /* split is defined for exactly two panes */
    }
    if (s->layout_kind == GATES_LAYOUT_ROW || s->layout_kind == GATES_LAYOUT_COLUMN ||
        s->layout_kind == GATES_LAYOUT_SPLIT || s->layout_kind == GATES_LAYOUT_SCROLL ||
        s->layout_kind == GATES_LAYOUT_FORM || s->layout_kind == GATES_LAYOUT_GRID ||
        s->layout_kind == GATES_LAYOUT_WRAP) {
        for (gates_u32 a = s->first_child; a != GATES_NONE;
             a = gates_i_slot(tree, a)->next_sibling) {
            for (gates_u32 b = gates_i_slot(tree, a)->next_sibling; b != GATES_NONE;
                 b = gates_i_slot(tree, b)->next_sibling) {
                if (rects_overlap(gates_i_slot(tree, a)->layout_rect,
                                  gates_i_slot(tree, b)->layout_rect)) {
                    return false;
                }
            }
        }
    }
    for (gates_u32 c = s->first_child; c != GATES_NONE;
         c = gates_i_slot(tree, c)->next_sibling) {
        if (!validate_idx(tree, c)) {
            return false;
        }
    }
    return true;
}

bool gates_layout_validate(const gates_tree_t *tree, gates_node_t node) {
    if (!gates_i_valid(tree, node)) {
        return false;
    }
    return validate_idx(tree, node.index);
}
