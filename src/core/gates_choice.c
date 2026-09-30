/* gates_gui_lib - option controls: radio group and choice, one node each with
 * stable option ids. The choice's list is a menu
 * overlay (gates_overlay.c). Platform-free. */
#include <gates/widget.h>
#include <gates/event.h>
#include "gates_tree_internal.h"

#include <string.h>

static gates_widget_state_t *options_state(const gates_tree_t *tree, gates_node_t node) {
    if (tree == nullptr || !gates_i_valid(tree, node)) {
        return nullptr;
    }
    const gates_node_slot_t *s = gates_i_slot(tree, node.index);
    if (s->kind != GATES_NODE_RADIO && s->kind != GATES_NODE_CHOICE) {
        return nullptr;
    }
    return gates_i_state(tree, s->state_index);
}

const gates_i_option_t *gates_i_option_find(const gates_widget_state_t *st, gates_u32 id) {
    for (gates_u32 i = 0; id != 0 && i < st->opt_count; i++) {
        if (st->opts[i].id == id) {
            return &st->opts[i];
        }
    }
    return nullptr;
}

bool gates_i_options_any_enabled(const gates_widget_state_t *st) {
    for (gates_u32 i = 0; i < st->opt_count; i++) {
        if (!st->opts[i].disabled) {
            return true;
        }
    }
    return false;
}

void gates_i_options_free(gates_tree_t *tree, gates_widget_state_t *st) {
    if (st->opts != nullptr) {
        tree->alloc.free_fn(tree->alloc.ctx, st->opts);
    }
    st->opts = nullptr;
    st->opt_count = 0;
}

