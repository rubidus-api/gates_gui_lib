/* gates_gui_lib - the menu bar: titles over command menus, menu mode
 * (0.3.0). The menus are ordinary menu overlays (gates_overlay.c) that know
 * which title opened them. Platform-free. */
#include <gates/frame.h>
#include <gates/widget.h>
#include <gates/overlay.h>
#include <gates/ui.h>
#include "gates_tree_internal.h"

#include <string.h>

#define BAR_ROW_EXTRA 6

static gates_widget_state_t *state_at(const gates_tree_t *tree, gates_u32 idx) {
    return gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
}

static gates_i_menubar *bar_of(const gates_tree_t *tree, gates_node_t bar) {
    if (tree == nullptr || !gates_i_valid(tree, bar) ||
        gates_i_slot(tree, bar.index)->kind != GATES_NODE_MENUBAR) {
        return nullptr;
    }
    const gates_widget_state_t *st = state_at(tree, bar.index);
    return st != nullptr ? st->mbar : nullptr;
}

static void mark_bar(gates_tree_t *tree) {
    if (tree->menubar != GATES_NONE) {
        gates_i_mark_dirty(tree, tree->menubar, GATES_DIRTY_PAINT);
    }
}

/* -- building ------------------------------------------------------------------------ */

gates_err_t gates_menubar_create(gates_tree_t *tree, gates_node_t parent, gates_node_t scope,
                                 gates_node_t *out_bar) {
    if (tree == nullptr || out_bar == nullptr || !gates_i_valid(tree, scope) ||
        (!gates_node_eq(parent, GATES_NODE_NULL) && !gates_i_valid(tree, parent))) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *out_bar = GATES_NODE_NULL;
    if (tree->menubar != GATES_NONE) {
        return PROVEN_ERR_INVALID_STATE;
    }
    gates_allocator_t a = tree->alloc;
    proven_result_mem_mut_t r = a.alloc_fn(a.ctx, sizeof(gates_i_menubar), alignof(gates_i_menubar));
    if (!proven_is_ok(r.err)) {
        return r.err;
    }
    gates_i_menubar *mb = (gates_i_menubar *)r.value.ptr;
    *mb = (gates_i_menubar){ .scope_index = scope.index, .scope_generation = scope.generation };
    gates_node_desc_t nd = { .kind = GATES_NODE_MENUBAR };
    gates_node_t bar = GATES_NODE_NULL;
    gates_err_t err = gates_node_create(tree, parent, &nd, &bar);
    gates_u32 state = GATES_NONE;
    if (gates_is_ok(err)) {
        err = gates_i_state_acquire(tree, &state);
        if (!gates_is_ok(err)) {
            gates_i_node_undo(tree, bar); /* as widget creation does */
        }
    }
    if (!gates_is_ok(err)) {
        a.free_fn(a.ctx, mb);
        return err;
    }
    gates_i_slot(tree, bar.index)->state_index = state;
    state_at(tree, bar.index)->mbar = mb;
    tree->menubar = bar.index;
    tree->mb_mode = GATES_I_MB_OFF;
    tree->mb_hover = GATES_NONE;
    gates_i_mark_dirty(tree, bar.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    *out_bar = bar;
    return GATES_OK;
}

gates_err_t gates_menubar_add(gates_tree_t *tree, gates_node_t bar, gates_str_t title,
                              const gates_command_id_t *ids, gates_u32 count, gates_u32 *out_index) {
    gates_i_menubar *mb = bar_of(tree, bar);
    if (mb == nullptr || ids == nullptr || count == 0 || (title.size > 0 && title.ptr == nullptr)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_allocator_t a = tree->alloc;
    if (mb->count == mb->cap) {
        gates_u32 cap = mb->cap == 0 ? 4u : mb->cap * 2u;
        proven_result_mem_mut_t r =
            mb->items == nullptr
                ? a.alloc_fn(a.ctx, cap * sizeof *mb->items, alignof(gates_i_menubar_item))
                : a.realloc_fn(a.ctx, mb->items, mb->cap * sizeof *mb->items, cap * sizeof *mb->items,
                               alignof(gates_i_menubar_item));
        if (!proven_is_ok(r.err)) {
            return r.err;
        }
        mb->items = (gates_i_menubar_item *)r.value.ptr;
        mb->cap = cap;
    }
    /* Title and ids in one block: [ids][title]. */
    gates_usize_t ids_bytes = count * sizeof *ids;
    proven_result_mem_mut_t r = a.alloc_fn(a.ctx, ids_bytes + title.size + 1, alignof(gates_command_id_t));
    if (!proven_is_ok(r.err)) {
        return r.err;
    }
    gates_command_id_t *copy = (gates_command_id_t *)r.value.ptr;
    memcpy(copy, ids, ids_bytes);
    gates_u8 *t = (gates_u8 *)r.value.ptr + ids_bytes;
    if (title.size > 0) memcpy(t, title.ptr, title.size);
    mb->items[mb->count] = (gates_i_menubar_item){ .title = t, .title_len = (gates_u32)title.size,
                                                   .ids = copy, .count = count };
    if (out_index != nullptr) *out_index = mb->count;
    mb->count++;
    gates_i_mark_dirty(tree, bar.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    return GATES_OK;
}

void gates_i_menubar_free(gates_tree_t *tree, gates_widget_state_t *st) {
    gates_i_menubar *mb = st->mbar;
    if (mb == nullptr) {
        return;
    }
    for (gates_u32 i = 0; i < mb->count; i++) {
        tree->alloc.free_fn(tree->alloc.ctx, mb->items[i].ids); /* the block holds the title */
    }
    if (mb->items != nullptr) {
        tree->alloc.free_fn(tree->alloc.ctx, mb->items);
    }
    tree->alloc.free_fn(tree->alloc.ctx, mb);
    st->mbar = nullptr;
}

gates_u32 gates_menubar_count(const gates_tree_t *tree, gates_node_t bar) {
    const gates_i_menubar *mb = bar_of(tree, bar);
    return mb != nullptr ? mb->count : 0;
}

gates_str_t gates_menubar_title(const gates_tree_t *tree, gates_node_t bar, gates_u32 index) {
    const gates_i_menubar *mb = bar_of(tree, bar);
    if (mb == nullptr || index >= mb->count) {
        return (gates_str_t){0};
    }
    return (gates_str_t){ .ptr = mb->items[index].title, .size = mb->items[index].title_len };
}

/* -- geometry and paint --------------------------------------------------------------- */

static gates_str_t title_of(const gates_i_menubar *mb, gates_u32 t) {
    return (gates_str_t){ .ptr = mb->items[t].title, .size = mb->items[t].title_len };
}

static gates_i32 title_w(const gates_text_backend_t *be, gates_i32 font, const gates_i_menubar *mb,
                         gates_u32 t) {
    gates_i32 adv = be->metrics(be->ctx, font).advance;
    return gates_i_mn_width(be, font, title_of(mb, t)) + 2 * adv;
}

static gates_i32 row_h(const gates_text_backend_t *be, gates_i32 font) {
    gates_i32 h = be->metrics(be->ctx, font).line_height + BAR_ROW_EXTRA;
    return h < GATES_ACCESS_MIN_TARGET ? GATES_ACCESS_MIN_TARGET : h;
}

gates_size_t gates_i_menubar_measure(const gates_tree_t *tree, const gates_node_slot_t *s,
                                     const gates_text_backend_t *text) {
    const gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    gates_i32 font = gates_i_slot_font(tree, s);
    gates_i32 w = 0;
    for (gates_u32 t = 0; st != nullptr && st->mbar != nullptr && t < st->mbar->count; t++) {
        w += title_w(text, font, st->mbar, t);
    }
    return (gates_size_t){ w, row_h(text, font) };
}

gates_rect_t gates_i_menubar_title_rect(const gates_tree_t *tree, gates_u32 idx, gates_u32 title) {
    const gates_widget_state_t *st = state_at(tree, idx);
    const gates_text_backend_t *be = tree->text_backend;
    gates_rect_t r = gates_i_slot(tree, idx)->layout_rect;
    if (st == nullptr || st->mbar == nullptr || be == nullptr || title >= st->mbar->count) {
        return (gates_rect_t){0};
    }
    gates_i32 font = gates_i_font(tree, idx);
    gates_i32 x = r.x;
    for (gates_u32 t = 0; t < title; t++) {
        x += title_w(be, font, st->mbar, t);
    }
    return (gates_rect_t){ x, r.y, title_w(be, font, st->mbar, title), r.h };
}

gates_i32 gates_i_menubar_title_at(const gates_tree_t *tree, gates_u32 idx, gates_point_t p) {
    const gates_widget_state_t *st = state_at(tree, idx);
    if (st == nullptr || st->mbar == nullptr ||
        !gates_rect_contains(gates_i_slot(tree, idx)->layout_rect, p)) {
        return -1;
    }
    for (gates_u32 t = 0; t < st->mbar->count; t++) {
        if (gates_rect_contains(gates_i_menubar_title_rect(tree, idx, t), p)) {
            return (gates_i32)t;
        }
    }
    return -1;
}

gates_err_t gates_i_menubar_paint(const gates_tree_t *tree, gates_u32 idx, gates_draw_list_t *dl,
                                  const gates_theme_t *theme, const gates_text_backend_t *text) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    const gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    gates_rect_t r = s->layout_rect;
    gates_err_t err = gates_draw_rect(dl, r, gates_theme_color(theme, GATES_COLOR_PANEL_BG));
    if (gates_is_ok(err)) {
        err = gates_draw_rect(dl, (gates_rect_t){ r.x, r.y + r.h - 1, r.w, 1 },
                              gates_theme_color(theme, GATES_COLOR_CONTROL_BORDER));
    }
    if (st == nullptr || st->mbar == nullptr) {
        return err;
    }
    gates_i32 font = gates_i_font(tree, idx);
    gates_text_metrics_t m = text->metrics(text->ctx, font);
    bool mine = tree->menubar == idx;
    gates_i32 x = r.x;
    for (gates_u32 t = 0; t < st->mbar->count && gates_is_ok(err); t++) {
        gates_i32 w = title_w(text, font, st->mbar, t);
        gates_rect_t tr = { x, r.y, w, r.h - 1 };
        bool sel = mine && tree->mb_mode != GATES_I_MB_OFF && tree->mb_sel == t;
        bool hot = mine && !sel && tree->mb_hover == t;
        if (sel || hot) {
            err = gates_draw_rect(dl, tr, gates_theme_color(theme, sel ? GATES_COLOR_SELECTION_BG
                                                                      : GATES_COLOR_CONTROL_HOVER_BG));
        }
        if (gates_is_ok(err)) {
            gates_color_token_t fg = sel ? GATES_COLOR_SELECTION_FG : GATES_COLOR_PANEL_FG;
            err = gates_i_mn_draw(dl, text,
                                  (gates_rect_t){ x + m.advance, r.y + (r.h - 1 - m.line_height) / 2, 0,
                                                  m.line_height },
                                  title_of(st->mbar, t), font, gates_theme_color(theme, fg),
                                  gates_i_cues(tree));
        }
        x += w;
    }
    return err;
}

/* -- menu mode --------------------------------------------------------------------------- */

gates_u32 gates_i_menubar_live(const gates_tree_t *tree) {
    gates_u32 idx = tree->menubar;
    if (idx == GATES_NONE) {
        return GATES_NONE;
    }
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    const gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    if (!s->alive || s->destroy_pending || st == nullptr || st->mbar == nullptr ||
        st->mbar->count == 0 || !gates_i_reachable(tree, idx)) {
        return GATES_NONE;
    }
    return idx;
}

static void enter(gates_tree_t *tree) {
    tree->mb_mode = GATES_I_MB_HIGHLIGHT;
    tree->mb_sel = 0;
    gates_input_show_cues(tree);
    mark_bar(tree);
}

void gates_i_menubar_leave(gates_tree_t *tree) {
    gates_i32 i = gates_i_bar_menu_overlay(tree);
    if (i >= 0) {
        gates_i_menu_close_record(tree, (gates_u32)i);
    }
    tree->mb_mode = GATES_I_MB_OFF;
    mark_bar(tree);
}

gates_err_t gates_i_menubar_open(gates_tree_t *tree, gates_u32 t, bool keyboard) {
    gates_u32 bar = gates_i_menubar_live(tree);
    if (bar == GATES_NONE) {
        return PROVEN_ERR_INVALID_STATE;
    }
    const gates_i_menubar *mb = state_at(tree, bar)->mbar;
    if (t >= mb->count) {
        return PROVEN_ERR_OUT_OF_BOUNDS;
    }
    gates_i32 open = gates_i_bar_menu_overlay(tree);
    if (open >= 0) {
        gates_i_menu_close_record(tree, (gates_u32)open); /* a switch */
    }
    gates_rect_t tr = gates_i_menubar_title_rect(tree, bar, t);
    gates_node_t scope = { .index = mb->scope_index, .generation = mb->scope_generation };
    gates_node_t menu;
    gates_err_t err = gates_i_valid(tree, scope)
                          ? gates_i_menu_open_for_bar(tree, (gates_point_t){ tr.x, tr.y + tr.h }, tr.y,
                                                      scope, mb->items[t].ids, mb->items[t].count, t,
                                                      keyboard, &menu)
                          : PROVEN_ERR_INVALID_STATE;
    if (!gates_is_ok(err)) {
        gates_i_menubar_leave(tree);
        return err;
    }
    tree->mb_mode = GATES_I_MB_OPEN;
    tree->mb_sel = t;
    if (keyboard) {
        gates_input_show_cues(tree);
    }
    mark_bar(tree);
    return GATES_OK;
}

void gates_i_menubar_menu_closed(gates_tree_t *tree) {
    tree->mb_mode = GATES_I_MB_OFF; /* a switch or Escape sets the mode again after */
    mark_bar(tree);
}

static gates_u8 upper(gates_u8 c) {
    return (c >= 'a' && c <= 'z') ? (gates_u8)(c - 'a' + 'A') : c;
}

bool gates_i_menubar_key(gates_tree_t *tree, const gates_key_event_t *ev) {
    gates_u32 bar = gates_i_menubar_live(tree);
    if (bar == GATES_NONE) {
        tree->mb_mode = GATES_I_MB_OFF;
        return false;
    }
    const gates_i_menubar *mb = state_at(tree, bar)->mbar;
    gates_u32 n = mb->count;
    gates_err_t err = GATES_OK;
    switch (ev->key) {
    case GATES_KEY_LEFT: tree->mb_sel = (tree->mb_sel + n - 1) % n; break;
    case GATES_KEY_RIGHT: tree->mb_sel = (tree->mb_sel + 1) % n; break;
    case GATES_KEY_HOME: tree->mb_sel = 0; break;
    case GATES_KEY_END: tree->mb_sel = n - 1; break;
    case GATES_KEY_DOWN:
    case GATES_KEY_UP:
    case GATES_KEY_ENTER:
    case GATES_KEY_SPACE:
        err = gates_i_menubar_open(tree, tree->mb_sel, true);
        break;
    case GATES_KEY_ESCAPE:
    case GATES_KEY_F10:
        gates_i_menubar_leave(tree);
        break;
    default:
        if (ev->letter != 0 && !ev->ctrl) {
            for (gates_u32 t = 0; t < n; t++) {
                if (gates_mnemonic_of(title_of(mb, t)) == upper(ev->letter)) {
                    err = gates_i_menubar_open(tree, t, true);
                    break;
                }
            }
            break; /* an unknown letter is swallowed */
        }
        gates_i_menubar_leave(tree); /* Tab and the rest leave menu mode and go on */
        return false;
    }
    if (!gates_is_ok(err)) {
        tree->input_error = err;
    }
    mark_bar(tree);
    return true;
}

void gates_i_menubar_press(gates_tree_t *tree, gates_u32 idx, gates_point_t p) {
    if (idx != gates_i_menubar_live(tree)) {
        return;
    }
    gates_i32 t = gates_i_menubar_title_at(tree, idx, p);
    if (t < 0) {
        if (tree->mb_mode != GATES_I_MB_OFF) gates_i_menubar_leave(tree);
        return;
    }
    /* (With a bar menu open the overlay routes presses on the bar itself.) */
    gates_err_t err = gates_i_menubar_open(tree, (gates_u32)t, false);
    if (!gates_is_ok(err)) {
        tree->input_error = err;
    }
}

void gates_i_menubar_check(gates_tree_t *tree) {
    if (tree->mb_mode != GATES_I_MB_OFF && gates_i_menubar_live(tree) == GATES_NONE) {
        gates_i_menubar_leave(tree);
    }
}

void gates_i_menubar_destroying(gates_tree_t *tree, gates_u32 top) {
    if (tree->menubar == GATES_NONE) {
        return;
    }
    for (gates_u32 a = tree->menubar; a != GATES_NONE; a = gates_i_slot(tree, a)->parent) {
        if (a == top) {
            gates_i_menubar_leave(tree);
            tree->menubar = GATES_NONE;
            tree->mb_hover = GATES_NONE;
            return;
        }
    }
}

/* -- public queries and opening ------------------------------------------------------------ */

static bool is_live_bar(const gates_tree_t *tree, gates_node_t bar) {
    return bar_of(tree, bar) != nullptr && tree->menubar == bar.index;
}

gates_err_t gates_menubar_open(gates_tree_t *tree, gates_node_t bar, gates_u32 index) {
    const gates_i_menubar *mb = bar_of(tree, bar);
    if (mb == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (index >= mb->count) {
        return PROVEN_ERR_OUT_OF_BOUNDS;
    }
    if (!is_live_bar(tree, bar) || gates_i_menubar_live(tree) != bar.index) {
        return PROVEN_ERR_INVALID_STATE;
    }
    return gates_i_menubar_open(tree, index, true);
}

gates_i32 gates_menubar_open_index(const gates_tree_t *tree, gates_node_t bar) {
    if (!is_live_bar(tree, bar) || tree->mb_mode != GATES_I_MB_OPEN) {
        return -1;
    }
    return (gates_i32)tree->mb_sel;
}

gates_node_t gates_menubar_menu(const gates_tree_t *tree, gates_node_t bar) {
    if (!is_live_bar(tree, bar)) {
        return GATES_NODE_NULL;
    }
    gates_i32 i = gates_i_bar_menu_overlay(tree);
    return i < 0 ? GATES_NODE_NULL : gates_i_handle(tree, tree->overlays[i].index);
}

bool gates_menubar_active(const gates_tree_t *tree, gates_node_t bar) {
    return is_live_bar(tree, bar) && tree->mb_mode != GATES_I_MB_OFF;
}

gates_i32 gates_menubar_highlighted(const gates_tree_t *tree, gates_node_t bar) {
    return gates_menubar_active(tree, bar) ? (gates_i32)tree->mb_sel : -1;
}

bool gates_input_menu_key(gates_tree_t *tree) {
    if (tree == nullptr || gates_i_menubar_live(tree) == GATES_NONE) {
        return false;
    }
    if (tree->mb_mode != GATES_I_MB_OFF) {
        gates_i_menubar_leave(tree);
    } else {
        enter(tree);
    }
    return true;
}

/* F10 from gates_input_key (gates_input.c): the menu key when there is a bar. */
bool gates_i_menubar_f10(gates_tree_t *tree) {
    if (gates_i_menubar_live(tree) == GATES_NONE) {
        return false;
    }
    if (tree->mb_mode == GATES_I_MB_OFF) {
        enter(tree);
    } else {
        gates_i_menubar_leave(tree);
    }
    return true;
}