/* Checks a list and copies it into one block (array, then the labels). */
static gates_err_t options_copy(gates_tree_t *tree, const gates_option_t *options,
                                gates_u32 count, gates_i_option_t **out) {
    *out = nullptr;
    if (count > 0 && options == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_usize_t bytes = 0;
    for (gates_u32 i = 0; i < count; i++) {
        if (options[i].id == 0 || (options[i].label.size > 0 && options[i].label.ptr == nullptr)) {
            return PROVEN_ERR_INVALID_ARG;
        }
        for (gates_u32 k = 0; k < i; k++) {
            if (options[k].id == options[i].id) {
                return PROVEN_ERR_INVALID_ARG;
            }
        }
        bytes += options[i].label.size;
    }
    if (count == 0) {
        return GATES_OK;
    }
    gates_usize_t head = (gates_usize_t)count * sizeof(gates_i_option_t);
    proven_result_mem_mut_t r = tree->alloc.alloc_fn(tree->alloc.ctx, head + bytes,
                                                     alignof(gates_i_option_t));
    if (!proven_is_ok(r.err)) {
        return r.err;
    }
    gates_i_option_t *opts = (gates_i_option_t *)r.value.ptr;
    gates_u8 *text = (gates_u8 *)r.value.ptr + head;
    for (gates_u32 i = 0; i < count; i++) {
        if (options[i].label.size > 0) {
            memcpy(text, options[i].label.ptr, options[i].label.size);
        }
        opts[i] = (gates_i_option_t){
            .id = options[i].id,
            .label = text,
            .label_len = (gates_u32)options[i].label.size,
            .disabled = options[i].disabled,
        };
        text += options[i].label.size;
    }
    *out = opts;
    return GATES_OK;
}

static gates_err_t options_create(gates_tree_t *tree, gates_node_t parent, gates_node_kind_t kind,
                                  const gates_option_t *options, gates_u32 count,
                                  gates_u32 selected_id, gates_node_t *out_node) {
    if (tree == nullptr || out_node == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *out_node = GATES_NODE_NULL;
    gates_i_option_t *opts = nullptr;
    gates_err_t err = options_copy(tree, options, count, &opts);
    if (!gates_is_ok(err)) {
        return err;
    }
    bool known = selected_id == 0;
    for (gates_u32 i = 0; i < count && !known; i++) {
        known = options[i].id == selected_id;
    }
    gates_node_t node = GATES_NODE_NULL;
    gates_u32 state = GATES_NONE;
    if (!known) {
        err = PROVEN_ERR_INVALID_ARG;
    }
    if (gates_is_ok(err)) {
        gates_node_desc_t nd = { .kind = kind };
        err = gates_node_create(tree, GATES_NODE_NULL, &nd, &node);
    }
    if (gates_is_ok(err)) {
        err = gates_i_state_acquire(tree, &state);
        if (gates_is_ok(err)) {
            gates_i_slot(tree, node.index)->state_index = state;
        }
    }
    if (gates_is_ok(err) && gates_i_valid(tree, parent)) {
        err = gates_node_append(tree, parent, node);
    } else if (gates_is_ok(err) && !gates_node_eq(parent, GATES_NODE_NULL)) {
        err = PROVEN_ERR_INVALID_ARG;
    }
    if (!gates_is_ok(err)) {
        if (opts != nullptr) {
            tree->alloc.free_fn(tree->alloc.ctx, opts);
        }
        if (!gates_node_eq(node, GATES_NODE_NULL)) {
            gates_i_discard_detached(tree, node); /* never attached: freed at once */
        }
        return err;
    }
    gates_widget_state_t *st = gates_i_state(tree, state);
    st->has_options = true;
    st->opts = opts;
    st->opt_count = count;
    st->opt_sel = selected_id;
    st->opt_press = -1;
    *out_node = node;
    return GATES_OK;
}

gates_err_t gates_radio_create(gates_tree_t *tree, gates_node_t parent,
                               const gates_option_t *options, gates_u32 count,
                               gates_u32 selected_id, gates_node_t *out_node) {
    return options_create(tree, parent, GATES_NODE_RADIO, options, count, selected_id, out_node);
}

gates_err_t gates_choice_create(gates_tree_t *tree, gates_node_t parent,
                                const gates_option_t *options, gates_u32 count,
                                gates_u32 selected_id, gates_node_t *out_node) {
    return options_create(tree, parent, GATES_NODE_CHOICE, options, count, selected_id, out_node);
}

gates_err_t gates_options_set(gates_tree_t *tree, gates_node_t node,
                              const gates_option_t *options, gates_u32 count) {
    gates_widget_state_t *st = options_state(tree, node);
    if (st == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_i_option_t *opts = nullptr;
    gates_err_t err = options_copy(tree, options, count, &opts);
    if (!gates_is_ok(err)) {
        return err;
    }
    gates_i_choice_lists_check(tree, node.index); /* an open list shows the old rows */
    gates_i_options_free(tree, st);
    st->opts = opts;
    st->opt_count = count;
    st->opt_press = -1;
    if (st->opt_sel != 0 && gates_i_option_find(st, st->opt_sel) == nullptr) {
        st->opt_sel = 0;
        st->revision++;
    }
    gates_i_mark_dirty(tree, node.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    gates_i_focus_check(tree); /* no enabled option left: focus moves on */
    return GATES_OK;
}

gates_err_t gates_options_set_selected(gates_tree_t *tree, gates_node_t node, gates_u32 id) {
    gates_widget_state_t *st = options_state(tree, node);
    if (st == nullptr || (id != 0 && gates_i_option_find(st, id) == nullptr)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (st->opt_sel != id) {
        st->opt_sel = id;
        st->revision++;
        gates_i_mark_dirty(tree, node.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    }
    return GATES_OK;
}

gates_u32 gates_options_selected(const gates_tree_t *tree, gates_node_t node) {
    const gates_widget_state_t *st = options_state(tree, node);
    return st != nullptr ? st->opt_sel : 0;
}

gates_err_t gates_options_set_enabled(gates_tree_t *tree, gates_node_t node, gates_u32 id,
                                      bool enabled) {
    gates_widget_state_t *st = options_state(tree, node);
    gates_i_option_t *o = st != nullptr ? (gates_i_option_t *)gates_i_option_find(st, id) : nullptr;
    if (o == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (o->disabled == enabled) {
        o->disabled = !enabled;
        gates_i_mark_dirty(tree, node.index, GATES_DIRTY_PAINT);
        gates_i_focus_check(tree);
    }
    return GATES_OK;
}

gates_u32 gates_options_count(const gates_tree_t *tree, gates_node_t node) {
    const gates_widget_state_t *st = options_state(tree, node);
    return st != nullptr ? st->opt_count : 0;
}

gates_node_t gates_choice_list(const gates_tree_t *tree, gates_node_t choice) {
    if (options_state(tree, choice) == nullptr) {
        return GATES_NODE_NULL;
    }
    gates_i32 i = gates_i_choice_list_find(tree, choice.index);
    return i < 0 ? GATES_NODE_NULL : gates_i_handle(tree, tree->overlays[i].index);
}

bool gates_choice_list_open(const gates_tree_t *tree, gates_node_t choice) {
    return !gates_node_eq(gates_choice_list(tree, choice), GATES_NODE_NULL);
}

/* -- selection by the person ---------------------------------------------------------- */

gates_err_t gates_i_option_pick(gates_tree_t *tree, gates_u32 idx, gates_u32 id) {
    gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
    const gates_i_option_t *o = gates_i_option_find(st, id);
    if (o == nullptr || o->disabled) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (st->opt_sel == id) {
        return GATES_OK;
    }
    if (gates_i_wants_events(tree, idx)) {
        gates_err_t err = gates_i_event_reserve(tree, 1, 0);
        if (!gates_is_ok(err)) {
            return err; /* not announced: not done */
        }
    }
    st->opt_sel = id;
    st->revision++;
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    gates_i_event_push(tree, idx, GATES_EVENT_VALUE_CHANGED, GATES_ORIGIN_USER);
    return GATES_OK;
}

static void pick(gates_tree_t *tree, gates_u32 idx, gates_u32 id) {
    gates_err_t err = gates_i_option_pick(tree, idx, id);
    if (!gates_is_ok(err)) {
        tree->input_error = err;
    }
}

static gates_i32 row_of(const gates_widget_state_t *st, gates_u32 id) {
    for (gates_u32 i = 0; i < st->opt_count; i++) {
        if (st->opts[i].id == id) {
            return (gates_i32)i;
        }
    }
    return -1;
}

/* The next enabled row from `from` in direction dir (wrapping), or -1. */
static gates_i32 step(const gates_widget_state_t *st, gates_i32 from, gates_i32 dir) {
    gates_i32 n = (gates_i32)st->opt_count;
    for (gates_i32 k = 1; k <= n; k++) {
        gates_i32 row = ((from + dir * k) % n + n) % n;
        if (!st->opts[row].disabled) {
            return row;
        }
    }
    return -1;
}

bool gates_i_radio_key(gates_tree_t *tree, gates_u32 idx, const gates_key_event_t *ev) {
    gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
    if (st == nullptr || st->opt_count == 0 || ev->ctrl || ev->alt) {
        return false;
    }
    gates_i32 n = (gates_i32)st->opt_count;
    gates_i32 cur = row_of(st, st->opt_sel);
    gates_i32 row;
    switch (ev->key) {
    case GATES_KEY_DOWN:
    case GATES_KEY_RIGHT:
        row = step(st, cur < 0 ? -1 : cur, 1);
        break;
    case GATES_KEY_UP:
    case GATES_KEY_LEFT:
        row = step(st, cur < 0 ? n : cur, -1);
        break;
    case GATES_KEY_HOME:
        row = step(st, -1, 1);
        break;
    case GATES_KEY_END:
        row = step(st, n, -1);
        break;
    default:
        return false;
    }
    if (row >= 0) {
        pick(tree, idx, st->opts[row].id);
    }
    return true;
}

void gates_i_radio_activate(gates_tree_t *tree, gates_u32 idx) {
    gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
    if (st == nullptr || st->opt_sel != 0 || st->opt_count == 0) {
        return;
    }
    gates_i32 row = step(st, -1, 1);
    if (row >= 0) {
        pick(tree, idx, st->opts[row].id);
    }
}

/* -- geometry, shared by measure, paint and hit testing ------------------------------- */

gates_i32 gates_i_radio_row_h(gates_i32 line_height) {
    gates_i32 h = (line_height > GATES_CHECK_BOX ? line_height : GATES_CHECK_BOX) + GATES_RADIO_ROW_GAP;
    return h < GATES_ACCESS_MIN_TARGET ? GATES_ACCESS_MIN_TARGET : h; /* WCAG 2.5.8 */
}

gates_i32 gates_i_radio_row_at(const gates_tree_t *tree, gates_u32 idx, gates_point_t p) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    const gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    if (st == nullptr || !gates_rect_contains(s->layout_rect, p)) {
        return -1;
    }
    gates_i32 row = (p.y - s->layout_rect.y) /
                    gates_i_radio_row_h(tree->line_height > 0 ? tree->line_height : 16);
    return row < (gates_i32)st->opt_count ? row : -1;
}

static gates_str_t label_of(const gates_i_option_t *o) {
    return (gates_str_t){ .ptr = o->label, .size = o->label_len };
}

gates_size_t gates_i_options_measure(const gates_tree_t *tree, const gates_node_slot_t *s,
                                     const gates_text_backend_t *text) {
    const gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    gates_i32 font = gates_i_slot_font(tree, s);
    gates_text_metrics_t m = text->metrics(text->ctx, font);
    gates_i32 label_w = 0;
    for (gates_u32 i = 0; st != nullptr && i < st->opt_count; i++) {
        gates_size_t ls = text->measure(text->ctx, font, label_of(&st->opts[i]));
        if (ls.w > label_w) label_w = ls.w;
    }
    gates_i32 n = st != nullptr ? (gates_i32)st->opt_count : 0;
    if (s->kind == GATES_NODE_RADIO) {
        return (gates_size_t){ GATES_CHECK_BOX + GATES_CHECK_GAP + label_w,
                               n * gates_i_radio_row_h(m.line_height) };
    }
    gates_i32 inset_x = GATES_TEXTBOX_PAD_X + GATES_TEXTBOX_BORDER;
    gates_i32 inset_y = GATES_TEXTBOX_PAD_Y + GATES_TEXTBOX_BORDER;
    gates_i32 h = m.line_height + 2 * inset_y;
    return (gates_size_t){ label_w + GATES_CHOICE_ARROW_CELLS * m.advance + 2 * inset_x,
                           h < GATES_ACCESS_MIN_TARGET ? GATES_ACCESS_MIN_TARGET : h }; /* WCAG 2.5.8 */
}

static gates_err_t paint_radio(const gates_tree_t *tree, gates_u32 idx, gates_draw_list_t *dl,
                               const gates_theme_t *theme, const gates_text_backend_t *text,
                               bool inert) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    const gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    gates_i32 font = gates_i_font(tree, idx);
    gates_text_metrics_t m = text->metrics(text->ctx, font);
    gates_i32 row_h = gates_i_radio_row_h(m.line_height);
    gates_rect_t r = s->layout_rect;
    bool focused = tree->focus == idx;
    gates_i32 ring_row = row_of(st, st->opt_sel);
    if (ring_row < 0) ring_row = step(st, -1, 1);
    gates_err_t err = GATES_OK;
    for (gates_u32 i = 0; i < st->opt_count && gates_is_ok(err); i++) {
        const gates_i_option_t *o = &st->opts[i];
        gates_rect_t row = { r.x, r.y + (gates_i32)i * row_h, r.w, row_h };
        gates_rect_t box = { row.x, row.y + (row_h - GATES_CHECK_BOX) / 2, GATES_CHECK_BOX,
                             GATES_CHECK_BOX };
        err = gates_draw_rect(dl, box, gates_theme_color(theme, GATES_COLOR_CONTROL_BG));
        if (gates_is_ok(err)) {
            err = gates_draw_border(dl, box, 1, gates_theme_color(theme, GATES_COLOR_CONTROL_BORDER));
        }
        if (gates_is_ok(err) && o->id == st->opt_sel) {
            err = gates_draw_rect(dl, (gates_rect_t){ box.x + 3, box.y + 3, box.w - 6, box.h - 6 },
                                  gates_theme_color(theme, GATES_COLOR_SELECTION_BG));
        }
        if (gates_is_ok(err) && o->label_len > 0) {
            gates_size_t ls = text->measure(text->ctx, font, label_of(o));
            gates_color_token_t fg = inert || o->disabled ? GATES_COLOR_CONTROL_DISABLED_FG
                                                          : GATES_COLOR_PANEL_FG;
            err = gates_draw_text(dl, (gates_rect_t){ row.x + GATES_CHECK_BOX + GATES_CHECK_GAP,
                                                      row.y + (row_h - ls.h) / 2, ls.w, ls.h },
                                  label_of(o), font, gates_theme_color(theme, fg));
        }
        if (gates_is_ok(err) && focused && (gates_i32)i == ring_row) {
            err = gates_draw_border(dl, row, gates_theme_focus_width(theme),
                                    gates_theme_color(theme, GATES_COLOR_FOCUS_RING));
        }
    }
    return err;
}

static gates_err_t paint_choice(const gates_tree_t *tree, gates_u32 idx, gates_draw_list_t *dl,
                                const gates_theme_t *theme, const gates_text_backend_t *text,
                                bool inert, bool pressed, bool hovered) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    const gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    gates_i32 font = gates_i_font(tree, idx);
    gates_text_metrics_t m = text->metrics(text->ctx, font);
    gates_rect_t r = s->layout_rect;
    gates_color_token_t bg = inert     ? GATES_COLOR_CONTROL_BG
                             : pressed ? GATES_COLOR_CONTROL_PRESSED_BG
                             : hovered ? GATES_COLOR_CONTROL_HOVER_BG
                                       : GATES_COLOR_CONTROL_BG;
    gates_err_t err = gates_draw_rect(dl, r, gates_theme_color(theme, bg));
    if (gates_is_ok(err)) {
        err = gates_draw_border(dl, r, tree->focus == idx ? gates_theme_focus_width(theme)
                                                          : GATES_TEXTBOX_BORDER,
                                gates_theme_color(theme, tree->focus == idx
                                                             ? GATES_COLOR_FOCUS_RING
                                                             : GATES_COLOR_CONTROL_BORDER));
    }
    gates_color_t fg = gates_theme_color(theme, inert ? GATES_COLOR_CONTROL_DISABLED_FG
                                                      : GATES_COLOR_CONTROL_FG);
    gates_i32 inset_x = GATES_TEXTBOX_PAD_X + GATES_TEXTBOX_BORDER;
    const gates_i_option_t *o = gates_i_option_find(st, st->opt_sel);
    if (gates_is_ok(err) && o != nullptr && o->label_len > 0) {
        gates_size_t ls = text->measure(text->ctx, font, label_of(o));
        err = gates_draw_text(dl, (gates_rect_t){ r.x + inset_x, r.y + (r.h - ls.h) / 2, ls.w, ls.h },
                              label_of(o), font, fg);
    }
    /* Down arrow: a small triangle of shrinking rows at the right. */
    gates_i32 aw = GATES_CHOICE_ARROW_CELLS * m.advance;
    gates_i32 half = aw / 4 > 1 ? aw / 4 : 1;
    gates_i32 cx = r.x + r.w - inset_x - aw / 2;
    gates_i32 ty = r.y + (r.h - half) / 2;
    for (gates_i32 k = 0; k < half && gates_is_ok(err); k++) {
        gates_i32 w = 2 * (half - k) - 1;
        err = gates_draw_rect(dl, (gates_rect_t){ cx - w / 2, ty + k, w, 1 }, fg);
    }
    return err;
}

gates_err_t gates_i_options_paint(const gates_tree_t *tree, gates_u32 idx, gates_draw_list_t *dl,
                                  const gates_theme_t *theme, const gates_text_backend_t *text,
                                  bool pressed, bool hovered) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    const gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    if (st == nullptr) {
        return GATES_OK;
    }
    bool inert = gates_i_widget_inert(tree, st);
    return s->kind == GATES_NODE_RADIO
               ? paint_radio(tree, idx, dl, theme, text, inert)
               : paint_choice(tree, idx, dl, theme, text, inert, pressed, hovered);
}
